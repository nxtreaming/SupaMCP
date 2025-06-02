/**
 * @file mcp_sthttp_client_transport.c
 * @brief HTTP Streamable Client Transport Implementation
 *
 * This file implements the client-side HTTP Streamable transport for MCP 2025-03-26.
 * It provides functionality for connecting to MCP servers via HTTP POST requests
 * and receiving events via SSE streams.
 */
#include "internal/sthttp_client_internal.h"

// Forward declarations for transport interface
static int sthttp_client_init(mcp_transport_t* transport);
static void sthttp_client_destroy(mcp_transport_t* transport);
static int sthttp_client_start(mcp_transport_t* transport,
                                       mcp_transport_message_callback_t message_callback,
                                       void* user_data,
                                       mcp_transport_error_callback_t error_callback);
static int sthttp_client_stop(mcp_transport_t* transport);
static int sthttp_client_send(mcp_transport_t* transport, const void* data, size_t size);
static int sthttp_client_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);
static int sthttp_client_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);

/**
 * @brief Create HTTP Streamable client transport
 */
mcp_transport_t* mcp_transport_sthttp_client_create(const mcp_sthttp_client_config_t* config) {
    if (config == NULL) {
        mcp_log_error("HTTP Streamable client config cannot be NULL");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        mcp_log_error("Failed to allocate HTTP Streamable client transport");
        return NULL;
    }

    // Allocate client data
    sthttp_client_data_t* data = (sthttp_client_data_t*)malloc(sizeof(sthttp_client_data_t));
    if (data == NULL) {
        mcp_log_error("Failed to allocate HTTP Streamable client data");
        free(transport);
        return NULL;
    }

    // Initialize client data
    if (sthttp_client_init_data(data, config) != 0) {
        mcp_log_error("Failed to initialize HTTP Streamable client data");
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport type and protocol
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_STHTTP;

    // Initialize client operations
    transport->client.init = sthttp_client_init;
    transport->client.destroy = sthttp_client_destroy;
    transport->client.start = sthttp_client_start;
    transport->client.stop = sthttp_client_stop;
    transport->client.send = sthttp_client_send;
    transport->client.sendv = sthttp_client_sendv;
    transport->client.receive = sthttp_client_receive;

    // Set transport data
    transport->transport_data = data;

    mcp_log_info("HTTP Streamable client transport created for %s:%d", config->host, config->port);
    return transport;
}

/**
 * @brief Initialize the transport
 */
static int sthttp_client_init(mcp_transport_t* transport) {
    (void)transport;
    // This function is called by the transport framework
    // The actual initialization is done in the create function
    return 0;
}

/**
 * @brief Initialize HTTP client transport data
 */
int sthttp_client_init_data(sthttp_client_data_t* data, const mcp_sthttp_client_config_t* config) {
    if (data == NULL || config == NULL) {
        return -1;
    }

    // Clear the structure
    memset(data, 0, sizeof(sthttp_client_data_t));

    // Copy configuration
    data->config = *config;
    
    // Duplicate string fields
    if (config->host) {
        data->config.host = mcp_strdup(config->host);
        if (data->config.host == NULL) {
            return -1;
        }
    }
    
    if (config->mcp_endpoint) {
        data->config.mcp_endpoint = mcp_strdup(config->mcp_endpoint);
        if (data->config.mcp_endpoint == NULL) {
            free((void*)data->config.host);
            return -1;
        }
    }
    
    if (config->user_agent) {
        data->config.user_agent = mcp_strdup(config->user_agent);
    }
    
    if (config->api_key) {
        data->config.api_key = mcp_strdup(config->api_key);
    }
    
    if (config->custom_headers) {
        data->config.custom_headers = mcp_strdup(config->custom_headers);
    }

    // Initialize state
    data->state = MCP_CLIENT_STATE_DISCONNECTED;
    data->has_session = false;
    data->session_id = NULL;
    data->auto_reconnect = config->auto_reconnect_sse;
    data->reconnect_attempts = 0;
    data->shutdown_requested = false;

    // Create mutexes
    data->state_mutex = mcp_mutex_create();
    data->sse_mutex = mcp_mutex_create();
    data->stats_mutex = mcp_mutex_create();
    
    if (data->state_mutex == NULL || data->sse_mutex == NULL || data->stats_mutex == NULL) {
        sthttp_client_cleanup(data);
        return -1;
    }

    // Initialize statistics
    memset(&data->stats, 0, sizeof(data->stats));
    data->stats.connection_start_time = time(NULL);

    mcp_log_debug("HTTP Streamable client data initialized");
    return 0;
}

/**
 * @brief Cleanup HTTP client transport data
 */
void sthttp_client_cleanup(sthttp_client_data_t* data) {
    if (data == NULL) {
        return;
    }

    // Stop threads
    data->shutdown_requested = true;
    
    if (data->sse_conn && data->sse_conn->sse_thread_running) {
        data->sse_conn->sse_thread_running = false;
        mcp_thread_join(data->sse_conn->sse_thread, NULL);
    }

    if (data->reconnect_thread_running) {
        data->reconnect_thread_running = false;
        mcp_thread_join(data->reconnect_thread, NULL);
    }

    // Cleanup SSE connection
    if (data->sse_conn) {
        sse_client_disconnect(data);
    }

    // Free string fields
    free((void*)data->config.host);
    free((void*)data->config.mcp_endpoint);
    free((void*)data->config.user_agent);
    free((void*)data->config.api_key);
    free((void*)data->config.custom_headers);
    free(data->session_id);

    // Destroy mutexes
    if (data->state_mutex) {
        mcp_mutex_destroy(data->state_mutex);
    }
    if (data->sse_mutex) {
        mcp_mutex_destroy(data->sse_mutex);
    }
    if (data->stats_mutex) {
        mcp_mutex_destroy(data->stats_mutex);
    }

    mcp_log_debug("HTTP Streamable client data cleaned up");
}

/**
 * @brief Start the client transport
 */
static int sthttp_client_start(mcp_transport_t* transport,
                                       mcp_transport_message_callback_t message_callback,
                                       void* user_data,
                                       mcp_transport_error_callback_t error_callback) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;

    // Store callbacks
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    data->message_callback = message_callback;
    data->message_callback_user_data = user_data;
    data->error_callback = error_callback;
    
    mcp_mutex_lock(data->state_mutex);
    
    if (data->state != MCP_CLIENT_STATE_DISCONNECTED) {
        mcp_mutex_unlock(data->state_mutex);
        mcp_log_warn("HTTP Streamable client already started");
        return -1;
    }

    // Initialize socket system
    if (mcp_socket_init() != 0) {
        mcp_mutex_unlock(data->state_mutex);
        mcp_log_error("Failed to initialize socket system");
        return -1;
    }

    http_client_set_state(data, MCP_CLIENT_STATE_CONNECTING);
    mcp_mutex_unlock(data->state_mutex);

    // Start SSE connection if enabled
    if (data->config.enable_sse_streams) {
        if (sse_client_connect(data) != 0) {
            mcp_log_warn("Failed to establish SSE connection, continuing without SSE");
        }
    }

    http_client_set_state(data, MCP_CLIENT_STATE_CONNECTED);
    
    mcp_log_info("HTTP Streamable client transport started");
    return 0;
}

