#ifndef MCP_TCP_CLIENT_TRANSPORT_H
#define MCP_TCP_CLIENT_TRANSPORT_H

#include <mcp_transport.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Connection state enumeration for TCP client transport.
 */
typedef enum {
    MCP_CONNECTION_STATE_DISCONNECTED,   /**< Client is disconnected */
    MCP_CONNECTION_STATE_CONNECTING,     /**< Client is attempting to connect */
    MCP_CONNECTION_STATE_CONNECTED,      /**< Client is connected */
    MCP_CONNECTION_STATE_RECONNECTING,   /**< Client is attempting to reconnect */
    MCP_CONNECTION_STATE_FAILED          /**< Connection failed permanently */
} mcp_connection_state_t;

/**
 * @brief Callback function type for connection state changes.
 *
 * @param user_data User data provided when setting the callback.
 * @param state The new connection state.
 * @param attempt The current reconnection attempt number (0 for initial connection).
 */
typedef void (*mcp_connection_state_callback_t)(void* user_data, mcp_connection_state_t state, int attempt);

/**
 * @brief Configuration options for TCP client reconnection.
 */
typedef struct {
    bool enable_reconnect;              /**< Whether to enable automatic reconnection */
    int max_reconnect_attempts;         /**< Maximum number of reconnection attempts (0 = infinite) */
    uint32_t initial_reconnect_delay_ms; /**< Initial delay before first reconnection attempt (ms) */
    uint32_t max_reconnect_delay_ms;     /**< Maximum delay between reconnection attempts (ms) */
    float backoff_factor;               /**< Exponential backoff multiplier (e.g., 2.0 doubles delay each attempt) */
    bool randomize_delay;               /**< Whether to add randomness to delay (helps prevent thundering herd) */
} mcp_reconnect_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default reconnection configuration.
 */
extern const mcp_reconnect_config_t MCP_DEFAULT_RECONNECT_CONFIG;

/**
 * @brief Creates a TCP client transport instance.
 *
 * This transport connects to a specified TCP server host and port.
 * It requires a subsequent call to mcp_transport_start() to establish the
 * connection and start the background receive loop. Communication assumes
 * 4-byte network-order length prefix framing for messages.
 *
 * @param host The hostname or IP address of the server to connect to.
 * @param port The port number on the server to connect to.
 * @return A generic mcp_transport_t handle, or NULL on allocation error.
 *         The caller is responsible for destroying the transport using mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port);

/**
 * @brief Creates a TCP client transport instance with reconnection support.
 *
 * This function creates a TCP client transport with automatic reconnection capabilities.
 * The reconnection behavior is controlled by the provided configuration.
 *
 * @param host The hostname or IP address of the server to connect to.
 * @param port The port number on the server to connect to.
 * @param reconnect_config Reconnection configuration. If NULL, uses default configuration.
 * @return A generic mcp_transport_t handle, or NULL on allocation error.
 */
mcp_transport_t* mcp_tcp_client_create_reconnect(
    const char* host,
    uint16_t port,
    const mcp_reconnect_config_t* reconnect_config
);

/**
 * @brief Sets a callback function to be called when the connection state changes.
 *
 * @param transport The transport handle (must be a TCP client transport).
 * @param callback The callback function to call on connection state changes.
 * @param user_data User data to pass to the callback function.
 * @return 0 on success, non-zero on error.
 */
int mcp_tcp_client_set_connection_state_callback(
    mcp_transport_t* transport,
    mcp_connection_state_callback_t callback,
    void* user_data
);

/**
 * @brief Gets the current connection state of the TCP client transport.
 *
 * @param transport The transport handle (must be a TCP client transport).
 * @return The current connection state, or MCP_CONNECTION_STATE_DISCONNECTED on error.
 */
mcp_connection_state_t mcp_tcp_client_get_connection_state(mcp_transport_t* transport);

/**
 * @brief Manually triggers a reconnection attempt for the TCP client transport.
 *
 * This function can be used to manually trigger a reconnection attempt if the
 * client is currently disconnected. If the client is already connected or a
 * reconnection attempt is in progress, this function has no effect.
 *
 * @param transport The transport handle (must be a TCP client transport).
 * @return 0 on success (reconnection attempt started), non-zero on error.
 */
int mcp_tcp_client_reconnect(mcp_transport_t* transport);

#ifdef __cplusplus
}
#endif

#endif // MCP_TCP_CLIENT_TRANSPORT_H
