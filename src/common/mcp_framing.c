#include "mcp_framing.h"
#include "mcp_log.h"
#include "mcp_memory_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Platform-specific includes for byte order conversion
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

/**
 * @brief Helper function to allocate memory, using memory pool if available.
 *
 * @param size Size of memory to allocate in bytes.
 * @return Pointer to allocated memory, or NULL on failure.
 */
static void* framing_alloc(size_t size) {
    if (mcp_memory_pool_system_is_initialized()) {
        return mcp_pool_alloc(size);
    } else {
        return malloc(size);
    }
}

/**
 * @brief Helper function to free memory allocated with framing_alloc.
 *
 * @param ptr Pointer to memory to free.
 */
static void framing_free(void* ptr) {
    if (!ptr) return;

    if (mcp_memory_pool_system_is_initialized()) {
        mcp_pool_free(ptr);
    } else {
        free(ptr);
    }
}

/**
 * @brief Helper function to handle socket errors in a consistent way.
 *        Only used in mcp_framing_recv_message().
 *
 * @param error_code The socket error code
 * @param stop_flag Pointer to a flag indicating if the operation was intentionally stopped
 * @param read_result The result of the socket read operation
 * @param context_message Additional context message to include in the log
 * @param additional_info Additional information to include in warning logs (can be NULL)
 * @return void
 */
static void handle_socket_error(int error_code, volatile bool* stop_flag, int read_result,
                               const char* context_message, const char* additional_info) {
    // Special case: error code 0 during shutdown is normal
    if (error_code == 0) {
        // This is a common case during normal shutdown
        mcp_log_debug("mcp_framing_recv_message: Socket closed during %s (error: 0)", context_message);
    }
#ifdef _WIN32
    else if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN ||
        error_code == WSAENOTCONN || error_code == WSAECONNABORTED) {
        // Normal socket close during shutdown, log as debug
        mcp_log_debug("mcp_framing_recv_message: Socket closed/reset during %s (error: %d)",
                     context_message, error_code);
    }
#else
    else if (error_code == ECONNRESET || error_code == ENOTCONN) {
        // Normal socket close during shutdown, log as debug
        mcp_log_debug("mcp_framing_recv_message: Socket closed/reset during %s (error: %d)",
                     context_message, error_code);
    }
#endif
    else if (stop_flag && *stop_flag) {
        // It's an intentional stop, log as debug
        mcp_log_debug("mcp_framing_recv_message: Aborted while %s", context_message);
    } else {
        // It's an unexpected error, log as warn
        if (additional_info) {
            mcp_log_warn("mcp_framing_recv_message: Failed to %s (result: %d, error: %d, %s)",
                        context_message, read_result, error_code, additional_info);
        } else {
            mcp_log_warn("mcp_framing_recv_message: Failed to %s (result: %d, error: %d)",
                        context_message, read_result, error_code);
        }
    }
}

int mcp_framing_send_message(socket_t sock, const char* message_data, uint32_t message_len,
    volatile bool* stop_flag) {
    if (message_data == NULL && message_len > 0) {
        mcp_log_error("mcp_framing_send_message: message_data is NULL but message_len > 0");
        return -1;
    }
    if (message_len == 0) {
        mcp_log_warn("mcp_framing_send_message: Attempting to send zero-length message.");
        // Decide if sending only length prefix 0 is desired or an error.
        // For now, let's send it.
    }

    // Prepare length prefix in network byte order
    uint32_t net_len = htonl(message_len);

    // Prepare buffers for vectored send
    mcp_iovec_t iov[2];
    int iovcnt = 0;

#ifdef _WIN32
    iov[iovcnt].buf = (char*)&net_len;
    iov[iovcnt].len = (ULONG)sizeof(net_len);
    iovcnt++;
    if (message_len > 0) {
        iov[iovcnt].buf = (char*)message_data;
        iov[iovcnt].len = (ULONG)message_len;
        iovcnt++;
    }
#else
    iov[iovcnt].iov_base = (char*)&net_len;
    iov[iovcnt].iov_len = sizeof(net_len);
    iovcnt++;
    if (message_len > 0) {
        iov[iovcnt].iov_base = (char*)message_data;
        iov[iovcnt].iov_len = message_len;
        iovcnt++;
    }
#endif

    // Send using the socket utility function
    int result = mcp_socket_send_vectors(sock, iov, iovcnt, stop_flag);

    if (result != 0) {
        mcp_log_error("mcp_framing_send_message: mcp_socket_send_vectors failed (result: %d)", result);
        return -1;
    }

    return 0;
}

