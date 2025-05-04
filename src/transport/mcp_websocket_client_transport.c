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

// Connection state
typedef enum {
    WS_CLIENT_STATE_DISCONNECTED = 0,
    WS_CLIENT_STATE_CONNECTING,
    WS_CLIENT_STATE_CONNECTED,
    WS_CLIENT_STATE_CLOSING,
    WS_CLIENT_STATE_ERROR
} ws_client_state_t;

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
    ws_client_state_t state;        // Connection state
    bool reconnect;                 // Whether to reconnect on disconnect
    mcp_mutex_t* connection_mutex;  // Mutex for connection state
    mcp_cond_t* connection_cond;    // Condition variable for connection state

    // Message queue for outgoing messages
    ws_message_item_t* message_queue;     // Queue head
    ws_message_item_t* message_queue_tail; // Queue tail
    mcp_mutex_t* queue_mutex;             // Mutex for message queue

    // Reconnection parameters
    int reconnect_attempts;         // Number of reconnection attempts
    time_t last_reconnect_time;     // Time of last reconnection attempt
    uint32_t reconnect_delay_ms;    // Current reconnection delay in milliseconds

    // Ping parameters
    time_t last_ping_time;          // Time of last ping sent
} ws_client_data_t;

// Forward declarations
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);
static int ws_client_connect(ws_client_data_t* data);
static void ws_client_handle_reconnect(ws_client_data_t* data);
static void ws_client_enqueue_message(ws_client_data_t* data, const void* message,
                                     size_t size, ws_message_type_t type);
static ws_message_item_t* ws_client_dequeue_message(ws_client_data_t* data);
static void ws_client_free_message_queue(ws_client_data_t* data);

