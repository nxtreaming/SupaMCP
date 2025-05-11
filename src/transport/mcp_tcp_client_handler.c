#include "internal/transport_internal.h"
#include "internal/tcp_transport_internal.h"
#include "mcp_framing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "mcp_thread_local.h"
#include "mcp_log.h"
#include "mcp_buffer_pool.h"
#include "mcp_types.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

/**
 * @brief Thread function to handle a single client connection.
 *
 * This function runs in a separate thread and handles the communication with a client.
 * It receives messages from the client, processes them via the registered callback,
 * and sends responses back to the client.
 *
 * The function uses length-prefixed framing for message exchange and supports idle timeout.
 * It also handles thread-local memory arenas for efficient memory management.
 *
 * @param arg Pointer to the client connection structure (tcp_client_connection_t*)
 * @return NULL when the thread exits
 */
void* tcp_client_handler_thread_func(void* arg) {
    tcp_client_connection_t* client_conn = (tcp_client_connection_t*)arg;

    // --- Initial Sanity Check (before arena initialization) ---
    if (client_conn == NULL || client_conn->socket == MCP_INVALID_SOCKET || client_conn->transport == NULL) {
        mcp_log_error("Client handler started with invalid arguments or socket handle (Socket: %p, Transport: %p). Exiting immediately.",
                    client_conn ? (void*)client_conn->socket : NULL, client_conn ? client_conn->transport : NULL);
        return NULL;
    }

    // --- Initialize Thread-Local Arena for this handler thread ---
    // Use a 1MB arena size for efficient memory management
    const size_t ARENA_SIZE = 1024 * 1024;
    if (mcp_arena_init_current_thread(ARENA_SIZE) != 0) {
        mcp_log_error("Failed to initialize thread-local arena for client handler thread. Exiting.");
        return NULL;
    }

    mcp_log_debug("Thread-local arena initialized for client handler thread (size: %zu bytes).", ARENA_SIZE);

    mcp_transport_t* transport = client_conn->transport;
    mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)transport->transport_data;
    uint32_t message_length_host;
    char* message_buf = NULL;
    int frame_result;
    client_conn->should_stop = false;
    client_conn->last_activity_time = time(NULL);

#ifdef _WIN32
    mcp_log_debug("Client handler started for socket %p", (void*)client_conn->socket);
#else
    mcp_log_debug("Client handler started for socket %d", client_conn->socket);
