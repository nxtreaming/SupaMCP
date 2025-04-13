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
// Use the abstracted signature: void* func(void* arg)
void* tcp_client_receive_thread_func(void* arg) {
    // --- Initialize Thread-Local Arena for this receiver thread ---
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) { // Using 1MB default
        mcp_log_error("Failed to initialize thread-local arena for client receiver thread. Exiting.");
        return NULL; // Indicate error (void* return)
    }
    mcp_log_debug("Thread-local arena initialized for client receiver thread.");

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    uint32_t message_length_host;
    char* message_buf = NULL;
    int frame_result;
    bool error_signaled = false; // Track if error callback was already called in this loop iteration

    // Add initial connection health check
    mcp_log_debug("TCP Client receive thread started for socket %d", (int)data->sock);

    // Wait before sending handshake to ensure server is ready
    mcp_sleep_ms(1000); // Add wait time to ensure server is ready

    // Check if this is a thread startup during reconnection
    if (reconnection_in_progress) {
        mcp_log_info("Skipping initial ping due to reconnection");
        reconnection_in_progress = false; // Reset flag

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

    // 4. Create a single buffer containing length prefix and message content
    size_t total_send_len = 4 + content_length; // 4 byte length prefix + message content (including terminator)
    char* send_buffer = (char*)malloc(total_send_len);

    if (!send_buffer) {
        mcp_log_error("Failed to allocate buffer for ping message");
        mcp_arena_destroy_current_thread(); // Clean up arena before exiting
        return NULL;
    }

    // 5. Fill the buffer with length prefix and message content (including terminator)
    memcpy(send_buffer, &length_network_order, 4);
    memcpy(send_buffer + 4, ping_content, ping_length); // Copy content first
    send_buffer[4 + ping_length] = '\0'; // Explicitly add terminator

    // 6. Detailed logging
    mcp_log_debug("Ping message content (%u bytes): '%s'", content_length, ping_content);
    mcp_log_debug("Preparing combined message (total %zu bytes)", total_send_len);
    mcp_log_debug("Length prefix: %02X %02X %02X %02X",
                (unsigned char)send_buffer[0],
                (unsigned char)send_buffer[1],
                (unsigned char)send_buffer[2],
                (unsigned char)send_buffer[3]);

    // 7. Send complete message using framing function
    if (data->connected) {
        // Note: ping_content includes null terminator via strlen + 1 calculation for content_length
        // Pass NULL for stop_flag as the outer loop checks data->running
        int send_status = mcp_framing_send_message(data->sock, ping_content, content_length, NULL);
        free(send_buffer); // Free the temporary combined buffer

        if (send_status != 0) {
            mcp_log_error("Failed to send ping message using framing (status: %d)", send_status);
            mcp_arena_destroy_current_thread(); // Clean up arena before exiting
            return NULL;
        }

        mcp_log_info("Ping message sent successfully");
    } else {
        free(send_buffer); // Free buffer
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
            if (data->running) { // Only log/callback during normal operation
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
      } // End of main loop

      mcp_log_debug("TCP Client receive thread exiting for socket %d", (int)data->sock);
      data->connected = false;

      // Final check if buffer was released (should be NULL here)
    if (message_buf != NULL) {
        mcp_log_warn("message_buf was not NULL at thread exit, freeing.");
        free(message_buf); // Free if somehow not freed earlier
    }

    // --- Cleanup Thread-Local Arena ---
    mcp_arena_destroy_current_thread();
    mcp_log_debug("Thread-local arena cleaned up for client receiver thread.");

    return NULL; // Return NULL for void* compatibility
}
