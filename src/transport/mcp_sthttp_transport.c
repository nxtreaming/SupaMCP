#include "mcp_sthttp_transport.h"
#include "internal/sthttp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Default values
#define STHTTP_DEFAULT_HEARTBEAT_INTERVAL_MS 30000  // 30 seconds
#define STHTTP_DEFAULT_CORS_MAX_AGE 86400           // 24 hours
#define STHTTP_DEFAULT_CORS_ALLOW_ORIGIN "*"
#define STHTTP_DEFAULT_CORS_ALLOW_METHODS "GET, POST, OPTIONS, DELETE"
#define STHTTP_DEFAULT_CORS_ALLOW_HEADERS "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID"

// LWS service timeout in milliseconds
#define STHTTP_LWS_SERVICE_TIMEOUT_MS 100

// Cleanup thread interval in seconds
#define STHTTP_CLEANUP_INTERVAL_SECONDS 60

// Forward declarations of static functions
static int sthttp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback,
                                 void* user_data, mcp_transport_error_callback_t error_callback);
static int sthttp_transport_stop(mcp_transport_t* transport);
static int sthttp_transport_destroy(mcp_transport_t* transport);
static int sthttp_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static int sthttp_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);

static void free_transport_data(sthttp_transport_data_t* data);
static bool initialize_cors_settings(sthttp_transport_data_t* data, const mcp_sthttp_config_t* config);
static bool initialize_mutexes(sthttp_transport_data_t* data);
static bool setup_static_file_mount(sthttp_transport_data_t* data);
static bool initialize_session_manager(sthttp_transport_data_t* data);

/**
 * @brief Free all memory associated with transport data
 */
