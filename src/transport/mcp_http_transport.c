#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include <win_socket_compat.h>

// On Windows, strcasecmp is _stricmp
#define strcasecmp _stricmp
#endif

#include "mcp_http_transport.h"
#include "internal/http_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static int http_transport_start(mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback);
static int http_transport_stop(mcp_transport_t* transport);
static int http_transport_destroy(mcp_transport_t* transport);

// Create HTTP transport
mcp_transport_t* mcp_transport_http_create(const mcp_http_config_t* config) {
    if (config == NULL || config->host == NULL) {
        return NULL;
    }

    // Allocate transport
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        return NULL;
    }

    // Allocate transport data
    http_transport_data_t* data = (http_transport_data_t*)calloc(1, sizeof(http_transport_data_t));
    if (data == NULL) {
        free(transport);
        return NULL;
    }

    // Copy configuration
    data->config.host = mcp_strdup(config->host);
    data->config.port = config->port;
    data->config.use_ssl = config->use_ssl;
    data->config.timeout_ms = config->timeout_ms;

    if (config->cert_path) {
        data->config.cert_path = mcp_strdup(config->cert_path);
    }

    if (config->key_path) {
        data->config.key_path = mcp_strdup(config->key_path);
    }

    if (config->doc_root) {
        data->config.doc_root = mcp_strdup(config->doc_root);
    }

    // Initialize CORS settings
    data->enable_cors = config->enable_cors;
    if (config->cors_allow_origin) {
        data->cors_allow_origin = mcp_strdup(config->cors_allow_origin);
    } else {
        data->cors_allow_origin = mcp_strdup("*"); // Default to allow all origins
    }

    if (config->cors_allow_methods) {
        data->cors_allow_methods = mcp_strdup(config->cors_allow_methods);
    } else {
        data->cors_allow_methods = mcp_strdup("GET, POST, OPTIONS"); // Default methods
    }

    if (config->cors_allow_headers) {
        data->cors_allow_headers = mcp_strdup(config->cors_allow_headers);
    } else {
        data->cors_allow_headers = mcp_strdup("Content-Type, Authorization"); // Default headers
    }

    data->cors_max_age = config->cors_max_age > 0 ? config->cors_max_age : 86400; // Default to 24 hours

    // Initialize SSE mutex
    data->sse_mutex = mcp_mutex_create();
    if (data->sse_mutex == NULL) {
        if (data->config.host) free((void*)data->config.host);
        if (data->config.cert_path) free((void*)data->config.cert_path);
        if (data->config.key_path) free((void*)data->config.key_path);
        if (data->config.doc_root) free((void*)data->config.doc_root);
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize event mutex
    data->event_mutex = mcp_mutex_create();
    if (data->event_mutex == NULL) {
        mcp_mutex_destroy(data->sse_mutex);
        if (data->config.host) free((void*)data->config.host);
        if (data->config.cert_path) free((void*)data->config.cert_path);
        if (data->config.key_path) free((void*)data->config.key_path);
        if (data->config.doc_root) free((void*)data->config.doc_root);
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize SSE event storage (circular buffer)
    data->event_head = 0;
    data->event_tail = 0;
    data->stored_event_count = 0;
    data->next_event_id = 1;

    // Initialize SSE heartbeat
    data->send_heartbeats = true;
    data->heartbeat_interval_ms = 30000; // 30 seconds by default
    data->last_heartbeat = time(NULL);

    // Set transport type to server
    transport->type = MCP_TRANSPORT_TYPE_SERVER;

    // Initialize server operations
    transport->server.start = http_transport_start;
    transport->server.stop = http_transport_stop;
    transport->server.destroy = http_transport_destroy;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}

// Start HTTP transport
static int http_transport_start(mcp_transport_t* transport,
                        mcp_transport_message_callback_t message_callback,
                        void* user_data,
                        mcp_transport_error_callback_t error_callback) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    // Store callback and user data
    data->message_callback = message_callback;
    data->callback_user_data = user_data;
    data->error_callback = error_callback;

    // Create libwebsockets context
    struct lws_context_creation_info info = {0};
    info.port = data->config.port;
    info.iface = data->config.host;
    info.protocols = http_protocols;
    info.user = data;

    // Set options - use minimal options to avoid conflicts
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE |
                   LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Disable built-in 404 handling
    info.error_document_404 = NULL;

    // Store mount pointer for later cleanup
    data->mount = NULL;

    // Log the configuration
    mcp_log_info("Creating HTTP server on %s:%d", data->config.host, data->config.port);

    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.ssl_cert_filepath = data->config.cert_path;
        info.ssl_private_key_filepath = data->config.key_path;
    }

    // Set mount for static files if doc_root is provided
    if (data->config.doc_root) {
        mcp_log_info("Setting up static file mount for doc_root: %s", data->config.doc_root);

        // Check if directory exists
        FILE* test_file = NULL;
        char test_path[512];
        snprintf(test_path, sizeof(test_path), "%s/index.html", data->config.doc_root);
        test_file = fopen(test_path, "r");
        if (test_file) {
            fclose(test_file);
            mcp_log_info("Test file exists: %s", test_path);
        } else {
            mcp_log_error("Test file does not exist: %s", test_path);
        }

        // Allocate and initialize mount structure
        data->mount = (struct lws_http_mount*)calloc(1, sizeof(struct lws_http_mount));
        if (data->mount) {
            data->mount->mountpoint = "/";
            data->mount->mountpoint_len = 1;
            data->mount->origin = data->config.doc_root;
            data->mount->def = "index.html";
            data->mount->origin_protocol = LWSMPRO_FILE;
            data->mount->cgienv = NULL;
            data->mount->extra_mimetypes = NULL;
            data->mount->interpret = NULL;
            data->mount->cgi_timeout = 0;
            data->mount->cache_max_age = 0;
            data->mount->auth_mask = 0;
            data->mount->cache_reusable = 0;
            data->mount->cache_revalidate = 0;
            data->mount->cache_intermediaries = 0;
            data->mount->origin_protocol = LWSMPRO_FILE;
            // mountpoint_len is unsigned char, so we need to be careful with the length
            size_t len = strlen(data->mount->mountpoint);
            if (len > 255) {
                mcp_log_warn("Mountpoint length %zu truncated to 255", len);
                len = 255;
            }
            data->mount->mountpoint_len = (unsigned char)len;

            info.mounts = data->mount;
            mcp_log_info("Static file mount configured");
        }
    }

    data->context = lws_create_context(&info);
    if (data->context == NULL) {
        mcp_log_error("Failed to create HTTP server context");
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event thread
    if (mcp_thread_create(&data->event_thread, http_event_thread_func, transport) != 0) {
        mcp_log_error("Failed to create HTTP event thread");
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    mcp_log_info("HTTP transport started on %s:%d", data->config.host, data->config.port);
    return 0;
}

// Stop HTTP transport
static int http_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    mcp_log_info("Stopping HTTP transport...");

    // Set running flag to false
    data->running = false;

    // Force libwebsockets to break out of its service loop
    if (data->context) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets service");
    }

    // Wait for event thread to exit with a timeout
    mcp_log_info("Waiting for HTTP event thread to exit...");
    if (mcp_thread_join(data->event_thread, NULL) != 0) {
        mcp_log_error("Failed to join HTTP event thread");
    }

    // Destroy context
    if (data->context) {
        mcp_log_info("Destroying libwebsockets context...");
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("HTTP transport stopped");
    return 0;
}

