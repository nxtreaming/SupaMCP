#include "internal/websocket_client_internal.h"

// Helper function to resize receive buffer with optimized growth strategy
int ws_client_resize_receive_buffer(ws_client_data_t* data, size_t needed_size) {
    if (!data) {
        return -1;
    }

    // Calculate new buffer size with more efficient growth strategy
    // Use a growth factor of 1.5 instead of 2 to reduce memory waste
    size_t new_len;
    if (data->receive_buffer_len == 0) {
        // Start with default buffer size
        new_len = WS_DEFAULT_BUFFER_SIZE;
    } else {
        // Grow by 1.5x factor with alignment to 4KB boundaries for better memory allocation
        new_len = data->receive_buffer_len + (data->receive_buffer_len >> 1);
        // Round up to next 4KB boundary for better memory allocation
        new_len = (new_len + 4095) & ~4095;
    }

    // Ensure the new size is at least as large as needed
    while (new_len < needed_size) {
        new_len = new_len + (new_len >> 1);
        // Round up to next 4KB boundary
        new_len = (new_len + 4095) & ~4095;
    }

    // Allocate new buffer
    char* new_buffer = (char*)realloc(data->receive_buffer, new_len);
    if (!new_buffer) {
        mcp_log_error("Failed to allocate WebSocket client receive buffer of size %zu", new_len);
        return -1;
    }

    // Update buffer information
    data->receive_buffer = new_buffer;
    data->receive_buffer_len = new_len;
    return 0;
}

// Helper function to process a complete message with optimized memory handling
int ws_client_process_complete_message(ws_client_data_t* data) {
    if (!data) {
        return -1;
    }

    // Ensure the buffer is null-terminated
    if (data->receive_buffer_used >= data->receive_buffer_len) {
        if (ws_client_resize_receive_buffer(data, data->receive_buffer_used + 1) != 0) {
            return -1;
        }
    }
    data->receive_buffer[data->receive_buffer_used] = '\0';

    // Process the message
    mcp_mutex_lock(data->response_mutex);

    // Optimization: In sync mode, we can avoid an extra copy by transferring ownership
    // of the receive buffer directly if we're in sync mode and no one else needs it
    if (data->sync_response_mode) {
        // Clean up any existing response data
        if (data->response_data) {
            free(data->response_data);
            data->response_data = NULL;
            data->response_data_len = 0;
        }

        // Copy the response data - we still need to copy because the receive buffer
        // will be reused for future messages
        data->response_data = (char*)malloc(data->receive_buffer_used + 1);
        if (!data->response_data) {
            mcp_log_error("Failed to allocate memory for WebSocket response data");
            data->response_error_code = -1;

            // Signal condition variable if in sync mode
            if (data->sync_response_mode) {
                mcp_cond_signal(data->response_cond);
            }

            mcp_mutex_unlock(data->response_mutex);
            return -1;
        }

        // Copy and null-terminate the data
        memcpy(data->response_data, data->receive_buffer, data->receive_buffer_used);
        data->response_data[data->receive_buffer_used] = '\0';
        data->response_data_len = data->receive_buffer_used;
        data->response_ready = true;
        data->response_error_code = 0;

        #ifdef MCP_VERBOSE_DEBUG
        mcp_log_debug("WebSocket client received response: %s", data->response_data);
        #endif

        // Signal waiting thread
        mcp_log_debug("WebSocket client in sync mode, signaling condition variable");
        mcp_cond_signal(data->response_cond);
    }
    else if (data->transport && data->transport->message_callback) {
        // For async mode, we can process directly from the receive buffer
        // without additional copying

        // Reset thread-local arena for JSON parsing
        mcp_arena_reset_current_thread();

        #ifdef MCP_VERBOSE_DEBUG
        mcp_log_debug("WebSocket client received message: %s", data->receive_buffer);
        #endif

        // Process message through callback directly from receive buffer
        int error_code = 0;
        char* response = data->transport->message_callback(
            data->transport->callback_user_data,
            data->receive_buffer,
            data->receive_buffer_used,
            &error_code
        );

        // Free the response if one was returned
        if (response) {
            free(response);
        }

        // Reset thread-local arena after processing
        mcp_arena_reset_current_thread();
    }

    mcp_mutex_unlock(data->response_mutex);

    // Reset buffer for next message
    data->receive_buffer_used = 0;
    return 0;
}

