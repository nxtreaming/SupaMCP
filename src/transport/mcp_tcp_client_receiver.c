#include "internal/tcp_client_transport_internal.h"
#include "mcp_framing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mcp_thread_local.h"

// Platform-specific includes needed for socket operations
#ifdef _WIN32
// Included via internal header
#else
#include <unistd.h>
#include <netinet/in.h>
#endif

// Define global flag variable
bool reconnection_in_progress = false;

/**
 * @internal
 * @brief Background thread function responsible for receiving messages from the server.
 * Reads messages using length-prefix framing, calls the message callback for processing,
 * and calls the error callback if fatal transport errors occur.
 * @param arg Pointer to the mcp_transport_t handle.
 * @return NULL on exit.
 */
void* tcp_client_receive_thread_func(void* arg) {
    // --- Initialize Thread-Local Arena for this receiver thread ---
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) {
        mcp_log_error("Failed to initialize thread-local arena for client receiver thread. Exiting.");
        return NULL;
    }
    mcp_log_debug("Thread-local arena initialized for client receiver thread.");

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    uint32_t message_length_host;
    char* message_buf = NULL;
    int frame_result;
    // Track if error callback was already called in this loop iteration
    bool error_signaled = false;

    // Add initial connection health check
    mcp_log_debug("TCP Client receive thread started for socket %d", (int)data->sock);

    // Check if this is a thread startup during reconnection
    if (reconnection_in_progress) {
        mcp_log_info("Skipping initial ping due to reconnection");
        reconnection_in_progress = false;

        // Only enter receive mode, do not send ping
        goto receive_loop;
    }

    // Send ping on first connection
    mcp_log_info("Preparing client ping message...");

    // Ensure connection is established
    if (!data->connected) {
        mcp_log_error("Cannot send ping, socket not connected");
        mcp_arena_destroy_current_thread(); // Clean up arena before exiting
        return NULL;
    }

    // 1. Define standard ping message (without authentication)
    static const char ping_content_no_auth[] = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":{},\"id\":0}";

    // Alternative ping message with API key parameter
    static const char ping_content_with_auth[] = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":{\"apiKey\":\"TEST_API_KEY_123\"},\"id\":0}";

    // Use the version without authentication for servers that don't require it
    const char* ping_content = ping_content_no_auth;
    uint32_t ping_length = (uint32_t)strlen(ping_content);

    // 2. Calculate message length (including NULL terminator)
    const uint32_t content_length = ping_length + 1; // +1 for terminator

    // 3. Convert to network byte order (Big-Endian)
    const uint32_t length_network_order = htonl(content_length);

    // 4. Detailed logging
    mcp_log_debug("Ping message content (%u bytes): '%s'", content_length, ping_content);
    mcp_log_debug("Preparing message with length prefix");
    mcp_log_debug("Length prefix: %02X %02X %02X %02X",
                (unsigned char)((char*)&length_network_order)[0],
                (unsigned char)((char*)&length_network_order)[1],
                (unsigned char)((char*)&length_network_order)[2],
                (unsigned char)((char*)&length_network_order)[3]);

    // 5. Send message using vectored I/O to avoid creating a temporary buffer
    if (data->connected) {
        // Prepare buffers for vectored send
        mcp_iovec_t iov[2];
        int iovcnt = 0;

#ifdef _WIN32
        iov[iovcnt].buf = (char*)&length_network_order;
        iov[iovcnt].len = (ULONG)sizeof(length_network_order);
        iovcnt++;
        iov[iovcnt].buf = (char*)ping_content;
        iov[iovcnt].len = (ULONG)content_length;
        iovcnt++;
#else // POSIX
        iov[iovcnt].iov_base = (char*)&length_network_order;
        iov[iovcnt].iov_len = sizeof(length_network_order);
        iovcnt++;
        iov[iovcnt].iov_base = (char*)ping_content;
        iov[iovcnt].iov_len = content_length;
        iovcnt++;
#endif

        // Send using the socket utility function
        int send_status = mcp_socket_send_vectors(data->sock, iov, iovcnt, NULL);

        if (send_status != 0) {
            mcp_log_error("Failed to send ping message (status: %d)", send_status);
            mcp_arena_destroy_current_thread(); // Clean up arena before exiting
            return NULL;
        }

        mcp_log_info("Ping message sent successfully");
    } else {
        mcp_log_error("Cannot send ping, connection already closed");
        mcp_arena_destroy_current_thread(); // Clean up arena before exiting
        return NULL;
    }

receive_loop:
    // Main message reception loop
    while (data->running) {
        // Check connection status
        if (!data->connected) {
            mcp_log_info("Connection lost, exiting receive thread");
            break;
        }

        // --- 1. Receive Framed Message ---
        mcp_log_debug("Waiting to receive framed message from server...");
        frame_result = mcp_framing_recv_message(
            data->sock,
            &message_buf, // Let framing function allocate buffer
            &message_length_host,
            MAX_MCP_MESSAGE_SIZE,
            NULL // Pass NULL; rely on shutdown() in stop() to unblock recv
        );
        mcp_log_debug("mcp_framing_recv_message returned: %d", frame_result);

        if (frame_result != 0) {
            // Only log/callback during normal operation
            if (data->running) {
                int last_error = mcp_socket_get_last_error();
                mcp_log_error("mcp_framing_recv_message failed for socket %d. Result: %d, Last Error: %d",
                              (int)data->sock, frame_result, last_error);
                if (transport->error_callback && !error_signaled) {
                    // Determine error type based on result/errno if possible
                    transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                    error_signaled = true;
                }
            } else {
                mcp_log_debug("Client receive thread for socket %d interrupted or stopped during framing recv.", (int)data->sock);
            }
            data->connected = false; // Mark as disconnected
            // message_buf should be NULL if framing function failed before allocation
            free(message_buf); // Free buffer if allocated before error
            message_buf = NULL;
            break; // Exit loop on any error/close/abort
        }

        // --- 2. Process received message ---
        // message_buf is allocated by mcp_framing_recv_message and includes null terminator
        mcp_log_debug("Received message from server (%u bytes): '%s'", message_length_host, message_buf);

        if (transport->message_callback != NULL) {
            mcp_log_debug("Calling client message callback...");
            int callback_error_code = 0;
            char* unused_response = transport->message_callback(
                transport->callback_user_data,
                message_buf,
                message_length_host,
                &callback_error_code
            );

            mcp_log_debug("Client message callback returned: error_code=%d, response=%s",
                         callback_error_code,
                         unused_response ? "non-NULL" : "NULL");

            free(unused_response); // Client doesn't need this response

            if (callback_error_code != 0) {
                mcp_log_warn("Client message callback error: %d", callback_error_code);
            }
        } else {
            mcp_log_error("No client message callback registered! Cannot process message.");
        }

        // --- 3. Release message buffer ---
        // Caller (this function) is responsible for freeing the buffer allocated by mcp_framing_recv_message
        free(message_buf);
        message_buf = NULL;
    }

    mcp_log_debug("TCP Client receive thread exiting for socket %d", (int)data->sock);
    data->connected = false;

    // Final check if buffer was released (should be NULL here)
    if (message_buf != NULL) {
        mcp_log_warn("message_buf was not NULL at thread exit, freeing.");
        free(message_buf);
    }

    // --- Cleanup Thread-Local Arena ---
    mcp_arena_destroy_current_thread();
    mcp_log_debug("Thread-local arena cleaned up for client receiver thread.");

    return NULL;
}
