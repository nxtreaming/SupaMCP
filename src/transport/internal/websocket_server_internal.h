#ifndef WEBSOCKET_SERVER_INTERNAL_H
#define WEBSOCKET_SERVER_INTERNAL_H

#include "mcp_websocket_transport.h"
#include "websocket_common.h"
#include "transport_internal.h"
#include "transport_interfaces.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "mcp_thread_local.h"
#include "mcp_buffer_pool.h"

#include "libwebsockets.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Define default maximum number of simultaneous WebSocket clients
#define DEFAULT_MAX_WEBSOCKET_CLIENTS 1024
#define DEFAULT_SEGMENT_COUNT 16
#define DEFAULT_BUFFER_POOL_SIZE 256
#define DEFAULT_BUFFER_POOL_BUFFER_SIZE 4096

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
    time_t last_activity;           // Time of last activity for timeout detection
    uint32_t ping_sent;             // Number of pings sent without response
} ws_client_t;

// WebSocket server transport data
typedef struct {
    struct lws_context* context;    // libwebsockets context
    const struct lws_protocols* protocols; // WebSocket protocols
    bool running;                   // Running flag
    mcp_thread_t event_thread;      // Event loop thread

    // Dynamic client management
    ws_client_t* clients;           // Dynamically allocated clients array
    uint32_t* client_bitmap;        // Dynamically allocated bitmap
    uint32_t max_clients;           // Maximum number of clients
    uint32_t bitmap_size;           // Size of bitmap array in uint32_t units

    // Segmented mutex for better concurrency
    mcp_mutex_t** segment_mutexes;  // Array of segment mutexes
    uint32_t num_segments;          // Number of mutex segments
    mcp_mutex_t* global_mutex;      // Global mutex for operations affecting all clients

    // Statistics
    uint32_t active_clients;        // Number of active clients
    uint32_t peak_clients;          // Peak number of active clients
    uint32_t total_connections;     // Total number of connections since start
    uint32_t rejected_connections;  // Number of rejected connections due to max clients

    mcp_transport_t* transport;     // Transport handle
    mcp_websocket_config_t config;  // WebSocket configuration
    time_t last_ping_time;          // Time of last ping check
    time_t last_cleanup_time;       // Time of last inactive client cleanup
    time_t start_time;              // Time when the server was started

    // Buffer pool for efficient memory management
    mcp_buffer_pool_t* buffer_pool; // Pool of reusable buffers
    uint32_t buffer_allocs;         // Total number of buffer allocations
    uint32_t buffer_reuses;         // Number of buffer reuses from pool
    uint32_t buffer_misses;         // Number of times a buffer couldn't be acquired from pool
    size_t total_buffer_memory;     // Total memory used for buffers
} ws_server_data_t;

// WebSocket protocol definitions
extern struct lws_protocols server_protocols[3];

// Bitmap helper functions
void ws_server_set_client_bit(uint32_t* bitmap, int index, int bitmap_size);
void ws_server_clear_client_bit(uint32_t* bitmap, int index, int bitmap_size);
bool ws_server_test_client_bit(uint32_t* bitmap, int index, int bitmap_size);
int ws_server_find_free_client_slot(ws_server_data_t* data);

// Mutex helper functions
mcp_mutex_t* ws_server_get_client_mutex(ws_server_data_t* data, int client_index);
void ws_server_lock_client(ws_server_data_t* data, int client_index);
void ws_server_unlock_client(ws_server_data_t* data, int client_index);
void ws_server_lock_all_clients(ws_server_data_t* data);
void ws_server_unlock_all_clients(ws_server_data_t* data);

// Client management functions
int ws_server_client_init(ws_client_t* client, int client_id, struct lws* wsi);
void ws_server_client_cleanup(ws_client_t* client, ws_server_data_t* server_data);
int ws_server_client_resize_buffer(ws_client_t* client, size_t needed_size, ws_server_data_t* server_data);
void ws_server_client_update_activity(ws_client_t* client);
int ws_server_client_send_ping(ws_client_t* client);
int ws_server_client_send_response(ws_client_t* client, struct lws* wsi, const char* response, size_t response_len);
int ws_server_client_process_message(ws_server_data_t* data, ws_client_t* client, struct lws* wsi);
int ws_server_client_handle_received_data(ws_server_data_t* data, ws_client_t* client, struct lws* wsi, void* in, size_t len, bool is_final);

// Server callback and related functions
int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
ws_client_t* ws_server_find_client_by_wsi(ws_server_data_t* data, struct lws* wsi);

// Server event thread and timeout functions
void* ws_server_event_thread(void* arg);
void ws_server_cleanup_inactive_clients(ws_server_data_t* data);

#endif // WEBSOCKET_SERVER_INTERNAL_H
