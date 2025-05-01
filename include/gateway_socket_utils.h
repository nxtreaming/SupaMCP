#ifndef MCP_GATEWAY_SOCKET_UTILS_H
#define MCP_GATEWAY_SOCKET_UTILS_H

#include "mcp_connection_pool.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sends a length-prefixed message over a socket.
 *
 * Prepends a 4-byte network-order length prefix to the message before sending.
 *
 * @param sock The socket to send on.
 * @param message The null-terminated message string to send.
 * @param timeout_ms Send timeout in milliseconds (0 for default/blocking).
 * @return 0 on success, -1 on socket error, -2 on timeout.
 */
int gateway_send_message(SOCKET sock, const char* message, int timeout_ms);

/**
 * @brief Receives a length-prefixed message from a socket.
 *
 * Reads the 4-byte length prefix, allocates memory for the message,
 * and reads the message body.
 *
 * @param sock The socket to receive from.
 * @param[out] message_out Pointer to store the allocated message string. Caller must free this.
 * @param[out] message_len_out Pointer to store the length of the received message (excluding null terminator).
 * @param max_size Maximum allowed message size to prevent excessive allocation.
 * @param timeout_ms Receive timeout in milliseconds (0 for default/blocking).
 * @return 0 on success, -1 on socket error, -2 on timeout, -3 on connection closed, -4 on invalid length/size error.
 */
int gateway_receive_message(SOCKET sock, char** message_out, size_t* message_len_out, size_t max_size, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // MCP_GATEWAY_SOCKET_UTILS_H
