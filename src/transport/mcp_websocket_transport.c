#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_websocket_transport.h"
#include "mcp_websocket_common.h"
#include "internal/transport_internal.h"
#include "internal/transport_interfaces.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_socket_utils.h"
#include "mcp_thread_local.h"

#include "libwebsockets.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Define maximum number of simultaneous WebSocket clients
#define MAX_WEBSOCKET_CLIENTS 64

// WebSocket client connection state
typedef enum {
    WS_CLIENT_STATE_INACTIVE = 0,   // Client slot is unused
    WS_CLIENT_STATE_CONNECTING,     // Client is connecting
    WS_CLIENT_STATE_ACTIVE,         // Client is connected and active
    WS_CLIENT_STATE_CLOSING,        // Client is closing
    WS_CLIENT_STATE_ERROR           // Client encountered an error
} ws_client_state_t;

// WebSocket client connection information
typedef struct {
    struct lws* wsi;                // libwebsockets connection handle
    ws_client_state_t state;        // Connection state
    char* receive_buffer;           // Receive buffer
    size_t receive_buffer_len;      // Receive buffer length
    size_t receive_buffer_used;     // Used receive buffer length
    int client_id;                  // Client ID for tracking
    ws_message_item_t* response_queue; // Queue of responses to send
    ws_message_item_t* response_queue_tail; // Tail of response queue for faster insertion
    mcp_mutex_t* queue_mutex;       // Mutex for response queue
    time_t last_activity;           // Time of last activity for timeout detection
    uint32_t ping_sent;             // Number of pings sent without response
} ws_client_t;

// WebSocket server transport data
typedef struct {
    struct lws_context* context;    // libwebsockets context
    const struct lws_protocols* protocols; // WebSocket protocols
    bool running;                   // Running flag
    mcp_thread_t event_thread;      // Event loop thread
    mcp_mutex_t* clients_mutex;     // Clients list mutex
    ws_client_t clients[MAX_WEBSOCKET_CLIENTS]; // Clients list
    mcp_transport_t* transport;     // Transport handle
    mcp_websocket_config_t config;  // WebSocket configuration
    time_t last_ping_time;          // Time of last ping check
    time_t last_cleanup_time;       // Time of last inactive client cleanup
} ws_server_data_t;

// Forward declarations
static int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);
static void ws_server_check_timeouts(ws_server_data_t* data);
static void ws_server_cleanup_inactive_clients(ws_server_data_t* data);

// Helper functions for client management
static ws_client_t* ws_server_find_client_by_wsi(ws_server_data_t* data, struct lws* wsi);
static void ws_client_update_activity(ws_client_t* client);
static int ws_client_send_ping(ws_client_t* client);

// Helper functions for response queue management
static void ws_client_queue_response(ws_client_t* client, const void* data, size_t size, ws_message_type_t type);
static ws_message_item_t* ws_client_dequeue_response(ws_client_t* client);
static void ws_client_free_response_queue(ws_client_t* client);

// WebSocket protocol definitions
static struct lws_protocols server_protocols[3];

