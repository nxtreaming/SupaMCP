#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_http_streamable_transport.h"
#include "internal/http_streamable_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Default values
#define HTTP_STREAMABLE_DEFAULT_HEARTBEAT_INTERVAL_MS 30000  // 30 seconds
#define HTTP_STREAMABLE_DEFAULT_CORS_MAX_AGE 86400           // 24 hours
#define HTTP_STREAMABLE_DEFAULT_CORS_ALLOW_ORIGIN "*"
#define HTTP_STREAMABLE_DEFAULT_CORS_ALLOW_METHODS "GET, POST, OPTIONS, DELETE"
#define HTTP_STREAMABLE_DEFAULT_CORS_ALLOW_HEADERS "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID"

// LWS service timeout in milliseconds
#define HTTP_STREAMABLE_LWS_SERVICE_TIMEOUT_MS 100

// Cleanup thread interval in seconds
#define HTTP_STREAMABLE_CLEANUP_INTERVAL_SECONDS 60

// Forward declarations of static functions
static int http_streamable_transport_start(mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback);
static int http_streamable_transport_stop(mcp_transport_t* transport);
static int http_streamable_transport_destroy(mcp_transport_t* transport);
static int http_streamable_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static int http_streamable_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);

static void free_transport_data(http_streamable_transport_data_t* data);
static bool initialize_cors_settings(http_streamable_transport_data_t* data, const mcp_http_streamable_config_t* config);
static bool initialize_mutexes(http_streamable_transport_data_t* data);
static bool setup_static_file_mount(http_streamable_transport_data_t* data);
static bool initialize_session_manager(http_streamable_transport_data_t* data);

/**
 * @brief Free all memory associated with transport data
 */
