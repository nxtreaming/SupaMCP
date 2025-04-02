#ifndef MCP_TCP_CLIENT_TRANSPORT_H
#define MCP_TCP_CLIENT_TRANSPORT_H

#include <mcp_transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // MCP_TCP_CLIENT_TRANSPORT_H
