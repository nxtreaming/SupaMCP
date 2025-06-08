#ifndef WEBSOCKET_COMMON_H
#define WEBSOCKET_COMMON_H

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
#define WS_PING_INTERVAL_MS 30000      // 30 seconds (reduced from 60s for more responsive connection monitoring)
#define WS_PING_TIMEOUT_MS 20000       // 20 seconds (reduced from 30s to detect dead connections faster)
#define WS_CLEANUP_INTERVAL_MS 60000   // 60 seconds (reduced from 120s to free resources faster)
#define WS_DEFAULT_CONNECT_TIMEOUT_MS 10000  // 10 seconds (reduced from 15s for faster connection establishment)
#define WS_MAX_RECONNECT_ATTEMPTS 10
#define WS_RECONNECT_DELAY_MS 2000     // 2 seconds (reduced from 3s for faster reconnection)
#define WS_MAX_RECONNECT_DELAY_MS 30000 // 30 seconds (reduced from 60s for faster recovery)
#define WS_MAX_PING_FAILURES 3         // Maximum number of ping failures before closing connection

// Buffer pool configuration
#define DEFAULT_BUFFER_POOL_SIZE 256         // Number of buffers in the pool
#define DEFAULT_BUFFER_POOL_BUFFER_SIZE 4096 // Size of each buffer in the pool

// Client buffer management configuration
#define WS_CLIENT_SEND_BUFFER_POOL_SIZE 64   // Number of send buffers in client pool
#define WS_CLIENT_REUSABLE_BUFFER_SIZE 4096  // Size of reusable send buffer for small messages
#define WS_CLIENT_SMALL_MESSAGE_THRESHOLD 3072 // Messages smaller than this use reusable buffer

// Buffer growth factor
#define WS_BUFFER_GROWTH_FACTOR 1.5    // Factor to grow buffer when needed

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

// Message queue removed to improve performance

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

// Message queue functions removed to improve performance

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

/**
 * @brief Get a human-readable string for a libwebsockets callback reason
 *
 * @param reason The callback reason value
 * @return const char* Human-readable string representation
 */
const char* websocket_get_callback_reason_string(enum lws_callback_reasons reason);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_COMMON_H
