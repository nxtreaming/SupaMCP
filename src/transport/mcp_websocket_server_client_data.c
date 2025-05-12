#include "internal/websocket_server_internal.h"

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
        char debug_buffer[1024] = {0};
        size_t copy_len = len < 1000 ? len : 1000;
        memcpy(debug_buffer, in, copy_len);
        debug_buffer[copy_len] = '\0';

        // Log as hex for the first 32 bytes
        char hex_buffer[200] = {0};
        size_t hex_len = len < 32 ? len : 32;
        for (size_t i = 0; i < hex_len; i++) {
            sprintf(hex_buffer + i*3, "%02x ", (unsigned char)((char*)in)[i]);
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
