/**
 * @file mcp_tcp_client_receiver.c
 * @brief Implementation of TCP client message receiver functionality.
 *
 * This file implements the receiver thread for TCP client connections,
 * which continuously reads messages from the server using length-prefixed
 * framing, processes them via the registered callback, and handles
 * connection errors and reconnection.
 */
#include "internal/tcp_client_transport_internal.h"
#include "mcp_framing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mcp_thread_local.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

// Constants
#define RECEIVER_ARENA_SIZE (1024 * 1024)  // 1MB arena for thread-local memory
#define SELECT_TIMEOUT_SEC 1               // 1 second timeout for select()
#define SELECT_TIMEOUT_USEC 0              // 0 microseconds

// External global flag variable (defined in mcp_tcp_client_reconnect.c)
extern bool reconnection_in_progress;

// Define standard ping messages
static const char PING_MESSAGE_NO_AUTH[] = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":{},\"id\":0}";
static const char PING_MESSAGE_WITH_AUTH[] = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":{\"apiKey\":\"TEST_API_KEY_123\"},\"id\":0}";

/**
 * @brief Send a ping message to the server.
 *
 * This function sends a simple ping message to the server to verify
 * that the connection is working. It uses vectored I/O to efficiently
 * send the length prefix and message content in a single operation.
 *
 * @param data The TCP client transport data
 * @return 0 on success, -1 on failure
 */
static int send_ping_message(mcp_tcp_client_transport_data_t* data) {
    if (!data) {
        mcp_log_error("NULL data parameter in send_ping_message");
        return -1;
    }

    mcp_log_info("Preparing client ping message...");

    // Ensure connection is established
    if (!data->connected || data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Cannot send ping, socket not connected");
        return -1;
    }

    // Use the version without authentication for servers that don't require it
    // (The WITH_AUTH version is available if needed in the future)
    const char* ping_content = PING_MESSAGE_NO_AUTH;
    uint32_t ping_length = (uint32_t)strlen(ping_content);

    // Calculate message length (including NULL terminator)
    const uint32_t content_length = ping_length + 1; // +1 for terminator

    // Convert to network byte order (Big-Endian)
    const uint32_t length_network_order = htonl(content_length);

    // Send message using vectored I/O to avoid creating a temporary buffer
    // This is more efficient than concatenating the length and content
    mcp_iovec_t iov[2];

    // Initialize the iovec array with length prefix and message content
#ifdef _WIN32
    // Windows WSABUF structure
    iov[0].buf = (char*)&length_network_order;
    iov[0].len = (ULONG)sizeof(length_network_order);

    iov[1].buf = (char*)ping_content;
    iov[1].len = (ULONG)content_length;
#else
    // POSIX iovec structure
    iov[0].iov_base = (void*)&length_network_order;
    iov[0].iov_len = sizeof(length_network_order);

    iov[1].iov_base = (void*)ping_content;
    iov[1].iov_len = content_length;
#endif

    // Send using the socket utility function
    mcp_log_debug("Sending ping message (length: %u bytes)", content_length);
    int send_status = mcp_socket_send_vectors(data->sock, iov, 2, NULL);

    if (send_status != 0) {
        int error_code = mcp_socket_get_lasterror();
        mcp_log_error("Failed to send ping message (status: %d, error: %d)",
                     send_status, error_code);
        return -1;
    }

    mcp_log_info("Ping message sent successfully");
    return 0;
}

/**
 * @internal
 * @brief Background thread function responsible for receiving messages from the server.
 *
 * This function runs in a separate thread and continuously reads messages from
 * the server using length-prefix framing. It processes each message by calling
 * the registered message callback and handles connection errors appropriately.
 *
 * The thread uses a thread-local memory arena for efficient memory management
 * and performs periodic health checks to ensure the connection is still active.
 *
 * @param arg Pointer to the mcp_transport_t handle
 * @return NULL on exit
 */
