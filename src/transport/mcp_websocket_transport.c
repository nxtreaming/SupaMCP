#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_websocket_transport.h"
#include "internal/transport_internal.h"
#include "internal/transport_interfaces.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_socket_utils.h"
#include "mcp_thread_local.h"

#include "libwebsockets.h"
#include <string.h>
#include <stdlib.h>

// Define maximum number of simultaneous WebSocket clients
#define MAX_WEBSOCKET_CLIENTS 64

// WebSocket client connection state
typedef enum {
    WS_CLIENT_STATE_INACTIVE = 0,
    WS_CLIENT_STATE_CONNECTING,
    WS_CLIENT_STATE_ACTIVE,
    WS_CLIENT_STATE_CLOSING
} ws_client_state_t;

// Response queue item
typedef struct response_item {
    char* data;                     // Response data
    size_t len;                     // Response length
    struct response_item* next;     // Next item in queue
} response_item_t;

// WebSocket client connection information
typedef struct {
    struct lws* wsi;                // libwebsockets connection handle
    ws_client_state_t state;        // Connection state
    char* receive_buffer;           // Receive buffer
    size_t receive_buffer_len;      // Receive buffer length
    size_t receive_buffer_used;     // Used receive buffer length
    int client_id;                  // Client ID for tracking
    response_item_t* response_queue; // Queue of responses to send
    mcp_mutex_t* queue_mutex;       // Mutex for response queue
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
} ws_server_data_t;

// Forward declarations
static int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);

// Helper functions for response queue management
static void ws_client_queue_response(ws_client_t* client, const char* data, size_t len);
static response_item_t* ws_client_dequeue_response(ws_client_t* client);
static void ws_client_free_response_queue(ws_client_t* client);

