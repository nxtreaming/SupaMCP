#ifndef MCP_FRAMING_H
#define MCP_FRAMING_H

#include "mcp_socket_utils.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sends a message preceded by its 4-byte network-order length.
 *
 * This function takes a data buffer, calculates its length, prepends the
 * 4-byte length in network byte order (big-endian), and sends both the
 * length and the data over the socket using vectored I/O if possible.
 *
 * @param sock The socket descriptor to send the message on.
 * @param message_data The buffer containing the message payload.
 * @param message_len The length of the message payload in bytes.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error (e.g., socket error, invalid length) or if aborted by stop_flag.
 */
int mcp_framing_send_message(socket_t sock, const char* message_data, uint32_t message_len, volatile bool* stop_flag);

/**
 * @brief Receives a message preceded by its 4-byte network-order length.
 *
 * This function first reads the 4-byte length prefix, decodes it from network
 * byte order, allocates a buffer of the appropriate size, and then reads the
 * message payload into the allocated buffer.
 *
 * The caller is responsible for freeing the allocated buffer pointed to by `message_data_out`.
 *
 * @param sock The socket descriptor to receive the message from.
 * @param message_data_out Pointer to a char* that will be set to the newly allocated buffer containing the message payload.
 * @param message_len_out Pointer to a uint32_t that will be set to the length of the received message payload.
 * @param max_message_size The maximum allowed message size to prevent excessive memory allocation.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error (e.g., socket error, connection closed, invalid length, allocation failure) or if aborted by stop_flag.
 */
int mcp_framing_recv_message(socket_t sock, char** message_data_out, uint32_t* message_len_out, uint32_t max_message_size, volatile bool* stop_flag);


#ifdef __cplusplus
}
#endif

#endif // MCP_FRAMING_H
