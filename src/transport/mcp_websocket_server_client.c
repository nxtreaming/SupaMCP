#include "internal/websocket_server_internal.h"

// Helper function to initialize a client
int ws_server_client_init(ws_client_t* client, int client_id, struct lws* wsi) {
    if (!client) {
        return -1;
    }

    client->wsi = wsi;
    client->state = WS_CLIENT_STATE_ACTIVE;
    client->receive_buffer = NULL;
    client->receive_buffer_len = 0;
    client->receive_buffer_used = 0;
    client->client_id = client_id;
    client->last_activity = time(NULL);
    client->ping_sent = 0;

    return 0;
}

// Helper function to clean up a client
void ws_server_client_cleanup(ws_client_t* client, ws_server_data_t* server_data) {
    if (!client) {
        return;
    }

    // Free receive buffer
    if (client->receive_buffer) {
        // If buffer pool exists and buffer size matches pool buffer size, return to pool
        if (server_data && server_data->buffer_pool &&
            client->receive_buffer_len == WS_BUFFER_POOL_BUFFER_SIZE) {
            mcp_buffer_pool_release(server_data->buffer_pool, client->receive_buffer);
            mcp_log_debug("Returned buffer to pool for client %d", client->client_id);
        } else {
            // Otherwise free normally
            free(client->receive_buffer);

            // Update memory statistics
            if (server_data) {
                server_data->total_buffer_memory -= client->receive_buffer_len;
            }
        }

        client->receive_buffer = NULL;
        client->receive_buffer_len = 0;
        client->receive_buffer_used = 0;
    }

    // Reset client state
    client->state = WS_CLIENT_STATE_INACTIVE;
    client->wsi = NULL;

    // Update bitmap and statistics if server_data is provided
    if (server_data) {
        // Clear bit in bitmap
        ws_server_clear_client_bit(server_data->client_bitmap, client->client_id);

        // Decrement active client count
        if (server_data->active_clients > 0) {
            server_data->active_clients--;
        }
    }
}

// Helper function to resize a client's receive buffer with optimized allocation strategy
int ws_server_client_resize_buffer(ws_client_t* client, size_t needed_size, ws_server_data_t* server_data) {
    if (!client) {
        return -1;
    }

    // Calculate new buffer size with more efficient growth strategy
    size_t new_len;

    if (client->receive_buffer_len == 0) {
        // Start with default buffer size
        new_len = WS_DEFAULT_BUFFER_SIZE;
    } else {
        // Use a more efficient growth factor (1.5x) with 4KB alignment for better memory allocation
        new_len = client->receive_buffer_len + (client->receive_buffer_len >> 1);
        // Round up to next 4KB boundary for better memory allocation
        new_len = (new_len + 4095) & ~4095;
    }

    // Ensure the new size is at least as large as needed
    while (new_len < needed_size) {
        new_len = new_len + (new_len >> 1);
        // Round up to next 4KB boundary
        new_len = (new_len + 4095) & ~4095;
    }

    // Fast path: If the current buffer is already large enough, just return success
    if (client->receive_buffer && client->receive_buffer_len >= needed_size) {
        return 0;
    }

    char* new_buffer = NULL;

    // Try to get buffer from pool if server_data is provided and buffer pool exists
    // Only use pool for buffers that fit within pool buffer size
    if (server_data && server_data->buffer_pool && new_len <= WS_BUFFER_POOL_BUFFER_SIZE) {
        // Try to get a buffer from the pool
        new_buffer = (char*)mcp_buffer_pool_acquire(server_data->buffer_pool);

        if (new_buffer) {
            // Successfully got a buffer from the pool
            server_data->buffer_reuses++;

            // Copy existing data if any
            if (client->receive_buffer && client->receive_buffer_used > 0) {
                memcpy(new_buffer, client->receive_buffer, client->receive_buffer_used);
            }

            // Free old buffer if it exists
            if (client->receive_buffer) {
                // If old buffer was from pool, return it to pool
                if (client->receive_buffer_len == WS_BUFFER_POOL_BUFFER_SIZE && server_data->buffer_pool) {
                    mcp_buffer_pool_release(server_data->buffer_pool, client->receive_buffer);
                } else {
                    free(client->receive_buffer);
                    if (server_data) {
                        server_data->total_buffer_memory -= client->receive_buffer_len;
                    }
                }
            }

            // Update buffer information
            client->receive_buffer = new_buffer;
            client->receive_buffer_len = WS_BUFFER_POOL_BUFFER_SIZE;
            return 0;
        } else if (server_data) {
            // Failed to get buffer from pool
            server_data->buffer_misses++;
        }
    }

    // Fall back to regular allocation if pool is not available or buffer is too large
    if (client->receive_buffer) {
        // Try to reuse existing buffer with realloc for better performance
        new_buffer = (char*)realloc(client->receive_buffer, new_len);

        // Update memory statistics if server_data is provided
        if (server_data) {
            server_data->total_buffer_memory -= client->receive_buffer_len;
            server_data->total_buffer_memory += new_len;
            server_data->buffer_allocs++;
        }
    } else {
        // Allocate new buffer
        new_buffer = (char*)malloc(new_len);

        // Update memory statistics if server_data is provided
        if (server_data) {
            server_data->total_buffer_memory += new_len;
            server_data->buffer_allocs++;
        }
    }

    if (!new_buffer) {
        mcp_log_error("Failed to allocate WebSocket receive buffer of size %zu", new_len);
        return -1;
    }

    // Update buffer information
    client->receive_buffer = new_buffer;
    client->receive_buffer_len = new_len;
    return 0;
}