// WebSocket protocol definitions
static struct lws_protocols server_protocols[] = {
    {
        "mcp-protocol",            // Protocol name
        ws_server_callback,        // Callback function
        0,                         // Per-connection user data size
        4096,                      // Rx buffer size
        0, NULL, 0                 // Reserved fields
    },
    {
        "http-only",               // HTTP protocol for handshake
        ws_server_callback,        // Callback function
        0,                         // Per-connection user data size
        4096,                      // Rx buffer size
        0, NULL, 0                 // Reserved fields
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } // Terminator
};

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
            data->clients[client_index].wsi = wsi;
            data->clients[client_index].state = WS_CLIENT_STATE_ACTIVE;
            data->clients[client_index].receive_buffer = NULL;
            data->clients[client_index].receive_buffer_len = 0;
            data->clients[client_index].receive_buffer_used = 0;
            data->clients[client_index].client_id = client_index;
            data->clients[client_index].response_queue = NULL;
            data->clients[client_index].queue_mutex = mcp_mutex_create();

            if (!data->clients[client_index].queue_mutex) {
                mcp_log_error("Failed to create client queue mutex");
                data->clients[client_index].state = WS_CLIENT_STATE_INACTIVE;
                mcp_mutex_unlock(data->clients_mutex);
                return -1;
            }

            // Store client index in user data
            lws_set_opaque_user_data(wsi, &data->clients[client_index]);

            mcp_mutex_unlock(data->clients_mutex);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            // Client disconnected
            mcp_log_info("WebSocket connection closed");

            mcp_mutex_lock(data->clients_mutex);

            // Find the client
            for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                if (data->clients[i].wsi == wsi) {
                    // Clean up client data
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
                    break;
                }
            }

            mcp_mutex_unlock(data->clients_mutex);
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

                        // Log the actual message content
                        if (len - 4 < 1000) {
                            char content_buffer[1024] = {0};
                            memcpy(content_buffer, (char*)in + 4, len - 4);
                            content_buffer[len - 4] = '\0';
                            mcp_log_debug("Message content after skipping prefix: '%s'", content_buffer);
                        }

                        // Continue with processing the message
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
                                    ws_client_queue_response(client, response, strlen(response));

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

            // Normal copy if not a length-prefixed message
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
                        ws_client_queue_response(client, response, strlen(response));

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
            response_item_t* item = ws_client_dequeue_response(client);
            if (item) {
                // LWS requires some pre-padding for its headers
                unsigned char* buf = (unsigned char*)malloc(LWS_PRE + item->len);
                if (!buf) {
                    mcp_log_error("Failed to allocate WebSocket send buffer");

                    // Put the item back in the queue
                    mcp_mutex_lock(client->queue_mutex);
                    item->next = client->response_queue;
                    client->response_queue = item;
                    mcp_mutex_unlock(client->queue_mutex);

                    // Request another callback
                    lws_callback_on_writable(wsi);
                    return 0;
                }

                // Copy data to buffer after pre-padding
                memcpy(buf + LWS_PRE, item->data, item->len);

                // Send data
                int result = lws_write(wsi, buf + LWS_PRE, item->len, LWS_WRITE_TEXT);

                // Free buffer
                free(buf);

                // Free response item
                free(item->data);
                free(item);

                if (result < 0) {
                    mcp_log_error("WebSocket server write failed");
                    return -1;
                }

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
static void ws_client_queue_response(ws_client_t* client, const char* data, size_t len) {
    if (!client || !data || len == 0) {
        return;
    }

    // Create new response item
    response_item_t* item = (response_item_t*)malloc(sizeof(response_item_t));
    if (!item) {
        mcp_log_error("Failed to allocate response item");
        return;
    }

    // Copy response data
    item->data = (char*)malloc(len);
    if (!item->data) {
        mcp_log_error("Failed to allocate response data");
        free(item);
        return;
    }
    memcpy(item->data, data, len);
    item->len = len;
    item->next = NULL;

    // Lock mutex
    mcp_mutex_lock(client->queue_mutex);

    // Add to queue (at the end)
    if (!client->response_queue) {
        client->response_queue = item;
    } else {
        response_item_t* current = client->response_queue;
        while (current->next) {
            current = current->next;
        }
        current->next = item;
    }

    // Unlock mutex
    mcp_mutex_unlock(client->queue_mutex);

    // Request a callback when the socket is writable
    if (client->wsi) {
        lws_callback_on_writable(client->wsi);
    }
}

// Helper function to remove and return the first response from the client's queue
static response_item_t* ws_client_dequeue_response(ws_client_t* client) {
    if (!client || !client->response_queue) {
        return NULL;
    }

    response_item_t* item = NULL;

    // Lock mutex
    mcp_mutex_lock(client->queue_mutex);

    // Remove first item from queue
    if (client->response_queue) {
        item = client->response_queue;
        client->response_queue = item->next;
        item->next = NULL;
    }

    // Unlock mutex
    mcp_mutex_unlock(client->queue_mutex);

    return item;
}

// Helper function to free all responses in the client's queue
static void ws_client_free_response_queue(ws_client_t* client) {
    if (!client) {
        return;
    }

    // Lock mutex
    mcp_mutex_lock(client->queue_mutex);

    // Free all items in queue
    response_item_t* current = client->response_queue;
    while (current) {
        response_item_t* next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    client->response_queue = NULL;

    // Unlock mutex
    mcp_mutex_unlock(client->queue_mutex);
}

// Server event loop thread function
static void* ws_server_event_thread(void* arg) {
    ws_server_data_t* data = (ws_server_data_t*)arg;

    while (data->running) {
        lws_service(data->context, 50); // 50ms timeout
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
    // Suppress unused parameter warnings
    (void)message_callback;
    (void)user_data;
    (void)error_callback;
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Create libwebsockets context
    struct lws_context_creation_info info = {0};
    info.port = data->config.port;
    info.iface = data->config.host;
    info.protocols = data->protocols;
    info.user = data;
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE |
                   LWS_SERVER_OPTION_VALIDATE_UTF8 |
                   LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Set mount for WebSocket path
    static const struct lws_http_mount mount = {
        .mountpoint = "/ws",
        .mountpoint_len = 3,
        .origin = "http://localhost",
        .origin_protocol = LWSMPRO_CALLBACK,
        .mount_next = NULL
    };
    info.mounts = &mount;

    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.ssl_cert_filepath = data->config.cert_path;
        info.ssl_private_key_filepath = data->config.key_path;
    }

    data->context = lws_create_context(&info);
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
        data->clients[i].queue_mutex = NULL;
    }

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