// Server callback function implementation
static int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len) {
    // Suppress unused parameter warning
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_server_data_t* data = (ws_server_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    // Debug log for all callback reasons except frequent ones
    if (reason != LWS_CALLBACK_SERVER_WRITEABLE && reason != LWS_CALLBACK_RECEIVE) {
        mcp_log_debug("WebSocket server callback: reason=%d", reason);
    }

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // New client connection
            mcp_log_info("WebSocket connection established");

            mcp_mutex_lock(data->clients_mutex);

            // Find an empty client slot
            int client_index = -1;
            for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                if (data->clients[i].state == WS_CLIENT_STATE_INACTIVE) {
                    client_index = i;
                    break;
                }
            }

            if (client_index == -1) {
                mcp_mutex_unlock(data->clients_mutex);
                mcp_log_error("Maximum WebSocket clients reached");
                return -1; // Reject connection
            }

            // Initialize client data
            ws_client_t* client = &data->clients[client_index];
            client->wsi = wsi;
            client->state = WS_CLIENT_STATE_ACTIVE;
            client->receive_buffer = NULL;
            client->receive_buffer_len = 0;
            client->receive_buffer_used = 0;
            client->client_id = client_index;
            client->response_queue = NULL;
            client->response_queue_tail = NULL;
            client->queue_mutex = mcp_mutex_create();
            client->last_activity = time(NULL);
            client->ping_sent = 0;

            if (!client->queue_mutex) {
                mcp_log_error("Failed to create client queue mutex");
                client->state = WS_CLIENT_STATE_INACTIVE;
                mcp_mutex_unlock(data->clients_mutex);
                return -1;
            }

            // Store client pointer in user data
            lws_set_opaque_user_data(wsi, client);

            mcp_log_info("Client %d connected", client_index);

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
                    // Clean up client resources
                    free(client->receive_buffer);
                    client->receive_buffer = NULL;
                    client->receive_buffer_len = 0;
                    client->receive_buffer_used = 0;

                    // Free response queue
                    ws_client_free_response_queue(client);

                    // Destroy mutex
                    if (client->queue_mutex) {
                        mcp_mutex_destroy(client->queue_mutex);
                        client->queue_mutex = NULL;
                    }

                    client->state = WS_CLIENT_STATE_INACTIVE;
                    client->wsi = NULL;
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
                ws_client_update_activity(client);
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

            // Ensure buffer is large enough
            if (client->receive_buffer_used + len > client->receive_buffer_len) {
                size_t new_len = client->receive_buffer_len == 0 ? 4096 : client->receive_buffer_len * 2;
                while (new_len < client->receive_buffer_used + len) {
                    new_len *= 2;
                }

                char* new_buffer = (char*)realloc(client->receive_buffer, new_len);
                if (!new_buffer) {
                    mcp_log_error("Failed to allocate WebSocket receive buffer");
                    return -1;
                }

                client->receive_buffer = new_buffer;
                client->receive_buffer_len = new_len;
            }

            // Update client activity timestamp
            ws_client_update_activity(client);

            // Log the raw message data for debugging (including hex dump)
            if (len < 1000) {  // Only log if not too large
                char debug_buffer[1024] = {0};
                size_t copy_len = len < 1000 ? len : 1000;
                memcpy(debug_buffer, in, copy_len);
                debug_buffer[copy_len] = '\0';  // Ensure null termination

                // Log as text
                mcp_log_debug("WebSocket server raw data (text): '%s'", debug_buffer);

                // Log as hex for the first 32 bytes
                char hex_buffer[200] = {0};
                size_t hex_len = len < 32 ? len : 32;
                for (size_t i = 0; i < hex_len; i++) {
                    sprintf(hex_buffer + i*3, "%02x ", (unsigned char)((char*)in)[i]);
                }
                mcp_log_debug("WebSocket server raw data (hex): %s", hex_buffer);

                // Check if this is a JSON message (most common case)
                if (len > 0 && ((char*)in)[0] == '{') {
                    mcp_log_debug("Detected JSON message");
                }

                // Check if this might be a length-prefixed message
                if (len >= 4) {
                    // Interpret first 4 bytes as a 32-bit length (network byte order)
                    uint32_t msg_len = 0;
                    // Convert from network byte order (big endian) to host byte order
                    msg_len = ((unsigned char)((char*)in)[0] << 24) |
                              ((unsigned char)((char*)in)[1] << 16) |
                              ((unsigned char)((char*)in)[2] << 8) |
                              ((unsigned char)((char*)in)[3]);

                    // Log the extracted length
                    mcp_log_debug("Possible message length prefix: %u bytes (total received: %zu bytes)",
                                 msg_len, len);

                    // If this looks like a length-prefixed message, skip the length prefix
                    if (msg_len <= len - 4 && msg_len > 0) {
                        mcp_log_debug("Detected length-prefixed message, skipping 4-byte prefix");
                        // Copy data without the length prefix
                        memcpy(client->receive_buffer + client->receive_buffer_used,
                               (char*)in + 4, len - 4);
                        client->receive_buffer_used += (len - 4);

                        // Process the message immediately if it's a complete message
                        if (lws_is_final_fragment(wsi)) {
                            // Ensure the buffer is null-terminated for string operations
                            if (client->receive_buffer_used < client->receive_buffer_len) {
                                client->receive_buffer[client->receive_buffer_used] = '\0';
                            } else {
                                // Resize buffer to add null terminator
                                char* new_buffer = (char*)realloc(client->receive_buffer, client->receive_buffer_used + 1);
                                if (!new_buffer) {
                                    mcp_log_error("Failed to resize WebSocket server receive buffer for null terminator");
                                    return -1;
                                }
                                client->receive_buffer = new_buffer;
                                client->receive_buffer_len = client->receive_buffer_used + 1;
                                client->receive_buffer[client->receive_buffer_used] = '\0';
                            }

                            // Process complete message
                            if (data->transport && data->transport->message_callback) {
                                // Initialize thread-local arena for JSON parsing
                                mcp_log_debug("Initializing thread-local arena for server message processing");
                                if (mcp_arena_init_current_thread(4096) != 0) {
                                    mcp_log_error("Failed to initialize thread-local arena in WebSocket server callback");
                                }

                                int error_code = 0;
                                char* response = data->transport->message_callback(
                                    data->transport->callback_user_data,
                                    client->receive_buffer,
                                    client->receive_buffer_used,
                                    &error_code
                                );

                                // If there's a response, queue it for sending
                                if (response) {
                                    // Add response to client's queue
                                    ws_client_queue_response(client, response, strlen(response), WS_MESSAGE_TYPE_TEXT);

                                    // Free the response as it's been copied to the queue
                                    free(response);
                                }
                            }

                            // Reset buffer
                            client->receive_buffer_used = 0;
                        }

                        return 0;  // Skip the rest of the processing
                    }
                }
            }

            // Normal copy if not a length-prefixed message or if length prefix check failed
            memcpy(client->receive_buffer + client->receive_buffer_used, in, len);
            client->receive_buffer_used += len;

            // Check if this is a complete message
            if (lws_is_final_fragment(wsi)) {
                // Ensure the buffer is null-terminated for string operations
                if (client->receive_buffer_used < client->receive_buffer_len) {
                    client->receive_buffer[client->receive_buffer_used] = '\0';
                } else {
                    // Resize buffer to add null terminator
                    char* new_buffer = (char*)realloc(client->receive_buffer, client->receive_buffer_used + 1);
                    if (!new_buffer) {
                        mcp_log_error("Failed to resize WebSocket server receive buffer for null terminator");
                        return -1;
                    }
                    client->receive_buffer = new_buffer;
                    client->receive_buffer_len = client->receive_buffer_used + 1;
                    client->receive_buffer[client->receive_buffer_used] = '\0';
                }

                // Process complete message
                if (data->transport && data->transport->message_callback) {
                    // Initialize thread-local arena for JSON parsing
                    mcp_log_debug("Initializing thread-local arena for server message processing");
                    if (mcp_arena_init_current_thread(4096) != 0) {
                        mcp_log_error("Failed to initialize thread-local arena in WebSocket server callback");
                    }

                    int error_code = 0;
                    char* response = data->transport->message_callback(
                        data->transport->callback_user_data,
                        client->receive_buffer,
                        client->receive_buffer_used,
                        &error_code
                    );

                    // If there's a response, queue it for sending
                    if (response) {
                        // Add response to client's queue
                        ws_client_queue_response(client, response, strlen(response), WS_MESSAGE_TYPE_TEXT);

                        // Free the response as it's been copied to the queue
                        free(response);
                    }
                }

                // Reset buffer
                client->receive_buffer_used = 0;
            }

            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // Ready to send data to client
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            // Dequeue a response
            ws_message_item_t* item = ws_client_dequeue_response(client);
            if (item) {
                // Send data (buffer already has LWS_PRE padding)
                enum lws_write_protocol write_protocol =
                    (item->type == WS_MESSAGE_TYPE_BINARY) ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;

                int result = lws_write(wsi, item->data + LWS_PRE, item->size, write_protocol);

                if (result < 0) {
                    mcp_log_error("WebSocket server write failed");

                    // Put the item back in the queue
                    mcp_mutex_lock(client->queue_mutex);
                    item->next = client->response_queue;
                    if (!client->response_queue) {
                        client->response_queue_tail = item;
                    }
                    client->response_queue = item;
                    mcp_mutex_unlock(client->queue_mutex);

                    // Request another callback
                    lws_callback_on_writable(wsi);
                    return 0;
                }

                // Free response item
                free(item->data);
                free(item);

                // Update activity timestamp
                ws_client_update_activity(client);

                // Check if there are more responses to send
                mcp_mutex_lock(client->queue_mutex);
                bool has_more = (client->response_queue != NULL);
                mcp_mutex_unlock(client->queue_mutex);

                if (has_more) {
                    // Request another callback
                    lws_callback_on_writable(wsi);
                }
            }

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

            if (lws_write(wsi, p, head_len, LWS_WRITE_HTTP) != len) {
                return 1;
            }

            // Close the connection after sending the response
            return -1;
        }

        default:
            break;
    }

    return 0;
}