#endif

    // Main receive loop with length prefix framing and idle timeout
    while (!client_conn->should_stop && client_conn->state == CLIENT_STATE_ACTIVE) {
        // === Add extra check for socket validity at start of loop ===
        mcp_mutex_lock(tcp_data->client_mutex);
        bool still_active = (client_conn->state == CLIENT_STATE_ACTIVE && client_conn->socket != MCP_INVALID_SOCKET);
        mcp_mutex_unlock(tcp_data->client_mutex);

        if (!still_active) {
            mcp_log_debug("Handler %d: Detected inactive state or invalid socket at start of loop. Exiting.", (int)client_conn->socket);
            break; // Exit loop cleanly
        }
        // === End extra check ===

        // --- Calculate Deadline for Idle Timeout ---
        time_t deadline = 0;
        if (tcp_data->idle_timeout_ms > 0) {
            deadline = client_conn->last_activity_time + (tcp_data->idle_timeout_ms / 1000) + ((tcp_data->idle_timeout_ms % 1000) > 0 ? 1 : 0);
        }

        // --- 1. Wait for Data or Timeout ---
        // Start with full timeout for select/poll
        uint32_t wait_ms = tcp_data->idle_timeout_ms;
        if (wait_ms == 0)
            wait_ms = 30000; // If idle timeout is not set, use 30 seconds as default

        if (client_conn->socket == MCP_INVALID_SOCKET) {
            mcp_log_debug("Exiting handler thread for invalid socket");
            goto client_cleanup;
        }

        // Re-check state and socket validity before blocking call
        if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == MCP_INVALID_SOCKET) {
            mcp_log_debug("Handler %d: Detected non-active state or invalid socket before wait.", (int)client_conn->socket);
            goto client_cleanup;
        }

        // Returns: 1 if readable, 0 if timeout, -1 on error or if aborted by stop_flag.
        int wait_result = mcp_socket_wait_readable(client_conn->socket, (int)wait_ms, &client_conn->should_stop);

        // Check stop signal *immediately* after wait returns (wait_result will be -1 if stopped)
        if (client_conn->should_stop) {
            mcp_log_debug("Handler %d: Stop signal detected immediately after wait.", (int)client_conn->socket);
            goto client_cleanup;
        }

        // Check wait_result based on new function's return values
        if (wait_result == -1) {
            // Error or stop signal
            // Avoid logging error if we are stopping anyway
            if (!client_conn->should_stop) {
                int last_error = mcp_socket_get_last_error();
                mcp_log_error("mcp_socket_wait_readable failed for socket %d: Error %d", (int)client_conn->socket, last_error);
            } else {
                mcp_log_debug("mcp_socket_wait_readable aborted by stop signal for socket %d.", (int)client_conn->socket);
            }
            mcp_log_debug("Handler %d: Exiting due to socket error or stop signal (wait).", (int)client_conn->socket);
            goto client_cleanup;
        } else if (wait_result == 0) {
            // Timeout
            if (tcp_data->idle_timeout_ms > 0 && time(NULL) >= deadline) {
                mcp_log_info("Idle timeout exceeded for socket %d. Closing connection.", (int)client_conn->socket);
                mcp_log_debug("Handler %d: Exiting due to idle timeout.", (int)client_conn->socket);
                goto client_cleanup;
            }
            // If no idle timeout configured, or deadline not reached, just loop again
            continue;
        }
        // else: wait_result == 1 (socket is readable)

        // --- 2. Receive Framed Message ---
        mcp_log_debug("Attempting to receive framed message on socket %d...", (int)client_conn->socket);
        frame_result = mcp_framing_recv_message(
            client_conn->socket,
            &message_buf, // Let framing function allocate buffer
            &message_length_host,
            MAX_MCP_MESSAGE_SIZE,
            &client_conn->should_stop
        );
        mcp_log_debug("mcp_framing_recv_message returned: %d", frame_result);

        if (frame_result != 0) {
            // Avoid logging error if we are stopping anyway
            if (!client_conn->should_stop) {
                int last_error = mcp_socket_get_last_error();
                mcp_log_error("mcp_framing_recv_message failed for socket %d. Result: %d, Last Error: %d",
                              (int)client_conn->socket, frame_result, last_error);
            } else {
                mcp_log_debug("mcp_framing_recv_message aborted by stop signal for socket %d.", (int)client_conn->socket);
            }
            // message_buf should be NULL if framing function failed before allocation
            // Free buffer if allocated before error
            free(message_buf);
            message_buf = NULL;
            mcp_log_debug("Handler %d: Exiting due to framing error or stop signal.", (int)client_conn->socket);
            // Exit loop on any error/close/abort
            goto client_cleanup;
        }

        // Update activity time after successful receive
        client_conn->last_activity_time = time(NULL);

        // --- 3. Process Message via Callback ---
        // Ensure message is null-terminated for string operations
        bool has_null_terminator = false;

        // Check if message already has a null terminator
        if (message_length_host > 0) {
            if (message_buf[message_length_host - 1] == '\0') {
                has_null_terminator = true;
                mcp_log_debug("Message already ends with NULL terminator");
            } else {
                // Add null terminator after message body
                message_buf[message_length_host] = '\0';
                mcp_log_debug("Adding NULL terminator after message body");
            }
        }

        // Calculate effective length (excluding null terminator if present)
        size_t effective_length = message_length_host;
        if (has_null_terminator && message_length_host > 0) {
            effective_length = message_length_host - 1;
        }

        // Sanitize message by replacing control characters with spaces
        // This helps prevent JSON parsing errors and other issues
        for (size_t i = 0; i < effective_length; i++) {
            unsigned char c = (unsigned char)message_buf[i];
            // Allow tab, newline, and carriage return, but replace other control chars
            if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
                mcp_log_warn("Sanitizing control character at position %zu: 0x%02X", i, c);
                message_buf[i] = ' ';
            }
        }

        // Log a preview of the received message (limit to reasonable length for logs)
        const size_t MAX_LOG_PREVIEW = 200;
        mcp_log_debug("Received message from client (length: %zu): '%.*s%s'",
                     effective_length,
                     (int)(effective_length > MAX_LOG_PREVIEW ? MAX_LOG_PREVIEW : effective_length),
                     message_buf,
                     effective_length > MAX_LOG_PREVIEW ? "..." : "");

        char* response_str = NULL;
        int callback_error_code = 0;
        if (transport->message_callback != NULL) {
            response_str = transport->message_callback(transport->callback_user_data, message_buf,
                                                       message_length_host, &callback_error_code);
            mcp_log_debug("Message callback returned: error_code=%d, response=%s",
                         callback_error_code,
                         response_str ? "non-NULL" : "NULL");
        } else {
            mcp_log_error("No message callback registered! Cannot process message.");
        }

        // --- 4. Send Response (if any) ---
        if (response_str != NULL) {
            size_t response_len = strlen(response_str);

            // Log a preview of the response (limit to reasonable length for logs)
            // Reuse the same MAX_LOG_PREVIEW constant defined above
            mcp_log_debug("Preparing to send response (length: %zu): '%.*s%s'",
                         response_len,
                         (int)(response_len > MAX_LOG_PREVIEW ? MAX_LOG_PREVIEW : response_len),
                         response_str,
                         response_len > MAX_LOG_PREVIEW ? "..." : "");

            // Validate response size
            if (response_len == 0) {
                mcp_log_warn("Empty response generated by callback, skipping send");
                free(response_str);
                response_str = NULL;
            }
            else if (response_len > MAX_MCP_MESSAGE_SIZE) {
                mcp_log_error("Response too large (%zu bytes, max: %d) for socket %d",
                             response_len, MAX_MCP_MESSAGE_SIZE, (int)client_conn->socket);
                free(response_str);
                response_str = NULL;
            }
            else {
                // Re-check state and socket validity before sending
                if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == MCP_INVALID_SOCKET) {
                    mcp_log_debug("Handler %d: Connection no longer active before send", (int)client_conn->socket);
                    free(response_str);
                    response_str = NULL;
                    goto client_cleanup;
                }

                // Use framing function to send response
                int send_result = mcp_framing_send_message(
                    client_conn->socket,
                    response_str,
                    (uint32_t)response_len,
                    &client_conn->should_stop
                );

                // Check stop signal immediately after send
                if (client_conn->should_stop) {
                    mcp_log_debug("Handler %d: Stop signal detected after send", (int)client_conn->socket);
                    free(response_str);
                    response_str = NULL;
                    goto client_cleanup;
                }

                // Check for send errors
                if (send_result != 0) {
                    if (!client_conn->should_stop) {
                        int last_error = mcp_socket_get_last_error();
                        mcp_log_error("Send failed on socket %d. Result: %d, Error: %d",
                                     (int)client_conn->socket, send_result, last_error);
                    } else {
                        mcp_log_debug("Send aborted by stop signal on socket %d", (int)client_conn->socket);
                    }
                    free(response_str);
                    response_str = NULL;
                    goto client_cleanup;
                }

                // Update activity time after successful send
                client_conn->last_activity_time = time(NULL);

                // Update statistics
                tcp_stats_update_message_sent(&tcp_data->stats, response_len);

                // Free the response string after successful send
                free(response_str);
                response_str = NULL;
            }
        }
        else if (callback_error_code != 0) {
            mcp_log_warn("Callback error (%d) with no response for socket %d",
                        callback_error_code, (int)client_conn->socket);
        }

        // 8. Release or free the received message buffer AFTER processing and potential send
        // message_buf is always allocated by malloc in mcp_framing_recv_message
        free(message_buf);
        message_buf = NULL;
    }