/**
 * @brief Stop the client transport
 */
static int sthttp_client_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
    
    mcp_mutex_lock(data->state_mutex);
    
    if (data->state == MCP_CLIENT_STATE_DISCONNECTED) {
        mcp_mutex_unlock(data->state_mutex);
        return 0;
    }

    data->shutdown_requested = true;
    http_client_set_state(data, MCP_CLIENT_STATE_DISCONNECTED);
    mcp_mutex_unlock(data->state_mutex);

    // Disconnect SSE
    sse_client_disconnect(data);

    // Cleanup socket system
    mcp_socket_cleanup();

    mcp_log_info("HTTP Streamable client transport stopped");
    return 0;
}

/**
 * @brief Send message via the client transport
 */
static int sthttp_client_send(mcp_transport_t* transport, const void* data_ptr, size_t size) {
    if (transport == NULL || transport->transport_data == NULL || data_ptr == NULL || size == 0) {
        return -1;
    }

    // Convert data to string (assuming it's JSON)
    char* message = (char*)malloc(size + 1);
    if (message == NULL) {
        return -1;
    }
    memcpy(message, data_ptr, size);
    message[size] = '\0';

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
    
    mcp_mutex_lock(data->state_mutex);
    
    if (data->state != MCP_CLIENT_STATE_CONNECTED && data->state != MCP_CLIENT_STATE_SSE_CONNECTED) {
        mcp_mutex_unlock(data->state_mutex);
        mcp_log_error("HTTP Streamable client not connected");
        return -1;
    }
    
    mcp_mutex_unlock(data->state_mutex);

    // Send HTTP POST request
    http_response_t response;
    memset(&response, 0, sizeof(response));
    
    int result = http_client_send_request(data, message, &response);
    if (result == 0 && response.body != NULL) {
        // Update statistics
        http_client_update_stats(data, "request_sent");
        http_client_update_stats(data, "response_received");
        
        // Call message callback with response
        if (data->message_callback) {
            int error_code = 0;
            char* callback_response = data->message_callback(data->message_callback_user_data, response.body, strlen(response.body), &error_code);
            if (callback_response) {
                free(callback_response);
            }
        }
    } else {
        http_client_update_stats(data, "connection_error");
        
        // Call error callback
        if (data->error_callback) {
            data->error_callback(data->message_callback_user_data, -1);
        }
    }

    http_client_free_response(&response);
    free(message);
    return result;
}