// Helper function to add a response to the client's queue
static void ws_client_queue_response(ws_client_t* client, const void* data, size_t size, ws_message_type_t type) {
    if (!client || !data || size == 0) {
        return;
    }

    // Use common function to enqueue message
    if (mcp_websocket_enqueue_message(
            &client->response_queue,
            &client->response_queue_tail,
            client->queue_mutex,
            data,
            size,
            type) != 0) {
        return;
    }

    // Update activity timestamp
    ws_client_update_activity(client);

    // Request a callback when the socket is writable
    if (client->wsi) {
        lws_callback_on_writable(client->wsi);
    }
}

// Helper function to remove and return the first response from the client's queue
static ws_message_item_t* ws_client_dequeue_response(ws_client_t* client) {
    if (!client) {
        return NULL;
    }

    // Use common function to dequeue message
    return mcp_websocket_dequeue_message(
        &client->response_queue,
        &client->response_queue_tail,
        client->queue_mutex);
}

// Helper function to free all responses in the client's queue
static void ws_client_free_response_queue(ws_client_t* client) {
    if (!client) {
        return;
    }

    // Use common function to free message queue
    mcp_websocket_free_message_queue(
        &client->response_queue,
        &client->response_queue_tail,
        client->queue_mutex);
}

// Helper function to find a client by WebSocket instance
static ws_client_t* ws_server_find_client_by_wsi(ws_server_data_t* data, struct lws* wsi) {
    if (!data || !wsi) {
        return NULL;
    }

    // First try to get the client from opaque user data (faster)
    ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
    if (client) {
        return client;
    }

    // If not found, search through the client list
    mcp_mutex_lock(data->clients_mutex);

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (data->clients[i].wsi == wsi) {
            client = &data->clients[i];
            break;
        }
    }

    mcp_mutex_unlock(data->clients_mutex);

    return client;
}

