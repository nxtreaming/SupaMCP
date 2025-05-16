#ifndef MCP_TRANSPORT_FACTORY_H
#define MCP_TRANSPORT_FACTORY_H

#include <mcp_transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enumeration of supported transport types.
 */
typedef enum mcp_transport_type {
    MCP_TRANSPORT_STDIO,       /**< Standard input/output transport */
    MCP_TRANSPORT_TCP,         /**< TCP server transport */
    MCP_TRANSPORT_TCP_CLIENT,  /**< TCP client transport */
    MCP_TRANSPORT_WS_SERVER,   /**< WebSocket server transport */
    MCP_TRANSPORT_WS_CLIENT,   /**< WebSocket client transport */
    MCP_TRANSPORT_WS_POOL,     /**< WebSocket connection pool transport */
    MCP_TRANSPORT_HTTP_SERVER, /**< HTTP server transport */
    MCP_TRANSPORT_HTTP_CLIENT  /**< HTTP client transport */
} mcp_transport_type_t;

/**
 * @brief Configuration options for transports.
 *
 * This union contains configuration options for all transport types.
 * Only the fields relevant to the chosen transport type should be used.
 */
typedef union mcp_transport_config {
    struct {
        const char* host;         /**< Hostname or IP address to bind to (for TCP server) or connect to (for TCP client) */
        uint16_t port;            /**< Port number */
        uint32_t idle_timeout_ms; /**< Idle connection timeout in milliseconds (0 to disable, TCP server only) */
    } tcp;

    struct {
        const char* host;         /**< Hostname or IP address to bind to (for server) or connect to (for client) */
        uint16_t port;            /**< Port number */
        const char* path;         /**< WebSocket endpoint path (e.g., "/ws") */
        const char* origin;       /**< Origin header for client (optional) */
        const char* protocol;     /**< WebSocket protocol name (optional) */
        int use_ssl;              /**< Whether to use SSL/TLS (1 for true, 0 for false) */
        const char* cert_path;    /**< Path to SSL certificate (if use_ssl is true) */
        const char* key_path;     /**< Path to SSL private key (if use_ssl is true) */
        uint32_t connect_timeout_ms; /**< Connection timeout in milliseconds (0 = default) */
    } ws;

    struct {
        const char* host;         /**< Hostname or IP address to connect to */
        uint16_t port;            /**< Port number */
        const char* path;         /**< WebSocket endpoint path (e.g., "/ws") */
        const char* origin;       /**< Origin header for client (optional) */
        const char* protocol;     /**< WebSocket protocol name (optional) */
        int use_ssl;              /**< Whether to use SSL/TLS (1 for true, 0 for false) */
        const char* cert_path;    /**< Path to SSL certificate (if use_ssl is true) */
        const char* key_path;     /**< Path to SSL private key (if use_ssl is true) */
        uint32_t connect_timeout_ms; /**< Connection timeout in milliseconds (0 = default) */
        uint32_t min_connections; /**< Minimum number of connections to maintain in the pool */
        uint32_t max_connections; /**< Maximum number of connections allowed in the pool */
        uint32_t idle_timeout_ms; /**< Maximum idle time before a connection is closed */
        uint32_t health_check_ms; /**< Interval for health checks */
    } ws_pool;

    struct {
        const char* host;         /**< Hostname or IP address to bind to */
        uint16_t port;            /**< Port number */
        int use_ssl;              /**< Whether to use SSL/TLS (1 for true, 0 for false) */
        const char* cert_path;    /**< Path to SSL certificate (if use_ssl is true) */
        const char* key_path;     /**< Path to SSL private key (if use_ssl is true) */
        const char* doc_root;     /**< Document root for serving static files (optional) */
        uint32_t timeout_ms;      /**< Connection timeout in milliseconds (0 to disable) */
    } http;

    struct {
        const char* host;         /**< Hostname or IP address to connect to */
        uint16_t port;            /**< Port number */
        int use_ssl;              /**< Whether to use SSL/TLS (1 for true, 0 for false) */
        const char* cert_path;    /**< Path to SSL certificate (if use_ssl is true) */
        const char* key_path;     /**< Path to SSL private key (if use_ssl is true) */
        uint32_t timeout_ms;      /**< Connection timeout in milliseconds (0 to disable) */
        const char* api_key;      /**< API key for authentication (optional) */
    } http_client;

} mcp_transport_config_t;

/**
 * @brief Creates a transport instance of the specified type with the given configuration.
 *
 * This factory function simplifies the creation of different transport types
 * by centralizing the creation logic and providing a unified interface.
 *
 * @param type The type of transport to create.
 * @param config Configuration options for the transport. The relevant fields
 *               should be set based on the chosen transport type.
 * @return A pointer to the created transport instance, or NULL on failure.
 *         The caller is responsible for destroying the transport using
 *         mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_factory_create(
    mcp_transport_type_t type,
    const mcp_transport_config_t* config
);

#ifdef __cplusplus
}
#endif

#endif // MCP_TRANSPORT_FACTORY_H