// Helper function to update client activity timestamp
void ws_server_client_update_activity(ws_client_t* client) {
    if (!client) {
        return;
    }

    client->last_activity = time(NULL);
    client->ping_sent = 0; // Reset ping counter on activity
}

// Helper function to send a ping to a client
int ws_server_client_send_ping(ws_client_t* client) {
    if (!client || !client->wsi || client->state != WS_CLIENT_STATE_ACTIVE) {
        return -1;
    }

    // Increment ping counter
    client->ping_sent++;

    // Request a callback to send a ping
    return lws_callback_on_writable(client->wsi);
}

// Helper function to find a client by WebSocket instance with optimized search
ws_client_t* ws_server_find_client_by_wsi(ws_server_data_t* data, struct lws* wsi) {
    if (!data || !wsi) {
        return NULL;
    }

    // First try to get the client from opaque user data (fastest path)
    ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
    if (client) {
        return client;
    }

    // If not found, search through the client list using bitmap for efficiency
    mcp_mutex_lock(data->clients_mutex);

    // Use bitmap to quickly skip inactive clients
    const int num_words = (MAX_WEBSOCKET_CLIENTS >> 5) + 1;

    for (int i = 0; i < num_words; i++) {
        uint32_t word = data->client_bitmap[i];

        // Skip words with no active clients
        if (word == 0) {
            continue;
        }

        // Process only active bits in this word
        while (word) {
            // Find position of least significant 1 bit
            int bit_pos;

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
            // MSVC on x86/x64
            unsigned long pos;
            _BitScanForward(&pos, word);
            bit_pos = (int)pos;
#elif defined(__GNUC__) || defined(__clang__)
            // GCC or Clang
            bit_pos = __builtin_ffs(word) - 1;
#else
            // Fallback for other compilers
            bit_pos = 0;
            while ((word & (1U << bit_pos)) == 0) {
                bit_pos++;
            }
#endif

            // Calculate client index
            int index = (i << 5) + bit_pos;
            if (index >= MAX_WEBSOCKET_CLIENTS) {
                break;
            }

            // Check if this client matches the wsi
            if (data->clients[index].wsi == wsi) {
                client = &data->clients[index];

                // Store client pointer in opaque user data for faster lookup next time
                lws_set_opaque_user_data(wsi, client);

                mcp_mutex_unlock(data->clients_mutex);
                return client;
            }

            // Clear the bit we just processed and continue
            word &= ~(1U << bit_pos);
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
    return NULL;
}

// Helper function to send a response to a client with optimized buffer handling
int ws_server_client_send_response(ws_client_t* client, struct lws* wsi, const char* response, size_t response_len) {
    if (!client || !wsi || !response || response_len == 0) {
        return -1;
    }

    // Always use heap allocation for WebSocket buffer to avoid MSVC stack array issues
    unsigned char* buffer = (unsigned char*)malloc(LWS_PRE + response_len);
    if (!buffer) {
        mcp_log_error("Failed to allocate WebSocket response buffer of size %zu", response_len);
        return -1;
    }

    // Copy response data
    memcpy(buffer + LWS_PRE, response, response_len);

    // Send data directly
    int result = lws_write(wsi, buffer + LWS_PRE, response_len, LWS_WRITE_TEXT);

    // Free buffer
    free(buffer);

    if (result < 0) {
        mcp_log_error("WebSocket server direct write failed");
        return -1;
    }

    // Update activity timestamp
    ws_server_client_update_activity(client);

    return 0;
}

// Helper function to process a complete message
int ws_server_client_process_message(ws_server_data_t* data, ws_client_t* client, struct lws* wsi) {
    if (!data || !client || !wsi) {
        return -1;
    }

    // Ensure the buffer is null-terminated for string operations
    if (client->receive_buffer_used < client->receive_buffer_len) {
        client->receive_buffer[client->receive_buffer_used] = '\0';
    }
    else {
        if (ws_server_client_resize_buffer(client, client->receive_buffer_used + 1, data) != 0) {
            return -1;
        }
        client->receive_buffer[client->receive_buffer_used] = '\0';
    }

    // Process complete message
    if (data->transport && data->transport->message_callback) {
        // Reset thread-local arena for JSON parsing
        mcp_log_debug("Resetting thread-local arena for server message processing");
        mcp_arena_reset_current_thread();

        int error_code = 0;
        char* response = data->transport->message_callback(
            data->transport->callback_user_data,
            client->receive_buffer,
            client->receive_buffer_used,
            &error_code
        );

        // If there's a response, send it directly
        if (response) {
            size_t response_len = strlen(response);
            ws_server_client_send_response(client, wsi, response, response_len);

            // Free the response
            free(response);
        }

        // Reset thread-local arena after processing
        mcp_arena_reset_current_thread();
    }

    // Reset buffer
    client->receive_buffer_used = 0;

    return 0;
}

// Helper function to handle received data
int ws_server_client_handle_received_data(ws_server_data_t* data, ws_client_t* client,
    struct lws* wsi, void* in, size_t len, bool is_final) {
    if (!data || !client || !wsi || !in || len == 0) {
        return -1;
    }

    // Update client activity timestamp
    ws_server_client_update_activity(client);

    // Ensure buffer is large enough
    if (client->receive_buffer_used + len > client->receive_buffer_len) {
        if (ws_server_client_resize_buffer(client, client->receive_buffer_used + len, data) != 0) {
            return -1;
        }
    }

    // Log the raw message data for debugging (if not too large)
#ifdef MCP_VERBOSE_DEBUG
    if (len < 1000) {
        char debug_buffer[1024] = { 0 };
        size_t copy_len = len < 1000 ? len : 1000;
        memcpy(debug_buffer, in, copy_len);
        debug_buffer[copy_len] = '\0';

        // Log as hex for the first 32 bytes
        char hex_buffer[200] = { 0 };
        size_t hex_len = len < 32 ? len : 32;
        for (size_t i = 0; i < hex_len; i++) {
            sprintf(hex_buffer + i * 3, "%02x ", (unsigned char)((char*)in)[i]);
        }
        mcp_log_debug("WebSocket server raw data (hex): %s", hex_buffer);

        // Check if this is a JSON message
        if (len > 0 && ((char*)in)[0] == '{') {
            mcp_log_debug("Detected JSON message");
        }
    }
#endif

    // Check if this might be a length-prefixed message
    if (len >= 4) {
        // Interpret first 4 bytes as a 32-bit length (network byte order)
        uint32_t msg_len = 0;
        // Convert from network byte order (big endian) to host byte order
        msg_len = ((unsigned char)((char*)in)[0] << 24) |
            ((unsigned char)((char*)in)[1] << 16) |
            ((unsigned char)((char*)in)[2] << 8) |
            ((unsigned char)((char*)in)[3]);

#ifdef MCP_VERBOSE_DEBUG
        // Log the extracted length
        mcp_log_debug("Possible message length prefix: %u bytes (total received: %zu bytes)",
            msg_len, len);
#endif

        // If this looks like a length-prefixed message, skip the length prefix
        if (msg_len <= len - 4 && msg_len > 0) {
            mcp_log_debug("Detected length-prefixed message, skipping 4-byte prefix");
            // Copy data without the length prefix
            memcpy(client->receive_buffer + client->receive_buffer_used,
                (char*)in + 4, len - 4);
            client->receive_buffer_used += (len - 4);

            // Process the message immediately if it's a complete message
            if (is_final) {
                return ws_server_client_process_message(data, client, wsi);
            }

            return 0;  // Skip the rest of the processing
        }
    }

    // Normal copy if not a length-prefixed message or if length prefix check failed
    memcpy(client->receive_buffer + client->receive_buffer_used, in, len);
    client->receive_buffer_used += len;

    // Check if this is a complete message
    if (is_final) {
        return ws_server_client_process_message(data, client, wsi);
    }

    return 0;
}