// Helper function to update client activity timestamp
static void ws_client_update_activity(ws_client_t* client) {
    if (!client) {
        return;
    }

    client->last_activity = time(NULL);
    client->ping_sent = 0; // Reset ping counter on activity
}

// Helper function to send a ping to a client
static int ws_client_send_ping(ws_client_t* client) {
    if (!client || !client->wsi || client->state != WS_CLIENT_STATE_ACTIVE) {
        return -1;
    }

    // Increment ping counter
    client->ping_sent++;

    // Request a callback to send a ping
    return lws_callback_on_writable(client->wsi);
}

// Helper function to check for client timeouts and send pings
static void ws_server_check_timeouts(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only check every PING_INTERVAL_MS milliseconds
    if (difftime(now, data->last_ping_time) * 1000 < WS_PING_INTERVAL_MS) {
        return;
    }

    data->last_ping_time = now;

    mcp_mutex_lock(data->clients_mutex);

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        ws_client_t* client = &data->clients[i];

        if (client->state == WS_CLIENT_STATE_ACTIVE && client->wsi) {
            // Check if client has been inactive for too long
            if (difftime(now, client->last_activity) * 1000 > WS_PING_TIMEOUT_MS) {
                // If we've sent too many pings without response, close the connection
                if (client->ping_sent > 2) {
                    mcp_log_warn("Client %d timed out, closing connection", client->client_id);
                    client->state = WS_CLIENT_STATE_CLOSING;
                    lws_set_timeout(client->wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
                } else {
                    // Send a ping to check if client is still alive
                    mcp_log_debug("Sending ping to client %d", client->client_id);
                    ws_client_send_ping(client);
                }
            }
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

// Helper function to clean up inactive clients
static void ws_server_cleanup_inactive_clients(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only clean up every CLEANUP_INTERVAL_MS milliseconds
    if (difftime(now, data->last_cleanup_time) * 1000 < WS_CLEANUP_INTERVAL_MS) {
        return;
    }

    data->last_cleanup_time = now;

    mcp_mutex_lock(data->clients_mutex);

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        ws_client_t* client = &data->clients[i];

        // Clean up clients in error state or closing state without a valid wsi
        if ((client->state == WS_CLIENT_STATE_ERROR ||
             (client->state == WS_CLIENT_STATE_CLOSING && !client->wsi)) &&
            difftime(now, client->last_activity) > 10) { // 10 seconds grace period

            mcp_log_info("Cleaning up inactive client %d", client->client_id);

            // Free resources
            free(client->receive_buffer);
            client->receive_buffer = NULL;
            client->receive_buffer_len = 0;
            client->receive_buffer_used = 0;

            // Free response queue
            ws_client_free_response_queue(client);

            // Reset client state
            client->state = WS_CLIENT_STATE_INACTIVE;
            client->wsi = NULL;
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

// Server event loop thread function
static void* ws_server_event_thread(void* arg) {
    ws_server_data_t* data = (ws_server_data_t*)arg;

    while (data->running) {
        lws_service(data->context, 50); // 50ms timeout

        // Check for client timeouts and send pings
        ws_server_check_timeouts(data);

        // Clean up inactive clients
        ws_server_cleanup_inactive_clients(data);
    }

    return NULL;
}

// Server transport start function
static int ws_server_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // Store callback information
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Initialize protocols
    mcp_websocket_init_protocols(server_protocols, ws_server_callback);

    // Create libwebsockets context using common function
    data->context = mcp_websocket_create_context(
        data->config.host,
        data->config.port,
        data->config.path,
        server_protocols,
        data,
        true, // is_server
        data->config.use_ssl,
        data->config.cert_path,
        data->config.key_path
    );

    if (!data->context) {
        mcp_log_error("Failed to create WebSocket server context");
        return -1;
    }

    // Initialize client list
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        data->clients[i].state = WS_CLIENT_STATE_INACTIVE;
        data->clients[i].wsi = NULL;
        data->clients[i].receive_buffer = NULL;
        data->clients[i].receive_buffer_len = 0;
        data->clients[i].receive_buffer_used = 0;
        data->clients[i].client_id = i;
        data->clients[i].response_queue = NULL;
        data->clients[i].response_queue_tail = NULL;
        data->clients[i].queue_mutex = NULL;
        data->clients[i].last_activity = 0;
        data->clients[i].ping_sent = 0;
    }

    // Initialize timestamps
    data->last_ping_time = time(NULL);
    data->last_cleanup_time = time(NULL);

    // Create mutex
    data->clients_mutex = mcp_mutex_create();
    if (!data->clients_mutex) {
        mcp_log_error("Failed to create WebSocket server mutex");
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_server_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket server event thread");
        mcp_mutex_destroy(data->clients_mutex);
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    mcp_log_info("WebSocket server started on %s:%d", data->config.host, data->config.port);

    return 0;
}

// Server transport stop function
static int ws_server_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Set running flag to false to stop event loop
    data->running = false;

    // Force libwebsockets to break out of its service loop
    if (data->context) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets service");
    }

    // Wait for event thread to exit
    if (data->event_thread) {
        mcp_log_info("Waiting for WebSocket server event thread to exit...");
        mcp_thread_join(data->event_thread, NULL);
        data->event_thread = 0;
    }

    // Clean up client resources
    mcp_mutex_lock(data->clients_mutex);
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (data->clients[i].state != WS_CLIENT_STATE_INACTIVE) {
            free(data->clients[i].receive_buffer);
            data->clients[i].receive_buffer = NULL;
            data->clients[i].receive_buffer_len = 0;
            data->clients[i].receive_buffer_used = 0;

            // Free response queue
            ws_client_free_response_queue(&data->clients[i]);

            // Destroy mutex
            if (data->clients[i].queue_mutex) {
                mcp_mutex_destroy(data->clients[i].queue_mutex);
                data->clients[i].queue_mutex = NULL;
            }

            data->clients[i].state = WS_CLIENT_STATE_INACTIVE;
            data->clients[i].wsi = NULL;
        }
    }
    mcp_mutex_unlock(data->clients_mutex);

    // Destroy mutex
    mcp_mutex_destroy(data->clients_mutex);

    // Destroy libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("WebSocket server stopped");

    return 0;
}

// Server transport send function
static int ws_server_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    // Suppress unused parameter warnings
    (void)transport;
    (void)data;
    (void)size;
    // Server transport doesn't support direct send
    // Responses are sent in the callback
    mcp_log_error("WebSocket server transport doesn't support direct send");
    return -1;
}

