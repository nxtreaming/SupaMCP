#include "internal/websocket_client_internal.h"

// Helper function to check if client is connected
bool ws_client_is_connected(ws_client_data_t* data) {
    if (!data || !data->connection_mutex) {
        return false;
    }

    bool is_connected = false;
    mcp_mutex_lock(data->connection_mutex);

    // Check connection state
    is_connected = (data->state == WS_CLIENT_STATE_CONNECTED);

    // Silently detect inconsistency but don't change behavior or log
    // This avoids excessive logging while maintaining the original behavior

    mcp_mutex_unlock(data->connection_mutex);
    return is_connected;
}

// Helper function to ensure client is connected, with optional timeout
int ws_client_ensure_connected(ws_client_data_t* data, uint32_t timeout_ms) {
    if (!data || !data->running) {
        mcp_log_error("WebSocket client ensure_connected: Invalid data or client not running");
        return -1;
    }

    // Check if already connected
    if (ws_client_is_connected(data)) {
        mcp_log_debug("WebSocket client already connected, proceeding immediately");
        return 0;
    }

    // Get current connection state for logging
    int state = -1;
    if (data->connection_mutex) {
        mcp_mutex_lock(data->connection_mutex);
        state = data->state;
        mcp_mutex_unlock(data->connection_mutex);
    }

    // Wait for connection with timeout
    mcp_log_debug("WebSocket client not connected (state=%d), waiting for connection with timeout %u ms...",
                 state, timeout_ms);

    time_t start_time = time(NULL);
    int wait_result = ws_client_wait_for_connection(data, timeout_ms);
    time_t end_time = time(NULL);
    double elapsed_seconds = difftime(end_time, start_time);

    if (wait_result != 0) {
        mcp_log_error("WebSocket client connection failed after %.1f seconds (timeout was %u ms)",
                     elapsed_seconds, timeout_ms);
        return -1;
    }

    // Check if connected after wait
    if (!ws_client_is_connected(data)) {
        // Get current state for more detailed error
        if (data->connection_mutex) {
            mcp_mutex_lock(data->connection_mutex);
            state = data->state;
            mcp_mutex_unlock(data->connection_mutex);
        }

        mcp_log_error("WebSocket client still not connected after %.1f seconds (state=%d)",
                     elapsed_seconds, state);
        return -1;
    }

    mcp_log_debug("WebSocket client connected after %.1f seconds, proceeding with operation",
                 elapsed_seconds);
    return 0;
}

// Helper function to connect to the WebSocket server
int ws_client_connect(ws_client_data_t* data) {
    if (!data || !data->context || !data->connection_mutex) {
        return -1;
    }

    // Update connection state
    mcp_mutex_lock(data->connection_mutex);
    data->state = WS_CLIENT_STATE_CONNECTING;
    mcp_mutex_unlock(data->connection_mutex);

    // Connect to server
    struct lws_client_connect_info connect_info = {0};
    connect_info.context = data->context;
    connect_info.address = data->config.host;
    connect_info.port = data->config.port;

    // Make sure path starts with a slash
    char path_buffer[256] = {0};
    if (data->config.path) {
        if (data->config.path[0] != '/') {
            snprintf(path_buffer, sizeof(path_buffer), "/%s", data->config.path);
            connect_info.path = path_buffer;
        } else {
            connect_info.path = data->config.path;
        }
    } else {
        connect_info.path = "/";
    }

    connect_info.host = data->config.host;
    connect_info.origin = data->config.origin ? data->config.origin : data->config.host;
    connect_info.protocol = data->config.protocol ? data->config.protocol : "mcp-protocol";
#if 0
    // Store the wsi pointer in the client data: it useless in current implementation
    connect_info.pwsi = &data->wsi;
#endif
    // Set additional options for better UTF-8 handling
    // Enable permessage-deflate extension for better compression of UTF-8
    connect_info.alpn = "http/1.1";

    // Set explicit UTF-8 handling flag
    connect_info.client_exts = NULL; // Use default extensions which include permessage-deflate

    // Add flags to optimize connection speed and force immediate upgrade
    connect_info.ssl_connection = data->config.use_ssl ?
        (LCCSCF_USE_SSL | LCCSCF_PIPELINE) : LCCSCF_PIPELINE;

    connect_info.local_protocol_name = "mcp-protocol";
    // Don't use retry policy
    connect_info.retry_and_idle_policy = NULL;
    connect_info.userdata = data;

    // Force immediate connection
    // Use latest version
    connect_info.ietf_version_or_minus_one = -1;

    if (!lws_client_connect_via_info(&connect_info)) {
        mcp_log_error("Failed to connect to WebSocket server");

        // Update connection state
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_ERROR;
        mcp_mutex_unlock(data->connection_mutex);

        return -1;
    }

    mcp_log_info("WebSocket client connecting to %s:%d%s",
                data->config.host, data->config.port, data->config.path ? data->config.path : "/");

    return 0;
}

