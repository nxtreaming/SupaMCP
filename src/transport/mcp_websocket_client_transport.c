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
#include "mcp_client.h"

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

    // Reconnection parameters
    int reconnect_attempts;         // Number of reconnection attempts
    time_t last_reconnect_time;     // Time of last reconnection attempt
    uint32_t reconnect_delay_ms;    // Current reconnection delay in milliseconds

    // Ping parameters
    time_t last_ping_time;          // Time of last ping sent
    time_t last_pong_time;          // Time of last pong received
    time_t last_activity_time;      // Time of last activity (send or receive)
    uint32_t ping_interval_ms;      // Ping interval in milliseconds
    uint32_t ping_timeout_ms;       // Ping timeout in milliseconds
    bool ping_in_progress;          // Whether a ping is currently in progress
    uint32_t missed_pongs;          // Number of consecutive missed pongs

    // Synchronous request-response handling
    bool sync_response_mode;        // Whether to use synchronous response mode
    mcp_mutex_t* response_mutex;    // Mutex for response handling
    mcp_cond_t* response_cond;      // Condition variable for response handling
    char* response_data;            // Response data buffer
    size_t response_data_len;       // Response data length
    bool response_ready;            // Whether a response is ready
    int response_error_code;        // Response error code
} ws_client_data_t;

// Forward declarations
static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);
static int ws_client_connect(ws_client_data_t* data);
static void ws_client_handle_reconnect(ws_client_data_t* data);
static void ws_client_update_activity(ws_client_data_t* data);
static int ws_client_send_ping(ws_client_data_t* data);
static int ws_client_send_and_wait_response(ws_client_data_t* ws_data, const void* data,
                                           size_t size, char** response_out,
                                           size_t* response_size_out, uint32_t timeout_ms);
static int ws_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);
static bool ws_client_is_connected(ws_client_data_t* data);
static int ws_client_ensure_connected(ws_client_data_t* data, uint32_t timeout_ms);
static int ws_client_handle_received_data(ws_client_data_t* data, void* in, size_t len, bool is_final);
static void ws_client_cleanup_resources(ws_client_data_t* data);
static int ws_client_resize_receive_buffer(ws_client_data_t* data, size_t needed_size);
static int ws_client_process_complete_message(ws_client_data_t* data);
static int ws_client_send_buffer(ws_client_data_t* data, const void* buffer, size_t size);
static int ws_client_wait_for_connection(ws_client_data_t* data, uint32_t timeout_ms);
static void* ws_client_event_thread(void* arg);

// WebSocket protocol definitions
static struct lws_protocols client_protocols[3];

// Helper function to check if client is connected
static bool ws_client_is_connected(ws_client_data_t* data) {
    if (!data) {
        return false;
    }

    bool is_connected = false;
    mcp_mutex_lock(data->connection_mutex);

    // Check connection state
    is_connected = (data->state == WS_CLIENT_STATE_CONNECTED);

    // Silently detect inconsistency but don't change behavior or log
    // This avoids excessive logging while maintaining the original behavior

    mcp_mutex_unlock(data->connection_mutex);
    return is_connected;
}

// Helper function to ensure client is connected, with optional timeout
static int ws_client_ensure_connected(ws_client_data_t* data, uint32_t timeout_ms) {
    if (!data || !data->running) {
        return -1;
    }

    // Check if already connected
    if (ws_client_is_connected(data)) {
        return 0;
    }

    // Wait for connection with timeout
    mcp_log_debug("WebSocket client not connected, waiting for connection...");
    if (ws_client_wait_for_connection(data, timeout_ms) != 0) {
        mcp_log_error("WebSocket client not connected, cannot send message");
        return -1;
    }

    // Check if connected after wait
    if (!ws_client_is_connected(data)) {
        mcp_log_error("WebSocket client still not connected after wait");
        return -1;
    }

    mcp_log_debug("WebSocket client connected, proceeding with operation");
    return 0;
}

// Helper function to resize receive buffer
static int ws_client_resize_receive_buffer(ws_client_data_t* data, size_t needed_size) {
    if (!data) {
        return -1;
    }

    // Calculate new buffer size
    size_t new_len = data->receive_buffer_len == 0 ? WS_DEFAULT_BUFFER_SIZE : data->receive_buffer_len * 2;
    while (new_len < needed_size) {
        new_len *= 2;
    }

    // Allocate new buffer
    char* new_buffer = (char*)realloc(data->receive_buffer, new_len);
    if (!new_buffer) {
        mcp_log_error("Failed to allocate WebSocket client receive buffer");
        return -1;
    }

    // Update buffer information
    data->receive_buffer = new_buffer;
    data->receive_buffer_len = new_len;
    return 0;
}

