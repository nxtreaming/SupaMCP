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