// Helper function to handle received data
int ws_client_handle_received_data(ws_client_data_t* data, void* in, size_t len, bool is_final) {
    if (!data || !in || len == 0) {
        return -1;
    }

    // Update activity time
    ws_client_update_activity(data);

    // Check if we need to resize the buffer
    if (data->receive_buffer_used + len >= data->receive_buffer_len) {
        if (ws_client_resize_receive_buffer(data, data->receive_buffer_used + len + 1) != 0) {
            return -1;
        }
    }

    // Copy data to buffer
    memcpy(data->receive_buffer + data->receive_buffer_used, in, len);
    data->receive_buffer_used += len;

    // If this is the final fragment, process the message
    if (is_final) {
        return ws_client_process_complete_message(data);
    }

    return 0;
}

// Helper function to send a buffer via WebSocket with optimized memory handling
int ws_client_send_buffer(ws_client_data_t* data, const void* buffer, size_t size) {
    if (!data || !buffer || size == 0 || !data->wsi) {
        return -1;
    }

    // Always use heap allocation for WebSocket buffer to avoid MSVC stack array issues
    unsigned char* buf = (unsigned char*)malloc(LWS_PRE + size);
    if (!buf) {
        mcp_log_error("Failed to allocate buffer for WebSocket message of size %zu", size);
        return -1;
    }

    // Copy the message data
    memcpy(buf + LWS_PRE, buffer, size);

    // Send the message directly
    int result = lws_write(data->wsi, buf + LWS_PRE, size, LWS_WRITE_TEXT);

    // Free the buffer
    free(buf);

    if (result < 0) {
        mcp_log_error("Failed to send WebSocket message directly");
        return -1;
    }

    // Update activity time
    data->last_activity_time = time(NULL);

    #ifdef MCP_VERBOSE_DEBUG
    mcp_log_debug("WebSocket message sent directly, size: %zu, result: %d", size, result);
    #endif
    return 0;
}

