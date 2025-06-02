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
    MCP_TRANSPORT_STDIO,                    /**< Standard input/output transport */
    MCP_TRANSPORT_TCP,                      /**< TCP server transport */
    MCP_TRANSPORT_TCP_CLIENT,               /**< TCP client transport */
    MCP_TRANSPORT_WS_SERVER,                /**< WebSocket server transport */
    MCP_TRANSPORT_WS_CLIENT,                /**< WebSocket client transport */
    MCP_TRANSPORT_WS_POOL,                  /**< WebSocket connection pool transport */
    MCP_TRANSPORT_HTTP_SERVER,              /**< HTTP server transport */
    MCP_TRANSPORT_HTTP_CLIENT,              /**< HTTP client transport */
    MCP_TRANSPORT_STHTTP,                   /**< HTTP Streamable server transport (MCP 2025-03-26) */
    MCP_TRANSPORT_STHTTP_CLIENT             /**< HTTP Streamable client transport (MCP 2025-03-26) */
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

    struct {
        const char* host;                    /**< Hostname or IP address to bind to */
        uint16_t port;                       /**< Port number */
        int use_ssl;                         /**< Whether to use SSL/TLS (1 for true, 0 for false) */
        const char* cert_path;               /**< Path to SSL certificate (if use_ssl is true) */
        const char* key_path;                /**< Path to SSL private key (if use_ssl is true) */
        const char* doc_root;                /**< Document root for serving static files (optional) */
        uint32_t timeout_ms;                 /**< Connection timeout in milliseconds (0 to disable) */
        const char* mcp_endpoint;            /**< MCP endpoint path (default: "/mcp") */
        int enable_sessions;                 /**< Whether to enable session management (1 for true, 0 for false) */
        uint32_t session_timeout_seconds;    /**< Session timeout in seconds (0 for default) */
        int validate_origin;                 /**< Whether to validate Origin header (1 for true, 0 for false) */
        const char* allowed_origins;         /**< Comma-separated list of allowed origins */
        int enable_cors;                     /**< Whether to enable CORS (1 for true, 0 for false) */
        const char* cors_allow_origin;       /**< Allowed origins for CORS */
        const char* cors_allow_methods;      /**< Allowed methods for CORS */
        const char* cors_allow_headers;      /**< Allowed headers for CORS */
        int cors_max_age;                    /**< Max age for CORS preflight requests in seconds */
        int enable_sse_resumability;         /**< Whether to enable SSE stream resumability (1 for true, 0 for false) */
        uint32_t max_stored_events;          /**< Maximum number of events to store for resumability */
        int send_heartbeats;                 /**< Whether to send SSE heartbeats (1 for true, 0 for false) */
        uint32_t heartbeat_interval_ms;      /**< Heartbeat interval in milliseconds */
        int enable_legacy_endpoints;         /**< Whether to enable legacy HTTP+SSE endpoints (1 for true, 0 for false) */
    } sthttp;

    struct {
        const char* host;                    /**< Server hostname or IP address */
        uint16_t port;                       /**< Server port number */
        int use_ssl;                         /**< Whether to use HTTPS/SSL (1 for true, 0 for false) */
        const char* cert_path;               /**< Path to SSL certificate (optional) */
        const char* key_path;                /**< Path to SSL private key (optional) */
        const char* ca_cert_path;            /**< Path to CA certificate for verification (optional) */
        int verify_ssl;                      /**< Whether to verify SSL certificates (1 for true, 0 for false) */
        const char* mcp_endpoint;            /**< MCP endpoint path (default: "/mcp") */
        const char* user_agent;              /**< User-Agent header (optional) */
        const char* api_key;                 /**< API key for authentication (optional) */
        uint32_t connect_timeout_ms;         /**< Connection timeout in milliseconds */
        uint32_t request_timeout_ms;         /**< Request timeout in milliseconds */
        uint32_t sse_reconnect_delay_ms;     /**< SSE reconnection delay in milliseconds */
        uint32_t max_reconnect_attempts;     /**< Maximum SSE reconnection attempts (0 = infinite) */
        int enable_sessions;                 /**< Whether to use session management (1 for true, 0 for false) */
        int enable_sse_streams;              /**< Whether to enable SSE event streams (1 for true, 0 for false) */
        int auto_reconnect_sse;              /**< Whether to automatically reconnect SSE streams (1 for true, 0 for false) */
        const char* custom_headers;          /**< Additional custom headers (format: "Key1: Value1\r\nKey2: Value2") */
    } sthttp_client;

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