// Server transport sendv function
static int ws_server_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    // Suppress unused parameter warnings
    (void)transport;
    (void)buffers;
    (void)buffer_count;
    // Server transport doesn't support direct send
    // Responses are sent in the callback
    mcp_log_error("WebSocket server transport doesn't support direct sendv");
    return -1;
}

// Server transport destroy function
static void ws_server_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;
    if (!data) {
        free(transport);
        return;
    }

    // Stop transport if running
    if (data->running) {
        ws_server_transport_stop(transport);
    }

    // Free config strings
    // Note: We don't free the strings in config because they are owned by the caller

    // Free transport data
    free(data);

    // Free transport
    free(transport);
}

// Create WebSocket server transport
mcp_transport_t* mcp_transport_websocket_server_create(const mcp_websocket_config_t* config) {
    if (!config || !config->host) {
        return NULL;
    }

    // Allocate transport
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) {
        return NULL;
    }

    // Allocate server data
    ws_server_data_t* data = (ws_server_data_t*)calloc(1, sizeof(ws_server_data_t));
    if (!data) {
        free(transport);
        return NULL;
    }

    // Copy config
    data->config = *config;
    data->protocols = server_protocols;
    data->transport = transport;

    // Initialize protocols
    mcp_websocket_init_protocols(server_protocols, ws_server_callback);

    // Initialize timestamps
    data->last_ping_time = time(NULL);
    data->last_cleanup_time = time(NULL);

    // Set transport type and operations
    transport->type = MCP_TRANSPORT_TYPE_SERVER;
    transport->server.start = ws_server_transport_start;
    transport->server.stop = ws_server_transport_stop;
    transport->server.destroy = ws_server_transport_destroy;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}