// Helper function to send a message and wait for a response with optimized waiting strategy
int ws_client_send_and_wait_response(
    ws_client_data_t* ws_data,
    const void* data,
    size_t size,
    char** response_out,
    size_t* response_size_out,
    uint32_t timeout_ms
) {
    if (!ws_data || !data || size == 0 || !response_out) {
        return -1;
    }

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Ensure client is connected with appropriate timeout
    // Use a shorter timeout for connection to leave more time for response
    uint32_t connect_timeout = timeout_ms > 5000 ? 5000 : timeout_ms / 2;
    if (ws_client_ensure_connected(ws_data, connect_timeout) != 0) {
        return -1;
    }

    // Set up synchronous response mode - use a single critical section for setup
    mcp_mutex_lock(ws_data->response_mutex);

    // Reset response state
    ws_data->sync_response_mode = true;
    ws_data->response_ready = false;
    if (ws_data->response_data) {
        free(ws_data->response_data);
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
    }
    ws_data->response_error_code = 0;

    mcp_log_debug("WebSocket client entering synchronous response mode");

    // Log the message we're about to send (only in verbose debug mode)
    #ifdef MCP_VERBOSE_DEBUG
    mcp_log_debug("WebSocket client sending message: %.*s", (int)size, (const char*)data);
    #endif

    // Unlock before sending to avoid holding lock during network I/O
    mcp_mutex_unlock(ws_data->response_mutex);

    // Send the message
    if (ws_client_send_buffer(ws_data, data, size) != 0) {
        mcp_log_error("Failed to send WebSocket message");

        // Reset synchronous response mode
        mcp_mutex_lock(ws_data->response_mutex);
        ws_data->sync_response_mode = false;
        mcp_mutex_unlock(ws_data->response_mutex);

        return -1;
    }

    // Wait for response with timeout - no need for initial delay
    // The server will process the message asynchronously
    int result = 0;
    mcp_mutex_lock(ws_data->response_mutex);

    // Check if response is already ready (unlikely but possible)
    if (!ws_data->response_ready) {
        if (timeout_ms > 0) {
            // Wait with timeout using adaptive chunk sizes
            uint32_t remaining_timeout = timeout_ms;

            // Use progressive wait chunks - start with smaller chunks and increase
            // This provides better responsiveness for quick responses while reducing
            // CPU usage for longer waits
            uint32_t min_wait_chunk = 10;    // 10ms minimum wait
            uint32_t max_wait_chunk = 250;   // 250ms maximum wait
            uint32_t current_wait_chunk = min_wait_chunk;
            time_t last_log_time = time(NULL);

            mcp_log_debug("WebSocket client waiting for response with timeout %u ms", timeout_ms);

            while (!ws_data->response_ready && ws_data->running && remaining_timeout > 0) {
                // Calculate appropriate wait time
                uint32_t wait_time = (remaining_timeout < current_wait_chunk) ?
                                    remaining_timeout : current_wait_chunk;

                // Wait for response or timeout
                result = mcp_cond_timedwait(ws_data->response_cond, ws_data->response_mutex, wait_time);

                // Check for response or error
                if (ws_data->response_ready || result != 0) {
                    break;
                }

                // Update remaining timeout
                remaining_timeout -= wait_time;

                // Progressively increase wait chunk size for longer waits
                // This reduces CPU usage for longer waits
                if (current_wait_chunk < max_wait_chunk) {
                    current_wait_chunk = current_wait_chunk * 3 / 2; // Increase by 50%
                    if (current_wait_chunk > max_wait_chunk) {
                        current_wait_chunk = max_wait_chunk;
                    }
                }

                // Log progress only once per second to reduce log spam
                time_t now = time(NULL);
                if (difftime(now, last_log_time) >= 1.0) {
                    mcp_log_debug("WebSocket client still waiting for response, %u ms remaining", remaining_timeout);
                    last_log_time = now;
                }
            }

            if (!ws_data->response_ready) {
                mcp_log_error("WebSocket client response timeout after %u ms", timeout_ms);
                result = -2; // Timeout
            }
        } else {
            // Wait indefinitely with periodic checks
            mcp_log_debug("WebSocket client waiting for response indefinitely");
            time_t last_log_time = time(NULL);

            while (!ws_data->response_ready && ws_data->running) {
                // Wait with a reasonable timeout to allow periodic checks
                result = mcp_cond_timedwait(ws_data->response_cond, ws_data->response_mutex, 1000);

                // Check for response
                if (ws_data->response_ready) {
                    break;
                }

                // Check for error other than timeout
                if (result != 0 && result != -2) { // -2 is timeout
                    mcp_log_debug("WebSocket client wait returned error %d", result);
                    break;
                }

                // Log progress periodically
                time_t now = time(NULL);
                if (difftime(now, last_log_time) >= 5.0) {
                    mcp_log_debug("WebSocket client still waiting for response (indefinite wait)");
                    last_log_time = now;
                }
            }
        }
    }

    // Process response
    if (ws_data->response_ready && ws_data->response_data) {
        // Transfer ownership of the response data to caller
        *response_out = ws_data->response_data;
        if (response_size_out) {
            *response_size_out = ws_data->response_data_len;
        }

        // Clear our reference without freeing
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
        result = 0;
    } else {
        // No response or error
        *response_out = NULL;
        if (response_size_out) {
            *response_size_out = 0;
        }

        if (result == 0) {
            result = -1; // General error
        }
    }

    // Reset synchronous response mode
    ws_data->sync_response_mode = false;
    ws_data->response_ready = false;

    mcp_mutex_unlock(ws_data->response_mutex);

    return result;
}
