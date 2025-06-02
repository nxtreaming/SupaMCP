/**
 * @file mcp_sthttp_client_transport.h
 * @brief HTTP Streamable Client Transport for MCP 2025-03-26
 *
 * This header defines the client-side implementation of the Streamable HTTP
 * transport as specified in MCP 2025-03-26. It provides functionality for
 * connecting to MCP servers via HTTP POST requests and SSE streams.
 */
#ifndef MCP_STHTTP_CLIENT_TRANSPORT_H
#define MCP_STHTTP_CLIENT_TRANSPORT_H

#include "mcp_transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for HTTP Streamable client transport
 */
typedef struct {
    const char* host;                    /**< Server hostname or IP address */
    uint16_t port;                       /**< Server port number */
    bool use_ssl;                        /**< Whether to use HTTPS/SSL */
    const char* cert_path;               /**< Path to SSL certificate (optional) */
    const char* key_path;                /**< Path to SSL private key (optional) */
    const char* ca_cert_path;            /**< Path to CA certificate for verification (optional) */
    bool verify_ssl;                     /**< Whether to verify SSL certificates */
    
    const char* mcp_endpoint;            /**< MCP endpoint path (default: "/mcp") */
    const char* user_agent;              /**< User-Agent header (optional) */
    const char* api_key;                 /**< API key for authentication (optional) */
    
    uint32_t connect_timeout_ms;         /**< Connection timeout in milliseconds */
    uint32_t request_timeout_ms;         /**< Request timeout in milliseconds */
    uint32_t sse_reconnect_delay_ms;     /**< SSE reconnection delay in milliseconds */
    uint32_t max_reconnect_attempts;     /**< Maximum SSE reconnection attempts (0 = infinite) */
    
    bool enable_sessions;                /**< Whether to use session management */
    bool enable_sse_streams;             /**< Whether to enable SSE event streams */
    bool auto_reconnect_sse;             /**< Whether to automatically reconnect SSE streams */
    
    const char* custom_headers;          /**< Additional custom headers (format: "Key1: Value1\r\nKey2: Value2") */
} mcp_sthttp_client_config_t;

/**
 * @brief Default configuration for HTTP Streamable client transport
 */
#define MCP_STHTTP_CLIENT_CONFIG_DEFAULT { \
    .host = "localhost", \
    .port = 8080, \
    .use_ssl = false, \
    .cert_path = NULL, \
    .key_path = NULL, \
    .ca_cert_path = NULL, \
    .verify_ssl = true, \
    .mcp_endpoint = "/mcp", \
    .user_agent = "SupaMCP-Client/1.0", \
    .api_key = NULL, \
    .connect_timeout_ms = 10000, \
    .request_timeout_ms = 30000, \
    .sse_reconnect_delay_ms = 5000, \
    .max_reconnect_attempts = 10, \
    .enable_sessions = true, \
    .enable_sse_streams = true, \
    .auto_reconnect_sse = true, \
    .custom_headers = NULL \
}

/**
 * @brief Connection state for the client transport
 */
typedef enum {
    MCP_CLIENT_STATE_DISCONNECTED,      /**< Not connected */
    MCP_CLIENT_STATE_CONNECTING,        /**< Connecting to server */
    MCP_CLIENT_STATE_CONNECTED,         /**< Connected and ready */
    MCP_CLIENT_STATE_SSE_CONNECTING,    /**< Establishing SSE stream */
    MCP_CLIENT_STATE_SSE_CONNECTED,     /**< SSE stream active */
    MCP_CLIENT_STATE_RECONNECTING,      /**< Reconnecting after failure */
    MCP_CLIENT_STATE_ERROR              /**< Error state */
} mcp_client_connection_state_t;

/**
 * @brief Client connection statistics
 */
