#ifndef MCP_TCP_POOL_TRANSPORT_H
#define MCP_TCP_POOL_TRANSPORT_H

#include <mcp_transport.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a TCP client transport instance that uses a connection pool.
 *
 * This transport connects to a specified TCP server host and port using a pool of
 * connections. It requires a subsequent call to mcp_transport_start() to establish
 * the connections and start the transport. Communication assumes 4-byte network-order
 * length prefix framing for messages.
 *
 * @param host The hostname or IP address of the server to connect to.
 * @param port The port number on the server to connect to.
 * @param min_connections The minimum number of connections to maintain in the pool.
 * @param max_connections The maximum number of connections allowed in the pool.
 * @param idle_timeout_ms The maximum time (in milliseconds) an idle connection can remain
 *                        in the pool before being closed. 0 means no timeout.
 * @param connect_timeout_ms Timeout (in milliseconds) for establishing a single new connection.
 * @param request_timeout_ms Timeout (in milliseconds) for the complete request-response cycle.
 * @return A generic mcp_transport_t handle, or NULL on allocation error.
 *         The caller is responsible for destroying the transport using mcp_transport_destroy().
 */
mcp_transport_t* mcp_tcp_pool_transport_create(
    const char* host,
    uint16_t port,
    size_t min_connections,
    size_t max_connections,
    int idle_timeout_ms,
    int connect_timeout_ms,
    int request_timeout_ms
);

#ifdef __cplusplus
}
#endif

#endif // MCP_TCP_POOL_TRANSPORT_H