// Helper function to update activity time
void ws_client_update_activity(ws_client_data_t* data) {
    if (!data) {
        return;
    }

    data->last_activity_time = time(NULL);
}

// Helper function to handle reconnection with optimized backoff strategy
void ws_client_handle_reconnect(ws_client_data_t* data) {
    if (!data || !data->reconnect || !data->running || !data->connection_mutex) {
        return;
    }

    uint32_t delay_ms = 0;
    int reconnect_attempts = 0;
    bool should_reconnect = false;
    struct lws_context* context = NULL;
    
    // Use smaller critical section to reduce lock contention
    mcp_mutex_lock(data->connection_mutex);

    // Check if we've exceeded the maximum number of reconnection attempts
    if (data->reconnect_attempts >= WS_MAX_RECONNECT_ATTEMPTS) {
        mcp_log_error("WebSocket client exceeded maximum reconnection attempts (%d)", WS_MAX_RECONNECT_ATTEMPTS);
        data->state = WS_CLIENT_STATE_ERROR;
        mcp_mutex_unlock(data->connection_mutex);
        return;
    }

    // Calculate reconnection delay within critical section
    time_t now = time(NULL);

    // Reset reconnection delay if it's been more than a minute since last attempt
    if (data->reconnect_attempts == 0 || difftime(now, data->last_reconnect_time) >= 60) {
        // Reset reconnection delay for first attempt or after long pause
        data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
        data->reconnect_attempts = 1;
    } else {
        // Use a more gradual backoff with jitter to prevent reconnection storms
        // Add some randomness (jitter) to prevent synchronized reconnection attempts
        // from multiple clients
        uint32_t base_delay = data->reconnect_delay_ms + (data->reconnect_delay_ms / 2); // Use 1.5x instead of 2x for more gradual backoff

        // Add jitter of +/- 20%
        uint32_t jitter = (base_delay / 5); // 20% of base delay
        uint32_t jitter_value = rand() % (jitter * 2 + 1); // Random value between 0 and 2*jitter

        data->reconnect_delay_ms = base_delay - jitter + jitter_value;

        // Cap at maximum delay
        if (data->reconnect_delay_ms > WS_MAX_RECONNECT_DELAY_MS) {
            data->reconnect_delay_ms = WS_MAX_RECONNECT_DELAY_MS;
        }

        data->reconnect_attempts++;
    }

    data->last_reconnect_time = now;
    
    // Get all necessary information within critical section
    delay_ms = data->reconnect_delay_ms;
    reconnect_attempts = data->reconnect_attempts;
    should_reconnect = data->running;
    if (data->context) {
        context = data->context;
    }
    
    // Log information within critical section
    mcp_log_info("WebSocket client reconnecting in %u ms (attempt %d of %d)",
                delay_ms, reconnect_attempts, WS_MAX_RECONNECT_ATTEMPTS);

    // Unlock mutex before performing blocking operations
    mcp_mutex_unlock(data->connection_mutex);

    // Sleep outside of critical section
    mcp_sleep_ms(delay_ms);

    // Attempt to reconnect outside of critical section
    if (should_reconnect) {
        // Check if context is still valid before reconnecting
        if (context) {
            // Use previously obtained state information for reconnection
            ws_client_connect(data);
        } else {
            mcp_log_error("Cannot reconnect: WebSocket context is invalid");
        }
    }
}

// Helper function to send a ping directly using libwebsockets API
// FIXME: NO ONE use this function
static int ws_client_send_ping(ws_client_data_t* data) {
    if (!data || !data->wsi || data->state != WS_CLIENT_STATE_CONNECTED) {
        return -1;
    }

    // Don't send ping if we're in synchronous response mode
    if (data->sync_response_mode) {
        mcp_log_debug("Skipping ping while in synchronous response mode");
        return 0;
    }

    // Use libwebsockets' built-in ping mechanism
    if (lws_callback_on_writable(data->wsi) < 0) {
        mcp_log_error("Failed to request writable callback for ping");
        return -1;
    }

    // Mark ping as in progress
    data->ping_in_progress = true;
    data->last_ping_time = time(NULL);

    mcp_log_debug("Requested ping to server");
    return 0;
}