// WebSocket protocol definitions
static struct lws_protocols client_protocols[3];

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
            data->state = WS_CLIENT_STATE_CONNECTED;
            data->reconnect_attempts = 0; // Reset reconnection attempts on successful connection
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

            // Request a writable callback to send any queued messages
            if (data->message_queue) {
                lws_callback_on_writable(wsi);
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            // Connection error
            mcp_log_error("WebSocket client connection error: %s",
                         in ? (char*)in : "unknown error");
            data->wsi = NULL;

            // Signal that the connection failed
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_ERROR;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED: {
            // Connection closed
            mcp_log_info("WebSocket client connection closed");
            data->wsi = NULL;

            // Signal that the connection is closed
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_DISCONNECTED;
            mcp_cond_signal(data->connection_cond);
            mcp_mutex_unlock(data->connection_mutex);

            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
            // Received pong from server
            mcp_log_debug("Received pong from server");
            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Receive data
            mcp_log_debug("WebSocket client data received: %zu bytes", len);

            // Ensure buffer is large enough
            if (data->receive_buffer_used + len > data->receive_buffer_len) {
                size_t new_len = data->receive_buffer_len == 0 ? WS_DEFAULT_BUFFER_SIZE : data->receive_buffer_len * 2;
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

                // WebSocket messages don't have length prefixes - they're already properly framed by the WebSocket protocol
                // For WebSocket, we should just copy the data directly to our buffer

                // Check if the first character is '{' which indicates a JSON message (most common case)
                if (len > 0 && ((char*)in)[0] == '{') {
                    mcp_log_debug("Detected JSON message");
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

            // Disable automatic ping for now
            /*
            time_t now = time(NULL);
            if (difftime(now, data->last_ping_time) * 1000 >= WS_CLIENT_PING_INTERVAL_MS) {
                // Send a ping frame
                unsigned char buf[LWS_PRE + 0];
                int result = lws_write(wsi, &buf[LWS_PRE], 0, LWS_WRITE_PING);

                if (result >= 0) {
                    // Update last ping time
                    data->last_ping_time = now;
                    mcp_log_debug("Sent ping to server");
                } else {
                    mcp_log_error("Failed to send ping to server");
                }
            }
            */

            // Then check if we have any messages to send
            ws_message_item_t* item = ws_client_dequeue_message(data);
            if (item) {
                // Send the message
                enum lws_write_protocol write_protocol =
                    (item->type == WS_MESSAGE_TYPE_BINARY) ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;

                int result = lws_write(wsi, item->data + LWS_PRE, item->size, write_protocol);

                if (result < 0) {
                    mcp_log_error("WebSocket client write failed");

                    // Put the message back in the queue
                    mcp_mutex_lock(data->queue_mutex);
                    item->next = data->message_queue;
                    data->message_queue = item;
                    if (!data->message_queue_tail) {
                        data->message_queue_tail = item;
                    }
                    mcp_mutex_unlock(data->queue_mutex);
                } else {
                    // Free the message
                    free(item->data);
                    free(item);

                    // Request another writable callback if there are more messages
                    if (data->message_queue) {
                        lws_callback_on_writable(wsi);
                    }
                }
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

// Helper function to enqueue a message
static void ws_client_enqueue_message(ws_client_data_t* data, const void* message,
                                     size_t size, ws_message_type_t type) {
    if (!data || !message || size == 0 || !data->queue_mutex) {
        return;
    }

    // Use common function to enqueue message
    if (mcp_websocket_enqueue_message(
            &data->message_queue,
            &data->message_queue_tail,
            data->queue_mutex,
            message,
            size,
            type) != 0) {
        return;
    }

    // Request a writable callback to send the message
    if (data->wsi) {
        lws_callback_on_writable(data->wsi);
    }
}

// Helper function to dequeue a message
static ws_message_item_t* ws_client_dequeue_message(ws_client_data_t* data) {
    if (!data || !data->queue_mutex) {
        return NULL;
    }

    // Use common function to dequeue message
    return mcp_websocket_dequeue_message(
        &data->message_queue,
        &data->message_queue_tail,
        data->queue_mutex);
}

// Helper function to free the message queue
static void ws_client_free_message_queue(ws_client_data_t* data) {
    if (!data || !data->queue_mutex) {
        return;
    }

    // Use common function to free message queue
    mcp_websocket_free_message_queue(
        &data->message_queue,
        &data->message_queue_tail,
        data->queue_mutex);
}

// Helper function to connect to the WebSocket server
static int ws_client_connect(ws_client_data_t* data) {
    if (!data || !data->context) {
        return -1;
    }

    // Update connection state
    mcp_mutex_lock(data->connection_mutex);
    data->state = WS_CLIENT_STATE_CONNECTING;
    mcp_mutex_unlock(data->connection_mutex);

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

        // Update connection state
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_ERROR;
        mcp_mutex_unlock(data->connection_mutex);

        return -1;
    }

    mcp_log_info("WebSocket client connecting to %s:%d%s",
                data->config.host, data->config.port, data->config.path ? data->config.path : "/");

    return 0;
}

// Helper function to handle reconnection
static void ws_client_handle_reconnect(ws_client_data_t* data) {
    if (!data || !data->reconnect || !data->running) {
        return;
    }

    mcp_mutex_lock(data->connection_mutex);

    // Check if we've exceeded the maximum number of reconnection attempts
    if (data->reconnect_attempts >= 10) { // Use constant value instead of macro
        mcp_log_error("WebSocket client exceeded maximum reconnection attempts (%d)", 10);
        data->state = WS_CLIENT_STATE_ERROR;
        mcp_mutex_unlock(data->connection_mutex);
        return;
    }

    // Calculate reconnection delay with exponential backoff
    time_t now = time(NULL);
    if (data->reconnect_attempts == 0 || difftime(now, data->last_reconnect_time) >= 60) {
        // Reset reconnection delay if this is the first attempt or if it's been more than a minute
        data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
        data->reconnect_attempts = 1;
    } else {
        // Exponential backoff with a maximum delay
        data->reconnect_delay_ms *= 2;
        if (data->reconnect_delay_ms > 60000) { // Use constant value instead of macro (60 seconds)
            data->reconnect_delay_ms = 60000;
        }
        data->reconnect_attempts++;
    }

    data->last_reconnect_time = now;

    mcp_log_info("WebSocket client reconnecting in %u ms (attempt %d of %d)",
                data->reconnect_delay_ms, data->reconnect_attempts, 10); // Use constant value instead of macro

    mcp_mutex_unlock(data->connection_mutex);

    // Sleep for the reconnection delay
    mcp_sleep_ms(data->reconnect_delay_ms);

    // Attempt to reconnect
    if (data->running) {
        ws_client_connect(data);
    }
}

// Helper function to request sending a ping
static void ws_client_send_ping(ws_client_data_t* data) {
    if (!data || !data->wsi || data->state != WS_CLIENT_STATE_CONNECTED) {
        return;
    }

    // Request a writable callback to send a ping
    lws_callback_on_writable(data->wsi);

    mcp_log_debug("Requested ping to server");
}

// Client event loop thread function
static void* ws_client_event_thread(void* arg) {
    ws_client_data_t* data = (ws_client_data_t*)arg;

    while (data->running) {
        // Service the WebSocket context
        lws_service(data->context, 50); // 50ms timeout

        // Check if we need to reconnect
        mcp_mutex_lock(data->connection_mutex);
        bool need_reconnect = (data->state == WS_CLIENT_STATE_DISCONNECTED ||
                              data->state == WS_CLIENT_STATE_ERROR) &&
                              data->reconnect && data->running;

        // Disable ping check for now
        /*
        bool need_ping = data->state == WS_CLIENT_STATE_CONNECTED &&
                         difftime(time(NULL), data->last_ping_time) * 1000 >= WS_CLIENT_PING_INTERVAL_MS;
        */
        mcp_mutex_unlock(data->connection_mutex);

        if (need_reconnect) {
            ws_client_handle_reconnect(data);
        }

        // Disable ping for now
        /*
        if (need_ping) {
            ws_client_send_ping(data);
        }
        */
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
    if (data->state == WS_CLIENT_STATE_CONNECTED) {
        mcp_mutex_unlock(data->connection_mutex);
        return 0;
    }

    // Wait for connection with timeout
    if (timeout_ms > 0) {
        // Wait with timeout
        uint32_t remaining_timeout = timeout_ms;
        uint32_t wait_chunk = 100; // Wait in smaller chunks to check for state changes

        while (data->state != WS_CLIENT_STATE_CONNECTED &&
               data->state != WS_CLIENT_STATE_ERROR &&
               data->running &&
               remaining_timeout > 0) {

            uint32_t wait_time = (remaining_timeout < wait_chunk) ? remaining_timeout : wait_chunk;
            result = mcp_cond_timedwait(data->connection_cond, data->connection_mutex, wait_time);

            if (result != 0) {
                // Timeout or error
                break;
            }

            remaining_timeout -= wait_time;
        }
    } else {
        // Wait indefinitely
        while (data->state != WS_CLIENT_STATE_CONNECTED &&
               data->state != WS_CLIENT_STATE_ERROR &&
               data->running) {
            mcp_cond_wait(data->connection_cond, data->connection_mutex);
        }
    }

    // Check if connected
    if (data->state != WS_CLIENT_STATE_CONNECTED) {
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
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // Store callback information
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Initialize protocols
    mcp_websocket_init_protocols(client_protocols, ws_client_callback);

    // Create libwebsockets context using common function
    data->context = mcp_websocket_create_context(
        data->config.host,
        data->config.port,
        data->config.path,
        client_protocols,
        data,
        false, // is_server
        data->config.use_ssl,
        data->config.cert_path,
        data->config.key_path
    );

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

    // Initialize message queue mutex
    data->queue_mutex = mcp_mutex_create();
    if (!data->queue_mutex) {
        mcp_log_error("Failed to create WebSocket client queue mutex");
        mcp_mutex_destroy(data->connection_mutex);
        mcp_cond_destroy(data->connection_cond);
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    // Initialize reconnection parameters
    data->reconnect_attempts = 0;
    data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
    data->last_reconnect_time = time(NULL);

    // Initialize ping parameters
    data->last_ping_time = time(NULL);

    // Set initial state
    data->state = WS_CLIENT_STATE_DISCONNECTED;
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_client_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket client event thread");
        mcp_mutex_destroy(data->queue_mutex);
        mcp_mutex_destroy(data->connection_mutex);
        mcp_cond_destroy(data->connection_cond);
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    // Initiate connection
    if (ws_client_connect(data) != 0) {
        mcp_log_error("Failed to initiate WebSocket client connection");
        // Don't return error here, as the event thread will handle reconnection
    }

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

    // Update connection state
    mcp_mutex_lock(data->connection_mutex);
    data->state = WS_CLIENT_STATE_CLOSING;
    mcp_cond_signal(data->connection_cond);
    mcp_mutex_unlock(data->connection_mutex);

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

    // Clean up message queue
    ws_client_free_message_queue(data);

    // Clean up resources
    free(data->receive_buffer);
    data->receive_buffer = NULL;
    data->receive_buffer_len = 0;
    data->receive_buffer_used = 0;

    // Destroy mutexes and condition variable
    if (data->queue_mutex) {
        mcp_mutex_destroy(data->queue_mutex);
        data->queue_mutex = NULL;
    }

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

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Enqueue the message
    ws_client_enqueue_message(ws_data, data, size, WS_MESSAGE_TYPE_TEXT);

    // If we're connected, request a writable callback
    mcp_mutex_lock(ws_data->connection_mutex);
    bool is_connected = (ws_data->state == WS_CLIENT_STATE_CONNECTED);
    mcp_mutex_unlock(ws_data->connection_mutex);

    if (is_connected && ws_data->wsi) {
        lws_callback_on_writable(ws_data->wsi);
    } else {
        // If not connected, try to connect or wait for reconnection
        if (ws_client_wait_for_connection(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
            mcp_log_warn("WebSocket client not connected, message queued for later delivery");
            // Don't return error, as the message is queued and will be sent when connected
        }
    }

    return 0;
}

// Client transport sendv function
static int ws_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Calculate total size using common function
    size_t total_size = mcp_websocket_calculate_total_size(buffers, buffer_count);

    // Allocate a temporary buffer to combine all the buffers
    unsigned char* combined_buffer = (unsigned char*)malloc(total_size);
    if (!combined_buffer) {
        mcp_log_error("Failed to allocate WebSocket combined buffer");
        return -1;
    }

    // Combine buffers using common function
    if (mcp_websocket_combine_buffers(buffers, buffer_count, combined_buffer, total_size) != 0) {
        free(combined_buffer);
        mcp_log_error("Failed to combine WebSocket buffers");
        return -1;
    }

    // Enqueue the message
    ws_client_enqueue_message(ws_data, combined_buffer, total_size, WS_MESSAGE_TYPE_TEXT);

    // Free the temporary buffer
    free(combined_buffer);

    // If we're connected, request a writable callback
    mcp_mutex_lock(ws_data->connection_mutex);
    bool is_connected = (ws_data->state == WS_CLIENT_STATE_CONNECTED);
    mcp_mutex_unlock(ws_data->connection_mutex);

    if (is_connected && ws_data->wsi) {
        lws_callback_on_writable(ws_data->wsi);
    } else {
        // If not connected, try to connect or wait for reconnection
        if (ws_client_wait_for_connection(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
            mcp_log_warn("WebSocket client not connected, message queued for later delivery");
            // Don't return error, as the message is queued and will be sent when connected
        }
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
    } else {
        // If not running, make sure we clean up any resources that might not have been cleaned up

        // Clean up message queue
        if (data->queue_mutex) {
            ws_client_free_message_queue(data);
            mcp_mutex_destroy(data->queue_mutex);
        }

        // Clean up receive buffer
        if (data->receive_buffer) {
            free(data->receive_buffer);
        }

        // Clean up mutexes and condition variable
        if (data->connection_mutex) {
            mcp_mutex_destroy(data->connection_mutex);
        }

        if (data->connection_cond) {
            mcp_cond_destroy(data->connection_cond);
        }

        // Clean up libwebsockets context
        if (data->context) {
            lws_context_destroy(data->context);
        }
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

    // Initialize protocols
    mcp_websocket_init_protocols(client_protocols, ws_client_callback);

    // Initialize connection state
    data->state = WS_CLIENT_STATE_DISCONNECTED;
    data->connection_mutex = NULL;
    data->connection_cond = NULL;
    data->queue_mutex = NULL;

    // Initialize message queue
    data->message_queue = NULL;
    data->message_queue_tail = NULL;

    // Initialize reconnection parameters
    data->reconnect_attempts = 0;
    data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
    data->last_reconnect_time = 0;

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