static void free_transport_data(http_streamable_transport_data_t* data) {
    if (data == NULL) {
        return;
    }

    // Free configuration strings
    if (data->mcp_endpoint) {
        free(data->mcp_endpoint);
        data->mcp_endpoint = NULL;
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

    // Free allowed origins
    if (data->allowed_origins) {
        free_allowed_origins(data->allowed_origins, data->allowed_origins_count);
        data->allowed_origins = NULL;
        data->allowed_origins_count = 0;
    }

    // Free mount structure
    if (data->mount) {
        free(data->mount);
        data->mount = NULL;
    }

    // Free SSE clients array
    if (data->sse_clients) {
        free(data->sse_clients);
        data->sse_clients = NULL;
    }

    // Destroy global SSE context
    if (data->global_sse_context) {
        sse_stream_context_destroy(data->global_sse_context);
        data->global_sse_context = NULL;
    }

    // Destroy session manager
    if (data->session_manager) {
        mcp_session_manager_destroy(data->session_manager);
        data->session_manager = NULL;
    }

    // Destroy mutexes
    if (data->sse_mutex) {
        mcp_mutex_destroy(data->sse_mutex);
        data->sse_mutex = NULL;
    }

    // Free the data structure itself
    free(data);
}

/**
 * @brief Initialize CORS settings for the transport
 */
static bool initialize_cors_settings(http_streamable_transport_data_t* data, const mcp_http_streamable_config_t* config) {
    if (data == NULL || config == NULL) {
        return false;
    }

    data->enable_cors = config->enable_cors;

    // Set CORS allow origin
    if (config->cors_allow_origin) {
        data->cors_allow_origin = mcp_strdup(config->cors_allow_origin);
    } else {
        data->cors_allow_origin = mcp_strdup(HTTP_STREAMABLE_DEFAULT_CORS_ALLOW_ORIGIN);
    }

    if (data->cors_allow_origin == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow origin");
        return false;
    }

    // Set CORS allow methods
    if (config->cors_allow_methods) {
        data->cors_allow_methods = mcp_strdup(config->cors_allow_methods);
    } else {
        data->cors_allow_methods = mcp_strdup(HTTP_STREAMABLE_DEFAULT_CORS_ALLOW_METHODS);
    }

    if (data->cors_allow_methods == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow methods");
        return false;
    }

    // Set CORS allow headers
    if (config->cors_allow_headers) {
        data->cors_allow_headers = mcp_strdup(config->cors_allow_headers);
    } else {
        data->cors_allow_headers = mcp_strdup(HTTP_STREAMABLE_DEFAULT_CORS_ALLOW_HEADERS);
    }

    if (data->cors_allow_headers == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow headers");
        return false;
    }

    // Set CORS max age
    data->cors_max_age = config->cors_max_age > 0 ? config->cors_max_age : HTTP_STREAMABLE_DEFAULT_CORS_MAX_AGE;

    mcp_log_debug("CORS settings initialized: enabled=%d, origin=%s, methods=%s, headers=%s, max_age=%d",
                 data->enable_cors, data->cors_allow_origin, data->cors_allow_methods,
                 data->cors_allow_headers, data->cors_max_age);

    return true;
}

/**
 * @brief Initialize mutexes for the transport
 */
static bool initialize_mutexes(http_streamable_transport_data_t* data) {
    if (data == NULL) {
        return false;
    }

    // Initialize SSE mutex
    data->sse_mutex = mcp_mutex_create();
    if (data->sse_mutex == NULL) {
        mcp_log_error("Failed to create SSE mutex");
        return false;
    }

    return true;
}

/**
 * @brief Set up static file mount for the HTTP server
 */
static bool setup_static_file_mount(http_streamable_transport_data_t* data) {
    if (data == NULL || data->config.doc_root == NULL) {
        return false;
    }

    mcp_log_info("Setting up static file mount for doc_root: %s", data->config.doc_root);

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
 * @brief Initialize session manager
 */
static bool initialize_session_manager(http_streamable_transport_data_t* data) {
    if (data == NULL) {
        return false;
    }

    if (!data->config.enable_sessions) {
        mcp_log_info("Session management disabled");
        return true;
    }

    uint32_t timeout = data->config.session_timeout_seconds;
    if (timeout == 0) {
        timeout = MCP_SESSION_DEFAULT_TIMEOUT_SECONDS;
    }

    data->session_manager = mcp_session_manager_create(timeout);
    if (data->session_manager == NULL) {
        mcp_log_error("Failed to create session manager");
        return false;
    }

    mcp_log_info("Session manager initialized with timeout: %u seconds", timeout);
    return true;
}

mcp_transport_t* mcp_transport_http_streamable_create(const mcp_http_streamable_config_t* config) {
    if (config == NULL || config->host == NULL) {
        mcp_log_error("Invalid Streamable HTTP configuration");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        mcp_log_error("Failed to allocate memory for Streamable HTTP transport");
        return NULL;
    }

    // Allocate transport data
    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)calloc(1, sizeof(http_streamable_transport_data_t));
    if (data == NULL) {
        mcp_log_error("Failed to allocate memory for Streamable HTTP transport data");
        free(transport);
        return NULL;
    }

    // Copy configuration
    memcpy(&data->config, config, sizeof(mcp_http_streamable_config_t));

    // Set MCP endpoint
    if (config->mcp_endpoint) {
        data->mcp_endpoint = mcp_strdup(config->mcp_endpoint);
    } else {
        data->mcp_endpoint = mcp_strdup(MCP_ENDPOINT_DEFAULT);
    }

    if (data->mcp_endpoint == NULL) {
        mcp_log_error("Failed to allocate memory for MCP endpoint");
        free_transport_data(data);
        free(transport);
        return NULL;
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

    // Initialize session manager
    if (!initialize_session_manager(data)) {
        mcp_log_error("Failed to initialize session manager");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    // Parse allowed origins if origin validation is enabled
    if (config->validate_origin && config->allowed_origins) {
        if (!parse_allowed_origins(config->allowed_origins, &data->allowed_origins, &data->allowed_origins_count)) {
            mcp_log_error("Failed to parse allowed origins");
            free_transport_data(data);
            free(transport);
            return NULL;
        }
        data->validate_origin = true;
    } else {
        data->validate_origin = false;
    }

    // Initialize SSE settings
    data->send_heartbeats = config->send_heartbeats;
    data->heartbeat_interval_ms = config->heartbeat_interval_ms > 0 ? 
                                  config->heartbeat_interval_ms : 
                                  HTTP_STREAMABLE_DEFAULT_HEARTBEAT_INTERVAL_MS;

    // Create global SSE context for non-session streams
    size_t max_events = config->max_stored_events > 0 ? config->max_stored_events : MAX_SSE_STORED_EVENTS_DEFAULT;
    data->global_sse_context = sse_stream_context_create(max_events);
    if (data->global_sse_context == NULL) {
        mcp_log_error("Failed to create global SSE context");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    // Initialize SSE clients array
    data->max_sse_clients = 1000; // Default maximum
    data->sse_clients = (struct lws**)calloc(data->max_sse_clients, sizeof(struct lws*));
    if (data->sse_clients == NULL) {
        mcp_log_error("Failed to allocate SSE clients array");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    // Set transport type to server
    transport->type = MCP_TRANSPORT_TYPE_SERVER;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_HTTP_STREAMABLE;

    // Initialize server operations
    transport->server.start = http_streamable_transport_start;
    transport->server.stop = http_streamable_transport_stop;
    transport->server.destroy = http_streamable_transport_destroy;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    mcp_log_info("Streamable HTTP transport created for %s:%d (SSL: %s, endpoint: %s)",
                data->config.host, data->config.port,
                data->config.use_ssl ? "enabled" : "disabled",
                data->mcp_endpoint);

    return transport;
}

/**
 * @brief Start Streamable HTTP transport
 */
static int http_streamable_transport_start(mcp_transport_t* transport,
                                         mcp_transport_message_callback_t message_callback,
                                         void* user_data,
                                         mcp_transport_error_callback_t error_callback) {
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid parameters for http_streamable_transport_start");
        return -1;
    }

    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;

    // Store callback and user data
    data->message_callback = message_callback;
    data->callback_user_data = user_data;
    data->error_callback = error_callback;

    // Create libwebsockets context
    struct lws_context_creation_info info = {0};
    info.port = data->config.port;
    info.iface = data->config.host;
    info.protocols = http_streamable_protocols;
    info.user = data;

    // Set options
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE |
                   LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

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
        mcp_log_error("Failed to create Streamable HTTP server context");
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event thread
    int thread_result = mcp_thread_create(&data->event_thread, http_streamable_event_thread_func, transport);
    if (thread_result != 0) {
        mcp_log_error("Failed to create Streamable HTTP event thread: %d", thread_result);
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    // Create cleanup thread for session management
    if (data->session_manager) {
        thread_result = mcp_thread_create(&data->cleanup_thread, http_streamable_cleanup_thread_func, transport);
        if (thread_result != 0) {
            mcp_log_error("Failed to create cleanup thread: %d", thread_result);
            // Continue without cleanup thread - not critical
        }
    }

    mcp_log_info("Streamable HTTP transport started on %s:%d", data->config.host, data->config.port);
    return 0;
}

/**
 * @brief Stop Streamable HTTP transport
 */
static int http_streamable_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;

    // Set running flag to false
    data->running = false;

    // Wait for event thread to finish
    if (data->event_thread) {
        mcp_thread_join(data->event_thread, NULL);
        data->event_thread = 0;
    }

    // Wait for cleanup thread to finish
    if (data->cleanup_thread) {
        mcp_thread_join(data->cleanup_thread, NULL);
        data->cleanup_thread = 0;
    }

    // Destroy libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("Streamable HTTP transport stopped");
    return 0;
}

/**
 * @brief Destroy Streamable HTTP transport
 */
static int http_streamable_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return -1;
    }

    // Stop the transport first
    http_streamable_transport_stop(transport);

    // Free transport data
    if (transport->transport_data) {
        free_transport_data((http_streamable_transport_data_t*)transport->transport_data);
        transport->transport_data = NULL;
    }

    // Free transport structure
    free(transport);

    mcp_log_info("Streamable HTTP transport destroyed");
    return 0;
}

/**
 * @brief Send data through Streamable HTTP transport
 */
static int http_streamable_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    mcp_buffer_t buffer = { data, size };
    return http_streamable_transport_sendv(transport, &buffer, 1);
}

/**
 * @brief Send data from multiple buffers through Streamable HTTP transport
 */
static int http_streamable_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (transport == NULL || transport->transport_data == NULL || buffers == NULL || buffer_count == 0) {
        return -1;
    }

    http_streamable_transport_data_t* transport_data = (http_streamable_transport_data_t*)transport->transport_data;

    if (!transport_data->running) {
        mcp_log_error("Streamable HTTP transport not running");
        return -1;
    }

    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        total_size += buffers[i].size;
    }

    // Combine buffers into a single message
    char* message = (char*)malloc(total_size + 1);
    if (message == NULL) {
        mcp_log_error("Failed to allocate memory for message");
        return -1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        memcpy(message + offset, buffers[i].data, buffers[i].size);
        offset += buffers[i].size;
    }
    message[total_size] = '\0';

    // Send as SSE event to all connected clients
    mcp_mutex_lock(transport_data->sse_mutex);

    int sent_count = 0;
    for (size_t i = 0; i < transport_data->sse_client_count; i++) {
        if (transport_data->sse_clients[i] != NULL) {
            if (send_sse_event(transport_data->sse_clients[i], NULL, "message", message) == 0) {
                sent_count++;
            }
        }
    }

    mcp_mutex_unlock(transport_data->sse_mutex);

    free(message);

    mcp_log_debug("Sent message to %d SSE clients", sent_count);
    return sent_count > 0 ? 0 : -1;
}

