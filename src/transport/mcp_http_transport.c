#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include "win_socket_compat.h"
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

// Default values
#define HTTP_DEFAULT_HEARTBEAT_INTERVAL_MS 30000  // 30 seconds
#define HTTP_DEFAULT_CORS_MAX_AGE 86400           // 24 hours
#define HTTP_DEFAULT_CORS_ALLOW_ORIGIN "*"
#define HTTP_DEFAULT_CORS_ALLOW_METHODS "GET, POST, OPTIONS"
#define HTTP_DEFAULT_CORS_ALLOW_HEADERS "Content-Type, Authorization"

// LWS service timeout in milliseconds
#define HTTP_LWS_SERVICE_TIMEOUT_MS 100

// Buffer sizes
#define HTTP_TEST_PATH_BUFFER_SIZE 512

// Forward declarations of static functions
static int http_transport_start(mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback);
static int http_transport_stop(mcp_transport_t* transport);
static int http_transport_destroy(mcp_transport_t* transport);
static void free_transport_data(http_transport_data_t* data);
static bool initialize_cors_settings(http_transport_data_t* data, const mcp_http_config_t* config);
static bool initialize_mutexes(http_transport_data_t* data);
static bool setup_static_file_mount(http_transport_data_t* data);
static void free_stored_sse_events(http_transport_data_t* data);

/**
 * @brief Free all memory associated with transport data
 *
 * @param data Transport data to free
 */
static void free_transport_data(http_transport_data_t* data) {
    if (data == NULL) {
        return;
    }

    // Free configuration
    if (data->config.host) {
        free((void*)data->config.host);
        data->config.host = NULL;
    }

    if (data->config.cert_path) {
        free((void*)data->config.cert_path);
        data->config.cert_path = NULL;
    }

    if (data->config.key_path) {
        free((void*)data->config.key_path);
        data->config.key_path = NULL;
    }

    if (data->config.doc_root) {
        free((void*)data->config.doc_root);
        data->config.doc_root = NULL;
    }

    // Free CORS settings
    if (data->cors_allow_origin) {
        free(data->cors_allow_origin);
        data->cors_allow_origin = NULL;
    }

    if (data->cors_allow_methods) {
        free(data->cors_allow_methods);
        data->cors_allow_methods = NULL;
    }

    if (data->cors_allow_headers) {
        free(data->cors_allow_headers);
        data->cors_allow_headers = NULL;
    }

    // Free mount structure
    if (data->mount) {
        free(data->mount);
        data->mount = NULL;
    }

    // Free stored SSE events
    free_stored_sse_events(data);

    // Destroy mutexes
    if (data->sse_mutex) {
        mcp_mutex_destroy(data->sse_mutex);
        data->sse_mutex = NULL;
    }

    if (data->event_mutex) {
        mcp_mutex_destroy(data->event_mutex);
        data->event_mutex = NULL;
    }

    // Free the data structure itself
    free(data);
}

/**
 * @brief Free all stored SSE events in the circular buffer
 *
 * @param data Transport data containing the events
 */
static void free_stored_sse_events(http_transport_data_t* data) {
    if (data == NULL || data->stored_event_count <= 0) {
        return;
    }

    // Lock the event mutex to safely access the events
    mcp_mutex_lock(data->event_mutex);

    int current = data->event_head;
    int count = 0;

    // Process all events in the buffer
    while (count < data->stored_event_count) {
        if (data->stored_events[current].id) {
            free(data->stored_events[current].id);
            data->stored_events[current].id = NULL;
        }

        if (data->stored_events[current].event_type) {
            free(data->stored_events[current].event_type);
            data->stored_events[current].event_type = NULL;
        }

        if (data->stored_events[current].data) {
            free(data->stored_events[current].data);
            data->stored_events[current].data = NULL;
        }

        // Move to the next event in the circular buffer
        current = (current + 1) % MAX_SSE_STORED_EVENTS;
        count++;
    }

    // Reset circular buffer state
    data->event_head = 0;
    data->event_tail = 0;
    data->stored_event_count = 0;

    mcp_mutex_unlock(data->event_mutex);
}