typedef struct {
    uint64_t requests_sent;              /**< Total requests sent */
    uint64_t responses_received;         /**< Total responses received */
    uint64_t sse_events_received;        /**< Total SSE events received */
    uint64_t reconnect_attempts;         /**< Total reconnection attempts */
    uint64_t connection_errors;          /**< Total connection errors */
    time_t last_request_time;            /**< Timestamp of last request */
    time_t last_sse_event_time;          /**< Timestamp of last SSE event */
    time_t connection_start_time;        /**< Timestamp when connection was established */
} mcp_client_connection_stats_t;

/**
 * @brief Callback for connection state changes
 *
 * @param transport The client transport instance
 * @param old_state Previous connection state
 * @param new_state New connection state
 * @param user_data User data passed during callback registration
 */
typedef void (*mcp_client_state_callback_t)(
    mcp_transport_t* transport,
    mcp_client_connection_state_t old_state,
    mcp_client_connection_state_t new_state,
    void* user_data
);

/**
 * @brief Callback for SSE events
 *
 * @param transport The client transport instance
 * @param event_id Event ID (may be NULL)
 * @param event_type Event type (may be NULL)
 * @param data Event data
 * @param user_data User data passed during callback registration
 */
typedef void (*mcp_client_sse_event_callback_t)(
    mcp_transport_t* transport,
    const char* event_id,
    const char* event_type,
    const char* data,
    void* user_data
);

/**
 * @brief Create HTTP Streamable client transport
 *
 * @param config Configuration for the client transport
 * @return Pointer to the created transport, or NULL on failure
 */
mcp_transport_t* mcp_transport_sthttp_client_create(const mcp_sthttp_client_config_t* config);

/**
 * @brief Get current connection state
 *
 * @param transport The client transport instance
 * @return Current connection state
 */
mcp_client_connection_state_t mcp_sthttp_client_get_state(mcp_transport_t* transport);

/**
 * @brief Get connection statistics
 *
 * @param transport The client transport instance
 * @param stats Pointer to structure to fill with statistics
 * @return 0 on success, non-zero on error
 */
int mcp_sthttp_client_get_stats(mcp_transport_t* transport, mcp_client_connection_stats_t* stats);

/**
 * @brief Get current session ID (if sessions are enabled)
 *
 * @param transport The client transport instance
 * @return Session ID string, or NULL if no session or sessions disabled
 */
const char* mcp_sthttp_client_get_session_id(mcp_transport_t* transport);

/**
 * @brief Set connection state change callback
 *
 * @param transport The client transport instance
 * @param callback Callback function to call on state changes
 * @param user_data User data to pass to callback
 * @return 0 on success, non-zero on error
 */
int mcp_sthttp_client_set_state_callback(
    mcp_transport_t* transport,
    mcp_client_state_callback_t callback,
    void* user_data
);

/**
 * @brief Set SSE event callback
 *
 * @param transport The client transport instance
 * @param callback Callback function to call on SSE events
 * @param user_data User data to pass to callback
 * @return 0 on success, non-zero on error
 */
int mcp_sthttp_client_set_sse_callback(
    mcp_transport_t* transport,
    mcp_client_sse_event_callback_t callback,
    void* user_data
);

/**
 * @brief Force reconnection of SSE stream
 *
 * @param transport The client transport instance
 * @return 0 on success, non-zero on error
 */
int mcp_sthttp_client_reconnect_sse(mcp_transport_t* transport);

/**
 * @brief Terminate current session (if sessions are enabled)
 *
 * @param transport The client transport instance
 * @return 0 on success, non-zero on error
 */
int mcp_sthttp_client_terminate_session(mcp_transport_t* transport);

/**
 * @brief Enable or disable automatic SSE reconnection
 *
 * @param transport The client transport instance
 * @param enable Whether to enable automatic reconnection
 * @return 0 on success, non-zero on error
 */
int mcp_sthttp_client_set_auto_reconnect(mcp_transport_t* transport, bool enable);

#ifdef __cplusplus
}
#endif

#endif // MCP_STHTTP_CLIENT_TRANSPORT_H
