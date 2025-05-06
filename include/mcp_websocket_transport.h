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

#ifdef __cplusplus
}
#endif

#endif // MCP_WEBSOCKET_TRANSPORT_H