static void free_transport_data(sthttp_transport_data_t* data) {
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

    // Free dynamic SSE clients array
    if (data->sse_clients) {
        dynamic_sse_clients_destroy(data->sse_clients);
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

    // Destroy cleanup synchronization objects
    if (data->cleanup_condition) {
        mcp_cond_destroy(data->cleanup_condition);
        data->cleanup_condition = NULL;
    }

    if (data->cleanup_mutex) {
        mcp_mutex_destroy(data->cleanup_mutex);
        data->cleanup_mutex = NULL;
    }

    // Destroy mutexes (note: sse_mutex is now part of dynamic_sse_clients)
    // No longer needed since dynamic_sse_clients manages its own mutex

    // Free the data structure itself
    free(data);
}

/**
 * @brief Initialize CORS settings for the transport
 */
static bool initialize_cors_settings(sthttp_transport_data_t* data, const mcp_sthttp_config_t* config) {
    if (data == NULL || config == NULL) {
        return false;
    }

    data->enable_cors = config->enable_cors;

    // Set CORS allow origin
    if (config->cors_allow_origin) {
        data->cors_allow_origin = mcp_strdup(config->cors_allow_origin);
    } else {
        data->cors_allow_origin = mcp_strdup(STHTTP_DEFAULT_CORS_ALLOW_ORIGIN);
    }

    if (data->cors_allow_origin == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow origin");
        return false;
    }

    // Set CORS allow methods
    if (config->cors_allow_methods) {
        data->cors_allow_methods = mcp_strdup(config->cors_allow_methods);
    } else {
        data->cors_allow_methods = mcp_strdup(STHTTP_DEFAULT_CORS_ALLOW_METHODS);
    }

    if (data->cors_allow_methods == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow methods");
        return false;
    }

    // Set CORS allow headers
    if (config->cors_allow_headers) {
        data->cors_allow_headers = mcp_strdup(config->cors_allow_headers);
    } else {
        data->cors_allow_headers = mcp_strdup(STHTTP_DEFAULT_CORS_ALLOW_HEADERS);
    }

    if (data->cors_allow_headers == NULL) {
        mcp_log_error("Failed to allocate memory for CORS allow headers");
        return false;
    }

    // Set CORS max age
    data->cors_max_age = config->cors_max_age > 0 ? config->cors_max_age : STHTTP_DEFAULT_CORS_MAX_AGE;

    mcp_log_debug("CORS settings initialized: enabled=%d, origin=%s, methods=%s, headers=%s, max_age=%d",
                 data->enable_cors, data->cors_allow_origin, data->cors_allow_methods,
                 data->cors_allow_headers, data->cors_max_age);

    return true;
}

/**
 * @brief Initialize mutexes for the transport
 */
static bool initialize_mutexes(sthttp_transport_data_t* data) {
    if (data == NULL) {
        return false;
    }

    // No longer need SSE mutex since dynamic_sse_clients manages its own mutex
    return true;
}

/**
 * @brief Set up static file mount for the HTTP server
 */
static bool setup_static_file_mount(sthttp_transport_data_t* data) {
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
static bool initialize_session_manager(sthttp_transport_data_t* data) {
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

/**
 * @brief Start Streamable HTTP transport
 */
static int sthttp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback,
                                 void* user_data, mcp_transport_error_callback_t error_callback) {
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid parameters for sthttp_transport_start");
        return -1;
    }

    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;

    // Store callback and user data
    data->message_callback = message_callback;
    data->callback_user_data = user_data;
    data->error_callback = error_callback;

    // Create libwebsockets context
    struct lws_context_creation_info info = {0};
    info.port = data->config.port;
    info.iface = data->config.host;
    info.protocols = sthttp_protocols;
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

        mcp_log_info("Enabling SSL with cert: %s, key: %s", data->config.cert_path, data->config.key_path);

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
    int thread_result = mcp_thread_create(&data->event_thread, sthttp_event_thread_func, transport);
    if (thread_result != 0) {
        mcp_log_error("Failed to create Streamable HTTP event thread: %d", thread_result);
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    // Create cleanup thread for session management
    if (data->session_manager) {
        thread_result = mcp_thread_create(&data->cleanup_thread, sthttp_cleanup_thread_func, transport);
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
static int sthttp_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;

    // Set running flag to false
    data->running = false;

    // Signal cleanup thread to shutdown
    if (data->cleanup_mutex && data->cleanup_condition) {
        mcp_mutex_lock(data->cleanup_mutex);
        data->cleanup_shutdown = true;
        mcp_cond_signal(data->cleanup_condition);
        mcp_mutex_unlock(data->cleanup_mutex);
    }

    // Cancel all connections to help libwebsockets shutdown faster
    if (data->context) {
        lws_cancel_service(data->context);
    }

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
static int sthttp_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return -1;
    }

    // Stop the transport first
    sthttp_transport_stop(transport);

    // Free transport data
    if (transport->transport_data) {
        free_transport_data((sthttp_transport_data_t*)transport->transport_data);
        transport->transport_data = NULL;
    }

    // Cleanup CORS header cache
    cors_header_cache_cleanup();

    // Free transport structure
    free(transport);

    mcp_log_info("Streamable HTTP transport destroyed");
    return 0;
}

/**
 * @brief Send data through Streamable HTTP transport
 */
static int sthttp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    mcp_buffer_t buffer = { data, size };
    return sthttp_transport_sendv(transport, &buffer, 1);
}

/**
 * @brief Send data from multiple buffers through Streamable HTTP transport
 */
static int sthttp_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (transport == NULL || transport->transport_data == NULL || buffers == NULL || buffer_count == 0) {
        return -1;
    }

    sthttp_transport_data_t* transport_data = (sthttp_transport_data_t*)transport->transport_data;
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

    // Send as SSE event to all connected clients using dynamic array
    int sent_count = dynamic_sse_clients_broadcast(transport_data->sse_clients, NULL, "message", message);

    free(message);

    mcp_log_debug("Sent message to %d SSE clients", sent_count);
    return sent_count > 0 ? 0 : -1;
}

// Public API functions

