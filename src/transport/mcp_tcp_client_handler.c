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

// Thread function to handle a single client connection
void* tcp_client_handler_thread_func(void* arg) {
    tcp_client_connection_t* client_conn = (tcp_client_connection_t*)arg;

    // --- Initialize Thread-Local Arena for this handler thread ---
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) { // Use a reasonable default size
        mcp_log_error("Failed to initialize thread-local arena for client handler thread. Exiting.");
        // Cannot reliably clean up client_conn if it's invalid here, assume acceptor handles it
        return NULL;
    }
    mcp_log_debug("Thread-local arena initialized for client handler thread.");

    // --- Initial Sanity Check ---
    if (client_conn == NULL || client_conn->socket == MCP_INVALID_SOCKET || client_conn->transport == NULL) {
        mcp_log_error("Client handler started with invalid arguments or socket handle (Socket: %p, Transport: %p). Exiting immediately.",
                    client_conn ? (void*)client_conn->socket : NULL, client_conn ? client_conn->transport : NULL);
        mcp_arena_destroy_current_thread(); // Clean up arena before exiting
        return NULL;
    }
    // --- End Initial Sanity Check ---

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
        // message_buf is allocated by mcp_framing_recv_message and includes null terminator
        if (message_length_host > 0 && message_buf[message_length_host - 1] != '\0') {
            message_buf[message_length_host] = '\0';
            mcp_log_debug("Adding NULL terminator after message body");
        } else if (message_length_host > 0 && message_buf[message_length_host - 1] == '\0') {
            mcp_log_debug("Message already ends with NULL terminator");
        }

        // Check and debug non-printable characters
        size_t effective_length = message_length_host;
        if (message_length_host > 0 && message_buf[message_length_host - 1] == '\0') {
            // Exclude terminator
            effective_length = message_length_host - 1;
        }

        for (size_t i = 0; i < effective_length; i++) {
            if (message_buf[i] < 32 && message_buf[i] != '\t' && message_buf[i] != '\n' && message_buf[i] != '\r') {
                mcp_log_warn("Found control character at position %zu: 0x%02X",
                           i, (unsigned char)message_buf[i]);
                // Replace control characters with spaces to avoid JSON parsing errors
                message_buf[i] = ' ';
            }
        }

        // Log the received message for debugging
        mcp_log_debug("Received message from client: '%.*s'", (int)effective_length, message_buf);

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
            mcp_log_debug("Preparing to send response (length: %zu): '%.*s'",
                         response_len,
                         (int)(response_len > 200 ? 200 : response_len), // Limit log output
                         response_str);

            if (response_len > 0 && response_len <= MAX_MCP_MESSAGE_SIZE) {
                // Re-check state and socket validity before sending
                if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == MCP_INVALID_SOCKET) {
                    mcp_log_debug("Handler %d: Detected non-active state or invalid socket before send.",
                                  (int)client_conn->socket);
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

                // Check stop signal *immediately* after send returns (send_result will be -1 if stopped)
                if (client_conn->should_stop) {
                    mcp_log_debug("Handler %d: Stop signal detected immediately after send.", (int)client_conn->socket);
                    free(response_str);
                    response_str = NULL;
                    goto client_cleanup;
                }

                // Check for non-zero (error/abort)
                if (send_result != 0) {
                    if (!client_conn->should_stop) {
                        int last_error = mcp_socket_get_last_error();
                        mcp_log_error("mcp_framing_send_message failed for socket %d. Result: %d, Last Error: %d",
                                      (int)client_conn->socket, send_result, last_error);
                    } else {
                        mcp_log_debug("mcp_framing_send_message aborted by stop signal for socket %d.",
                                      (int)client_conn->socket);
                    }
                    free(response_str);
                    mcp_log_debug("Handler %d: Exiting due to socket error or stop signal (send).", (int)client_conn->socket);
                    goto client_cleanup;
                }
                // Update activity time after successful send
                client_conn->last_activity_time = time(NULL);
            } else if (response_len > MAX_MCP_MESSAGE_SIZE) {
                mcp_log_error("Response generated by callback is too large (%zu bytes) for socket %d",
                              response_len, (int)client_conn->socket);
                // Don't send, but still need to free the response string
            }
            // Free the original response string after handling it
            free(response_str);
            response_str = NULL;
        } else if (callback_error_code != 0) {
            mcp_log_warn("Message callback indicated error (%d) but returned no response string for socket %d",
                         callback_error_code, (int)client_conn->socket);
            // No response_str to free in this case
        }

        // 8. Release or free the received message buffer AFTER processing and potential send
        // message_buf is always allocated by malloc in mcp_framing_recv_message
        free(message_buf);
        message_buf = NULL;
    }

client_cleanup:
    // --- Cleanup on Exit ---
    mcp_log_debug("Handler %d: Entering cleanup.", (int)client_conn->socket);
    // Free or release buffer if loop exited unexpectedly (should be NULL here)
    if (message_buf != NULL) {
        mcp_log_warn("message_buf was not NULL at handler cleanup, freeing.");
        free(message_buf); // Framing function uses malloc
    }

    // Ensure all resources are properly cleaned up before thread exit
    socket_t sock_to_close = MCP_INVALID_SOCKET;

    // Acquire mutex to update client_conn state
    mcp_mutex_lock(tcp_data->client_mutex);

    // Only clean up if slot state indicates it might still be considered active/initializing by the acceptor
    // Check state under lock
    if (client_conn->state != CLIENT_STATE_INACTIVE) {
        // Save socket to close, avoiding invalid socket operations after lock release
        sock_to_close = client_conn->socket;

        // Mark as invalid socket to prevent other threads from using it
        client_conn->socket = MCP_INVALID_SOCKET;

        // Mark slot as INACTIVE
        client_conn->state = CLIENT_STATE_INACTIVE;

        // Reset thread handle (no need to close/destroy here, as it's managed by mcp_thread_create/join)
        client_conn->thread_handle = 0;

        // Log that client_conn slot is now inactive
        mcp_log_debug("Client connection slot marked as INACTIVE");
    } else {
        // Log if cleanup was skipped because state was already inactive
        // Socket might be invalid here, use cautiously
        mcp_log_debug("Handler %d: Cleanup skipped, state already INACTIVE.", (int)client_conn->socket);
    }

    mcp_mutex_unlock(tcp_data->client_mutex);

    // Close socket (outside of mutex)
    if (sock_to_close != MCP_INVALID_SOCKET) {
#ifdef _WIN32
        mcp_log_debug("Closing client connection socket %p", (void*)sock_to_close);
#else
        mcp_log_debug("Closing client connection socket %d", sock_to_close);
#endif
        mcp_socket_close(sock_to_close);
    }

    // --- Cleanup Thread-Local Arena ---
    mcp_arena_destroy_current_thread();
    mcp_log_debug("Thread-local arena cleaned up for client handler thread.");

    return NULL;
}