// Function to wait for WebSocket connection to be established
int ws_client_wait_for_connection(ws_client_data_t* data, uint32_t timeout_ms) {
    if (!data || !data->connection_mutex || !data->connection_cond) {
        return -1;
    }

    int result = 0;
    bool need_connect = false;
    ws_client_state_t current_state;
    
    // Use smaller critical section to check initial state
    mcp_mutex_lock(data->connection_mutex);
    
    // If already connected, return immediately
    if (data->state == WS_CLIENT_STATE_CONNECTED) {
        mcp_mutex_unlock(data->connection_mutex);
        return 0;
    }
    
    // If not connecting, try to connect
    need_connect = (data->state != WS_CLIENT_STATE_CONNECTING);
    current_state = data->state;
    
    // Unlock mutex before calling connect to reduce lock contention
    mcp_mutex_unlock(data->connection_mutex);
    
    // If needed, attempt to connect outside critical section
    if (need_connect) {
        mcp_log_debug("WebSocket client not connecting (state=%d), attempting to connect...", current_state);
        if (ws_client_connect(data) != 0) {
            mcp_log_error("Failed to initiate WebSocket connection");
            return -1;
        }
    }
    
    // Re-lock mutex to wait for connection completion
    mcp_mutex_lock(data->connection_mutex);

    // Wait for connection with timeout
    if (timeout_ms > 0) {
        uint32_t remaining_timeout = timeout_ms;
        // Wait in smaller chunks to check for state changes
        uint32_t wait_chunk = 100;
        bool need_reconnect = false;

        mcp_log_debug("WebSocket client waiting for connection with timeout %u ms", timeout_ms);

        while (data->state != WS_CLIENT_STATE_CONNECTED &&
               data->state != WS_CLIENT_STATE_ERROR &&
               data->running &&
               remaining_timeout > 0) {

            // Calculate current wait time
            uint32_t wait_time = (remaining_timeout < wait_chunk) ? remaining_timeout : wait_chunk;
            
            // Use condition variable to wait
            result = mcp_cond_timedwait(data->connection_cond, data->connection_mutex, wait_time);

            // Check if we need to trigger a reconnect
            need_reconnect = (data->state == WS_CLIENT_STATE_DISCONNECTED);
            
            // If reconnect needed, do it outside critical section
            if (need_reconnect) {
                // Unlock mutex before calling connect
                mcp_mutex_unlock(data->connection_mutex);

                // Try to reconnect
                mcp_log_debug("WebSocket client disconnected during wait, attempting to reconnect...");
                if (ws_client_connect(data) != 0) {
                    mcp_log_error("Failed to initiate WebSocket reconnection");
                    return -1;
                }

                // Re-lock mutex to continue waiting
                mcp_mutex_lock(data->connection_mutex);
                
                // Recheck state after reconnection attempt
                continue;
            }

            if (result != 0) {
                // Timeout or error
                mcp_log_debug("WebSocket client connection wait returned %d, wait_time=%u ms, remaining=%u ms",
                             result, wait_time, remaining_timeout);

                // Only break on serious errors, not on timeout (-2)
                if (result != -2) { // -2 is timeout error
                    mcp_log_error("WebSocket client connection wait error: %d", result);
                    break;
                }
            }

            remaining_timeout -= wait_time;

            // Log remaining timeout every second
            if (remaining_timeout % 1000 == 0 && remaining_timeout > 0) {
                mcp_log_debug("WebSocket client still waiting for connection, %u ms remaining", remaining_timeout);
            }
        }
    } else {
        // Wait indefinitely
        bool need_reconnect = false;
        mcp_log_debug("WebSocket client waiting indefinitely for connection");
        
        while (data->state != WS_CLIENT_STATE_CONNECTED &&
               data->state != WS_CLIENT_STATE_ERROR &&
               data->running) {

            // Use condition variable to wait
            result = mcp_cond_wait(data->connection_cond, data->connection_mutex);

            // Check if we need to trigger a reconnect
            need_reconnect = (data->state == WS_CLIENT_STATE_DISCONNECTED);
            
            // If reconnect needed, do it outside critical section
            if (need_reconnect) {
                // Unlock mutex before calling connect
                mcp_mutex_unlock(data->connection_mutex);

                // Try to reconnect
                mcp_log_debug("WebSocket client disconnected during wait, attempting to reconnect...");
                if (ws_client_connect(data) != 0) {
                    mcp_log_error("Failed to initiate WebSocket reconnection");
                    return -1;
                }

                // Re-lock mutex to continue waiting
                mcp_mutex_lock(data->connection_mutex);
                
                // Recheck state after reconnection attempt
                continue;
            }
        }
    }

    // Check if connected based on state
    if (data->state != WS_CLIENT_STATE_CONNECTED) {
        mcp_log_error("WebSocket client failed to connect, state: %d, wsi: %p", data->state, data->wsi);
        result = -1;
    } else {
        mcp_log_debug("WebSocket client successfully connected");
    }

    // Silently detect inconsistency but don't change behavior or log

    mcp_mutex_unlock(data->connection_mutex);
    return result;
}