/**
 * @brief Initialize CORS settings for the HTTP transport
 *
 * @param data Transport data to initialize
 * @param config Configuration containing CORS settings
 * @return bool true if successful, false on failure
 */
static bool initialize_cors_settings(http_transport_data_t* data, const mcp_http_config_t* config) {
    if (data == NULL || config == NULL) {
        return false;
    }

    // Set CORS enabled flag
    data->enable_cors = config->enable_cors;

    // Set CORS allow origin
    if (config->cors_allow_origin) {
        data->cors_allow_origin = mcp_strdup(config->cors_allow_origin);
    } else {
        data->cors_allow_origin = mcp_strdup(HTTP_DEFAULT_CORS_ALLOW_ORIGIN);
    }

    if (data->cors_allow_origin == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow origin");
        return false;
    }

    // Set CORS allow methods
    if (config->cors_allow_methods) {
        data->cors_allow_methods = mcp_strdup(config->cors_allow_methods);
    } else {
        data->cors_allow_methods = mcp_strdup(HTTP_DEFAULT_CORS_ALLOW_METHODS);
    }

    if (data->cors_allow_methods == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow methods");
        free(data->cors_allow_origin);
        data->cors_allow_origin = NULL;
        return false;
    }

    // Set CORS allow headers
    if (config->cors_allow_headers) {
        data->cors_allow_headers = mcp_strdup(config->cors_allow_headers);
    } else {
        data->cors_allow_headers = mcp_strdup(HTTP_DEFAULT_CORS_ALLOW_HEADERS);
    }

    if (data->cors_allow_headers == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow headers");
        free(data->cors_allow_origin);
        data->cors_allow_origin = NULL;
        free(data->cors_allow_methods);
        data->cors_allow_methods = NULL;
        return false;
    }

    // Set CORS max age
    data->cors_max_age = config->cors_max_age > 0 ? config->cors_max_age : HTTP_DEFAULT_CORS_MAX_AGE;

    mcp_log_debug("CORS settings initialized: enabled=%d, origin=%s, methods=%s, headers=%s, max_age=%d",
                 data->enable_cors, data->cors_allow_origin, data->cors_allow_methods,
                 data->cors_allow_headers, data->cors_max_age);

    return true;
}

/**
 * @brief Initialize mutexes for the HTTP transport
 *
 * @param data Transport data to initialize
 * @return bool true if successful, false on failure
 */
static bool initialize_mutexes(http_transport_data_t* data) {
    if (data == NULL) {
        return false;
    }

    // Initialize SSE mutex
    data->sse_mutex = mcp_mutex_create();
    if (data->sse_mutex == NULL) {
        mcp_log_error("Failed to create SSE mutex");
        return false;
    }

    // Initialize event mutex
    data->event_mutex = mcp_mutex_create();
    if (data->event_mutex == NULL) {
        mcp_log_error("Failed to create event mutex");
        mcp_mutex_destroy(data->sse_mutex);
        data->sse_mutex = NULL;
        return false;
    }

    return true;
}

/**
 * @brief Set up static file mount for the HTTP server
 *
 * @param data Transport data containing configuration
 * @return bool true if successful, false on failure
 */
static bool setup_static_file_mount(http_transport_data_t* data) {
    if (data == NULL || data->config.doc_root == NULL) {
        return false;
    }

    mcp_log_info("Setting up static file mount for doc_root: %s", data->config.doc_root);

    // Check if directory exists by trying to open a test file
    FILE* test_file = NULL;
    char test_path[HTTP_TEST_PATH_BUFFER_SIZE];
    int path_len = snprintf(test_path, sizeof(test_path), "%s/index.html", data->config.doc_root);

    if (path_len < 0 || path_len >= (int)sizeof(test_path)) {
        mcp_log_error("Test path buffer overflow");
        return false;
    }

    test_file = fopen(test_path, "r");
    if (test_file) {
        fclose(test_file);
        mcp_log_info("Test file exists: %s", test_path);
    } else {
        mcp_log_warn("Test file does not exist: %s", test_path);
    }

    // Allocate and initialize mount structure
    data->mount = (struct lws_http_mount*)calloc(1, sizeof(struct lws_http_mount));
    if (data->mount == NULL) {
        mcp_log_error("Failed to allocate memory for HTTP mount");
        return false;
    }

    // Configure mount
    data->mount->mountpoint = "/";
    data->mount->origin = data->config.doc_root;
    data->mount->def = "index.html";
    data->mount->origin_protocol = LWSMPRO_FILE;

    // mountpoint_len is unsigned char, so we need to be careful with the length
    size_t len = strlen(data->mount->mountpoint);
    if (len > 255) {
        mcp_log_warn("Mountpoint length %zu truncated to 255", len);
        len = 255;
    }
    data->mount->mountpoint_len = (unsigned char)len;

    mcp_log_info("Static file mount configured successfully");
    return true;
}

