/**
 * @file mcp_http_streamable_transport.h
 * @brief Streamable HTTP Transport for MCP Protocol 2025-03-26
 *
 * This header defines the Streamable HTTP transport implementation
 * as specified in the MCP 2025-03-26 protocol specification.
 */
#ifndef MCP_HTTP_STREAMABLE_TRANSPORT_H
#define MCP_HTTP_STREAMABLE_TRANSPORT_H

#include "mcp_transport.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for Streamable HTTP transport
 */
typedef struct mcp_http_streamable_config {
    const char* host;         /**< Host to bind to (e.g., "127.0.0.1" for localhost only) */
    uint16_t port;            /**< Port to listen on */
    bool use_ssl;             /**< Whether to use HTTPS */
    const char* cert_path;    /**< Path to SSL certificate file (for HTTPS) */
    const char* key_path;     /**< Path to SSL private key file (for HTTPS) */
    const char* doc_root;     /**< Document root for serving static files (optional) */
    uint32_t timeout_ms;      /**< Connection timeout in milliseconds (0 to disable) */
    
    // MCP endpoint configuration
    const char* mcp_endpoint; /**< MCP endpoint path (default: "/mcp") */
    
    // Session management
    bool enable_sessions;     /**< Whether to enable session management */
    uint32_t session_timeout_seconds; /**< Session timeout in seconds (0 for default) */
    
    // Security settings
    bool validate_origin;     /**< Whether to validate Origin header (recommended for security) */
    const char* allowed_origins; /**< Comma-separated list of allowed origins (e.g., "http://localhost:3000,https://example.com") */
    
    // CORS settings
    bool enable_cors;                /**< Whether to enable CORS */
    const char* cors_allow_origin;   /**< Allowed origins for CORS (e.g., "*" for all) */
    const char* cors_allow_methods;  /**< Allowed methods for CORS (e.g., "GET, POST, OPTIONS, DELETE") */
    const char* cors_allow_headers;  /**< Allowed headers for CORS */
    int cors_max_age;                /**< Max age for CORS preflight requests in seconds */
    
    // SSE settings
    bool enable_sse_resumability;    /**< Whether to enable SSE stream resumability */
    uint32_t max_stored_events;      /**< Maximum number of events to store for resumability */
    bool send_heartbeats;            /**< Whether to send SSE heartbeats */
    uint32_t heartbeat_interval_ms;  /**< Heartbeat interval in milliseconds */
    
    // Backwards compatibility
    bool enable_legacy_endpoints;    /**< Whether to enable legacy HTTP+SSE endpoints for backwards compatibility */
} mcp_http_streamable_config_t;

/**
 * @brief Create a Streamable HTTP server transport
 *
 * This creates a server transport that implements the Streamable HTTP
 * transport as specified in MCP 2025-03-26.
 *
 * @param config Streamable HTTP transport configuration
 * @return mcp_transport_t* Transport handle or NULL on failure
 */
mcp_transport_t* mcp_transport_http_streamable_create(const mcp_http_streamable_config_t* config);

/**
 * @brief Create a Streamable HTTP client transport
 *
 * This creates a client transport that can connect to a Streamable HTTP
 * server implementing MCP 2025-03-26.
 *
 * @param host Host to connect to
 * @param port Port to connect to
 * @param mcp_endpoint MCP endpoint path (e.g., "/mcp")
 * @param use_ssl Whether to use HTTPS
 * @param api_key API key for authentication (optional)
 * @return mcp_transport_t* Transport handle or NULL on failure
 */
mcp_transport_t* mcp_transport_http_streamable_client_create(const char* host, 
                                                           uint16_t port,
                                                           const char* mcp_endpoint,
                                                           bool use_ssl,
                                                           const char* api_key);

/**
 * @brief Send a message with session context
 *
 * This function allows sending a message with an associated session ID
 * for the Streamable HTTP transport.
 *
 * @param transport Transport handle
 * @param data Message data to send
 * @param size Size of the message data
 * @param session_id Session ID to associate with the message (optional)
 * @return int 0 on success, non-zero on error
 */
int mcp_transport_http_streamable_send_with_session(mcp_transport_t* transport, 
                                                   const void* data, 
                                                   size_t size,
                                                   const char* session_id);

/**
 * @brief Get the MCP endpoint path for this transport
 *
 * @param transport Transport handle
 * @return const char* MCP endpoint path or NULL if not a streamable HTTP transport
 */
const char* mcp_transport_http_streamable_get_endpoint(mcp_transport_t* transport);

/**
 * @brief Check if the transport supports sessions
 *
 * @param transport Transport handle
 * @return bool true if sessions are supported and enabled, false otherwise
 */
bool mcp_transport_http_streamable_has_sessions(mcp_transport_t* transport);

/**
 * @brief Get active session count
 *
 * @param transport Transport handle
 * @return size_t Number of active sessions, or 0 if sessions not supported
 */
size_t mcp_transport_http_streamable_get_session_count(mcp_transport_t* transport);

/**
 * @brief Terminate a specific session
 *
 * @param transport Transport handle
 * @param session_id Session ID to terminate
 * @return bool true if session was found and terminated, false otherwise
 */
bool mcp_transport_http_streamable_terminate_session(mcp_transport_t* transport, const char* session_id);

/**
 * @brief Default configuration initializer
 *
 * This macro provides default values for the Streamable HTTP transport configuration.
 */
#define MCP_HTTP_STREAMABLE_CONFIG_DEFAULT { \
    .host = "127.0.0.1", \
    .port = 8080, \
    .use_ssl = false, \
    .cert_path = NULL, \
    .key_path = NULL, \
    .doc_root = NULL, \
    .timeout_ms = 30000, \
    .mcp_endpoint = "/mcp", \
    .enable_sessions = true, \
    .session_timeout_seconds = 3600, \
    .validate_origin = true, \
    .allowed_origins = "http://localhost:*,https://localhost:*", \
    .enable_cors = true, \
    .cors_allow_origin = "*", \
    .cors_allow_methods = "GET, POST, OPTIONS, DELETE", \
    .cors_allow_headers = "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID", \
    .cors_max_age = 86400, \
    .enable_sse_resumability = true, \
    .max_stored_events = 1000, \
    .send_heartbeats = true, \
    .heartbeat_interval_ms = 30000, \
    .enable_legacy_endpoints = true \
}

#ifdef __cplusplus
}
#endif

#endif /* MCP_HTTP_STREAMABLE_TRANSPORT_H */