// Destroy HTTP transport
static int http_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    // Stop transport if running
    if (data->running) {
        http_transport_stop(transport);
    }

    // Free configuration
    if (data->config.host) free((void*)data->config.host);
    if (data->config.cert_path) free((void*)data->config.cert_path);
    if (data->config.key_path) free((void*)data->config.key_path);
    if (data->config.doc_root) free((void*)data->config.doc_root);

    // Free CORS settings
    if (data->cors_allow_origin) free(data->cors_allow_origin);
    if (data->cors_allow_methods) free(data->cors_allow_methods);
    if (data->cors_allow_headers) free(data->cors_allow_headers);

    // Free mount structure
    if (data->mount) {
        free(data->mount);
        data->mount = NULL;
    }

    // Free stored SSE events (circular buffer)
    mcp_mutex_lock(data->event_mutex);
    if (data->stored_event_count > 0) {
        int current = data->event_head;
        int count = 0;

        // Process all events in the buffer
        while (count < data->stored_event_count) {
            if (data->stored_events[current].id) free(data->stored_events[current].id);
            if (data->stored_events[current].event_type) free(data->stored_events[current].event_type);
            if (data->stored_events[current].data) free(data->stored_events[current].data);

            // Move to the next event in the circular buffer
            current = (current + 1) % MAX_SSE_STORED_EVENTS;
            count++;
        }
    }
    mcp_mutex_unlock(data->event_mutex);

    // Destroy mutexes
    if (data->sse_mutex) {
        mcp_mutex_destroy(data->sse_mutex);
    }

    if (data->event_mutex) {
        mcp_mutex_destroy(data->event_mutex);
    }

    // Free transport data
    free(data);

    // Free transport
    free(transport);

    return 0;
}

// Event thread function
void* http_event_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL || transport->transport_data == NULL) {
        return NULL;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    mcp_log_info("HTTP event thread started");

    // Run event loop
    while (data->running) {
        // Service libwebsockets
        lws_service(data->context, 100); // 100ms timeout

        // Send heartbeat if needed
        send_sse_heartbeat(data);
    }

    mcp_log_info("HTTP event thread exited");
    return NULL;
}