int mcp_framing_recv_message(socket_t sock, char** message_data_out, uint32_t* message_len_out,
    uint32_t max_message_size, volatile bool* stop_flag) {
    if (message_data_out == NULL || message_len_out == NULL) {
        mcp_log_error("mcp_framing_recv_message: Output parameters cannot be NULL.");
        return -1;
    }

    *message_data_out = NULL;
    *message_len_out = 0;

    char length_buf[4];
    uint32_t message_length_net, message_length_host;

    // 1. Read the 4-byte length prefix
    int read_result = mcp_socket_recv_exact(sock, length_buf, 4, stop_flag);
    if (read_result != 0) {
        int error_code = mcp_socket_get_lasterror();
        handle_socket_error(error_code, stop_flag, read_result, "reading length prefix", NULL);
        return -1; // Error, connection closed, or aborted
    }

    // 2. Decode length (Network to Host Byte Order)
    // Avoid memcpy by directly reading from the buffer
    message_length_net = *((uint32_t*)length_buf);
    message_length_host = ntohl(message_length_net);

    // 3. Sanity Check Length
    if (message_length_host == 0) {
        mcp_log_warn("mcp_framing_recv_message: Received zero-length message.");
        // Return success, but with NULL data and 0 length
        *message_data_out = NULL;
        *message_len_out = 0;
        return 0;
    }
    if (message_length_host > max_message_size) {
        mcp_log_error("mcp_framing_recv_message: Received message length %u exceeds maximum %u",
                      message_length_host, max_message_size);
        // TODO: Should we try to read and discard the oversized message?
        // For now, return error and let the caller handle the broken state.
        return -1;
    }

    // 4. Allocate Buffer for Message Body (+1 for potential null terminator)
    // Note: The framing layer itself doesn't guarantee null termination,
    // but allocating +1 makes it easier for JSON/string layers above.
    char* message_buf = (char*)malloc(message_length_host + 1);
    if (message_buf == NULL) {
        mcp_log_error("mcp_framing_recv_message: Failed to allocate buffer for message size %u", message_length_host);
        return -1;
    }

    // 5. Read the Message Body
    read_result = mcp_socket_recv_exact(sock, message_buf, message_length_host, stop_flag);
    if (read_result != 0) {
        int error_code = mcp_socket_get_lasterror();

        // Format additional info about message length
        char additional_info[64];
        snprintf(additional_info, sizeof(additional_info), "length: %u", message_length_host);

        handle_socket_error(error_code, stop_flag, read_result, "reading message body", additional_info);

        free(message_buf);
        // Error, connection closed, or aborted
        return -1;
    }

    // Add null terminator for convenience, even though framing doesn't strictly require it
    message_buf[message_length_host] = '\0';

    // 6. Set output parameters
    *message_data_out = message_buf;
    *message_len_out = message_length_host;

    return 0;
}