client_cleanup:
    // --- Cleanup on Exit ---
    mcp_log_debug("Handler %d: Entering cleanup", (int)client_conn->socket);

    // Free message buffer if still allocated
    if (message_buf != NULL) {
        mcp_log_warn("Freeing message buffer that was not properly released");
        free(message_buf);
        message_buf = NULL;
    }

    // Socket handling with proper synchronization
    socket_t sock_to_close = MCP_INVALID_SOCKET;
    int client_index = -1;

    // Critical section - update client state under lock
    mcp_mutex_lock(tcp_data->client_mutex);

    if (client_conn->state != CLIENT_STATE_INACTIVE) {
        // Save socket and client index for operations outside the lock
        sock_to_close = client_conn->socket;
        client_index = client_conn->client_index;

        // Update connection state
        client_conn->socket = MCP_INVALID_SOCKET;
        client_conn->state = CLIENT_STATE_INACTIVE;

        // Update statistics
        tcp_stats_update_connection_closed(&tcp_data->stats);

        mcp_log_debug("Client connection slot %d marked as INACTIVE", client_index);
    } else {
        mcp_log_debug("Client connection already inactive, skipping cleanup");
    }

    mcp_mutex_unlock(tcp_data->client_mutex);

    // Close socket outside of critical section
    if (sock_to_close != MCP_INVALID_SOCKET) {
        mcp_log_info("Closing client connection from %s:%d (socket %d, slot %d)",
                    client_conn->client_ip, client_conn->client_port,
                    (int)sock_to_close, client_index);

        // Shutdown the socket before closing
#ifdef _WIN32
        shutdown(sock_to_close, SD_BOTH);
#else
        shutdown(sock_to_close, SHUT_RDWR);
#endif
        mcp_socket_close(sock_to_close);
    }

    // Clean up thread-local memory arena
    mcp_arena_destroy_current_thread();
    mcp_log_debug("Thread-local arena cleaned up for client handler");

    return NULL;
}