// Client event loop thread function with optimized processing
void* ws_client_event_thread(void* arg) {
    ws_client_data_t* data = (ws_client_data_t*)arg;

    // Initialize thread-local arena for this thread
    mcp_log_debug("Initializing thread-local arena for WebSocket client event thread");
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) { // 1MB arena
        mcp_log_error("Failed to initialize thread-local arena in WebSocket client event thread");
    }

    // Initialize variables for adaptive timeout
    uint32_t service_timeout_ms = 10; // Start with 10ms timeout
    time_t last_activity_check = time(NULL);
    time_t last_ping_check = time(NULL);
    const time_t ACTIVITY_CHECK_INTERVAL = 1; // Check activity every 1 second
    const time_t PING_CHECK_INTERVAL = 5;     // Check ping every 5 seconds

    // Seed random number generator for jitter in reconnect
    srand((unsigned int)time(NULL));

    while (data->running) {
        // Service the WebSocket context with adaptive timeout
        // Use longer timeouts when inactive to reduce CPU usage
        if (data->context) {
            lws_service(data->context, service_timeout_ms);
        } else {
            // If context is invalid, sleep briefly to avoid CPU spinning
            mcp_sleep_ms(100);
            continue;
        }

        time_t now = time(NULL);
        bool need_reconnect = false;
        bool need_ping_check = false;

        // Only check connection state periodically to reduce lock contention
        if (difftime(now, last_activity_check) >= ACTIVITY_CHECK_INTERVAL) {
            last_activity_check = now;

            // Use a short critical section to minimize lock contention
            mcp_mutex_lock(data->connection_mutex);

            // Check connection state
            need_reconnect = (data->state == WS_CLIENT_STATE_DISCONNECTED ||
                             data->state == WS_CLIENT_STATE_ERROR) &&
                             data->reconnect && data->running;

            // Adjust service timeout based on activity
            if (difftime(now, data->last_activity_time) < 10) {
                // More active connection - use shorter timeout for responsiveness
                service_timeout_ms = 10;
            } else {
                // Less active connection - use longer timeout to reduce CPU usage
                service_timeout_ms = 50;
            }

            mcp_mutex_unlock(data->connection_mutex);
        }

        // Check ping state less frequently to reduce lock contention
        if (difftime(now, last_ping_check) >= PING_CHECK_INTERVAL) {
            last_ping_check = now;

            mcp_mutex_lock(data->connection_mutex);
            // Check if we need to request a ping check
            need_ping_check = data->state == WS_CLIENT_STATE_CONNECTED &&
                             !data->ping_in_progress &&
                             !data->sync_response_mode &&
                             difftime(now, data->last_activity_time) * 1000 >= data->ping_interval_ms;
            mcp_mutex_unlock(data->connection_mutex);
        }

        // Handle reconnection if needed
        if (need_reconnect) {
            ws_client_handle_reconnect(data);
            // Reset activity check time after reconnect attempt
            last_activity_check = time(NULL);
        }

        // Request ping if needed
        if (need_ping_check && data->wsi) {
            lws_callback_on_writable(data->wsi);
        }
    }

    // Destroy thread-local arena before thread exits
    mcp_log_debug("Destroying thread-local arena for WebSocket client event thread");
    mcp_arena_destroy_current_thread();

    return NULL;
}
