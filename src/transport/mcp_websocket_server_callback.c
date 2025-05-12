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

    // Debug log for important callback reasons only
    if (reason != LWS_CALLBACK_SERVER_WRITEABLE &&
        reason != LWS_CALLBACK_RECEIVE &&
        reason != LWS_CALLBACK_RECEIVE_PONG) {
        mcp_log_debug("WebSocket server callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // New client connection
            mcp_log_info("WebSocket connection established");

            mcp_mutex_lock(data->clients_mutex);

            // Find an empty client slot using bitmap
            int client_index = ws_server_find_free_client_slot(data);

            if (client_index == -1) {
                // Update rejection statistics
                data->rejected_connections++;

                mcp_mutex_unlock(data->clients_mutex);
                mcp_log_error("Maximum WebSocket clients reached (%d active, %d total connections, %d rejected)",
                             data->active_clients, data->total_connections, data->rejected_connections);
                return -1; // Reject connection
            }

            // Initialize client data
            ws_client_t* client = &data->clients[client_index];
            ws_server_client_init(client, client_index, wsi);

            // Store client pointer in user data
            lws_set_opaque_user_data(wsi, client);

            // Update bitmap and statistics
            ws_server_set_client_bit(data->client_bitmap, client_index);
            data->active_clients++;
            data->total_connections++;

            // Update peak clients if needed
            if (data->active_clients > data->peak_clients) {
                data->peak_clients = data->active_clients;
            }

            mcp_log_info("Client %d connected (active: %d, peak: %d, total: %d)",
                        client_index, data->active_clients, data->peak_clients, data->total_connections);

            mcp_mutex_unlock(data->clients_mutex);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            // Client disconnected
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);

            if (client) {
                mcp_log_info("Client %d disconnected", client->client_id);

                mcp_mutex_lock(data->clients_mutex);

                // Mark client as closing but don't free resources yet
                // This allows any pending messages to be processed
                client->state = WS_CLIENT_STATE_CLOSING;
                client->wsi = NULL;
                client->last_activity = time(NULL);

                // Reset ping counter
                client->ping_sent = 0;

                // If there's no pending data, clean up immediately
                // This reduces resource usage and makes slots available faster
                if (client->receive_buffer_used == 0) {
                    mcp_log_debug("No pending data for client %d, cleaning up immediately", client->client_id);
                    ws_server_client_cleanup(client, data);
                }

                mcp_mutex_unlock(data->clients_mutex);
            } else {
                mcp_log_info("Unknown client disconnected");
            }
            break;
        }

        case LWS_CALLBACK_PROTOCOL_DESTROY: {
            // Protocol is being destroyed, clean up all clients
            mcp_log_info("WebSocket protocol being destroyed, cleaning up all clients");

            mcp_mutex_lock(data->clients_mutex);

            for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                ws_client_t* client = &data->clients[i];
                if (client->state != WS_CLIENT_STATE_INACTIVE) {
                    ws_server_client_cleanup(client, data);
                }
            }

            mcp_mutex_unlock(data->clients_mutex);
            break;
        }

        case LWS_CALLBACK_RECEIVE_PONG: {
            // Received pong from client
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);

            if (client) {
                mcp_log_debug("Received pong from client %d", client->client_id);
                ws_server_client_update_activity(client);
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            // Receive data
            mcp_log_debug("WebSocket data received: %zu bytes", len);

            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            // Handle the received data
            return ws_server_client_handle_received_data(data, client, wsi, in, len, lws_is_final_fragment(wsi));
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // Ready to send data to client
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            // No message queue anymore, so nothing to send here
            // Messages are sent directly in the LWS_CALLBACK_RECEIVE handler

            // Update activity timestamp
            ws_server_client_update_activity(client);
            break;
        }

        case LWS_CALLBACK_HTTP: {
            // HTTP request (not WebSocket)
            mcp_log_info("HTTP request received: %s", (char*)in);

            // Return 200 OK with a simple message
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
            // Ready to send HTTP response
            unsigned char buffer[LWS_PRE + 128];
            unsigned char *p = &buffer[LWS_PRE];
            int head_len = sprintf((char *)p, "HTTP WebSocket server is running. Please use a WebSocket client to connect.");

            if (lws_write(wsi, p, head_len, LWS_WRITE_HTTP) != head_len) {
                return 1;
            }

            // Close the connection after sending the response
            return -1;
        }

        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            // This callback allows us to examine the HTTP headers and reject connections
            mcp_log_debug("WebSocket filter protocol connection");

            // Check if we're at or near capacity
            if (data->active_clients >= MAX_WEBSOCKET_CLIENTS - 5) {
                mcp_log_warn("WebSocket server near capacity (%d/%d), applying stricter filtering",
                           data->active_clients, MAX_WEBSOCKET_CLIENTS);

                // Here we could implement additional filtering logic
                // For example, rate limiting based on IP, authentication checks, etc.
            }
            return 0;
        }

        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
            // This is called when a client initiates a connection
            mcp_log_debug("WebSocket filter network connection");

            // Check if we're at capacity
            if (data->active_clients >= MAX_WEBSOCKET_CLIENTS) {
                mcp_log_warn("WebSocket server at capacity (%d/%d), rejecting connection",
                           data->active_clients, MAX_WEBSOCKET_CLIENTS);
                return -1; // Reject connection
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
            break;
    }

    return 0;
}
