#include "internal/websocket_client_internal.h"

// Initialize send buffer management for optimized memory usage
int ws_client_init_send_buffers(ws_client_data_t* data) {
    if (!data) {
        return -1;
    }

    // Create send buffer pool for reusable buffers
    data->send_buffer_pool = mcp_buffer_pool_create(
        WS_CLIENT_REUSABLE_BUFFER_SIZE + LWS_PRE,
        WS_CLIENT_SEND_BUFFER_POOL_SIZE
    );

    if (!data->send_buffer_pool) {
        mcp_log_warn("Failed to create WebSocket client send buffer pool, falling back to malloc");
    } else {
        mcp_log_debug("WebSocket client send buffer pool created with %d buffers of %d bytes each",
                     WS_CLIENT_SEND_BUFFER_POOL_SIZE, WS_CLIENT_REUSABLE_BUFFER_SIZE + LWS_PRE);
    }

    // Allocate reusable send buffer for small messages
    data->reusable_send_buffer = (unsigned char*)malloc(WS_CLIENT_REUSABLE_BUFFER_SIZE + LWS_PRE);
    if (!data->reusable_send_buffer) {
        mcp_log_error("Failed to allocate reusable send buffer");
        if (data->send_buffer_pool) {
            mcp_buffer_pool_destroy(data->send_buffer_pool);
            data->send_buffer_pool = NULL;
        }
        return -1;
    }

    data->reusable_buffer_size = WS_CLIENT_REUSABLE_BUFFER_SIZE;

    // Create mutex for send buffer access
    data->send_buffer_mutex = mcp_mutex_create();
    if (!data->send_buffer_mutex) {
        mcp_log_error("Failed to create send buffer mutex");
        free(data->reusable_send_buffer);
        data->reusable_send_buffer = NULL;
        if (data->send_buffer_pool) {
            mcp_buffer_pool_destroy(data->send_buffer_pool);
            data->send_buffer_pool = NULL;
        }
        return -1;
    }

    // Initialize statistics
    data->buffer_reuses = 0;
    data->buffer_allocs = 0;
    data->utf8_validations_skipped = 0;
    data->ascii_only_messages = 0;

    mcp_log_debug("WebSocket client send buffer management initialized");
    return 0;
}

// Cleanup send buffer management
void ws_client_cleanup_send_buffers(ws_client_data_t* data) {
    if (!data) {
        return;
    }

    // Log statistics before cleanup
    if (data->buffer_allocs > 0 || data->buffer_reuses > 0) {
        mcp_log_info("WebSocket client buffer stats: %u reuses, %u allocs, %u UTF-8 validations skipped, %u ASCII-only messages",
                    data->buffer_reuses, data->buffer_allocs, data->utf8_validations_skipped, data->ascii_only_messages);
    }

    // Destroy send buffer mutex
    if (data->send_buffer_mutex) {
        mcp_mutex_destroy(data->send_buffer_mutex);
        data->send_buffer_mutex = NULL;
    }

    // Free reusable send buffer
    if (data->reusable_send_buffer) {
        free(data->reusable_send_buffer);
        data->reusable_send_buffer = NULL;
        data->reusable_buffer_size = 0;
    }

    // Destroy send buffer pool
    if (data->send_buffer_pool) {
        mcp_buffer_pool_destroy(data->send_buffer_pool);
        data->send_buffer_pool = NULL;
    }

    mcp_log_debug("WebSocket client send buffer management cleaned up");
}

// Fast ASCII-only detection to skip UTF-8 validation when possible
bool ws_client_is_ascii_only(const void* buffer, size_t size) {
    if (!buffer || size == 0) {
        return true;
    }

    const unsigned char* bytes = (const unsigned char*)buffer;

    // Process in chunks of 8 bytes for better performance
    size_t chunk_count = size / 8;
    const uint64_t* chunks = (const uint64_t*)bytes;

    for (size_t i = 0; i < chunk_count; i++) {
        // Check if any byte in the 8-byte chunk has the high bit set
        if (chunks[i] & 0x8080808080808080ULL) {
            return false;
        }
    }

    // Check remaining bytes
    for (size_t i = chunk_count * 8; i < size; i++) {
        if (bytes[i] > 127) {
            return false;
        }
    }

    return true;
}

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

