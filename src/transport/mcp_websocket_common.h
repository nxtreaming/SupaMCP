#ifndef MCP_WEBSOCKET_COMMON_H
#define MCP_WEBSOCKET_COMMON_H

#include "mcp_transport.h"
#include "mcp_sync.h"
#include "libwebsockets.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Common buffer sizes and timeouts
#define WS_DEFAULT_BUFFER_SIZE 4096
#define WS_PING_INTERVAL_MS 60000      // 60 seconds
#define WS_PING_TIMEOUT_MS 30000       // 30 seconds
#define WS_CLEANUP_INTERVAL_MS 120000  // 120 seconds
#define WS_DEFAULT_CONNECT_TIMEOUT_MS 10000  // 10 seconds
#define WS_MAX_RECONNECT_ATTEMPTS 10
#define WS_RECONNECT_DELAY_MS 2000     // 2 seconds
#define WS_MAX_RECONNECT_DELAY_MS 60000 // 60 seconds

// Message types
typedef enum {
    WS_MESSAGE_TYPE_TEXT = 0,
    WS_MESSAGE_TYPE_BINARY
} ws_message_type_t;

// Connection states
typedef enum {
    WS_STATE_DISCONNECTED = 0,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_CLOSING
} ws_connection_state_t;

// Message queue item
typedef struct ws_message_item {
    unsigned char* data;         // Message data (with LWS_PRE padding)
    size_t size;                 // Message size
    ws_message_type_t type;      // Message type
    struct ws_message_item* next; // Next item in queue
} ws_message_item_t;

/**
 * @brief Initialize WebSocket protocols
 * 
 * @param protocols Array to store protocol definitions
 * @param callback Callback function for WebSocket events
 */
void mcp_websocket_init_protocols(
    struct lws_protocols* protocols,
    lws_callback_function* callback
);

/**
 * @brief Enqueue a message to a WebSocket message queue
 * 
 * @param queue_head Pointer to queue head pointer
 * @param queue_tail Pointer to queue tail pointer
 * @param queue_mutex Mutex for queue access
 * @param message Message data
 * @param size Message size
 * @param type Message type
 * @return int 0 on success, -1 on failure
 */
int mcp_websocket_enqueue_message(
    ws_message_item_t** queue_head,
    ws_message_item_t** queue_tail,
    mcp_mutex_t* queue_mutex,
    const void* message,
    size_t size,
    ws_message_type_t type
);

/**
 * @brief Dequeue a message from a WebSocket message queue
 * 
 * @param queue_head Pointer to queue head pointer
 * @param queue_tail Pointer to queue tail pointer
 * @param queue_mutex Mutex for queue access
 * @return ws_message_item_t* Message item or NULL if queue is empty
 */
ws_message_item_t* mcp_websocket_dequeue_message(
    ws_message_item_t** queue_head,
    ws_message_item_t** queue_tail,
    mcp_mutex_t* queue_mutex
);

/**
 * @brief Free all messages in a WebSocket message queue
 * 
 * @param queue_head Pointer to queue head pointer
 * @param queue_tail Pointer to queue tail pointer
 * @param queue_mutex Mutex for queue access
 */
void mcp_websocket_free_message_queue(
    ws_message_item_t** queue_head,
    ws_message_item_t** queue_tail,
    mcp_mutex_t* queue_mutex
);

/**
 * @brief Create a libwebsockets context
 * 
 * @param host Host to bind to (server) or connect to (client)
 * @param port Port to bind to (server) or connect to (client)
 * @param path WebSocket path
 * @param protocols WebSocket protocols
 * @param user_data User data for callbacks
 * @param is_server Whether this is a server context
 * @param use_ssl Whether to use SSL
 * @param cert_path Path to SSL certificate
 * @param key_path Path to SSL private key
 * @return struct lws_context* Context or NULL on failure
 */
struct lws_context* mcp_websocket_create_context(
    const char* host,
    uint16_t port,
    const char* path,
    const struct lws_protocols* protocols,
    void* user_data,
    bool is_server,
    bool use_ssl,
    const char* cert_path,
    const char* key_path
);

/**
 * @brief Calculate total size of multiple buffers
 * 
 * @param buffers Array of buffers
 * @param buffer_count Number of buffers
 * @return size_t Total size
 */
size_t mcp_websocket_calculate_total_size(
    const mcp_buffer_t* buffers,
    size_t buffer_count
);

/**
 * @brief Combine multiple buffers into a single buffer
 * 
 * @param buffers Array of buffers
 * @param buffer_count Number of buffers
 * @param combined_buffer Buffer to store combined data
 * @param combined_size Size of combined buffer
 * @return int 0 on success, -1 on failure
 */
int mcp_websocket_combine_buffers(
    const mcp_buffer_t* buffers,
    size_t buffer_count,
    void* combined_buffer,
    size_t combined_size
);

#ifdef __cplusplus
}
#endif

#endif // MCP_WEBSOCKET_COMMON_H
