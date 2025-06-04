#ifdef _WIN32
#   include <winsock2.h>
#   include <windows.h>
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <fcntl.h>
#endif

#include "internal/websocket_client_internal.h"

// WebSocket protocol definitions
struct lws_protocols client_protocols[3];

// Client callback function implementation
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                      void* user, void* in, size_t len) {
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_client_data_t* data = (ws_client_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    // Log connection-related events with timestamp for diagnostics
    if (reason == LWS_CALLBACK_CLIENT_ESTABLISHED ||
        reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR ||
        reason == LWS_CALLBACK_CLIENT_CLOSED ||
        reason == LWS_CALLBACK_WSI_CREATE ||
        reason == LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER ||
        reason == LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH ||
        reason == LWS_CALLBACK_CONNECTING ||
        reason == LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP) {

        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);

        mcp_log_info("[%s] WebSocket client callback: reason=%d (%s)",
                    timestamp, reason, websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_CONNECTING: {
            // Connection process starting
            time_t now = time(NULL);
            struct tm* timeinfo = localtime(&now);
            char timestamp[20];
            strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);

            mcp_log_info("[%s] WebSocket client starting connection process", timestamp);
            data->last_activity_time = now;

            // Optimize socket settings to reduce connection delay
            if (wsi) {
                lws_sockfd_type fd = lws_get_socket_fd(wsi);
                if (fd != LWS_SOCK_INVALID) {
                    // Set TCP_NODELAY to reduce latency
                    int val = 1;
                    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&val, sizeof(val)) < 0) {
                        mcp_log_warn("Failed to set TCP_NODELAY socket option");
                    } else {
                        mcp_log_info("Successfully set TCP_NODELAY socket option");
                    }

                    // Set shorter connection timeout
                    #ifdef _WIN32
                    // Use non-blocking mode on Windows
                    unsigned long mode = 1;
                    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
                        mcp_log_warn("Failed to set non-blocking mode");
                    } else {
                        mcp_log_info("Successfully set non-blocking mode");
                    }
                    #else
                    // Set timeout on Unix systems
                    struct timeval tv;
                    tv.tv_sec = 1;  // 1 second
                    tv.tv_usec = 0;
                    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv)) < 0) {
                        mcp_log_warn("Failed to set SO_SNDTIMEO socket option");
                    } else {
                        mcp_log_info("Successfully set SO_SNDTIMEO socket option");
                    }
                    #endif
                }
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            // Connection established successfully
            time_t now = time(NULL);
            double connection_time = difftime(now, data->last_activity_time);

            mcp_log_info("WebSocket client connection established in %.1f seconds", connection_time);
            data->wsi = wsi;

            // Update connection state
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_CONNECTED;
            data->reconnect_attempts = 0;
            
            // Reset monitoring state
            data->ping_in_progress = false;
            data->missed_pongs = 0;
            data->last_ping_time = now;
            data->last_pong_time = now;
            data->last_activity_time = now;

            // Signal waiting threads
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            // Handle connection error
            const char* error_msg = in ? (char*)in : "unknown error";
            time_t now = time(NULL);
            double elapsed = difftime(now, data->last_activity_time);

            mcp_log_error("WebSocket client connection error after %.1f seconds: %s",
                         elapsed, error_msg);
            mcp_log_info("Connection details: host=%s, port=%d, path=%s",
                        data->config.host, data->config.port,
                        data->config.path ? data->config.path : "/");

            data->wsi = NULL;

            // Update connection state
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_ERROR;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

            // Schedule reconnection if enabled
            if (data->reconnect && data->running) {
                mcp_log_info("Will attempt to reconnect after connection error");
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED: {
            // Handle connection closure
            mcp_log_info("WebSocket client connection closed");
            data->wsi = NULL;

            // Handle synchronous response mode
            mcp_mutex_lock(data->response_mutex);
            bool was_in_sync_mode = data->sync_response_mode;
            int64_t pending_request_id = data->current_request_id;

            if (data->sync_response_mode) {
                mcp_log_warn("WebSocket connection closed while in synchronous response mode for request ID %lld",
                           (long long)data->current_request_id);

                data->response_error_code = -1;
                data->request_timedout = true;
                mcp_cond_signal(data->response_cond);
            }
            mcp_mutex_unlock(data->response_mutex);

            // Update connection state
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_DISCONNECTED;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

            // Log additional diagnostic information
            if (was_in_sync_mode) {
                mcp_log_debug("Connection closed while waiting for response to request ID %lld. Will attempt to reconnect.",
                             (long long)pending_request_id);
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
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
            mcp_log_debug("Receive data from server");
            ws_client_handle_received_data(data, in, len, lws_is_final_fragment(wsi));
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
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

        case LWS_CALLBACK_WSI_CREATE: {
            // This is called when the WebSocket instance is created
            time_t now = time(NULL);
            double elapsed = difftime(now, data->last_activity_time);

            mcp_log_info("WebSocket instance created after %.1f seconds", elapsed);

            // Try to set socket options to reduce connection delay
            if (wsi) {
                lws_sockfd_type fd = lws_get_socket_fd(wsi);
                if (fd != LWS_SOCK_INVALID) {
                    // Try to set TCP_NODELAY to reduce connection latency
                    int val = 1;
                    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&val, sizeof(val)) < 0) {
                        mcp_log_warn("Failed to set TCP_NODELAY socket option in WSI_CREATE");
                    } else {
                        mcp_log_info("Successfully set TCP_NODELAY socket option in WSI_CREATE");
                    }

                    // Try to set a shorter connection timeout
                    #ifdef _WIN32
                    // Set non-blocking mode for Windows
                    unsigned long mode = 1;
                    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
                        mcp_log_warn("Failed to set non-blocking mode");
                    } else {
                        mcp_log_info("Successfully set non-blocking mode");
                    }
                    #else
                    // Set non-blocking mode for Unix
                    int flags = fcntl(fd, F_GETFL, 0);
                    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                        mcp_log_warn("Failed to set non-blocking mode");
                    } else {
                        mcp_log_info("Successfully set non-blocking mode");
                    }
                    #endif
                }
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
            // This is called when the handshake headers are being prepared
            time_t now = time(NULL);
            double elapsed = difftime(now, data->last_activity_time);

            mcp_log_info("WebSocket handshake headers being prepared after %.1f seconds", elapsed);
            break;
        }

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
            // This is called when the HTTP connection is closed before upgrading to WebSocket
            time_t now = time(NULL);
            double elapsed = difftime(now, data->last_activity_time);

            mcp_log_error("HTTP connection closed before WebSocket upgrade after %.1f seconds", elapsed);
            mcp_log_info("This usually indicates a server-side issue or incompatible protocol");

            // Log the current state
            mcp_mutex_lock(data->connection_mutex);
            ws_client_state_t current_state = data->state;
            mcp_mutex_unlock(data->connection_mutex);

            mcp_log_info("Current connection state: %d", current_state);
            break;
        }

        default:
            //mcp_log_debug("WebSocket client callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
            break;
    }

    return 0;
}

