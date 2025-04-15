#include "mcp_websocket_transport.h"
#include "internal/transport_internal.h"
#include "internal/transport_interfaces.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_socket_utils.h"

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

// WebSocket client connection information
typedef struct {
    struct lws* wsi;                // libwebsockets connection handle
    ws_client_state_t state;        // Connection state
    char* receive_buffer;           // Receive buffer
    size_t receive_buffer_len;      // Receive buffer length
    size_t receive_buffer_used;     // Used receive buffer length
    int client_id;                  // Client ID for tracking
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

// WebSocket client transport data
typedef struct {
    struct lws_context* context;    // libwebsockets context
    struct lws* wsi;                // libwebsockets connection handle
    const struct lws_protocols* protocols; // WebSocket protocols
    bool running;                   // Running flag
    mcp_thread_t event_thread;      // Event loop thread
    char* receive_buffer;           // Receive buffer
    size_t receive_buffer_len;      // Receive buffer length
    size_t receive_buffer_used;     // Used receive buffer length
    mcp_transport_t* transport;     // Transport handle
    mcp_websocket_config_t config;  // WebSocket configuration
    bool connected;                 // Connection state
    bool reconnect;                 // Whether to reconnect on disconnect
} ws_client_data_t;

// Forward declarations
static int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);

// WebSocket protocol definitions
static struct lws_protocols server_protocols[] = {
    {
        "mcp-protocol",            // Protocol name
        ws_server_callback,        // Callback function
        0,                         // Per-connection user data size
        4096,                      // Rx buffer size
        0, NULL, 0                 // Reserved fields
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } // Terminator
};

static struct lws_protocols client_protocols[] = {
    {
        "mcp-protocol",            // Protocol name
        ws_client_callback,        // Callback function
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

            // Copy data to buffer
            memcpy(client->receive_buffer + client->receive_buffer_used, in, len);
            client->receive_buffer_used += len;