mcp_transport_t* mcp_transport_sthttp_create(const mcp_sthttp_config_t* config) {
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
    sthttp_transport_data_t* data = (sthttp_transport_data_t*)calloc(1, sizeof(sthttp_transport_data_t));
    if (data == NULL) {
        mcp_log_error("Failed to allocate memory for Streamable HTTP transport data");
        free(transport);
        return NULL;
    }

    // Copy configuration
    memcpy(&data->config, config, sizeof(mcp_sthttp_config_t));

    // Set MCP endpoint
    if (config->mcp_endpoint) {
        data->mcp_endpoint = mcp_strdup(config->mcp_endpoint);
    }
    else {
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

    // Initialize CORS header cache
    if (cors_header_cache_init() != 0) {
        mcp_log_error("Failed to initialize CORS header cache");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    // Enable optimized parsers by default
    data->use_optimized_parsers = true;
    mcp_log_info("Streamable HTTP optimizations enabled");

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
    }
    else {
        data->validate_origin = false;
    }

    // Initialize SSE settings
    data->send_heartbeats = config->send_heartbeats;
    data->heartbeat_interval_ms = config->heartbeat_interval_ms > 0 ?
        config->heartbeat_interval_ms :
        STHTTP_DEFAULT_HEARTBEAT_INTERVAL_MS;

    // Create global SSE context for non-session streams
    size_t max_events = config->max_stored_events > 0 ? config->max_stored_events : MAX_SSE_STORED_EVENTS_DEFAULT;
    data->global_sse_context = sse_stream_context_create(max_events);
    if (data->global_sse_context == NULL) {
        mcp_log_error("Failed to create global SSE context");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    // Initialize dynamic SSE clients array
    size_t initial_capacity = config->max_sse_clients > 0 ?
        (config->max_sse_clients < STHTTP_INITIAL_SSE_CLIENTS ? config->max_sse_clients : STHTTP_INITIAL_SSE_CLIENTS) :
        STHTTP_INITIAL_SSE_CLIENTS;

    data->sse_clients = dynamic_sse_clients_create(initial_capacity);
    if (data->sse_clients == NULL) {
        mcp_log_error("Failed to create dynamic SSE clients array");
        free_transport_data(data);
        free(transport);
        return NULL;
    }
    mcp_log_info("Initialized dynamic SSE clients array with initial capacity %zu", initial_capacity);

    // Initialize cleanup thread synchronization
    data->cleanup_mutex = mcp_mutex_create();
    if (data->cleanup_mutex == NULL) {
        mcp_log_error("Failed to create cleanup mutex");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    data->cleanup_condition = mcp_cond_create();
    if (data->cleanup_condition == NULL) {
        mcp_log_error("Failed to create cleanup condition variable");
        free_transport_data(data);
        free(transport);
        return NULL;
    }

    data->cleanup_shutdown = false;

    // Set transport type to server
    transport->type = MCP_TRANSPORT_TYPE_SERVER;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_STHTTP;

    // Initialize server operations
    transport->server.start = sthttp_transport_start;
    transport->server.stop = sthttp_transport_stop;
    transport->server.destroy = sthttp_transport_destroy;

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

int mcp_transport_sthttp_send_with_session(mcp_transport_t* transport, const void* data, size_t size, const char* session_id) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL) {
        return -1;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_STHTTP) {
        mcp_log_error("Transport is not a Streamable HTTP transport");
        return -1;
    }

    sthttp_transport_data_t* transport_data = (sthttp_transport_data_t*)transport->transport_data;

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
    return sthttp_transport_send(transport, data, size);
}

const char* mcp_transport_sthttp_get_endpoint(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return NULL;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_STHTTP) {
        return NULL;
    }

    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;
    return data->mcp_endpoint;
}

bool mcp_transport_sthttp_has_sessions(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return false;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_STHTTP) {
        return false;
    }

    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;
    return data->session_manager != NULL;
}

size_t mcp_transport_sthttp_get_session_count(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return 0;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_STHTTP) {
        return 0;
    }

    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;
    if (data->session_manager == NULL) {
        return 0;
    }

    return mcp_session_manager_get_active_count(data->session_manager);
}

bool mcp_transport_sthttp_terminate_session(mcp_transport_t* transport, const char* session_id) {
    if (transport == NULL || transport->transport_data == NULL || session_id == NULL) {
        return false;
    }

    if (transport->protocol_type != MCP_TRANSPORT_PROTOCOL_STHTTP) {
        return false;
    }

    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;
    if (data->session_manager == NULL) {
        return false;
    }

    return mcp_session_manager_terminate_session(data->session_manager, session_id);
}