/**
 * @brief Create HTTP transport
 *
 * This function creates an HTTP transport with the specified configuration.
 *
 * @param config HTTP configuration
 * @return mcp_transport_t* Newly created transport or NULL on failure
 */
mcp_transport_t* mcp_transport_http_create(const mcp_http_config_t* config) {
    if (config == NULL || config->host == NULL) {
        mcp_log_error("Invalid HTTP configuration");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        mcp_log_error("Failed to allocate memory for HTTP transport");
        return NULL;
    }

    // Allocate transport data
    http_transport_data_t* data = (http_transport_data_t*)calloc(1, sizeof(http_transport_data_t));
    if (data == NULL) {
        mcp_log_error("Failed to allocate memory for HTTP transport data");
        free(transport);
        return NULL;
    }

    // Copy configuration
    data->config.host = mcp_strdup(config->host);
    if (data->config.host == NULL) {
        mcp_log_error("Failed to allocate memory for host");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    data->config.port = config->port;
    data->config.use_ssl = config->use_ssl;
    data->config.timeout_ms = config->timeout_ms;

    // Copy optional configuration fields
    if (config->cert_path != NULL) {
        data->config.cert_path = mcp_strdup(config->cert_path);
        if (data->config.cert_path == NULL && config->cert_path[0] != '\0') {
            mcp_log_error("Failed to allocate memory for cert_path");
            free_transport_data(data);
            free(transport);
            return NULL;
        }
    }

    if (config->key_path != NULL) {
        data->config.key_path = mcp_strdup(config->key_path);
        if (data->config.key_path == NULL && config->key_path[0] != '\0') {
            mcp_log_error("Failed to allocate memory for key_path");
            free_transport_data(data);
            free(transport);
            return NULL;
        }
    }

    if (config->doc_root != NULL) {
        data->config.doc_root = mcp_strdup(config->doc_root);
        if (data->config.doc_root == NULL && config->doc_root[0] != '\0') {
            mcp_log_error("Failed to allocate memory for doc_root");
            free_transport_data(data);
            free(transport);
            return NULL;
        }
    }

    // Initialize CORS settings
    if (!initialize_cors_settings(data, config)) {
        mcp_log_error("Failed to initialize CORS settings");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    // Initialize mutexes
    if (!initialize_mutexes(data)) {
        mcp_log_error("Failed to initialize mutexes");
        free_transport_data(data);
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
    data->heartbeat_interval_ms = HTTP_DEFAULT_HEARTBEAT_INTERVAL_MS;
    data->last_heartbeat = time(NULL);
    data->last_heartbeat_time = time(NULL);
    data->heartbeat_counter = 0;

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

    mcp_log_info("HTTP transport created for %s:%d (SSL: %s)",
                data->config.host, data->config.port,
                data->config.use_ssl ? "enabled" : "disabled");

    return transport;
}

/**
 * @brief Start HTTP transport
 *
 * This function starts the HTTP transport, creating the libwebsockets context
 * and event thread.
 *
 * @param transport Transport to start
 * @param message_callback Callback function for received messages
 * @param user_data User data to pass to callbacks
 * @param error_callback Callback function for errors
 * @return int 0 on success, -1 on failure
 */
static int http_transport_start(mcp_transport_t* transport,
                              mcp_transport_message_callback_t message_callback,
                              void* user_data,
                              mcp_transport_error_callback_t error_callback) {
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid parameters for http_transport_start");
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

    // Log the configuration
    mcp_log_info("Creating HTTP server on %s:%d", data->config.host, data->config.port);

    // Configure SSL if enabled
    if (data->config.use_ssl) {
        if (data->config.cert_path == NULL || data->config.key_path == NULL) {
            mcp_log_error("SSL enabled but cert_path or key_path is NULL");
            return -1;
        }

        mcp_log_info("Enabling SSL with cert: %s, key: %s",
                    data->config.cert_path, data->config.key_path);

        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.ssl_cert_filepath = data->config.cert_path;
        info.ssl_private_key_filepath = data->config.key_path;
    }

    // Set up static file mount if doc_root is provided
    if (data->config.doc_root != NULL) {
        if (!setup_static_file_mount(data)) {
            mcp_log_error("Failed to set up static file mount");
            return -1;
        }

        info.mounts = data->mount;
    }

    // Create libwebsockets context
    data->context = lws_create_context(&info);
    if (data->context == NULL) {
        mcp_log_error("Failed to create HTTP server context");
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event thread
    int thread_result = mcp_thread_create(&data->event_thread, http_event_thread_func, transport);
    if (thread_result != 0) {
        mcp_log_error("Failed to create HTTP event thread: %d", thread_result);
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    mcp_log_info("HTTP transport started on %s:%d", data->config.host, data->config.port);
    return 0;
}

/**
 * @brief Stop HTTP transport
 *
 * This function stops the HTTP transport, shutting down the event thread
 * and destroying the libwebsockets context.
 *
 * @param transport Transport to stop
 * @return int 0 on success, -1 on failure
 */
static int http_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid parameters for http_transport_stop");
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    mcp_log_info("Stopping HTTP transport...");

    // Check if transport is already stopped
    if (!data->running) {
        mcp_log_info("HTTP transport already stopped");
        return 0;
    }

    // Set running flag to false to signal event thread to exit
    data->running = false;

    // Force libwebsockets to break out of its service loop
    if (data->context != NULL) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets service");
    }

    // Wait for event thread to exit
    mcp_log_info("Waiting for HTTP event thread to exit...");
    int join_result = mcp_thread_join(data->event_thread, NULL);
    if (join_result != 0) {
        mcp_log_error("Failed to join HTTP event thread: %d", join_result);
    } else {
        mcp_log_info("HTTP event thread joined successfully");
    }

    // Destroy libwebsockets context
    if (data->context != NULL) {
        mcp_log_info("Destroying libwebsockets context...");
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("HTTP transport stopped");
    return 0;
}

/**
 * @brief Destroy HTTP transport
 *
 * This function destroys the HTTP transport, freeing all associated resources.
 *
 * @param transport Transport to destroy
 * @return int 0 on success, -1 on failure
 */
static int http_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid parameters for http_transport_destroy");
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    mcp_log_info("Destroying HTTP transport...");

    // Stop transport if running
    if (data->running) {
        mcp_log_info("Transport still running, stopping first");
        http_transport_stop(transport);
    }

    // Free all transport data
    free_transport_data(data);

    // Free transport structure
    free(transport);

    mcp_log_info("HTTP transport destroyed");
    return 0;
}

/**
 * @brief HTTP event thread function
 *
 * This function runs the libwebsockets event loop and sends SSE heartbeats.
 *
 * @param arg Transport pointer cast to void*
 * @return void* Always returns NULL
 */
void* http_event_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid parameters for http_event_thread_func");
        return NULL;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    // Check if context is initialized
    if (data->context == NULL) {
        mcp_log_error("HTTP event thread started with NULL context");
        return NULL;
    }

    mcp_log_info("HTTP event thread started");

    // Run event loop until data->running is set to false
    while (data->running) {
        // Service libwebsockets with timeout
        int service_result = lws_service(data->context, HTTP_LWS_SERVICE_TIMEOUT_MS);
        if (service_result < 0) {
            mcp_log_error("lws_service returned error: %d", service_result);
            // Don't break the loop on error, just continue
        }

        // Send heartbeat if needed
        send_sse_heartbeat(data);
    }

    mcp_log_info("HTTP event thread exited");
    return NULL;
}
