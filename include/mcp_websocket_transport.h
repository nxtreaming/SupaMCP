#ifndef MCP_WEBSOCKET_TRANSPORT_H
#define MCP_WEBSOCKET_TRANSPORT_H

#include "mcp_transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket transport configuration
 */
typedef struct {
    const char* host;           // Host to bind to (server) or connect to (client)
    uint16_t port;              // Port to bind to (server) or connect to (client)
    const char* path;           // WebSocket endpoint path (e.g., "/ws")
    const char* origin;         // Origin header for client (optional)
    const char* protocol;       // WebSocket protocol name (optional)
    bool use_ssl;               // Whether to use SSL/TLS
    const char* cert_path;      // Path to SSL certificate (if use_ssl is true)
    const char* key_path;       // Path to SSL private key (if use_ssl is true)
    uint32_t connect_timeout_ms; // Connection timeout in milliseconds (0 = default)
} mcp_websocket_config_t;

/**
 * @brief Creates a WebSocket server transport
 *
 * @param config WebSocket configuration
 * @return mcp_transport_t* Transport handle or NULL on failure
 */
mcp_transport_t* mcp_transport_websocket_server_create(const mcp_websocket_config_t* config);

/**
 * @brief Creates a WebSocket client transport
 *
 * @param config WebSocket configuration
 * @return mcp_transport_t* Transport handle or NULL on failure
 */
mcp_transport_t* mcp_transport_websocket_client_create(const mcp_websocket_config_t* config);

/**
 * @brief Get the connection state of a WebSocket client transport
 *
 * @param transport WebSocket client transport handle
 * @return int 1 if connected, 0 if not connected, -1 on error
 */
int mcp_transport_websocket_client_is_connected(mcp_transport_t* transport);

/**
 * @brief Get statistics from a WebSocket server transport
 *
 * @param transport WebSocket server transport handle
 * @param active_clients Pointer to store the number of active clients (can be NULL)
 * @param peak_clients Pointer to store the peak number of clients (can be NULL)
 * @param total_connections Pointer to store the total number of connections since start (can be NULL)
 * @param rejected_connections Pointer to store the number of rejected connections (can be NULL)
 * @param uptime_seconds Pointer to store the server uptime in seconds (can be NULL)
 * @return int 0 on success, -1 on error
 */
int mcp_transport_websocket_server_get_stats(mcp_transport_t* transport,
                                           uint32_t* active_clients,
                                           uint32_t* peak_clients,
                                           uint32_t* total_connections,
                                           uint32_t* rejected_connections,
                                           double* uptime_seconds);

/**
 * @brief Get memory statistics from a WebSocket server transport
 *
 * @param transport WebSocket server transport handle
 * @param buffer_allocs Pointer to store the number of buffer allocations (can be NULL)
 * @param buffer_reuses Pointer to store the number of buffer reuses from pool (can be NULL)
 * @param buffer_misses Pointer to store the number of buffer pool misses (can be NULL)
 * @param total_buffer_memory Pointer to store the total memory used for buffers (can be NULL)
 * @param pool_size Pointer to store the size of the buffer pool (can be NULL)
 * @param pool_buffer_size Pointer to store the size of each buffer in the pool (can be NULL)
 * @return int 0 on success, -1 on error
 */
int mcp_transport_websocket_server_get_memory_stats(mcp_transport_t* transport,
                                                  uint32_t* buffer_allocs,
                                                  uint32_t* buffer_reuses,
                                                  uint32_t* buffer_misses,
                                                  size_t* total_buffer_memory,
                                                  uint32_t* pool_size,
                                                  size_t* pool_buffer_size);

#ifdef __cplusplus
}
#endif

#endif // MCP_WEBSOCKET_TRANSPORT_H
