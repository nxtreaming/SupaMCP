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

/**
 * @brief Sends multiple messages in a batch operation.
 *
 * This function takes an array of message buffers and their lengths, and sends them
 * all over the socket in a single batch operation. Each message is framed with its
 * 4-byte length prefix in network byte order.
 *
 * @param sock The socket descriptor to send the messages on.
 * @param messages Array of message buffers.
 * @param lengths Array of message lengths.
 * @param count Number of messages to send.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error (e.g., socket error, invalid parameters) or if aborted by stop_flag.
 */
int mcp_framing_send_batch(socket_t sock, const char** messages, const uint32_t* lengths, size_t count, volatile bool* stop_flag);

/**
 * @brief Receives multiple messages in a batch operation.
 *
 * This function attempts to receive up to 'max_count' messages from the socket.
 * Each message is expected to be framed with a 4-byte length prefix in network byte order.
 * The function will stop receiving messages when the socket has no more data available,
 * or when 'max_count' messages have been received.
 *
 * @param sock The socket descriptor to receive messages from.
 * @param messages_out Array to store received message buffers. Must be pre-allocated with at least max_count entries.
 * @param lengths_out Array to store received message lengths. Must be pre-allocated with at least max_count entries.
 * @param max_count Maximum number of messages to receive.
 * @param count_out Pointer to store the actual number of messages received.
 * @param max_message_size The maximum allowed message size to prevent excessive memory allocation.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error (e.g., socket error, connection closed, invalid length, allocation failure) or if aborted by stop_flag.
 */
int mcp_framing_recv_batch(socket_t sock, char** messages_out, uint32_t* lengths_out, size_t max_count, size_t* count_out, uint32_t max_message_size, volatile bool* stop_flag);

#ifdef __cplusplus
}
#endif

#endif // MCP_FRAMING_H
