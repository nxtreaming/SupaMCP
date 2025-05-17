#include "internal/websocket_server_internal.h"

// WebSocket protocol definitions
struct lws_protocols server_protocols[3];

// Server callback function implementation
int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                     void* user, void* in, size_t len) {
    // Suppress unused parameter warning
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_server_data_t* data = (ws_server_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    // Log only non-frequent callback reasons to reduce log volume
    if (reason != LWS_CALLBACK_SERVER_WRITEABLE &&
        reason != LWS_CALLBACK_RECEIVE &&
        reason != LWS_CALLBACK_RECEIVE_PONG) {
        mcp_log_debug("WebSocket server callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // Handle new client connection
            mcp_log_info("WebSocket connection established");

            ws_server_lock_all_clients(data);

            // Find empty client slot using bitmap
            int client_index = ws_server_find_free_client_slot(data);
            if (client_index == -1) {
                data->rejected_connections++;
                ws_server_unlock_all_clients(data);
                mcp_log_error("Maximum WebSocket clients reached (%u active, %u total connections, %u rejected, max: %u)",
                             data->active_clients, data->total_connections, data->rejected_connections, data->max_clients);
                return -1;
            }

            // Initialize client data
            ws_client_t* client = &data->clients[client_index];
            ws_server_client_init(client, client_index, wsi);
            lws_set_opaque_user_data(wsi, client);

            // Update statistics
            ws_server_set_client_bit(data->client_bitmap, client_index, data->bitmap_size);
            data->active_clients++;
            data->total_connections++;

            if (data->active_clients > data->peak_clients) {
                data->peak_clients = data->active_clients;
            }

            mcp_log_info("Client %d connected (active: %u, peak: %u, total: %u, max: %u)",
                        client_index, data->active_clients, data->peak_clients, data->total_connections, data->max_clients);

            ws_server_unlock_all_clients(data);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            // Handle client disconnection
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (client) {
                int client_id = client->client_id;
                mcp_log_info("Client %d disconnected", client_id);

                // Lock only this client's segment
                ws_server_lock_client(data, client_id);

                // Mark client as closing but preserve pending data
                client->state = WS_CLIENT_STATE_CLOSING;
                client->wsi = NULL;
                client->last_activity = time(NULL);
                client->ping_sent = 0;

                // Clean up immediately if no pending data
                if (client->receive_buffer_used == 0) {
                    mcp_log_debug("No pending data for client %d, cleaning up immediately", client_id);
                    ws_server_client_cleanup(client, data);
                }

                ws_server_unlock_client(data, client_id);
            } else {
                mcp_log_info("Unknown client disconnected");
            }
            break;
        }

        case LWS_CALLBACK_PROTOCOL_DESTROY: {
            // Clean up all clients when protocol is destroyed
            mcp_log_info("WebSocket protocol being destroyed, cleaning up all clients");

            ws_server_lock_all_clients(data);
            for (uint32_t i = 0; i < data->max_clients; i++) {
                ws_client_t* client = &data->clients[i];
                if (client->state != WS_CLIENT_STATE_INACTIVE) {
                    ws_server_client_cleanup(client, data);
                }
            }
            ws_server_unlock_all_clients(data);
            break;
        }

        case LWS_CALLBACK_RECEIVE_PONG: {
            // Update activity on pong receipt
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (client) {
                mcp_log_debug("Received pong from client %d", client->client_id);
                ws_server_client_update_activity(client);
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            // Process received data
            mcp_log_debug("WebSocket data received: %zu bytes", len);

            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            return ws_server_client_handle_received_data(data, client, wsi, in, len, lws_is_final_fragment(wsi));
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // Handle writable state
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            // Update activity timestamp
            ws_server_client_update_activity(client);
            break;
        }

        case LWS_CALLBACK_HTTP: {
            // Handle plain HTTP request
            mcp_log_info("HTTP request received: %s", (char*)in);

            // Send informational response
            unsigned char buffer[LWS_PRE + 128];
            unsigned char *p = &buffer[LWS_PRE];
            int head_len = sprintf((char *)p, "HTTP WebSocket server is running. Please use a WebSocket client to connect.");

            if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK, "text/plain",
                                           head_len, &p, &buffer[sizeof(buffer)])) {
                return 1;
            }

            if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, &buffer[sizeof(buffer)])) {
                return 1;
            }

            lws_callback_on_writable(wsi);
            return 0;
        }

        case LWS_CALLBACK_HTTP_WRITEABLE: {
            // Send HTTP response
            unsigned char buffer[LWS_PRE + 128];
            unsigned char *p = &buffer[LWS_PRE];
            int head_len = sprintf((char *)p, "HTTP WebSocket server is running. Please use a WebSocket client to connect.");

            if (lws_write(wsi, p, head_len, LWS_WRITE_HTTP) != head_len) {
                return 1;
            }

            // Close connection after response
            return -1;
        }

        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            // Filter incoming connections
            mcp_log_debug("WebSocket filter protocol connection");

            // Apply stricter filtering when near capacity
            uint32_t capacity_threshold = data->max_clients > 10 ? data->max_clients - 10 : data->max_clients / 2;
            if (data->active_clients >= capacity_threshold) {
                mcp_log_warn("WebSocket server near capacity (%u/%u), applying stricter filtering",
                           data->active_clients, data->max_clients);

                // Could implement additional filtering here (IP-based rate limiting, auth checks, etc.)
            }
            return 0;
        }

        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
            // This is called when a client initiates a connection
            mcp_log_debug("WebSocket filter network connection");

            // Check if we're at capacity
            if (data->active_clients >= data->max_clients) {
                mcp_log_warn("WebSocket server at capacity (%u/%u), rejecting connection",
                           data->active_clients, data->max_clients);
                return -1;
            }
            return 0;
        }

        case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
            // A new client connection is being instantiated
            mcp_log_debug("WebSocket new client instantiated");
            return 0;
        }

        case LWS_CALLBACK_WSI_CREATE: {
            // A new WebSocket instance is being created
            mcp_log_debug("WebSocket instance created");
            return 0;
        }

        default:
            //mcp_log_debug("WebSocket server default callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
            break;
    }

    return 0;
}
