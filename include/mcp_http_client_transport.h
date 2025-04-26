/**
 * @file mcp_http_client_transport.h
 * @brief HTTP client transport for MCP
 */

#ifndef MCP_HTTP_CLIENT_TRANSPORT_H
#define MCP_HTTP_CLIENT_TRANSPORT_H

#include "mcp_transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP client transport configuration
 */
typedef struct {
    const char* host;         /**< Host to connect to (e.g., "127.0.0.1") */
    uint16_t port;            /**< Port to connect to */
    bool use_ssl;             /**< Whether to use HTTPS */
    const char* cert_path;    /**< Path to SSL certificate file (for HTTPS) */
    const char* key_path;     /**< Path to SSL private key file (for HTTPS) */
    uint32_t timeout_ms;      /**< Connection timeout in milliseconds (0 to disable) */
    const char* api_key;      /**< API key for authentication (optional) */
} mcp_http_client_config_t;

/**
 * @brief Creates an HTTP client transport instance.
 *
 * This transport connects to a specified HTTP server host and port.
 * It requires a subsequent call to mcp_transport_start() to establish the
 * connection and start the background receive loop.
 *
 * @param host The hostname or IP address of the server to connect to.
 * @param port The port number on the server to connect to.
 * @return A generic mcp_transport_t handle, or NULL on allocation error.
 *         The caller is responsible for destroying the transport using mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_http_client_create(const char* host, uint16_t port);

/**
 * @brief Creates an HTTP client transport instance with detailed configuration.
 *
 * This transport connects to a specified HTTP server with the given configuration.
 * It requires a subsequent call to mcp_transport_start() to establish the
 * connection and start the background receive loop.
 *
 * @param config The HTTP client configuration.
 * @return A generic mcp_transport_t handle, or NULL on allocation error.
 *         The caller is responsible for destroying the transport using mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_http_client_create_with_config(const mcp_http_client_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // MCP_HTTP_CLIENT_TRANSPORT_H
