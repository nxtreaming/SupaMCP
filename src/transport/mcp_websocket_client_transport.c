#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/websocket_client_internal.h"

// WebSocket protocol definitions
struct lws_protocols client_protocols[3];

// Client callback function implementation
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                      void* user, void* in, size_t len) {
    // Suppress unused parameter warning
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_client_data_t* data = (ws_client_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    // Debug log for important callback reasons only
    if (reason == LWS_CALLBACK_CLIENT_ESTABLISHED ||
        reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR ||
        reason == LWS_CALLBACK_CLIENT_CLOSED) {
        mcp_log_debug("WebSocket client callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            // Connection established
            mcp_log_info("WebSocket client connection established");
            data->wsi = wsi;

            // Signal that the connection is established
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_CONNECTED;

            // Reset reconnection attempts on successful connection
            data->reconnect_attempts = 0;

            // Reset ping-related state
            data->ping_in_progress = false;
            data->missed_pongs = 0;
            data->last_ping_time = time(NULL);
            data->last_pong_time = time(NULL);
            data->last_activity_time = time(NULL);

            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            // Connection error
            mcp_log_error("WebSocket client connection error: %s",
                         in ? (char*)in : "unknown error");
            data->wsi = NULL;

            // Signal that the connection failed
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_ERROR;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED: {
            // Connection closed
            mcp_log_info("WebSocket client connection closed");
            data->wsi = NULL;

            // Signal that the connection is closed
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_DISCONNECTED;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);
            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
            // Received pong from server
            mcp_log_debug("Received pong from server");

            // Update pong time and activity time
            data->last_pong_time = time(NULL);
            ws_client_update_activity(data);

            // Reset ping state
            data->ping_in_progress = false;
            data->missed_pongs = 0;
            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Receive data

            // Handle the received data
            ws_client_handle_received_data(data, in, len, lws_is_final_fragment(wsi));
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            // Ready to send data to server

            // Update activity time
            ws_client_update_activity(data);

            // Check if we need to send a ping
            time_t now = time(NULL);
            bool send_ping = false;

            // If ping is in progress and we've exceeded the timeout, consider it missed
            if (data->ping_in_progress) {
                if (difftime(now, data->last_ping_time) * 1000 >= data->ping_timeout_ms) {
                    mcp_log_warn("WebSocket ping timeout detected");
                    data->ping_in_progress = false;
                    data->missed_pongs++;

                    // If we've missed too many pongs, log a warning but don't trigger reconnect
                    if (data->missed_pongs >= 3) {
                        mcp_log_warn("WebSocket connection may be unstable after %d missed pongs",
                                     data->missed_pongs);
                        // Reset missed pongs counter to avoid repeated warnings
                        data->missed_pongs = 0;
                    }
                }
            }
            // If no ping in progress, check if we need to send one based on inactivity
            else if (data->state == WS_CLIENT_STATE_CONNECTED && !data->sync_response_mode) {
                // Only send ping if we're not in synchronous response mode
                if (difftime(now, data->last_activity_time) * 1000 >= data->ping_interval_ms) {
                    send_ping = true;
                }
            }

            // Send ping if needed
            if (send_ping) {
                // Send a ping frame
                unsigned char buf[LWS_PRE + 0];
                int result = lws_write(wsi, &buf[LWS_PRE], 0, LWS_WRITE_PING);

                if (result >= 0) {
                    // Update ping state
                    data->ping_in_progress = true;
                    data->last_ping_time = now;
                    mcp_log_debug("Sent ping to server");
                } else {
                    mcp_log_error("Failed to send ping to server");
                }

                // Always request another writable callback to check for messages
                lws_callback_on_writable(wsi);
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

// Helper function to clean up resources
void ws_client_cleanup_resources(ws_client_data_t* data) {
    if (!data) {
        return;
    }

    // First, make sure we're not in a connected state
    // This helps prevent callbacks from being triggered during cleanup
    if (data->state == WS_CLIENT_STATE_CONNECTED) {
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_CLOSING;
        mcp_mutex_unlock(data->connection_mutex);
    }

    // Destroy libwebsockets context first to prevent any further callbacks
    if (data->context) {
        // Cancel any pending service calls
        lws_cancel_service(data->context);

        // Destroy the context
        lws_context_destroy(data->context);
        data->context = NULL;
        data->wsi = NULL; // wsi is owned by context, so it's invalid now
    }

    // Free receive buffer
    if (data->receive_buffer) {
        free(data->receive_buffer);
        data->receive_buffer = NULL;
        data->receive_buffer_len = 0;
        data->receive_buffer_used = 0;
    }

    // Free response data
    if (data->response_data) {
        free(data->response_data);
        data->response_data = NULL;
        data->response_data_len = 0;
    }

    // Destroy mutexes and condition variables
    // First signal any waiting threads to prevent deadlocks
    if (data->connection_mutex && data->connection_cond) {
        mcp_mutex_lock(data->connection_mutex);
        mcp_cond_signal(data->connection_cond);
        mcp_mutex_unlock(data->connection_mutex);
    }

    if (data->response_mutex && data->response_cond) {
        mcp_mutex_lock(data->response_mutex);
        mcp_cond_signal(data->response_cond);
        mcp_mutex_unlock(data->response_mutex);
    }

    // Now destroy the synchronization primitives
    if (data->connection_cond) {
        mcp_cond_destroy(data->connection_cond);
        data->connection_cond = NULL;
    }

    if (data->connection_mutex) {
        mcp_mutex_destroy(data->connection_mutex);
        data->connection_mutex = NULL;
    }

    if (data->response_cond) {
        mcp_cond_destroy(data->response_cond);
        data->response_cond = NULL;
    }

    if (data->response_mutex) {
        mcp_mutex_destroy(data->response_mutex);
        data->response_mutex = NULL;
    }
}

// Client transport send function
static int ws_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !transport->transport_data || !data || size == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Ensure client is connected
    if (ws_client_ensure_connected(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
        return -1;
    }

    // Send the message
    return ws_client_send_buffer(ws_data, data, size);
}

// Client transport receive function
static int ws_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    if (!transport || !transport->transport_data || !data || !size) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Check if we already have a response ready (from sendv)
    mcp_mutex_lock(ws_data->response_mutex);

    if (ws_data->response_ready && ws_data->response_data) {
        // We already have a response, return it immediately
        mcp_log_debug("WebSocket client already has response ready, returning immediately");

        // Copy the response data
        *data = ws_data->response_data;
        *size = ws_data->response_data_len;

        // Transfer ownership of the response data
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
        ws_data->response_ready = false;

        mcp_mutex_unlock(ws_data->response_mutex);
        return 0;
    }

    // No response ready, we need to wait for one
    mcp_log_debug("WebSocket client receive: no response ready, waiting for one");

    // Set up synchronous response mode
    ws_data->sync_response_mode = true;
    ws_data->response_ready = false;
    if (ws_data->response_data) {
        free(ws_data->response_data);
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
    }
    ws_data->response_error_code = 0;

    // Wait for response with timeout
    int result = 0;

    if (timeout_ms > 0) {
        // Wait with timeout
        uint32_t remaining_timeout = timeout_ms;
        uint32_t wait_chunk = 100; // Wait in smaller chunks to check for state changes

        mcp_log_debug("WebSocket client receive: waiting for response with timeout %u ms", timeout_ms);

        while (!ws_data->response_ready && ws_data->running && remaining_timeout > 0) {
            uint32_t wait_time = (remaining_timeout < wait_chunk) ? remaining_timeout : wait_chunk;
            result = mcp_cond_timedwait(ws_data->response_cond, ws_data->response_mutex, wait_time);

            if (result != 0) {
                // Timeout or error
                mcp_log_debug("WebSocket client receive: wait returned %d", result);
                break;
            }

            remaining_timeout -= wait_time;

            // Log remaining timeout every second
            if (remaining_timeout % 1000 == 0 && remaining_timeout > 0) {
                mcp_log_debug("WebSocket client receive: still waiting for response, %u ms remaining", remaining_timeout);
            }
        }

        if (!ws_data->response_ready) {
            mcp_log_error("WebSocket client receive: response timeout after %u ms", timeout_ms);
            result = -2; // Timeout
        }
    } else {
        // Wait indefinitely
        mcp_log_debug("WebSocket client receive: waiting for response indefinitely");

        while (!ws_data->response_ready && ws_data->running) {
            result = mcp_cond_wait(ws_data->response_cond, ws_data->response_mutex);

            if (result != 0) {
                // Error
                mcp_log_debug("WebSocket client receive: wait returned %d", result);
                break;
            }
        }
    }

    // Check if we got a response
    if (ws_data->response_ready && ws_data->response_data) {
        // Copy the response data
        *data = ws_data->response_data;
        *size = ws_data->response_data_len;

        // Transfer ownership of the response data
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
        result = 0;

        mcp_log_debug("WebSocket client receive: got response, size: %zu", *size);
    } else {
        // No response or error
        *data = NULL;
        *size = 0;

        if (result == 0) {
            // General error
            result = -1;
        }

        mcp_log_error("WebSocket client receive: failed to get response, result: %d", result);
    }

    // Reset synchronous response mode
    ws_data->sync_response_mode = false;
    ws_data->response_ready = false;

    mcp_mutex_unlock(ws_data->response_mutex);

    return result;
}

// Client transport sendv function with optimized buffer handling
static int ws_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // For WebSocket, we always use synchronous request-response for all messages
    // This ensures consistent behavior regardless of message content

    // Optimization: For the common case of 2 buffers where the first is a length prefix
    // and the second is the actual JSON data, we can avoid combining buffers
    if (buffer_count == 2 && buffers[0].size == sizeof(uint32_t)) {
        // This is the standard MCP message format with length prefix + JSON
        // For WebSocket, we only need to send the JSON part (second buffer)

        // Log the message for debugging if it's JSON (only in verbose debug mode)
        #ifdef MCP_VERBOSE_DEBUG
        if (buffers[1].size > 0 && ((const char*)buffers[1].data)[0] == '{') {
            const char* json_data = (const char*)buffers[1].data;
            mcp_log_debug("JSON data in sendv: %.*s", (int)buffers[1].size, json_data);
        }
        #endif

        char* response = NULL;
        size_t response_size = 0;

        // Get the timeout value from the client configuration
        // This is passed from mcp_client_send_and_wait to mcp_transport_receive
        uint32_t timeout_ms = 10000; // Default to 10 seconds if not specified

        // Log the timeout value
        mcp_log_debug("Using timeout: %u ms", timeout_ms);

        // Send only the JSON part (second buffer) and wait for response
        int result = ws_client_send_and_wait_response(
            ws_data,
            buffers[1].data,
            buffers[1].size,
            &response,
            &response_size,
            timeout_ms
        );

        if (result != 0) {
            mcp_log_error("WebSocket client send and wait response failed: %d", result);
            return result;
        }

        // Store the response in the transport's response buffer
        // The client will retrieve it using mcp_transport_receive
        mcp_mutex_lock(ws_data->response_mutex);

        if (ws_data->response_data) {
            free(ws_data->response_data);
        }

        ws_data->response_data = response;
        ws_data->response_data_len = response_size;
        ws_data->response_ready = true;

        mcp_mutex_unlock(ws_data->response_mutex);

        return 0;
    }
    else {
        // For other message formats, we need to combine the buffers

        // Calculate total size using common function
        size_t total_size = mcp_websocket_calculate_total_size(buffers, buffer_count);

        // Always use heap allocation for combined buffer to avoid MSVC stack array issues
        unsigned char* combined_buffer = (unsigned char*)malloc(total_size);
        if (!combined_buffer) {
            mcp_log_error("Failed to allocate WebSocket combined buffer of size %zu", total_size);
            return -1;
        }

        // Combine buffers using common function
        if (mcp_websocket_combine_buffers(buffers, buffer_count, combined_buffer, total_size) != 0) {
            free(combined_buffer);
            mcp_log_error("Failed to combine WebSocket buffers");
            return -1;
        }

        // Ensure client is connected
        if (ws_client_ensure_connected(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
            free(combined_buffer);
            return -1;
        }

        // Send the message
        int result = ws_client_send_buffer(ws_data, combined_buffer, total_size);

        // Free the buffer
        free(combined_buffer);

        return result;
    }
}

// Client transport start function
static int ws_client_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // Store callback information
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Initialize protocols
    mcp_websocket_init_protocols(client_protocols, ws_client_callback);

    // Create libwebsockets context using common function
    data->context = mcp_websocket_create_context(
        data->config.host,
        data->config.port,
        data->config.path,
        client_protocols,
        data,
        false, // is_server
        data->config.use_ssl,
        data->config.cert_path,
        data->config.key_path
    );

    if (!data->context) {
        mcp_log_error("Failed to create WebSocket client context");
        return -1;
    }

    // Initialize connection mutex and condition variable
    data->connection_mutex = mcp_mutex_create();
    if (!data->connection_mutex) {
        mcp_log_error("Failed to create WebSocket client connection mutex");
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    data->connection_cond = mcp_cond_create();
    if (!data->connection_cond) {
        mcp_log_error("Failed to create WebSocket client connection condition variable");
        mcp_mutex_destroy(data->connection_mutex);
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    // Initialize response mutex and condition variable
    data->response_mutex = mcp_mutex_create();
    if (!data->response_mutex) {
        mcp_log_error("Failed to create WebSocket client response mutex");
        mcp_mutex_destroy(data->connection_mutex);
        mcp_cond_destroy(data->connection_cond);
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    data->response_cond = mcp_cond_create();
    if (!data->response_cond) {
        mcp_log_error("Failed to create WebSocket client response condition variable");
        mcp_mutex_destroy(data->response_mutex);
        mcp_mutex_destroy(data->connection_mutex);
        mcp_cond_destroy(data->connection_cond);
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    // Initialize reconnection parameters
    data->reconnect_attempts = 0;
    data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
    data->last_reconnect_time = time(NULL);

    // Initialize ping parameters
    data->last_ping_time = time(NULL);
    data->last_pong_time = time(NULL);
    data->last_activity_time = time(NULL);
    data->ping_interval_ms = WS_PING_INTERVAL_MS;
    data->ping_timeout_ms = WS_PING_TIMEOUT_MS;
    data->ping_in_progress = false;
    data->missed_pongs = 0;

    // Set initial state
    data->state = WS_CLIENT_STATE_DISCONNECTED;
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_client_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket client event thread");
        ws_client_cleanup_resources(data);
        data->running = false;
        return -1;
    }

    // Initiate connection
    if (ws_client_connect(data) != 0) {
        mcp_log_error("Failed to initiate WebSocket client connection");
        // Don't return error here, as the event thread will handle reconnection
    }

    return 0;
}

// Client transport stop function
static int ws_client_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Check if already stopped
    if (!data->running) {
        mcp_log_debug("WebSocket client already stopped");
        return 0;
    }

    mcp_log_info("Stopping WebSocket client transport...");

    // First, set flags to prevent reconnection attempts
    data->reconnect = false;

    // Signal any waiting threads to wake up
    if (data->connection_mutex && data->connection_cond) {
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_CLOSING;
        mcp_cond_signal(data->connection_cond);
        mcp_mutex_unlock(data->connection_mutex);
    }

    if (data->response_mutex && data->response_cond) {
        mcp_mutex_lock(data->response_mutex);
        data->response_ready = true; // Force any waiting threads to wake up
        data->response_error_code = -1; // Indicate error
        mcp_cond_signal(data->response_cond);
        mcp_mutex_unlock(data->response_mutex);
    }

    // Now set running flag to false to stop event loop
    data->running = false;

    // Force libwebsockets to break out of its service loop
    if (data->context) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets client service");
    }

    // Wait for event thread to exit with a timeout
    if (data->event_thread) {
        mcp_log_info("Waiting for WebSocket client event thread to exit...");

        // Join with timeout to prevent hanging
        int join_result = mcp_thread_join(data->event_thread, NULL);
        if (join_result != 0) {
            mcp_log_warn("WebSocket client event thread join failed with code %d", join_result);
        } else {
            mcp_log_debug("WebSocket client event thread exited successfully");
        }

        data->event_thread = 0;
    }

    // Clean up resources
    ws_client_cleanup_resources(data);

    mcp_log_info("WebSocket client stopped");

    return 0;
}

// Client transport destroy function
static void ws_client_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;
    if (!data) {
        free(transport);
        return;
    }

    mcp_log_info("Destroying WebSocket client transport...");

    // Stop transport if running
    if (data->running) {
        // Call stop function to ensure proper cleanup
        ws_client_transport_stop(transport);
    } else {
        // Even if not running, make sure resources are cleaned up
        ws_client_cleanup_resources(data);
    }

    // Free any dynamically allocated config strings
    // Note: In the current implementation, these are typically not duplicated,
    // but we should check in case that changes in the future
    // We can't compare with the original config since it's not stored in the transport

    // Free transport data
    free(data);

    // Free transport
    free(transport);

    mcp_log_info("WebSocket client transport destroyed");
}

// Get WebSocket client connection state
int mcp_transport_websocket_client_is_connected(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!data->running) {
        return -1;
    }

    // Use the helper function to check connection state
    return ws_client_is_connected(data) ? 1 : 0;
}

// Create WebSocket client transport
mcp_transport_t* mcp_transport_websocket_client_create(const mcp_websocket_config_t* config) {
    if (!config || !config->host) {
        return NULL;
    }

    // Allocate transport
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) {
        return NULL;
    }

    // Allocate client data
    ws_client_data_t* data = (ws_client_data_t*)calloc(1, sizeof(ws_client_data_t));
    if (!data) {
        free(transport);
        return NULL;
    }

    // Copy config
    data->config = *config;
    data->protocols = client_protocols;
    data->transport = transport;
    data->reconnect = true; // Enable reconnection by default

    // Initialize protocols
    mcp_websocket_init_protocols(client_protocols, ws_client_callback);

    // Initialize connection state
    data->state = WS_CLIENT_STATE_DISCONNECTED;
    data->connection_mutex = NULL;
    data->connection_cond = NULL;

    // Initialize reconnection parameters
    data->reconnect_attempts = 0;
    data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
    data->last_reconnect_time = 0;

    // Initialize synchronous response mode
    data->sync_response_mode = false;
    data->response_mutex = NULL;
    data->response_cond = NULL;
    data->response_data = NULL;
    data->response_data_len = 0;
    data->response_ready = false;
    data->response_error_code = 0;

    // Set transport type and operations
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->client.start = ws_client_transport_start;
    transport->client.stop = ws_client_transport_stop;
    transport->client.destroy = ws_client_transport_destroy;
    transport->client.send = ws_client_transport_send;
    transport->client.sendv = ws_client_transport_sendv;
    transport->client.receive = ws_client_transport_receive;

    // Set transport protocol type to WebSocket
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_WEBSOCKET;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}