void* tcp_client_receive_thread_func(void* arg) {
    if (!arg) {
        mcp_log_error("NULL argument to receive thread function");
        return NULL;
    }

    // --- Initialize Thread-Local Arena for this receiver thread ---
    if (mcp_arena_init_current_thread(RECEIVER_ARENA_SIZE) != 0) {
        mcp_log_error("Failed to initialize thread-local arena for receiver thread");
        return NULL;
    }
    mcp_log_debug("Thread-local arena initialized for receiver thread (size: %d bytes)",
                 RECEIVER_ARENA_SIZE);

    // Extract transport and data from argument
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (!transport->transport_data) {
        mcp_log_error("Invalid transport data in receiver thread");
        mcp_arena_destroy_current_thread();
        return NULL;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Initialize variables for message reception
    uint32_t message_length_host = 0;
    char* message_buf = NULL;
    int frame_result = 0;

    // Track if error callback was already called in this loop iteration
    // to avoid calling it multiple times for the same error
    bool error_signaled = false;

    // Log thread startup
    mcp_log_info("TCP client receive thread started for socket %d", (int)data->sock);

    // Check if this is a thread startup during reconnection
    if (reconnection_in_progress) {
        mcp_log_info("Skipping initial ping due to reconnection");
        reconnection_in_progress = false;
    } else {
        // Send ping on first connection to verify connectivity
        mcp_log_debug("Sending initial ping message to verify connection");
        if (send_ping_message(data) != 0) {
            mcp_log_error("Failed to send initial ping message, exiting receiver thread");
            mcp_arena_destroy_current_thread();
            return NULL;
        }
    }

    // We use non-blocking socket operations with select() instead of a timeout
    // This allows us to periodically check if we should exit without disconnecting

    // Main message reception loop
    while (data->running) {
        // Check connection status
        if (!data->connected || data->sock == MCP_INVALID_SOCKET) {
            mcp_log_info("Connection lost or invalid socket, exiting receive thread");
            break;
        }

        // --- 1. Use select() to wait for data with a timeout ---
        fd_set read_fds;
        struct timeval tv;

        // Initialize the file descriptor set
        FD_ZERO(&read_fds);
        FD_SET(data->sock, &read_fds);

        // Set the timeout using constants
        tv.tv_sec = SELECT_TIMEOUT_SEC;
        tv.tv_usec = SELECT_TIMEOUT_USEC;

        // Wait for data or timeout
        // The first parameter is the highest file descriptor value plus one
        int select_result = select((int)data->sock + 1, &read_fds, NULL, NULL, &tv);

        // Check if we should exit (running flag may have changed during select)
        if (!data->running) {
            mcp_log_debug("Running flag cleared during select, exiting receive thread");
            break;
        }

        // Check select result
        if (select_result == -1) {
            // Select error
            int last_error = mcp_socket_get_lasterror();

            // Check for interrupted system call (not a real error)
            if (last_error == EINTR) {
                mcp_log_debug("select() interrupted, continuing");
                continue;
            }

            // Handle other select errors
            mcp_log_error("select() failed with error: %d", last_error);
            data->connected = false;

            // Call error callback if set
            if (transport->error_callback && !error_signaled) {
                transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                error_signaled = true;
            }

            break;
        } else if (select_result == 0) {
            // Timeout - no data available
            // Just continue the loop to check if we should exit
            continue;
        }

        // Data is available, receive it
        mcp_log_debug("Data available on socket %d, receiving message", (int)data->sock);

        // Receive the message using length-prefix framing
        frame_result = mcp_framing_recv_message(
            data->sock,
            &message_buf,           // Let framing function allocate buffer
            &message_length_host,   // Will receive the message length
            MAX_MCP_MESSAGE_SIZE,   // Maximum allowed message size
            NULL                    // We don't use stop_flag here
        );

        // Handle receive errors
        if (frame_result != 0) {
            // Get the error code
            int last_error = mcp_socket_get_lasterror();

            // Check if we're still running (not stopped externally)
            if (data->running) {
                // Log the error with details
                mcp_log_error("Failed to receive message on socket %d (result: %d, error: %d)",
                             (int)data->sock, frame_result, last_error);

                // Call error callback if set and not already called
                if (transport->error_callback && !error_signaled) {
                    transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                    error_signaled = true;
                }
            } else {
                // Thread was stopped externally during receive
                mcp_log_debug("Receive thread for socket %d stopped during message reception",
                             (int)data->sock);
            }

            // Mark as disconnected
            data->connected = false;

            // Free message buffer if allocated
            // (should be NULL if framing function failed before allocation)
            if (message_buf != NULL) {
                free(message_buf);
                message_buf = NULL;
            }

            // Exit loop on any error
            break;
        }

        // --- 2. Process received message ---
        // message_buf is allocated by mcp_framing_recv_message and includes null terminator

        // Log a preview of the received message (limit to reasonable length for logs)
        const size_t MAX_LOG_PREVIEW = 100;
        mcp_log_debug("Received message from server (length: %u bytes): '%.*s%s'",
                     message_length_host,
                     (int)(message_length_host > MAX_LOG_PREVIEW ? MAX_LOG_PREVIEW : message_length_host),
                     message_buf,
                     message_length_host > MAX_LOG_PREVIEW ? "..." : "");

        // Process the message via callback if registered
        if (transport->message_callback != NULL) {
            // Initialize error code
            int callback_error_code = 0;

            // Call the message callback
            mcp_log_debug("Calling message callback for received message");
            char* response = transport->message_callback(
                transport->callback_user_data,
                message_buf,
                message_length_host,
                &callback_error_code
            );

            // Log callback result
            if (callback_error_code != 0) {
                mcp_log_warn("Message callback returned error code: %d", callback_error_code);
            } else {
                mcp_log_debug("Message callback completed successfully");
            }

            // Free the response (client doesn't use it)
            if (response != NULL) {
                free(response);
            }
        } else {
            mcp_log_error("No message callback registered, cannot process received message");
        }

        // --- 3. Release message buffer ---
        // We are responsible for freeing the buffer allocated by mcp_framing_recv_message
        free(message_buf);
        message_buf = NULL;

        // Reset error_signaled flag for next iteration
        error_signaled = false;
    }

    // Loop has exited - thread is shutting down
    mcp_log_info("TCP client receive thread exiting for socket %d", (int)data->sock);

    // Mark as disconnected
    data->connected = false;

    // Final check if message buffer was properly released
    if (message_buf != NULL) {
        mcp_log_warn("Message buffer was not properly freed, cleaning up");
        free(message_buf);
        message_buf = NULL;
    }

    // --- Cleanup Thread-Local Arena ---
    mcp_arena_destroy_current_thread();
    mcp_log_debug("Thread-local arena cleaned up for receiver thread");

    return NULL;
}
