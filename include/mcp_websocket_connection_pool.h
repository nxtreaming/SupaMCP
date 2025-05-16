#ifndef MCP_WEBSOCKET_CONNECTION_POOL_H
#define MCP_WEBSOCKET_CONNECTION_POOL_H

#include "mcp_websocket_transport.h"
#include "mcp_sync.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Connection state in the pool
 */
typedef enum {
    WS_CONN_STATE_IDLE,       // Connection is idle and available for use
    WS_CONN_STATE_IN_USE,     // Connection is currently in use
    WS_CONN_STATE_CONNECTING, // Connection is being established
    WS_CONN_STATE_INVALID     // Connection is invalid and needs to be recreated
} mcp_ws_conn_state_t;

/**
 * @brief Connection pool configuration
 */
typedef struct {
    uint32_t min_connections;     // Minimum number of connections to maintain
    uint32_t max_connections;     // Maximum number of connections allowed
    uint32_t idle_timeout_ms;     // Maximum idle time before a connection is closed
    uint32_t health_check_ms;     // Interval for health checks
    uint32_t connect_timeout_ms;  // Connection timeout
    mcp_websocket_config_t ws_config; // WebSocket configuration
} mcp_ws_pool_config_t;

/**
 * @brief Connection entry in the pool
 */
typedef struct {
    mcp_transport_t* transport;   // WebSocket transport
    mcp_ws_conn_state_t state;    // Connection state
    time_t last_used;             // Last time the connection was used
    uint32_t id;                  // Unique connection ID
    bool is_healthy;              // Whether the connection is healthy
} mcp_ws_conn_entry_t;

/**
 * @brief WebSocket connection pool
 */
typedef struct {
    mcp_ws_pool_config_t config;          // Pool configuration
    mcp_ws_conn_entry_t* connections;     // Array of connection entries
    uint32_t total_connections;           // Total number of connections in the pool
    uint32_t available_connections;       // Number of available connections
    mcp_mutex_t* pool_mutex;              // Mutex for thread safety
    mcp_cond_t* pool_cond;                // Condition variable for waiting
    mcp_thread_t health_check_thread;     // Thread for health checks
    bool running;                         // Whether the pool is running
    uint32_t next_conn_id;                // Next connection ID to assign
} mcp_ws_connection_pool_t;

/**
 * @brief Create a WebSocket connection pool
 * 
 * @param config Pool configuration
 * @return mcp_ws_connection_pool_t* Pool handle or NULL on failure
 */
mcp_ws_connection_pool_t* mcp_ws_connection_pool_create(const mcp_ws_pool_config_t* config);

/**
 * @brief Destroy a WebSocket connection pool
 * 
 * @param pool Pool handle
 */
void mcp_ws_connection_pool_destroy(mcp_ws_connection_pool_t* pool);

/**
 * @brief Get a connection from the pool
 * 
 * @param pool Pool handle
 * @param timeout_ms Timeout in milliseconds (0 for no timeout)
 * @return mcp_transport_t* Transport handle or NULL on failure
 */
mcp_transport_t* mcp_ws_connection_pool_get(mcp_ws_connection_pool_t* pool, uint32_t timeout_ms);

/**
 * @brief Release a connection back to the pool
 * 
 * @param pool Pool handle
 * @param transport Transport handle
 * @return int 0 on success, -1 on failure
 */
int mcp_ws_connection_pool_release(mcp_ws_connection_pool_t* pool, mcp_transport_t* transport);

/**
 * @brief Get statistics from the connection pool
 * 
 * @param pool Pool handle
 * @param total_connections Pointer to store the total number of connections
 * @param available_connections Pointer to store the number of available connections
 * @param in_use_connections Pointer to store the number of connections in use
 * @param connecting_connections Pointer to store the number of connecting connections
 * @param invalid_connections Pointer to store the number of invalid connections
 * @return int 0 on success, -1 on failure
 */
int mcp_ws_connection_pool_get_stats(
    mcp_ws_connection_pool_t* pool,
    uint32_t* total_connections,
    uint32_t* available_connections,
    uint32_t* in_use_connections,
    uint32_t* connecting_connections,
    uint32_t* invalid_connections
);

#ifdef __cplusplus
}
#endif

#endif // MCP_WEBSOCKET_CONNECTION_POOL_H