// Helper function to process a complete message
static int ws_client_process_complete_message(ws_client_data_t* data) {
    if (!data) {
        return -1;
    }

    // Ensure the buffer is null-terminated
    if (data->receive_buffer_used >= data->receive_buffer_len) {
        if (ws_client_resize_receive_buffer(data, data->receive_buffer_used + 1) != 0) {
            return -1;
        }
    }
    data->receive_buffer[data->receive_buffer_used] = '\0';

    // Process the message
    mcp_mutex_lock(data->response_mutex);

    // Clean up any existing response data
    if (data->response_data) {
        free(data->response_data);
        data->response_data = NULL;
        data->response_data_len = 0;
    }

    // Copy the response data
    data->response_data = (char*)malloc(data->receive_buffer_used + 1);
    if (!data->response_data) {
        mcp_log_error("Failed to allocate memory for WebSocket response data");
        data->response_error_code = -1;

        // Signal condition variable if in sync mode
        if (data->sync_response_mode) {
            mcp_cond_signal(data->response_cond);
        }

        mcp_mutex_unlock(data->response_mutex);
        return -1;
    }

    // Copy and null-terminate the data
    memcpy(data->response_data, data->receive_buffer, data->receive_buffer_used);
    data->response_data[data->receive_buffer_used] = '\0';
    data->response_data_len = data->receive_buffer_used;
    data->response_ready = true;
    data->response_error_code = 0;

    #ifdef MCP_VERBOSE_DEBUG
    mcp_log_debug("WebSocket client received response: %s", data->response_data);
    #endif

    // Handle synchronous or asynchronous mode
    if (data->sync_response_mode) {
        mcp_log_debug("WebSocket client in sync mode, signaling condition variable");
        mcp_cond_signal(data->response_cond);
    } else if (data->transport && data->transport->message_callback) {
        // Initialize thread-local arena for JSON parsing
        mcp_log_debug("Initializing thread-local arena for client message processing");
        if (mcp_arena_init_current_thread(4096) != 0) {
            mcp_log_error("Failed to initialize thread-local arena in WebSocket client callback");
        }

        // Process message through callback
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

    mcp_mutex_unlock(data->response_mutex);

    // Reset buffer for next message
    data->receive_buffer_used = 0;
    return 0;
}

// Helper function to handle received data
static int ws_client_handle_received_data(ws_client_data_t* data, void* in, size_t len, bool is_final) {
    if (!data || !in || len == 0) {
        return -1;
    }

    // Update activity time
    ws_client_update_activity(data);

    // Ensure buffer is large enough
    if (data->receive_buffer_used + len > data->receive_buffer_len) {
        if (ws_client_resize_receive_buffer(data, data->receive_buffer_used + len) != 0) {
            return -1;
        }
    }

    // Only log raw message data in verbose debug mode
    #ifdef MCP_VERBOSE_DEBUG
    if (len < 1000) {
        char debug_buffer[1024] = {0};
        size_t copy_len = len < 1000 ? len : 1000;
        memcpy(debug_buffer, in, copy_len);
        debug_buffer[copy_len] = '\0';
        mcp_log_debug("WebSocket client raw data (text): '%s'", debug_buffer);
    }
    #endif

    // Copy data to receive buffer
    memcpy(data->receive_buffer + data->receive_buffer_used, in, len);
    data->receive_buffer_used += len;

    // Process complete message if this is the final fragment
    if (is_final) {
        return ws_client_process_complete_message(data);
    }

    return 0;
}

// Helper function to send a buffer via WebSocket
static int ws_client_send_buffer(ws_client_data_t* data, const void* buffer, size_t size) {
    if (!data || !buffer || size == 0 || !data->wsi) {
        return -1;
    }

    // Prepare the message with LWS_PRE padding
    unsigned char* buf = (unsigned char*)malloc(LWS_PRE + size);
    if (!buf) {
        mcp_log_error("Failed to allocate buffer for WebSocket message");
        return -1;
    }

    // Copy the message data
    memcpy(buf + LWS_PRE, buffer, size);

    // Send the message directly
    int result = lws_write(data->wsi, buf + LWS_PRE, size, LWS_WRITE_TEXT);

    // Free the buffer
    free(buf);

    if (result < 0) {
        mcp_log_error("Failed to send WebSocket message directly");
        return -1;
    }

    #ifdef MCP_VERBOSE_DEBUG
    mcp_log_debug("WebSocket message sent directly, size: %zu, result: %d", size, result);
    #endif
    return 0;
}

// Helper function to clean up resources
static void ws_client_cleanup_resources(ws_client_data_t* data) {
    if (!data) {
        return;
    }

    // First, make sure we're not in a connected state
    // This helps prevent callbacks from being triggered during cleanup
    if (data->state == WS_CLIENT_STATE_CONNECTED) {
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_CLOSING;
        mcp_mutex_unlock(data->connection_mutex);
    }

    // Destroy libwebsockets context first to prevent any further callbacks
    if (data->context) {
        // Cancel any pending service calls
        lws_cancel_service(data->context);

        // Destroy the context
        lws_context_destroy(data->context);
        data->context = NULL;
        data->wsi = NULL; // wsi is owned by context, so it's invalid now
    }

    // Free receive buffer
    if (data->receive_buffer) {
        free(data->receive_buffer);
        data->receive_buffer = NULL;
        data->receive_buffer_len = 0;
        data->receive_buffer_used = 0;
    }

    // Free response data
    if (data->response_data) {
        free(data->response_data);
        data->response_data = NULL;
        data->response_data_len = 0;
    }

    // Destroy mutexes and condition variables
    // First signal any waiting threads to prevent deadlocks
    if (data->connection_mutex && data->connection_cond) {
        mcp_mutex_lock(data->connection_mutex);
        mcp_cond_signal(data->connection_cond);
        mcp_mutex_unlock(data->connection_mutex);
    }

    if (data->response_mutex && data->response_cond) {
        mcp_mutex_lock(data->response_mutex);
        mcp_cond_signal(data->response_cond);
        mcp_mutex_unlock(data->response_mutex);
    }

    // Now destroy the synchronization primitives
    if (data->connection_cond) {
        mcp_cond_destroy(data->connection_cond);
        data->connection_cond = NULL;
    }

    if (data->connection_mutex) {
        mcp_mutex_destroy(data->connection_mutex);
        data->connection_mutex = NULL;
    }

    if (data->response_cond) {
        mcp_cond_destroy(data->response_cond);
        data->response_cond = NULL;
    }

    if (data->response_mutex) {
        mcp_mutex_destroy(data->response_mutex);
        data->response_mutex = NULL;
    }
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

    // Debug log for important callback reasons only
    if (reason == LWS_CALLBACK_CLIENT_ESTABLISHED ||
        reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR ||
        reason == LWS_CALLBACK_CLIENT_CLOSED) {
        mcp_log_debug("WebSocket client callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            // Connection established
            mcp_log_info("WebSocket client connection established");
            data->wsi = wsi;

            // Signal that the connection is established
            mcp_mutex_lock(data->connection_mutex);
            data->state = WS_CLIENT_STATE_CONNECTED;

            // Reset reconnection attempts on successful connection
            data->reconnect_attempts = 0;

            // Reset ping-related state
            data->ping_in_progress = false;
            data->missed_pongs = 0;
            data->last_ping_time = time(NULL);
            data->last_pong_time = time(NULL);
            data->last_activity_time = time(NULL);

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

            // Update pong time and activity time
            data->last_pong_time = time(NULL);
            ws_client_update_activity(data);

            // Reset ping state
            data->ping_in_progress = false;
            data->missed_pongs = 0;
            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // Receive data

            // Handle the received data
            ws_client_handle_received_data(data, in, len, lws_is_final_fragment(wsi));
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            // Ready to send data to server

            // Update activity time
            ws_client_update_activity(data);

            // Check if we need to send a ping
            time_t now = time(NULL);
            bool send_ping = false;

            // If ping is in progress and we've exceeded the timeout, consider it missed
            if (data->ping_in_progress) {
                if (difftime(now, data->last_ping_time) * 1000 >= data->ping_timeout_ms) {
                    mcp_log_warn("WebSocket ping timeout detected");
                    data->ping_in_progress = false;
                    data->missed_pongs++;

                    // If we've missed too many pongs, log a warning but don't trigger reconnect
                    if (data->missed_pongs >= 3) {
                        mcp_log_warn("WebSocket connection may be unstable after %d missed pongs",
                                     data->missed_pongs);
                        // Reset missed pongs counter to avoid repeated warnings
                        data->missed_pongs = 0;
                    }
                }
            }
            // If no ping in progress, check if we need to send one based on inactivity
            else if (data->state == WS_CLIENT_STATE_CONNECTED && !data->sync_response_mode) {
                // Only send ping if we're not in synchronous response mode
                if (difftime(now, data->last_activity_time) * 1000 >= data->ping_interval_ms) {
                    send_ping = true;
                }
            }

            // Send ping if needed
            if (send_ping) {
                // Send a ping frame
                unsigned char buf[LWS_PRE + 0];
                int result = lws_write(wsi, &buf[LWS_PRE], 0, LWS_WRITE_PING);

                if (result >= 0) {
                    // Update ping state
                    data->ping_in_progress = true;
                    data->last_ping_time = now;
                    mcp_log_debug("Sent ping to server");
                } else {
                    mcp_log_error("Failed to send ping to server");
                }

                // Always request another writable callback to check for messages
                lws_callback_on_writable(wsi);
            }
            break;
        }

        default:
            break;
    }

    return 0;
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

    // Add flags to optimize connection speed and force immediate upgrade
    connect_info.ssl_connection = data->config.use_ssl ?
        (LCCSCF_USE_SSL | LCCSCF_PIPELINE) : LCCSCF_PIPELINE;

    connect_info.local_protocol_name = "mcp-protocol";
    // Don't use retry policy
    connect_info.retry_and_idle_policy = NULL;
    connect_info.userdata = data;

    // Force immediate connection
    // Use latest version
    connect_info.ietf_version_or_minus_one = -1;

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

// Helper function to update activity time
static void ws_client_update_activity(ws_client_data_t* data) {
    if (!data) {
        return;
    }

    data->last_activity_time = time(NULL);
}

// Helper function to handle reconnection
static void ws_client_handle_reconnect(ws_client_data_t* data) {
    if (!data || !data->reconnect || !data->running) {
        return;
    }

    mcp_mutex_lock(data->connection_mutex);

    // Check if we've exceeded the maximum number of reconnection attempts
    if (data->reconnect_attempts >= WS_MAX_RECONNECT_ATTEMPTS) {
        mcp_log_error("WebSocket client exceeded maximum reconnection attempts (%d)", WS_MAX_RECONNECT_ATTEMPTS);
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
        if (data->reconnect_delay_ms > WS_MAX_RECONNECT_DELAY_MS) {
            data->reconnect_delay_ms = WS_MAX_RECONNECT_DELAY_MS;
        }
        data->reconnect_attempts++;
    }

    data->last_reconnect_time = now;

    mcp_log_info("WebSocket client reconnecting in %u ms (attempt %d of %d)",
                data->reconnect_delay_ms, data->reconnect_attempts, WS_MAX_RECONNECT_ATTEMPTS);

    mcp_mutex_unlock(data->connection_mutex);

    // Sleep for the reconnection delay
    mcp_sleep_ms(data->reconnect_delay_ms);

    // Attempt to reconnect
    if (data->running) {
        ws_client_connect(data);
    }
}

// Helper function to send a ping directly using libwebsockets API
static int ws_client_send_ping(ws_client_data_t* data) {
    if (!data || !data->wsi || data->state != WS_CLIENT_STATE_CONNECTED) {
        return -1;
    }

    // Don't send ping if we're in synchronous response mode
    if (data->sync_response_mode) {
        mcp_log_debug("Skipping ping while in synchronous response mode");
        return 0;
    }

    // Use libwebsockets' built-in ping mechanism
    if (lws_callback_on_writable(data->wsi) < 0) {
        mcp_log_error("Failed to request writable callback for ping");
        return -1;
    }

    // Mark ping as in progress
    data->ping_in_progress = true;
    data->last_ping_time = time(NULL);

    mcp_log_debug("Requested ping to server");
    return 0;
}

// Client event loop thread function
static void* ws_client_event_thread(void* arg) {
    ws_client_data_t* data = (ws_client_data_t*)arg;

    while (data->running) {
        // Service the WebSocket context with a very short timeout
        // This ensures the event loop is responsive
        lws_service(data->context, 10); // 10ms timeout for maximum responsiveness

        // Check if we need to reconnect
        mcp_mutex_lock(data->connection_mutex);
        bool need_reconnect = (data->state == WS_CLIENT_STATE_DISCONNECTED ||
                              data->state == WS_CLIENT_STATE_ERROR) &&
                              data->reconnect && data->running;

        // Check if we need to request a ping check
        // Only do this if there's no active message processing
        bool need_ping_check = data->state == WS_CLIENT_STATE_CONNECTED &&
                              !data->ping_in_progress &&
                              !data->sync_response_mode &&
                              difftime(time(NULL), data->last_activity_time) * 1000 >= data->ping_interval_ms;
        mcp_mutex_unlock(data->connection_mutex);

        if (need_reconnect) {
            ws_client_handle_reconnect(data);
        }

        // If we need to check ping, request a writable callback
        if (need_ping_check && data->wsi) {
            lws_callback_on_writable(data->wsi);
        }
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

    // Silently detect inconsistency but don't change behavior or log

    // If not connecting, try to connect
    if (data->state != WS_CLIENT_STATE_CONNECTING) {
        // Unlock mutex before calling connect
        mcp_mutex_unlock(data->connection_mutex);

        // Try to connect
        mcp_log_debug("WebSocket client not connecting, attempting to connect...");
        if (ws_client_connect(data) != 0) {
            mcp_log_error("Failed to initiate WebSocket connection");
            return -1;
        }

        // Re-lock mutex to continue waiting
        mcp_mutex_lock(data->connection_mutex);
    }

    // Wait for connection with timeout
    if (timeout_ms > 0) {
        // Wait with timeout
        uint32_t remaining_timeout = timeout_ms;
        // Wait in smaller chunks to check for state changes
        uint32_t wait_chunk = 100;

        mcp_log_debug("WebSocket client waiting for connection with timeout %u ms", timeout_ms);

        while (data->state != WS_CLIENT_STATE_CONNECTED &&
               data->wsi == NULL &&
               data->state != WS_CLIENT_STATE_ERROR &&
               data->running &&
               remaining_timeout > 0) {

            uint32_t wait_time = (remaining_timeout < wait_chunk) ? remaining_timeout : wait_chunk;
            result = mcp_cond_timedwait(data->connection_cond, data->connection_mutex, wait_time);

            // Check if we need to trigger a reconnect
            if (data->state == WS_CLIENT_STATE_DISCONNECTED) {
                // Unlock mutex before calling connect
                mcp_mutex_unlock(data->connection_mutex);

                // Try to reconnect
                mcp_log_debug("WebSocket client disconnected during wait, attempting to reconnect...");
                if (ws_client_connect(data) != 0) {
                    mcp_log_error("Failed to initiate WebSocket reconnection");
                    return -1;
                }

                // Re-lock mutex to continue waiting
                mcp_mutex_lock(data->connection_mutex);
            }

            if (result != 0) {
                // Timeout or error
                mcp_log_debug("WebSocket client connection wait returned %d", result);
                break;
            }

            remaining_timeout -= wait_time;

            // Log remaining timeout every second
            if (remaining_timeout % 1000 == 0 && remaining_timeout > 0) {
                mcp_log_debug("WebSocket client still waiting for connection, %u ms remaining", remaining_timeout);
            }
        }
    } else {
        // Wait indefinitely
        while (data->state != WS_CLIENT_STATE_CONNECTED &&
               data->wsi == NULL &&
               data->state != WS_CLIENT_STATE_ERROR &&
               data->running) {

            result = mcp_cond_wait(data->connection_cond, data->connection_mutex);

            // Check if we need to trigger a reconnect
            if (data->state == WS_CLIENT_STATE_DISCONNECTED) {
                // Unlock mutex before calling connect
                mcp_mutex_unlock(data->connection_mutex);

                // Try to reconnect
                mcp_log_debug("WebSocket client disconnected during wait, attempting to reconnect...");
                if (ws_client_connect(data) != 0) {
                    mcp_log_error("Failed to initiate WebSocket reconnection");
                    return -1;
                }

                // Re-lock mutex to continue waiting
                mcp_mutex_lock(data->connection_mutex);
            }
        }
    }

    // Check if connected based on state
    if (data->state != WS_CLIENT_STATE_CONNECTED) {
        mcp_log_error("WebSocket client failed to connect, state: %d, wsi: %p", data->state, data->wsi);
        result = -1;
    } else {
        mcp_log_debug("WebSocket client successfully connected");
    }

    // Silently detect inconsistency but don't change behavior or log

    mcp_mutex_unlock(data->connection_mutex);
    return result;
}

// Helper function to send a message and wait for a response
static int ws_client_send_and_wait_response(
    ws_client_data_t* ws_data,
    const void* data,
    size_t size,
    char** response_out,
    size_t* response_size_out,
    uint32_t timeout_ms
) {
    if (!ws_data || !data || size == 0 || !response_out) {
        return -1;
    }

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Ensure client is connected
    if (ws_client_ensure_connected(ws_data, 15000) != 0) {
        return -1;
    }

    // Set up synchronous response mode
    mcp_mutex_lock(ws_data->response_mutex);

    // Reset response state
    ws_data->sync_response_mode = true;
    ws_data->response_ready = false;
    if (ws_data->response_data) {
        free(ws_data->response_data);
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
    }
    ws_data->response_error_code = 0;

    mcp_log_debug("WebSocket client entering synchronous response mode");
    mcp_mutex_unlock(ws_data->response_mutex);

    // Log the message we're about to send (only in verbose debug mode)
    #ifdef MCP_VERBOSE_DEBUG
    mcp_log_debug("WebSocket client sending message: %.*s", (int)size, (const char*)data);
    #endif

    // Send the message
    if (ws_client_send_buffer(ws_data, data, size) != 0) {
        mcp_log_error("Failed to send WebSocket message");

        // Reset synchronous response mode
        mcp_mutex_lock(ws_data->response_mutex);
        ws_data->sync_response_mode = false;
        mcp_mutex_unlock(ws_data->response_mutex);

        return -1;
    }

    // Add a short delay to allow the server to process the message
    mcp_sleep_ms(1000);

    // Wait for response with timeout
    int result = 0;
    mcp_mutex_lock(ws_data->response_mutex);

    if (!ws_data->response_ready) {
        if (timeout_ms > 0) {
            // Wait with timeout
            uint32_t remaining_timeout = timeout_ms;
            // Wait in smaller chunks to check for state changes
            uint32_t wait_chunk = 100;

            mcp_log_debug("WebSocket client waiting for response with timeout %u ms", timeout_ms);

            while (!ws_data->response_ready && ws_data->running && remaining_timeout > 0) {
                uint32_t wait_time = (remaining_timeout < wait_chunk) ? remaining_timeout : wait_chunk;
                result = mcp_cond_timedwait(ws_data->response_cond, ws_data->response_mutex, wait_time);

                if (result != 0) {
                    // Timeout or error
                    mcp_log_debug("WebSocket client wait returned %d", result);
                    break;
                }

                remaining_timeout -= wait_time;

                // Log remaining timeout every second
                if (remaining_timeout % 1000 == 0) {
                    mcp_log_debug("WebSocket client still waiting for response, %u ms remaining", remaining_timeout);
                }
            }

            if (!ws_data->response_ready) {
                mcp_log_error("WebSocket client response timeout after %u ms", timeout_ms);
                result = -2; // Timeout
            }
        } else {
            // Wait indefinitely
            mcp_log_debug("WebSocket client waiting for response indefinitely");

            while (!ws_data->response_ready && ws_data->running) {
                result = mcp_cond_wait(ws_data->response_cond, ws_data->response_mutex);

                if (result != 0) {
                    // Error
                    mcp_log_debug("WebSocket client wait returned %d", result);
                    break;
                }
            }
        }
    }

    // Check if we got a response
    if (ws_data->response_ready && ws_data->response_data) {
        // Copy the response data
        *response_out = ws_data->response_data;
        if (response_size_out) {
            *response_size_out = ws_data->response_data_len;
        }

        // Transfer ownership of the response data
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
        result = 0;
    } else {
        // No response or error
        *response_out = NULL;
        if (response_size_out) {
            *response_size_out = 0;
        }

        if (result == 0) {
            result = -1; // General error
        }
    }

    // Reset synchronous response mode
    ws_data->sync_response_mode = false;
    ws_data->response_ready = false;

    mcp_mutex_unlock(ws_data->response_mutex);

    return result;
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

    // Ensure client is connected
    if (ws_client_ensure_connected(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
        return -1;
    }

    // Send the message
    return ws_client_send_buffer(ws_data, data, size);
}

// Client transport receive function
static int ws_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    if (!transport || !transport->transport_data || !data || !size) {
        return -1;
    }

    ws_client_data_t* ws_data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!ws_data->running) {
        mcp_log_error("WebSocket client is not running");
        return -1;
    }

    // Check if we already have a response ready (from sendv)
    mcp_mutex_lock(ws_data->response_mutex);

    if (ws_data->response_ready && ws_data->response_data) {
        // We already have a response, return it immediately
        mcp_log_debug("WebSocket client already has response ready, returning immediately");

        // Copy the response data
        *data = ws_data->response_data;
        *size = ws_data->response_data_len;

        // Transfer ownership of the response data
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
        ws_data->response_ready = false;

        mcp_mutex_unlock(ws_data->response_mutex);
        return 0;
    }

    // No response ready, we need to wait for one
    mcp_log_debug("WebSocket client receive: no response ready, waiting for one");

    // Set up synchronous response mode
    ws_data->sync_response_mode = true;
    ws_data->response_ready = false;
    if (ws_data->response_data) {
        free(ws_data->response_data);
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
    }
    ws_data->response_error_code = 0;

    // Wait for response with timeout
    int result = 0;

    if (timeout_ms > 0) {
        // Wait with timeout
        uint32_t remaining_timeout = timeout_ms;
        uint32_t wait_chunk = 100; // Wait in smaller chunks to check for state changes

        mcp_log_debug("WebSocket client receive: waiting for response with timeout %u ms", timeout_ms);

        while (!ws_data->response_ready && ws_data->running && remaining_timeout > 0) {
            uint32_t wait_time = (remaining_timeout < wait_chunk) ? remaining_timeout : wait_chunk;
            result = mcp_cond_timedwait(ws_data->response_cond, ws_data->response_mutex, wait_time);

            if (result != 0) {
                // Timeout or error
                mcp_log_debug("WebSocket client receive: wait returned %d", result);
                break;
            }

            remaining_timeout -= wait_time;

            // Log remaining timeout every second
            if (remaining_timeout % 1000 == 0 && remaining_timeout > 0) {
                mcp_log_debug("WebSocket client receive: still waiting for response, %u ms remaining", remaining_timeout);
            }
        }

        if (!ws_data->response_ready) {
            mcp_log_error("WebSocket client receive: response timeout after %u ms", timeout_ms);
            result = -2; // Timeout
        }
    } else {
        // Wait indefinitely
        mcp_log_debug("WebSocket client receive: waiting for response indefinitely");

        while (!ws_data->response_ready && ws_data->running) {
            result = mcp_cond_wait(ws_data->response_cond, ws_data->response_mutex);

            if (result != 0) {
                // Error
                mcp_log_debug("WebSocket client receive: wait returned %d", result);
                break;
            }
        }
    }

    // Check if we got a response
    if (ws_data->response_ready && ws_data->response_data) {
        // Copy the response data
        *data = ws_data->response_data;
        *size = ws_data->response_data_len;

        // Transfer ownership of the response data
        ws_data->response_data = NULL;
        ws_data->response_data_len = 0;
        result = 0;

        mcp_log_debug("WebSocket client receive: got response, size: %zu", *size);
    } else {
        // No response or error
        *data = NULL;
        *size = 0;

        if (result == 0) {
            // General error
            result = -1;
        }

        mcp_log_error("WebSocket client receive: failed to get response, result: %d", result);
    }

    // Reset synchronous response mode
    ws_data->sync_response_mode = false;
    ws_data->response_ready = false;

    mcp_mutex_unlock(ws_data->response_mutex);

    return result;
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

    // For WebSocket, we always use synchronous request-response for all messages
    // This ensures consistent behavior regardless of message content
    bool use_sync_mode = true;

    // Log the message for debugging if it's JSON (only in verbose debug mode)
    #ifdef MCP_VERBOSE_DEBUG
    if (buffer_count == 2 && buffers[0].size == sizeof(uint32_t) &&
        buffers[1].size > 0 && ((const char*)buffers[1].data)[0] == '{') {
        const char* json_data = (const char*)buffers[1].data;
        mcp_log_debug("JSON data in sendv: %.*s", (int)buffers[1].size, json_data);
    }
    #endif

    // Always use synchronous mode for WebSocket
    if (use_sync_mode) {
        // For WebSocket, we use synchronous request-response
        // Skip the length prefix (first buffer) and send only the JSON part (second buffer)
        // This is the standard approach for WebSocket transport
        char* response = NULL;
        size_t response_size = 0;

        // Get the timeout value from the client configuration
        // This is passed from mcp_client_send_and_wait to mcp_transport_receive
        uint32_t timeout_ms = 10000; // Default to 10 seconds if not specified

        // Log the timeout value
        mcp_log_debug("Using timeout: %u ms", timeout_ms);

        int result = ws_client_send_and_wait_response(
            ws_data,
            buffers[1].data,
            buffers[1].size,
            &response,
            &response_size,
            timeout_ms
        );

        // Free the temporary buffer
        free(combined_buffer);

        if (result != 0) {
            mcp_log_error("WebSocket client send and wait response failed: %d", result);
            return result;
        }

        // Store the response in the transport's response buffer
        // The client will retrieve it using mcp_transport_receive
        mcp_mutex_lock(ws_data->response_mutex);

        if (ws_data->response_data) {
            free(ws_data->response_data);
        }

        ws_data->response_data = response;
        ws_data->response_data_len = response_size;
        ws_data->response_ready = true;

        mcp_mutex_unlock(ws_data->response_mutex);

        return 0;
    } else {
        // For non-JSON-RPC messages, use direct sending
        // Ensure client is connected
        if (ws_client_ensure_connected(ws_data, WS_DEFAULT_CONNECT_TIMEOUT_MS) != 0) {
            free(combined_buffer);
            return -1;
        }

        // Send the message
        int result = ws_client_send_buffer(ws_data, combined_buffer, total_size);
        free(combined_buffer);
        return result;
    }
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

    // Initialize response mutex and condition variable
    data->response_mutex = mcp_mutex_create();
    if (!data->response_mutex) {
        mcp_log_error("Failed to create WebSocket client response mutex");
        mcp_mutex_destroy(data->connection_mutex);
        mcp_cond_destroy(data->connection_cond);
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    data->response_cond = mcp_cond_create();
    if (!data->response_cond) {
        mcp_log_error("Failed to create WebSocket client response condition variable");
        mcp_mutex_destroy(data->response_mutex);
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
    data->last_pong_time = time(NULL);
    data->last_activity_time = time(NULL);
    data->ping_interval_ms = WS_PING_INTERVAL_MS;
    data->ping_timeout_ms = WS_PING_TIMEOUT_MS;
    data->ping_in_progress = false;
    data->missed_pongs = 0;

    // Set initial state
    data->state = WS_CLIENT_STATE_DISCONNECTED;
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_client_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket client event thread");
        ws_client_cleanup_resources(data);
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

    // Check if already stopped
    if (!data->running) {
        mcp_log_debug("WebSocket client already stopped");
        return 0;
    }

    mcp_log_info("Stopping WebSocket client transport...");

    // First, set flags to prevent reconnection attempts
    data->reconnect = false;

    // Signal any waiting threads to wake up
    if (data->connection_mutex && data->connection_cond) {
        mcp_mutex_lock(data->connection_mutex);
        data->state = WS_CLIENT_STATE_CLOSING;
        mcp_cond_signal(data->connection_cond);
        mcp_mutex_unlock(data->connection_mutex);
    }

    if (data->response_mutex && data->response_cond) {
        mcp_mutex_lock(data->response_mutex);
        data->response_ready = true; // Force any waiting threads to wake up
        data->response_error_code = -1; // Indicate error
        mcp_cond_signal(data->response_cond);
        mcp_mutex_unlock(data->response_mutex);
    }

    // Now set running flag to false to stop event loop
    data->running = false;

    // Force libwebsockets to break out of its service loop
    if (data->context) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets client service");
    }

    // Wait for event thread to exit with a timeout
    if (data->event_thread) {
        mcp_log_info("Waiting for WebSocket client event thread to exit...");

        // Join with timeout to prevent hanging
        int join_result = mcp_thread_join(data->event_thread, NULL);
        if (join_result != 0) {
            mcp_log_warn("WebSocket client event thread join failed with code %d", join_result);
        } else {
            mcp_log_debug("WebSocket client event thread exited successfully");
        }

        data->event_thread = 0;
    }

    // Clean up resources
    ws_client_cleanup_resources(data);

    mcp_log_info("WebSocket client stopped");

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

    mcp_log_info("Destroying WebSocket client transport...");

    // Stop transport if running
    if (data->running) {
        // Call stop function to ensure proper cleanup
        ws_client_transport_stop(transport);
    } else {
        // Even if not running, make sure resources are cleaned up
        ws_client_cleanup_resources(data);
    }

    // Free any dynamically allocated config strings
    // Note: In the current implementation, these are typically not duplicated,
    // but we should check in case that changes in the future
    // We can't compare with the original config since it's not stored in the transport

    // Free transport data
    free(data);

    // Free transport
    free(transport);

    mcp_log_info("WebSocket client transport destroyed");
}

// Get WebSocket client connection state
int mcp_transport_websocket_client_is_connected(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_client_data_t* data = (ws_client_data_t*)transport->transport_data;

    // Check if client is running
    if (!data->running) {
        return -1;
    }

    // Use the helper function to check connection state
    return ws_client_is_connected(data) ? 1 : 0;
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

    // Initialize reconnection parameters
    data->reconnect_attempts = 0;
    data->reconnect_delay_ms = WS_RECONNECT_DELAY_MS;
    data->last_reconnect_time = 0;

    // Initialize synchronous response mode
    data->sync_response_mode = false;
    data->response_mutex = NULL;
    data->response_cond = NULL;
    data->response_data = NULL;
    data->response_data_len = 0;
    data->response_ready = false;
    data->response_error_code = 0;

    // Set transport type and operations
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->client.start = ws_client_transport_start;
    transport->client.stop = ws_client_transport_stop;
    transport->client.destroy = ws_client_transport_destroy;
    transport->client.send = ws_client_transport_send;
    transport->client.sendv = ws_client_transport_sendv;
    transport->client.receive = ws_client_transport_receive;

    // Set transport protocol type to WebSocket
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_WEBSOCKET;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}