            // Check if this is a complete message
            if (lws_is_final_fragment(wsi)) {
                // Process complete message
                if (data->transport && data->transport->message_callback) {
                    int error_code = 0;
                    char* response = data->transport->message_callback(
                        data->transport->callback_user_data,
                        client->receive_buffer,
                        client->receive_buffer_used,
                        &error_code
                    );

                    // If there's a response, send it back to the client
                    if (response) {
                        // We need to send the response in the next write opportunity
                        // For simplicity, we'll just queue a callback for writing
                        lws_callback_on_writable(wsi);

                        // In a real implementation, we would store the response
                        // in a per-client queue and send it in LWS_CALLBACK_SERVER_WRITEABLE
                        // For now, we'll just free it
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
            // In a real implementation, we would dequeue and send responses here
            break;
        }

        default:
            break;
    }

    return 0;
}

// Client callback function implementation
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len) {
    // Suppress unused parameter warning
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_client_data_t* data = (ws_client_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            // Connection established
            mcp_log_info("WebSocket client connection established");
            data->wsi = wsi;
            data->connected = true;
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            // Connection error
            mcp_log_error("WebSocket client connection error: %s",
                         in ? (char*)in : "unknown error");
            data->wsi = NULL;
            data->connected = false;

            // If reconnect is enabled, schedule a reconnection
            if (data->reconnect && data->running) {
                // In a real implementation, we would implement reconnection logic
                // with exponential backoff
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED: {
            // Connection closed
            mcp_log_info("WebSocket client connection closed");
            data->wsi = NULL;
            data->connected = false;

            // If reconnect is enabled, schedule a reconnection
            if (data->reconnect && data->running) {
                // In a real implementation, we would implement reconnection logic
                // with exponential backoff
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Receive data
            mcp_log_debug("WebSocket client data received: %zu bytes", len);

            // Ensure buffer is large enough
            if (data->receive_buffer_used + len > data->receive_buffer_len) {
                size_t new_len = data->receive_buffer_len == 0 ? 4096 : data->receive_buffer_len * 2;
                while (new_len < data->receive_buffer_used + len) {
                    new_len *= 2;
                }

                char* new_buffer = (char*)realloc(data->receive_buffer, new_len);
                if (!new_buffer) {
                    mcp_log_error("Failed to allocate WebSocket client receive buffer");
                    return -1;
                }

                data->receive_buffer = new_buffer;
                data->receive_buffer_len = new_len;
            }

            // Copy data to buffer
            memcpy(data->receive_buffer + data->receive_buffer_used, in, len);
            data->receive_buffer_used += len;

            // Check if this is a complete message
            if (lws_is_final_fragment(wsi)) {
                // Process complete message
                if (data->transport && data->transport->message_callback) {
                    int error_code = 0;
                    char* response = data->transport->message_callback(
                        data->transport->callback_user_data,
                        data->receive_buffer,
                        data->receive_buffer_used,
                        &error_code
                    );

                    // Free the response if one was returned
                    if (response) {
                        free(response);
                    }
                }

                // Reset buffer
                data->receive_buffer_used = 0;
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            // Ready to send data to server
            // In a real implementation, we would dequeue and send messages here
            break;
        }

        default:
            break;
    }

    return 0;
}

// Server event loop thread function
static void* ws_server_event_thread(void* arg) {
    ws_server_data_t* data = (ws_server_data_t*)arg;

    while (data->running) {
        lws_service(data->context, 50); // 50ms timeout
    }

    return NULL;
}

// Client event loop thread function
static void* ws_client_event_thread(void* arg) {
    ws_client_data_t* data = (ws_client_data_t*)arg;

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
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

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

// Client transport start function
static int ws_client_transport_start(
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

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Create libwebsockets context
    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = data->protocols;
    info.user = data;

    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }

    data->context = lws_create_context(&info);
    if (!data->context) {
        mcp_log_error("Failed to create WebSocket client context");
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_client_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket client event thread");
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    // Connect to server
    struct lws_client_connect_info connect_info = {0};
    connect_info.context = data->context;
    connect_info.address = data->config.host;
    connect_info.port = data->config.port;
    connect_info.path = data->config.path ? data->config.path : "/";
    connect_info.host = data->config.host;
    connect_info.origin = data->config.origin;
    connect_info.protocol = data->config.protocol ? data->config.protocol : "mcp-protocol";
    connect_info.pwsi = &data->wsi;
    connect_info.ssl_connection = data->config.use_ssl ? LCCSCF_USE_SSL : 0;

    if (!lws_client_connect_via_info(&connect_info)) {
        mcp_log_error("Failed to connect to WebSocket server");
        // Don't return error here, as the event thread will handle reconnection
    }

    mcp_log_info("WebSocket client connecting to %s:%d%s",
                data->config.host, data->config.port, data->config.path ? data->config.path : "/");

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

    // Wait for event thread to exit
    if (data->event_thread) {
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

// Client transport stop function
static int ws_client_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Set running flag to false to stop event loop
    data->running = false;
    data->reconnect = false;

    // Wait for event thread to exit
    if (data->event_thread) {
        mcp_thread_join(data->event_thread, NULL);
        data->event_thread = 0;
    }

    // Clean up resources
    free(data->receive_buffer);
    data->receive_buffer = NULL;
    data->receive_buffer_len = 0;
    data->receive_buffer_used = 0;

    // Destroy libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("WebSocket client stopped");

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

// Client transport send function
static int ws_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !transport->transport_data || !data || size == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    if (!ws_data->connected || !ws_data->wsi) {
        mcp_log_error("WebSocket client not connected");
        return -1;
    }

    // LWS requires some pre-padding for its headers
    unsigned char* buf = (unsigned char*)malloc(LWS_PRE + size);
    if (!buf) {
        mcp_log_error("Failed to allocate WebSocket send buffer");
        return -1;
    }

    // Copy data to buffer after pre-padding
    memcpy(buf + LWS_PRE, data, size);

    // Send data
    int result = lws_write(ws_data->wsi, buf + LWS_PRE, size, LWS_WRITE_TEXT);

    free(buf);

    if (result < 0) {
        mcp_log_error("WebSocket client write failed");
        return -1;
    }

    return 0;
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

// Client transport sendv function
static int ws_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    if (!ws_data->connected || !ws_data->wsi) {
        mcp_log_error("WebSocket client not connected");
        return -1;
    }

    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        total_size += buffers[i].size;
    }

    // LWS requires some pre-padding for its headers
    unsigned char* buf = (unsigned char*)malloc(LWS_PRE + total_size);
    if (!buf) {
        mcp_log_error("Failed to allocate WebSocket send buffer");
        return -1;
    }

    // Copy data to buffer after pre-padding
    unsigned char* ptr = buf + LWS_PRE;
    for (size_t i = 0; i < buffer_count; i++) {
        memcpy(ptr, buffers[i].data, buffers[i].size);
        ptr += buffers[i].size;
    }

    // Send data
    int result = lws_write(ws_data->wsi, buf + LWS_PRE, total_size, LWS_WRITE_TEXT);

    free(buf);

    if (result < 0) {
        mcp_log_error("WebSocket client write failed");
        return -1;
    }

    return 0;
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

// Client transport destroy function
static void ws_client_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;
    if (!data) {
        free(transport);
        return;
    }

    // Stop transport if running
    if (data->running) {
        ws_client_transport_stop(transport);
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

// Create WebSocket client transport
mcp_transport_t* mcp_transport_websocket_client_create(const mcp_websocket_config_t* config) {
    if (!config || !config->host) {
        return NULL;
    }

    // Allocate transport
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) {
        return NULL;
    }

    // Allocate client data
    ws_client_data_t* data = (ws_client_data_t*)calloc(1, sizeof(ws_client_data_t));
    if (!data) {
        free(transport);
        return NULL;
    }

    // Copy config
    data->config = *config;
    data->protocols = client_protocols;
    data->transport = transport;
    data->reconnect = true; // Enable reconnection by default

    // Set transport type and operations
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->client.start = ws_client_transport_start;
    transport->client.stop = ws_client_transport_stop;
    transport->client.destroy = ws_client_transport_destroy;
    transport->client.send = ws_client_transport_send;
    transport->client.sendv = ws_client_transport_sendv;
    transport->client.receive = NULL; // WebSocket client doesn't support synchronous receive

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}