int mcp_framing_send_batch(socket_t sock, const char** messages, const uint32_t* lengths, size_t count, volatile bool* stop_flag) {
    if (sock == MCP_INVALID_SOCKET || (count > 0 && (messages == NULL || lengths == NULL))) {
        mcp_log_error("mcp_framing_send_batch: Invalid parameters");
        return -1;
    }

    if (count == 0) {
        // Nothing to send, return success
        return 0;
    }

    // Calculate total number of iovec entries needed (2 per message: length + data)
    size_t total_iovecs = 0;
    for (size_t i = 0; i < count; i++) {
        // Each message needs 1 iovec for length, and 1 for data if length > 0
        total_iovecs += (lengths[i] > 0) ? 2 : 1;
    }

    // Allocate iovec array
    mcp_iovec_t* iov = (mcp_iovec_t*)framing_alloc(total_iovecs * sizeof(mcp_iovec_t));
    if (iov == NULL) {
        mcp_log_error("mcp_framing_send_batch: Failed to allocate iovec array");
        return -1;
    }

    // Allocate a single buffer for all length prefixes
    // This ensures the memory remains valid during the entire send operation
    char* length_buffer = (char*)framing_alloc(count * sizeof(uint32_t));
    if (length_buffer == NULL) {
        mcp_log_error("mcp_framing_send_batch: Failed to allocate length buffer");
        framing_free(iov);
        return -1;
    }

    // Fill iovec array
    size_t iov_index = 0;
    for (size_t i = 0; i < count; i++) {
        // Convert length to network byte order and store directly in the buffer
        uint32_t net_length = htonl(lengths[i]);
        memcpy(length_buffer + (i * sizeof(uint32_t)), &net_length, sizeof(uint32_t));

        // Add length prefix to iovec
#ifdef _WIN32
        iov[iov_index].buf = length_buffer + (i * sizeof(uint32_t));
        iov[iov_index].len = (ULONG)sizeof(uint32_t);
#else
        iov[iov_index].iov_base = length_buffer + (i * sizeof(uint32_t));
        iov[iov_index].iov_len = sizeof(uint32_t);
#endif
        iov_index++;

        // Add message data to iovec if length > 0
        if (lengths[i] > 0) {
#ifdef _WIN32
            iov[iov_index].buf = (char*)messages[i];
            iov[iov_index].len = (ULONG)lengths[i];
#else
            iov[iov_index].iov_base = (char*)messages[i];
            iov[iov_index].iov_len = lengths[i];
#endif
            iov_index++;
        }
    }

    // Send using vectored I/O
    int result = mcp_socket_send_vectors(sock, iov, (int)iov_index, stop_flag);

    // Free allocated memory - only after the send operation is complete
    framing_free(length_buffer);
    framing_free(iov);

    if (result != 0) {
        mcp_log_error("mcp_framing_send_batch: mcp_socket_send_vectors failed (result: %d)", result);
        return -1;
    }

    return 0;
}

int mcp_framing_recv_batch(socket_t sock, char** messages_out, uint32_t* lengths_out, size_t max_count, size_t* count_out, uint32_t max_message_size, volatile bool* stop_flag) {
    if (sock == MCP_INVALID_SOCKET || messages_out == NULL || lengths_out == NULL || count_out == NULL || max_count == 0) {
        mcp_log_error("mcp_framing_recv_batch: Invalid parameters");
        return -1;
    }

    // Initialize count
    *count_out = 0;

    // Try to receive up to max_count messages
    for (size_t i = 0; i < max_count; i++) {
        // Check if we should stop
        if (stop_flag && *stop_flag) {
            mcp_log_info("mcp_framing_recv_batch: Stopped by flag after receiving %zu messages", i);
            return 0;
        }

        // Try to receive a message
        char* message = NULL;
        uint32_t length = 0;

        // Use non-blocking mode for all but the first message
        // This allows us to receive as many messages as are available without blocking
        if (i > 0) {
            // Check if there's data available to read using mcp_socket_wait_readable with 0 timeout (non-blocking)
            int available = mcp_socket_wait_readable(sock, 0, stop_flag);
            if (available <= 0) {
                // No data available, timeout, error, or stopped by flag
                break;
            }
        }

        // Receive the message
        int result = mcp_framing_recv_message(sock, &message, &length, max_message_size, stop_flag);
        if (result != 0) {
            // If this is the first message, it's an error
            // If we've already received some messages, it's OK to stop here
            if (i == 0) {
                mcp_log_error("mcp_framing_recv_batch: Failed to receive first message");
                return -1;
            } else {
                break;
            }
        }

        // Store the message and length
        messages_out[i] = message;
        lengths_out[i] = length;
        (*count_out)++;
    }

    return 0;
}