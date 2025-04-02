#ifndef MCP_TCP_TRANSPORT_H
#define MCP_TCP_TRANSPORT_H

#include <mcp_transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a transport layer instance that listens for incoming TCP connections.
 *
 * This transport acts as a TCP server, listening on the specified host and port.
 * It handles multiple client connections concurrently (implementation detail, likely using threads).
 * Received messages (using 4-byte network-order length prefix framing) from any
 * connected client are passed to the message callback provided during mcp_transport_start.
 *
 * @param host The hostname or IP address to bind to (e.g., "0.0.0.0" for all interfaces, "127.0.0.1" for localhost).
 * @param port The port number to listen on.
 * @param idle_timeout_ms Idle connection timeout in milliseconds (0 to disable).
 * @return A pointer to the created transport instance, or NULL on failure.
 *         The caller is responsible for destroying the transport using mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_tcp_create(const char* host, uint16_t port, uint32_t idle_timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // MCP_TCP_TRANSPORT_H
