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
    mcp_mutex_t* connection_mutex;  // Mutex for connection state
    mcp_cond_t* connection_cond;    // Condition variable for connection state
} ws_client_data_t;

// Forward declarations
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);

// WebSocket protocol definitions
static struct lws_protocols client_protocols[] = {
    {
        "mcp-protocol",            // Protocol name
        ws_client_callback,        // Callback function
        0,                         // Per-connection user data size
        4096,                      // Rx buffer size
        0, NULL, 0                 // Reserved fields
    },
    {
        "http-only",               // HTTP protocol for handshake
        ws_client_callback,        // Callback function
        0,                         // Per-connection user data size
        4096,                      // Rx buffer size
        0, NULL, 0                 // Reserved fields
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } // Terminator
};

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

    // Debug log for all callback reasons except frequent ones
    if (reason != LWS_CALLBACK_CLIENT_WRITEABLE && reason != LWS_CALLBACK_CLIENT_RECEIVE) {
        mcp_log_debug("WebSocket client callback: reason=%d", reason);
    }

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            // Connection established
            mcp_log_info("WebSocket client connection established");
            data->wsi = wsi;

            // Signal that the connection is established
            mcp_mutex_lock(data->connection_mutex);
            data->connected = true;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            // Connection error
            mcp_log_error("WebSocket client connection error: %s",
                         in ? (char*)in : "unknown error");
            data->wsi = NULL;

            // Signal that the connection failed
            mcp_mutex_lock(data->connection_mutex);
            data->connected = false;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

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

            // Signal that the connection is closed
            mcp_mutex_lock(data->connection_mutex);
            data->connected = false;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

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

            // Log the raw message data for debugging (including hex dump)
            if (len < 1000) {  // Only log if not too large
                char debug_buffer[1024] = {0};
                size_t copy_len = len < 1000 ? len : 1000;
                memcpy(debug_buffer, in, copy_len);
                debug_buffer[copy_len] = '\0';  // Ensure null termination

                // Log as text
                mcp_log_debug("WebSocket client raw data (text): '%s'", debug_buffer);

                // Log as hex for the first 32 bytes
                char hex_buffer[200] = {0};
                size_t hex_len = len < 32 ? len : 32;
                for (size_t i = 0; i < hex_len; i++) {
                    sprintf(hex_buffer + i*3, "%02x ", (unsigned char)((char*)in)[i]);
                }
                mcp_log_debug("WebSocket client raw data (hex): %s", hex_buffer);

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
                        memcpy(data->receive_buffer + data->receive_buffer_used,
                               (char*)in + 4, len - 4);
                        data->receive_buffer_used += (len - 4);

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
                            if (data->receive_buffer_used < data->receive_buffer_len) {
                                data->receive_buffer[data->receive_buffer_used] = '\0';
                            } else {
                                // Resize buffer to add null terminator
                                char* new_buffer = (char*)realloc(data->receive_buffer, data->receive_buffer_used + 1);
                                if (!new_buffer) {
                                    mcp_log_error("Failed to resize WebSocket client receive buffer for null terminator");
                                    return -1;
                                }
                                data->receive_buffer = new_buffer;
                                data->receive_buffer_len = data->receive_buffer_used + 1;
                                data->receive_buffer[data->receive_buffer_used] = '\0';
                            }

                            // Process complete message
                            if (data->transport && data->transport->message_callback) {
                                // Initialize thread-local arena for JSON parsing
                                mcp_log_debug("Initializing thread-local arena for client message processing");
                                if (mcp_arena_init_current_thread(4096) != 0) {
                                    mcp_log_error("Failed to initialize thread-local arena in WebSocket client callback");
                                }

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

                        return 0;  // Skip the rest of the processing
                    }
                }
            }

            // Normal copy if not a length-prefixed message
            memcpy(data->receive_buffer + data->receive_buffer_used, in, len);
            data->receive_buffer_used += len;

            // Check if this is a complete message
            if (lws_is_final_fragment(wsi)) {
                // Ensure the buffer is null-terminated for string operations
                if (data->receive_buffer_used < data->receive_buffer_len) {
                    data->receive_buffer[data->receive_buffer_used] = '\0';
                } else {
                    // Resize buffer to add null terminator
                    char* new_buffer = (char*)realloc(data->receive_buffer, data->receive_buffer_used + 1);
                    if (!new_buffer) {
                        mcp_log_error("Failed to resize WebSocket client receive buffer for null terminator");
                        return -1;
                    }
                    data->receive_buffer = new_buffer;
                    data->receive_buffer_len = data->receive_buffer_used + 1;
                    data->receive_buffer[data->receive_buffer_used] = '\0';
                }

                // Process complete message
                if (data->transport && data->transport->message_callback) {
                    // Initialize thread-local arena for JSON parsing
                    mcp_log_debug("Initializing thread-local arena for client message processing");
                    if (mcp_arena_init_current_thread(4096) != 0) {
                        mcp_log_error("Failed to initialize thread-local arena in WebSocket client callback");
                    }

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

// Client event loop thread function
static void* ws_client_event_thread(void* arg) {
    ws_client_data_t* data = (ws_client_data_t*)arg;

    while (data->running) {
        lws_service(data->context, 50); // 50ms timeout
    }

    return NULL;
}

// Function to wait for WebSocket connection to be established
static int ws_client_wait_for_connection(ws_client_data_t* data, uint32_t timeout_ms) {
    if (!data || !data->connection_mutex || !data->connection_cond) {
        return -1;
    }

    int result = 0;
    mcp_mutex_lock(data->connection_mutex);

    // If already connected, return immediately
    if (data->connected) {
        mcp_mutex_unlock(data->connection_mutex);
        return 0;
    }

    // Wait for connection with timeout
    if (timeout_ms > 0) {
        result = mcp_cond_timedwait(data->connection_cond, data->connection_mutex, timeout_ms);
    } else {
        // Wait indefinitely
        while (!data->connected && data->running) {
            mcp_cond_wait(data->connection_cond, data->connection_mutex);
        }
    }

    // Check if connected
    if (!data->connected) {
        result = -1;
    }

    mcp_mutex_unlock(data->connection_mutex);
    return result;
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
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8 |
                   LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }

    data->context = lws_create_context(&info);
    if (!data->context) {
        mcp_log_error("Failed to create WebSocket client context");
        return -1;
    }

    // Initialize connection mutex and condition variable
    data->connection_mutex = mcp_mutex_create();
    if (!data->connection_mutex) {
        mcp_log_error("Failed to create WebSocket client connection mutex");
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    data->connection_cond = mcp_cond_create();
    if (!data->connection_cond) {
        mcp_log_error("Failed to create WebSocket client connection condition variable");
        mcp_mutex_destroy(data->connection_mutex);
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_client_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket client event thread");
        mcp_mutex_destroy(data->connection_mutex);
        mcp_cond_destroy(data->connection_cond);
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

    // Make sure path starts with a slash
    char path_buffer[256] = {0};
    if (data->config.path) {
        if (data->config.path[0] != '/') {
            snprintf(path_buffer, sizeof(path_buffer), "/%s", data->config.path);
            connect_info.path = path_buffer;
        } else {
            connect_info.path = data->config.path;
        }
    } else {
        connect_info.path = "/";
    }

    connect_info.host = data->config.host;
    connect_info.origin = data->config.origin ? data->config.origin : data->config.host;
    connect_info.protocol = data->config.protocol ? data->config.protocol : "mcp-protocol";
    connect_info.pwsi = &data->wsi;
    connect_info.ssl_connection = data->config.use_ssl ? LCCSCF_USE_SSL : 0;
    connect_info.local_protocol_name = "mcp-protocol";

    if (!lws_client_connect_via_info(&connect_info)) {
        mcp_log_error("Failed to connect to WebSocket server");
        // Don't return error here, as the event thread will handle reconnection
    }

    mcp_log_info("WebSocket client connecting to %s:%d%s",
                data->config.host, data->config.port, data->config.path ? data->config.path : "/");

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

    // Force libwebsockets to break out of its service loop
    if (data->context) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets client service");
    }

    // Wait for event thread to exit
    if (data->event_thread) {
        mcp_log_info("Waiting for WebSocket client event thread to exit...");
        mcp_thread_join(data->event_thread, NULL);
        data->event_thread = 0;
    }

    // Clean up resources
    free(data->receive_buffer);
    data->receive_buffer = NULL;
    data->receive_buffer_len = 0;
    data->receive_buffer_used = 0;

    // Destroy mutex and condition variable
    if (data->connection_mutex) {
        mcp_mutex_destroy(data->connection_mutex);
        data->connection_mutex = NULL;
    }

    if (data->connection_cond) {
        mcp_cond_destroy(data->connection_cond);
        data->connection_cond = NULL;
    }

    // Destroy libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("WebSocket client stopped");

    return 0;
}

// Client transport send function
static int ws_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !transport->transport_data || !data || size == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Wait for connection to be established (with 5 second timeout)
    if (ws_client_wait_for_connection(ws_data, 5000) != 0) {
        mcp_log_error("WebSocket client not connected or connection timeout");
        return -1;
    }

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

// Client transport sendv function
static int ws_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Wait for connection to be established (with 5 second timeout)
    if (ws_client_wait_for_connection(ws_data, 5000) != 0) {
        mcp_log_error("WebSocket client not connected or connection timeout");
        return -1;
    }

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
    data->connection_mutex = NULL;
    data->connection_cond = NULL;

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
