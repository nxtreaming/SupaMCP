#include "internal/websocket_server_internal.h"

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
    } else {
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