/**
 * @brief Send data from multiple buffers through the transport
 */
static int sthttp_client_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (transport == NULL || buffers == NULL || buffer_count == 0) {
        return -1;
    }

    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        total_size += buffers[i].size;
    }

    // Allocate combined buffer
    char* combined_data = (char*)malloc(total_size);
    if (combined_data == NULL) {
        return -1;
    }

    // Copy all buffers into combined buffer
    size_t offset = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        memcpy(combined_data + offset, buffers[i].data, buffers[i].size);
        offset += buffers[i].size;
    }

    // Send combined data
    int result = sthttp_client_send(transport, combined_data, total_size);
    free(combined_data);
    return result;
}

/**
 * @brief Receive data synchronously (not implemented for HTTP client)
 */
static int sthttp_client_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    // HTTP client transport doesn't support synchronous receive
    // Data is received via callbacks
    (void)transport;
    (void)data;
    (void)size;
    (void)timeout_ms;
    return -1;
}

/**
 * @brief Destroy the client transport
 */
static void sthttp_client_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return;
    }

    if (transport->transport_data != NULL) {
        sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
        
        // Stop the transport first
        sthttp_client_stop(transport);
        
        // Cleanup data
        sthttp_client_cleanup(data);
        free(data);
    }

    free(transport);
    mcp_log_debug("HTTP Streamable client transport destroyed");
}

// Note: Message and error callbacks are now set in the start function

/**
 * @brief Get current connection state
 */
mcp_client_connection_state_t mcp_sthttp_client_get_state(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return MCP_CLIENT_STATE_ERROR;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;

    mcp_mutex_lock(data->state_mutex);
    mcp_client_connection_state_t state = data->state;
    mcp_mutex_unlock(data->state_mutex);

    return state;
}

/**
 * @brief Get connection statistics
 */
int mcp_sthttp_client_get_stats(mcp_transport_t* transport, mcp_client_connection_stats_t* stats) {
    if (transport == NULL || transport->transport_data == NULL || stats == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;

    mcp_mutex_lock(data->stats_mutex);
    *stats = data->stats;
    mcp_mutex_unlock(data->stats_mutex);

    return 0;
}

/**
 * @brief Get current session ID
 */
const char* mcp_sthttp_client_get_session_id(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return NULL;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;

    if (!data->config.enable_sessions || !data->has_session) {
        return NULL;
    }

    return data->session_id;
}

/**
 * @brief Set connection state change callback
 */
int mcp_sthttp_client_set_state_callback(
    mcp_transport_t* transport,
    mcp_client_state_callback_t callback,
    void* user_data) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
    data->state_callback = callback;
    data->state_callback_user_data = user_data;

    return 0;
}

