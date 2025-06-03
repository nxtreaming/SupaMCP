#ifndef WEBSOCKET_CLIENT_INTERNAL_H
#define WEBSOCKET_CLIENT_INTERNAL_H

#include "mcp_websocket_transport.h"
#include "websocket_common.h"
#include "transport_internal.h"
#include "transport_interfaces.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "mcp_thread_local.h"
#include "mcp_client.h"

#include "libwebsockets.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Forward declaration of client protocols
extern struct lws_protocols client_protocols[3];

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
    int64_t current_request_id;     // Current request ID being processed
    bool request_timedout;         // Whether the current request has timed out
} ws_client_data_t;

// Buffer management functions
int ws_client_handle_received_data(ws_client_data_t* data, void* in, size_t len, bool is_final);
int ws_client_send_buffer(ws_client_data_t* data, const void* buffer, size_t size);

// Connection management functions
int ws_client_connect(ws_client_data_t* data);
int ws_client_ensure_connected(ws_client_data_t* data, uint32_t timeout_ms);
int ws_client_wait_for_connection(ws_client_data_t* data, uint32_t timeout_ms);
void ws_client_handle_reconnect(ws_client_data_t* data);
void ws_client_update_activity(ws_client_data_t* data);
bool ws_client_is_connected(ws_client_data_t* data);

// Synchronous request-response functions
int ws_client_send_and_wait_response(
    ws_client_data_t* ws_data,
    const void* data,
    size_t size,
    char** response_out,
    size_t* response_size_out,
    uint32_t timeout_ms
);

// Thread functions
void* ws_client_event_thread(void* arg);

#endif // WEBSOCKET_CLIENT_INTERNAL_H
