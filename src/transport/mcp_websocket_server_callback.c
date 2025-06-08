#include "internal/websocket_server_internal.h"

// WebSocket protocol definitions
struct lws_protocols server_protocols[3];

// Server callback function implementation
int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                     void* user, void* in, size_t len) {
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_server_data_t* data = (ws_server_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    // Log only important callback reasons to reduce log volume
    if (reason == LWS_CALLBACK_ESTABLISHED ||
        reason == LWS_CALLBACK_CLOSED ||
        reason == LWS_CALLBACK_PROTOCOL_DESTROY) {
        mcp_log_ws_debug("server callback: %s", websocket_get_callback_reason_string(reason));
    } else if (reason != LWS_CALLBACK_SERVER_WRITEABLE &&
               reason != LWS_CALLBACK_RECEIVE &&
               reason != LWS_CALLBACK_RECEIVE_PONG) {
        mcp_log_ws_verbose("server callback: %s", websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // Handle new client connection
            mcp_log_ws_info("connection established");

            ws_server_lock_all_clients(data);

            // Find empty client slot using bitmap
            int client_index = ws_server_find_free_client_slot(data);
            if (client_index == -1) {
                data->rejected_connections++;
                ws_server_unlock_all_clients(data);
                mcp_log_ws_error("maximum clients reached (%u active, %u total, %u rejected, max: %u)",
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

            mcp_log_ws_info("client %d connected (active: %u, peak: %u, total: %u, max: %u)",
                            client_index, data->active_clients, data->peak_clients, data->total_connections, data->max_clients);

            ws_server_unlock_all_clients(data);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            // Handle client disconnection
            mcp_log_ws_info("connection closed");

            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (client) {
                ws_server_lock_all_clients(data);

                // Clear client data and update statistics
                ws_server_clear_client_bit(data->client_bitmap, client->client_id, data->bitmap_size);
                data->active_clients--;

                mcp_log_ws_info("client %d disconnected (active: %u, max: %u)",
                                client->client_id, data->active_clients, data->max_clients);

                ws_server_client_cleanup(client, data);

                ws_server_unlock_all_clients(data);
            }
            break;
        }

        case LWS_CALLBACK_PROTOCOL_DESTROY: {
            // Clean up all clients when protocol is destroyed
            mcp_log_ws_info("protocol being destroyed, cleaning up all clients");

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
                mcp_log_ws_verbose("received pong from client %d", client->client_id);
                ws_server_client_update_activity(client);
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            // Process received data
            mcp_log_ws_verbose("data received: %zu bytes", len);

            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_ws_error("client not found");
                return -1;
            }

            return ws_server_client_handle_received_data(data, client, wsi, in, len, lws_is_final_fragment(wsi));
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // Handle writable state
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_ws_error("client not found");
                return -1;
            }

            // Update activity timestamp
            ws_server_client_update_activity(client);
            break;
        }

        case LWS_CALLBACK_HTTP: {
            // Handle plain HTTP request
            mcp_log_ws_debug("HTTP request received: %s", (char*)in);

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
            mcp_log_ws_verbose("filter protocol connection");

            // Apply stricter filtering when near capacity
            uint32_t capacity_threshold = data->max_clients > 10 ? data->max_clients - 10 : data->max_clients / 2;
            if (data->active_clients >= capacity_threshold) {
                mcp_log_ws_warn("near capacity (%u/%u), applying stricter filtering",
                                data->active_clients, data->max_clients);

                // Could implement additional filtering here (IP-based rate limiting, auth checks, etc.)
            }
            return 0;
        }

        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
            // This is called when a client initiates a connection
            mcp_log_ws_verbose("filter network connection");

            // Check if we're at capacity
            if (data->active_clients >= data->max_clients) {
                mcp_log_ws_warn("at capacity (%u/%u), rejecting connection",
                                data->active_clients, data->max_clients);
                return -1;
            }
            return 0;
        }

        case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
            // A new client connection is being instantiated
            mcp_log_ws_verbose("new client instantiated");
            return 0;
        }

        case LWS_CALLBACK_WSI_CREATE: {
            // A new WebSocket instance is being created
            mcp_log_ws_verbose("instance created");
            return 0;
        }

        default:
            //mcp_log_debug("WebSocket server default callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
            break;
    }

    return 0;
}