/**
 * @brief Set SSE event callback
 */
int mcp_sthttp_client_set_sse_callback(
    mcp_transport_t* transport,
    mcp_client_sse_event_callback_t callback,
    void* user_data) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
    data->sse_callback = callback;
    data->sse_callback_user_data = user_data;

    return 0;
}

/**
 * @brief Change connection state
 */
void http_client_set_state(sthttp_client_data_t* data, mcp_client_connection_state_t new_state) {
    if (data == NULL) {
        return;
    }

    mcp_client_connection_state_t old_state;

    mcp_mutex_lock(data->state_mutex);
    old_state = data->state;
    data->state = new_state;
    mcp_mutex_unlock(data->state_mutex);

    if (old_state != new_state) {
        mcp_log_debug("HTTP client state changed: %d -> %d", old_state, new_state);

        if (data->state_callback) {
            // Create a temporary transport structure for the callback
            mcp_transport_t temp_transport = {
                .type = MCP_TRANSPORT_TYPE_CLIENT,
                .protocol_type = MCP_TRANSPORT_PROTOCOL_STHTTP,
                .transport_data = data
            };
            data->state_callback(&temp_transport, old_state, new_state, data->state_callback_user_data);
        }
    }
}

/**
 * @brief Update connection statistics
 */
void http_client_update_stats(sthttp_client_data_t* data, const char* stat_type) {
    if (data == NULL || stat_type == NULL) {
        return;
    }

    mcp_mutex_lock(data->stats_mutex);

    if (strcmp(stat_type, "request_sent") == 0) {
        data->stats.requests_sent++;
        data->stats.last_request_time = time(NULL);
    } else if (strcmp(stat_type, "response_received") == 0) {
        data->stats.responses_received++;
    } else if (strcmp(stat_type, "sse_event_received") == 0) {
        data->stats.sse_events_received++;
        data->stats.last_sse_event_time = time(NULL);
    } else if (strcmp(stat_type, "reconnect_attempt") == 0) {
        data->stats.reconnect_attempts++;
    } else if (strcmp(stat_type, "connection_error") == 0) {
        data->stats.connection_errors++;
    }

    mcp_mutex_unlock(data->stats_mutex);
}

/**
 * @brief Force reconnection of SSE stream
 */
int mcp_sthttp_client_reconnect_sse(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;

    // Disconnect current SSE stream
    sse_client_disconnect(data);

    // Reconnect if SSE is enabled
    if (data->config.enable_sse_streams) {
        return sse_client_connect(data);
    }

    return 0;
}

/**
 * @brief Terminate current session
 */
int mcp_sthttp_client_terminate_session(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
    if (!data->config.enable_sessions || !data->has_session) {
        return 0; // No session to terminate
    }

    // Send DELETE request to terminate session
    char* request = http_client_build_request(data, "DELETE", NULL);
    if (request == NULL) {
        return -1;
    }

    // Create socket and send request
    socket_t socket_fd = http_client_create_socket(data->config.host, data->config.port, data->config.connect_timeout_ms);
    if (socket_fd == MCP_INVALID_SOCKET) {
        free(request);
        return -1;
    }

    int result = http_client_send_raw_request(socket_fd, request, data->config.request_timeout_ms);
    free(request);
    mcp_socket_close(socket_fd);

    if (result == 0) {
        // Clear session
        free(data->session_id);
        data->session_id = NULL;
        data->has_session = false;
        mcp_log_info("Session terminated");
    }

    return result;
}

/**
 * @brief Enable or disable automatic SSE reconnection
 */
int mcp_sthttp_client_set_auto_reconnect(mcp_transport_t* transport, bool enable) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    sthttp_client_data_t* data = (sthttp_client_data_t*)transport->transport_data;
    data->auto_reconnect = enable;

    mcp_log_debug("Auto-reconnect %s", enable ? "enabled" : "disabled");
    return 0;
}
