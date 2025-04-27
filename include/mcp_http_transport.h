#ifndef MCP_HTTP_TRANSPORT_H
#define MCP_HTTP_TRANSPORT_H

#include "mcp_transport.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for HTTP transport
 */
typedef struct mcp_http_config {
    const char* host;         /**< Host to bind to (e.g., "0.0.0.0" for all interfaces) */
    uint16_t port;            /**< Port to listen on */
    bool use_ssl;             /**< Whether to use HTTPS */
    const char* cert_path;    /**< Path to SSL certificate file (for HTTPS) */
    const char* key_path;     /**< Path to SSL private key file (for HTTPS) */
    const char* doc_root;     /**< Document root for serving static files (optional) */
    uint32_t timeout_ms;      /**< Connection timeout in milliseconds (0 to disable) */

    // CORS settings
    bool enable_cors;                /**< Whether to enable CORS */
    const char* cors_allow_origin;   /**< Allowed origins for CORS (e.g., "*" for all) */
    const char* cors_allow_methods;  /**< Allowed methods for CORS (e.g., "GET, POST, OPTIONS") */
    const char* cors_allow_headers;  /**< Allowed headers for CORS */
    int cors_max_age;                /**< Max age for CORS preflight requests in seconds */
} mcp_http_config_t;

/**
 * @brief Create an HTTP server transport
 *
 * @param config HTTP transport configuration
 * @return mcp_transport_t* Transport handle or NULL on failure
 */
mcp_transport_t* mcp_transport_http_create(const mcp_http_config_t* config);

/**
 * @brief Send an SSE (Server-Sent Event) to a specific session or all connected clients
 *
 * @param transport Transport handle
 * @param event Event name (can be NULL)
 * @param data Event data
 * @param session_id Session ID to send to (NULL to send to all clients)
 * @return int 0 on success, non-zero on failure
 */
int mcp_http_transport_send_sse(mcp_transport_t* transport, const char* event, const char* data, const char* session_id);

#ifdef __cplusplus
}
#endif

#endif // MCP_HTTP_TRANSPORT_H