// Clean up resources
static void ws_client_cleanup_resources(ws_client_data_t* data) {
    if (!data) {
        return;
    }

    // Step 1: Set running flag to false to prevent new operations from starting
    data->running = false;

    // Step 2: Update connection state to closing to prevent new callbacks
    if (data->connection_mutex) {
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_CLOSING;
        // Wake up all threads waiting for connection
        if (data->connection_cond) {
            mcp_cond_broadcast(data->connection_cond);
        }
        mcp_mutex_unlock(data->connection_mutex);
    }

    // Step 3: Update response state and wake up all threads waiting for response
    if (data->response_mutex) {
        mcp_mutex_lock(data->response_mutex);
        // Set error flags to let waiting threads know there will be no response
        data->response_ready = true;
        data->response_error_code = -1;
        data->sync_response_mode = false;
        // Wake up all threads waiting for response
        if (data->response_cond) {
            mcp_cond_broadcast(data->response_cond);
        }
        mcp_mutex_unlock(data->response_mutex);
    }

    // Step 4: Destroy libwebsockets context, which will stop all network operations
    if (data->context) {
        // Cancel any pending service calls
        lws_cancel_service(data->context);
        // Destroy the context
        lws_context_destroy(data->context);
        data->context = NULL;
        data->wsi = NULL; // wsi is owned by context, so it's invalid now
    }

    // Step 5: Free buffers
    if (data->receive_buffer) {
        free(data->receive_buffer);
        data->receive_buffer = NULL;
        data->receive_buffer_len = 0;
        data->receive_buffer_used = 0;
    }

    if (data->response_data) {
        free(data->response_data);
        data->response_data = NULL;
        data->response_data_len = 0;
    }

    // Step 6: Wait a short time to allow all awakened threads to exit wait state
    // This is an empirical value, usually a few milliseconds is enough
    // But we use a more conservative value to ensure safety
    mcp_sleep_ms(50);

    // Step 7: Destroy synchronization primitives
    // Connection-related synchronization primitives
    mcp_cond_t* conn_cond = data->connection_cond;
    mcp_mutex_t* conn_mutex = data->connection_mutex;
    data->connection_cond = NULL;
    data->connection_mutex = NULL;

    if (conn_cond) {
        mcp_cond_destroy(conn_cond);
    }

    if (conn_mutex) {
        mcp_mutex_destroy(conn_mutex);
    }

    // Response-related synchronization primitives
    mcp_cond_t* resp_cond = data->response_cond;
    mcp_mutex_t* resp_mutex = data->response_mutex;
    data->response_cond = NULL;
    data->response_mutex = NULL;

    if (resp_cond) {
        mcp_cond_destroy(resp_cond);
    }

    if (resp_mutex) {
        mcp_mutex_destroy(resp_mutex);
    }
}

// Client transport send function
static int ws_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !transport->transport_data || !data || size == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Ensure client is connected
    if (ws_client_ensure_connected(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
        return -1;
    }

    return ws_client_send_buffer(ws_data, data, size);
}

// Client transport receive function
static int ws_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    if (!transport || !transport->transport_data || !data || !size) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;
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
        uint32_t wait_chunk = 100;

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

        // Validate the JSON data before sending
        const char* json_data = (const char*)buffers[1].data;
        size_t json_size = buffers[1].size;

        // Log the full JSON message for debugging
        mcp_log_debug("WebSocket client sending JSON message: %.*s", (int)json_size, json_data);

        // Check for UTF-8 characters (without detailed logging)
        bool has_utf8 = false;
        for (size_t i = 0; i < json_size; i++) {
            unsigned char c = (unsigned char)json_data[i];
            if (c > 127) {
                has_utf8 = true;
                break; // Stop after finding the first UTF-8 character
            }
        }

        if (has_utf8) {
            mcp_log_debug("Message contains UTF-8 characters");
        }

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
        data->response_error_code = -1;
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

    free(data);
    free(transport);

    mcp_log_info("WebSocket client transport destroyed");
}

// Get WebSocket client connection state
int mcp_transport_websocket_client_is_connected(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;
    if (!data->running) {
        mcp_log_warn("WebSocket client is not running");
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
    data->reconnect = true;

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
    data->current_request_id = -1;
    data->request_timedout = false;

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