// Optimized send buffer function with buffer reuse and optional UTF-8 validation
int ws_client_send_buffer_optimized(ws_client_data_t* data, const void* buffer, size_t size, bool skip_utf8_validation) {
    if (!data || !buffer || size == 0 || !data->wsi) {
        return -1;
    }

    // Log the message content for debugging (optimized)
    #if MCP_ENABLE_DATA_LOGS
    if (size < 1000) {
        // Check if this is a JSON message first (most common case)
        if (size > 0 && ((char*)buffer)[0] == '{') {
            char debug_buffer[1024] = {0};
            size_t copy_len = size < 1000 ? size : 1000;
            memcpy(debug_buffer, buffer, copy_len);
            debug_buffer[copy_len] = '\0';
            mcp_log_data_verbose("sending JSON: %s", debug_buffer);
        } else {
            // Log as hex for non-JSON data (less common)
            char hex_buffer[200] = {0};
            size_t hex_len = size < 32 ? size : 32;
            for (size_t i = 0; i < hex_len; i++) {
                sprintf(hex_buffer + i*3, "%02x ", (unsigned char)((char*)buffer)[i]);
            }
            mcp_log_data_verbose("sending data (hex): %s", hex_buffer);
        }
    }
    #endif

    unsigned char* buf = NULL;
    bool using_reusable_buffer = false;
    bool using_pool_buffer = false;
    bool needs_sanitization = false;

    // Determine buffer allocation strategy
    if (size <= WS_CLIENT_SMALL_MESSAGE_THRESHOLD && data->reusable_send_buffer && data->send_buffer_mutex) {
        // Use reusable buffer for small messages
        mcp_mutex_lock(data->send_buffer_mutex);
        if (size + LWS_PRE <= data->reusable_buffer_size + LWS_PRE) {
            buf = data->reusable_send_buffer;
            using_reusable_buffer = true;
            data->buffer_reuses++;
        }
        mcp_mutex_unlock(data->send_buffer_mutex);
    }

    if (!buf && data->send_buffer_pool && size + LWS_PRE <= WS_CLIENT_REUSABLE_BUFFER_SIZE + LWS_PRE) {
        // Try to get buffer from pool
        buf = (unsigned char*)mcp_buffer_pool_acquire(data->send_buffer_pool);
        if (buf) {
            using_pool_buffer = true;
            data->buffer_reuses++;
        }
    }

    if (!buf) {
        // Fall back to malloc for large messages or when pools are unavailable
        buf = (unsigned char*)malloc(LWS_PRE + size);
        if (!buf) {
            mcp_log_error("Failed to allocate buffer for WebSocket message of size %zu", size);
            return -1;
        }
        data->buffer_allocs++;
    }

    // Copy the message data
    memcpy(buf + LWS_PRE, buffer, size);

    // Optimized UTF-8 validation - skip if requested or if ASCII-only
    bool has_utf8 = false;
    if (!skip_utf8_validation) {
        // Fast ASCII-only check first
        if (ws_client_is_ascii_only(buffer, size)) {
            data->ascii_only_messages++;
            data->utf8_validations_skipped++;
        } else {
            // Full UTF-8 validation for non-ASCII content
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
        }
    } else {
        data->utf8_validations_skipped++;
    }

    // Handle sanitization if needed
    if (needs_sanitization) {
        mcp_log_warn("Invalid UTF-8 detected, sanitizing message");

        // If using reusable or pool buffer, we need to sanitize in place
        // Replace invalid UTF-8 sequences with '?'
        for (size_t i = 0; i < size; i++) {
            unsigned char c = buf[LWS_PRE + i];
            if (c == 0xFE || c == 0xFF) {
                buf[LWS_PRE + i] = '?';
            }
        }
    }

    // Send the message directly
    int result = lws_write(data->wsi, buf + LWS_PRE, size, LWS_WRITE_TEXT);

    // Clean up buffer based on allocation type
    if (using_reusable_buffer) {
        // Reusable buffer - no cleanup needed, just unlock if we had locked
        // (already unlocked above)
    } else if (using_pool_buffer) {
        // Return buffer to pool
        mcp_buffer_pool_release(data->send_buffer_pool, buf);
    } else {
        // Free malloc'd buffer
        free(buf);
    }

    if (result < 0) {
        mcp_log_error("Failed to send WebSocket message directly");
        return -1;
    }

    // Update activity time
    data->last_activity_time = time(NULL);

    mcp_log_debug("WebSocket message sent directly, size: %zu, result: %d", size, result);
    return 0;
}

// Send a buffer via WebSocket with optimized memory handling (backward compatibility)
int ws_client_send_buffer(ws_client_data_t* data, const void* buffer, size_t size) {
    // Call optimized version with UTF-8 validation enabled by default
    return ws_client_send_buffer_optimized(data, buffer, size, false);
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

    mcp_log_ws_debug("entering synchronous response mode");

    // Log the message we're about to send (only when data logging enabled)
    #if MCP_ENABLE_DATA_LOGS
    mcp_log_data_verbose("sending message: %.*s", (int)size, (const char*)data);
    #endif

    // Unlock before sending to avoid holding lock during network I/O
    mcp_mutex_unlock(ws_data->response_mutex);

    // Send the message
    if (ws_client_send_buffer(ws_data, data, size) != 0) {
        mcp_log_ws_error("failed to send message");

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