// Public API functions


int mcp_transport_http_streamable_send_with_session(mcp_transport_t* transport,
                                                   const void* data,
                                                   size_t size,
                                                   const char* session_id) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL) {
        return -1;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_HTTP_STREAMABLE) {
        mcp_log_error("Transport is not a Streamable HTTP transport");
        return -1;
    }

    http_streamable_transport_data_t* transport_data = (http_streamable_transport_data_t*)transport->transport_data;

    if (!transport_data->running) {
        mcp_log_error("Streamable HTTP transport not running");
        return -1;
    }

    // If session_id is provided, send only to clients in that session
    if (session_id != NULL && transport_data->session_manager != NULL) {
        mcp_http_session_t* session = mcp_session_manager_get_session(transport_data->session_manager, session_id);
        if (session == NULL) {
            mcp_log_error("Session not found: %s", session_id);
            return -1;
        }

        // TODO: Send to specific session clients
        // For now, fall back to sending to all clients
        mcp_log_warn("Session-specific sending not yet implemented, sending to all clients");
    }

    // Send to all clients (fallback behavior)
    return http_streamable_transport_send(transport, data, size);
}

const char* mcp_transport_http_streamable_get_endpoint(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return NULL;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_HTTP_STREAMABLE) {
        return NULL;
    }

    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;
    return data->mcp_endpoint;
}

bool mcp_transport_http_streamable_has_sessions(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return false;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_HTTP_STREAMABLE) {
        return false;
    }

    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;
    return data->session_manager != NULL;
}

size_t mcp_transport_http_streamable_get_session_count(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return 0;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_HTTP_STREAMABLE) {
        return 0;
    }

    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;
    if (data->session_manager == NULL) {
        return 0;
    }

    return mcp_session_manager_get_active_count(data->session_manager);
}

bool mcp_transport_http_streamable_terminate_session(mcp_transport_t* transport, const char* session_id) {
    if (transport == NULL || transport->transport_data == NULL || session_id == NULL) {
        return false;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_HTTP_STREAMABLE) {
        return false;
    }

    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;
    if (data->session_manager == NULL) {
        return false;
    }

    return mcp_session_manager_terminate_session(data->session_manager, session_id);
}
