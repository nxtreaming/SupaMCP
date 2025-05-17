#include "internal/websocket_client_internal.h"

// Extract request/response ID from JSON data
static int64_t websocket_extract_request_id(const char* json_data, size_t data_len) {
    int64_t id = -1;  // -1 indicates "no valid ID"

    if (data_len > 0 && json_data && json_data[0] == '{') {
        const char* id_pos = strstr(json_data, "\"id\":");
        if (id_pos) {
            // Skip "id": and whitespace
            id_pos += 5;
            while (*id_pos == ' ' || *id_pos == '\t') id_pos++;
            id = strtoll(id_pos, NULL, 10);
        }
    }

    return id;
}

// Resize receive buffer with optimized growth strategy
static int ws_client_resize_receive_buffer(ws_client_data_t* data, size_t needed_size) {
    if (!data) {
        return -1;
    }

    // Calculate new buffer size with 1.5x growth factor to reduce memory waste
    size_t new_len;
    if (data->receive_buffer_len == 0) {
        new_len = WS_DEFAULT_BUFFER_SIZE;
    } else {
        // Grow by 1.5x factor with 4KB alignment for better memory allocation
        new_len = data->receive_buffer_len + (data->receive_buffer_len >> 1);
        new_len = (new_len + 4095) & ~4095; // Round up to next 4KB boundary
    }

    // Ensure the new size is sufficient
    while (new_len < needed_size) {
        new_len = new_len + (new_len >> 1);
        new_len = (new_len + 4095) & ~4095; // Round up to next 4KB boundary
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

// Process a complete message with optimized memory handling
static int ws_client_process_complete_message(ws_client_data_t* data) {
    if (!data) {
        return -1;
    }

    // Ensure buffer is null-terminated
    if (data->receive_buffer_used >= data->receive_buffer_len) {
        if (ws_client_resize_receive_buffer(data, data->receive_buffer_used + 1) != 0) {
            return -1;
        }
    }
    data->receive_buffer[data->receive_buffer_used] = '\0';

    mcp_mutex_lock(data->response_mutex);

    // Handle synchronous response mode
    if (data->sync_response_mode) {
        // Handle timed-out requests
        if (data->request_timedout) {
            int64_t response_id = websocket_extract_request_id(
                data->receive_buffer,
                data->receive_buffer_used
            );

            // If response matches our timed-out request, discard it
            if (response_id >= 0 && response_id == data->current_request_id) {
                mcp_log_debug("Received response for timed-out request ID %llu, discarding",
                             (unsigned long long)response_id);

                // Exit sync mode
                data->sync_response_mode = false;
                data->response_ready = false;
                data->current_request_id = -1;
                data->request_timedout = false;

                mcp_mutex_unlock(data->response_mutex);
                data->receive_buffer_used = 0;
                return 0;
            }
        }

        // Clean up existing response data
        if (data->response_data) {
            free(data->response_data);
            data->response_data = NULL;
            data->response_data_len = 0;
        }

        // Copy response data (needed because receive buffer will be reused)
        data->response_data = (char*)malloc(data->receive_buffer_used + 1);
        if (!data->response_data) {
            mcp_log_error("Failed to allocate memory for WebSocket response data");
            data->response_error_code = -1;
            
            if (data->sync_response_mode) {
                mcp_cond_signal(data->response_cond);
            }

            mcp_mutex_unlock(data->response_mutex);
            return -1;
        }

        // Copy and prepare response data
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
        // For async mode, process directly from receive buffer
        mcp_arena_reset_current_thread();

        #ifdef MCP_VERBOSE_DEBUG
        mcp_log_debug("WebSocket client received message: %s", data->receive_buffer);
        #endif

        // Process message through callback
        int error_code = 0;
        char* response = data->transport->message_callback(
            data->transport->callback_user_data,
            data->receive_buffer,
            data->receive_buffer_used,
            &error_code
        );

        if (response) {
            free(response);
        }

        mcp_arena_reset_current_thread();
    }

    mcp_mutex_unlock(data->response_mutex);

    // Reset buffer for next message
    data->receive_buffer_used = 0;
    return 0;
}

// Handle received WebSocket data
int ws_client_handle_received_data(ws_client_data_t* data, void* in, size_t len, bool is_final) {
    if (!data || !in || len == 0) {
        return -1;
    }

    // Update activity timestamp
    ws_client_update_activity(data);

    // Ensure buffer is large enough
    if (data->receive_buffer_used + len >= data->receive_buffer_len) {
        if (ws_client_resize_receive_buffer(data, data->receive_buffer_used + len + 1) != 0) {
            return -1;
        }
    }

    // Append data to buffer
    memcpy(data->receive_buffer + data->receive_buffer_used, in, len);
    data->receive_buffer_used += len;

    // Process complete message if this is the final fragment
    if (is_final) {
        return ws_client_process_complete_message(data);
    }

    return 0;
}

// Send a buffer via WebSocket with optimized memory handling
int ws_client_send_buffer(ws_client_data_t* data, const void* buffer, size_t size) {
    if (!data || !buffer || size == 0 || !data->wsi) {
        return -1;
    }

    // Log the message content for debugging
    #ifdef MCP_VERBOSE_DEBUG
    if (size < 1000) {
        char debug_buffer[1024] = {0};
        size_t copy_len = size < 1000 ? size : 1000;
        memcpy(debug_buffer, buffer, copy_len);
        debug_buffer[copy_len] = '\0';

        // Log as hex for the first 32 bytes to help diagnose encoding issues
        char hex_buffer[200] = {0};
        size_t hex_len = size < 32 ? size : 32;
        for (size_t i = 0; i < hex_len; i++) {
            sprintf(hex_buffer + i*3, "%02x ", (unsigned char)((char*)buffer)[i]);
        }
        mcp_log_debug("WebSocket client sending data (hex): %s", hex_buffer);

        // Check if this is a JSON message
        if (size > 0 && ((char*)buffer)[0] == '{') {
            mcp_log_debug("Sending JSON message: %s", debug_buffer);
        }
    }
    #endif

    // Always use heap allocation for WebSocket buffer to avoid MSVC stack array issues
    unsigned char* buf = (unsigned char*)malloc(LWS_PRE + size);
    if (!buf) {
        mcp_log_error("Failed to allocate buffer for WebSocket message of size %zu", size);
        return -1;
    }

    // Copy the message data
    memcpy(buf + LWS_PRE, buffer, size);

    // Validate and sanitize UTF-8 encoding before sending
    bool has_utf8 = false;
    bool needs_sanitization = false;

    // First pass: check if we have UTF-8 characters and if sanitization is needed
    for (size_t i = 0; i < size; i++) {
        unsigned char c = ((unsigned char*)buffer)[i];

        // Check for high-bit characters (potential UTF-8)
        if (c > 127) {
            has_utf8 = true;

            // Check for invalid UTF-8 sequences
            if (c == 0xFE || c == 0xFF) {
                mcp_log_error("Invalid UTF-8 byte detected at position %zu: 0x%02X", i, c);
                needs_sanitization = true;
            }

            // Check for incomplete UTF-8 sequences at the end of the buffer
            if (i == size - 1) {
                if ((c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 || (c & 0xF8) == 0xF0) {
                    mcp_log_error("Incomplete UTF-8 sequence at end of buffer: 0x%02X", c);
                    needs_sanitization = true;
                }
            }
        }
    }

    // If we have UTF-8 characters, log a simple message
    if (has_utf8) {
        mcp_log_debug("Message contains UTF-8 characters");
    }

    // If sanitization is needed, create a sanitized copy
    if (needs_sanitization) {
        mcp_log_warn("Invalid UTF-8 detected, sanitizing message");

        // Create a sanitized copy of the buffer
        unsigned char* sanitized_buf = (unsigned char*)malloc(LWS_PRE + size);
        if (!sanitized_buf) {
            mcp_log_error("Failed to allocate buffer for sanitized message");
            free(buf);
            return -1;
        }

        // Copy the message data and sanitize it
        memcpy(sanitized_buf + LWS_PRE, buffer, size);

        // Replace invalid UTF-8 sequences with '?'
        for (size_t i = 0; i < size; i++) {
            unsigned char c = sanitized_buf[LWS_PRE + i];
            if (c == 0xFE || c == 0xFF) {
                sanitized_buf[LWS_PRE + i] = '?';
            }
        }

        // Free the original buffer and use the sanitized one
        free(buf);
        buf = sanitized_buf;
    }

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

    mcp_log_debug("WebSocket message sent directly, size: %zu, result: %d", size, result);
    return 0;
}

// Send a message and wait for a response with optimized waiting strategy
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
    ws_data->request_timedout = false;
    if (ws_data->response_data) {
        free(ws_data->response_data);
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
    }
    ws_data->response_error_code = 0;

    // Extract and store the request ID
    int64_t request_id = websocket_extract_request_id((const char*)data, size);
    // Store the current request ID
    ws_data->current_request_id = request_id;
    if (request_id >= 0) {
        mcp_log_debug("WebSocket client expecting response for request ID: %llu",
                     (unsigned long long)request_id);
    }

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
        ws_data->current_request_id = -1;
        ws_data->request_timedout = false;
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

                // Add detailed logging for debugging
                mcp_log_debug("WebSocket client wait result: %d, wait_time: %u ms, remaining_timeout: %u ms",
                             result, wait_time, remaining_timeout);

                // Only exit loop if we got a response
                if (ws_data->response_ready) {
                    mcp_log_debug("WebSocket client received response, exiting wait loop");
                    break;
                }

                // Only exit loop on serious errors, not on timeout (-2)
                if (result != 0 && result != -2) { // -2 is timeout error
                    mcp_log_error("WebSocket client wait error: %d", result);
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
                mcp_log_error("WebSocket client response timeout after %u ms (actual elapsed time may be different)", timeout_ms);

                // Log detailed timeout information
                mcp_log_error("WebSocket client timeout details: initial timeout=%u ms, remaining=%u ms, elapsed=%u ms",
                             timeout_ms, remaining_timeout, timeout_ms - remaining_timeout);

                // Mark the request as timed out, but don't exit sync mode
                ws_data->request_timedout = true;

                // Log the request ID that timed out
                if (ws_data->current_request_id >= 0) {
                    mcp_log_error("Request with ID %llu timed out",
                                 (unsigned long long)ws_data->current_request_id);
                }

                result = -2; // Timeout
            }
        } else {
            // Wait indefinitely with periodic checks
            mcp_log_debug("WebSocket client waiting for response indefinitely");
            time_t last_log_time = time(NULL);

            while (!ws_data->response_ready && ws_data->running) {
                // Wait with a reasonable timeout to allow periodic checks
                result = mcp_cond_timedwait(ws_data->response_cond, ws_data->response_mutex, 1000);

                // Add detailed logging for debugging
                mcp_log_debug("WebSocket client indefinite wait result: %d", result);

                // Check for response
                if (ws_data->response_ready) {
                    mcp_log_debug("WebSocket client received response, exiting indefinite wait loop");
                    break;
                }

                // Check for error other than timeout
                if (result != 0 && result != -2) { // -2 is timeout
                    mcp_log_error("WebSocket client indefinite wait returned error %d", result);
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

    // Only exit sync mode if we got a response or client is shutting down
    if (ws_data->response_ready || !ws_data->running) {
        ws_data->sync_response_mode = false;
        ws_data->response_ready = false;
        ws_data->current_request_id = -1;
        ws_data->request_timedout = false;
    }
    // Otherwise, keep sync mode active to handle late responses
    else if (ws_data->request_timedout) {
        mcp_log_debug("WebSocket client keeping sync mode active for timed-out request ID %llu",
                     (unsigned long long)ws_data->current_request_id);
    }

    mcp_mutex_unlock(ws_data->response_mutex);

    return result;
}
