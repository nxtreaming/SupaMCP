#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// Thread function to handle a single client connection
#ifdef _WIN32
DWORD WINAPI tcp_client_handler_thread_func(LPVOID arg) {
#else
void* tcp_client_handler_thread_func(void* arg) {
#endif
    tcp_client_connection_t* client_conn = (tcp_client_connection_t*)arg;

    // --- Initial Sanity Check ---
    // Check if the handler was started with invalid arguments or an already invalid socket
    if (client_conn == NULL || client_conn->socket == INVALID_SOCKET_VAL) {
        mcp_log_error("Client handler started with invalid arguments or socket handle (Socket: %p). Exiting immediately.",
                    client_conn ? (void*)client_conn->socket : NULL);
        // Cannot reliably clean up if client_conn is NULL.
        // If socket is invalid, the acceptor or another thread likely already handled cleanup.
#ifdef _WIN32
        return 1; // Indicate error
#else
        return NULL; // Indicate error
#endif
    }
    // --- End Initial Sanity Check ---

    mcp_transport_t* transport = client_conn->transport;
    mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)transport->transport_data;
    char length_buf[4];
    uint32_t message_length_net, message_length_host;
    char* message_buf = NULL;
    bool buffer_malloced = false; // Flag to track if we used malloc fallback
    int read_result;
    client_conn->should_stop = false; // Initialize stop flag for this handler
    client_conn->last_activity_time = time(NULL); // Initialize activity time

#ifdef _WIN32
    mcp_log_debug("Client handler started for socket %p", (void*)client_conn->socket);
#else
    mcp_log_debug("Client handler started for socket %d", client_conn->socket);
#endif

    // Main receive loop with length prefix framing and idle timeout
    // Loop while the state is ACTIVE and no stop signal is received
    while (!client_conn->should_stop && client_conn->state == CLIENT_STATE_ACTIVE) {

        // --- Calculate Deadline for Idle Timeout ---
        time_t deadline = 0;
        if (tcp_data->idle_timeout_ms > 0) {
            deadline = client_conn->last_activity_time + (tcp_data->idle_timeout_ms / 1000) + ((tcp_data->idle_timeout_ms % 1000) > 0 ? 1 : 0);
        }


        // --- 1. Wait for Data or Timeout ---
        uint32_t wait_ms = tcp_data->idle_timeout_ms; // Start with full timeout for select/poll
        if (wait_ms == 0) wait_ms = 30000; // If idle timeout is not set, use 30 seconds as default
        
        //log_message(LOG_LEVEL_DEBUG, "Waiting for data on socket %d (hex: %p), timeout: %u ms...", 
        //           (int)client_conn->socket, (void*)client_conn->socket, wait_ms);

        if (client_conn->socket == INVALID_SOCKET_VAL) {
            // Socket is invalid, exit the handler thread immediately
            mcp_log_debug("Exiting handler thread for invalid socket");
            goto client_cleanup;
        }
        
        // Re-check state and socket validity before blocking call
        if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == INVALID_SOCKET_VAL) {
            mcp_log_debug("Handler %d: Detected non-active state or invalid socket before wait.", (int)client_conn->socket);
            goto client_cleanup;
        }

        // Use wait_for_socket_read from mcp_tcp_socket_utils.c
        int wait_result = wait_for_socket_read(client_conn->socket, wait_ms, &client_conn->should_stop);
        //log_message(LOG_LEVEL_DEBUG, "Wait result for socket %d: %d", (int)client_conn->socket, wait_result);

        // Check stop signal *immediately* after wait returns
        if (client_conn->should_stop) {
             mcp_log_debug("Handler %d: Stop signal detected immediately after wait.", (int)client_conn->socket);
             goto client_cleanup;
        }

        if (wait_result == -2) { // Stop signal (redundant check, but safe)
             mcp_log_debug("Client handler for socket %d interrupted by stop signal (wait_result == -2).", (int)client_conn->socket);
             goto client_cleanup;
        } else if (wait_result == -1) { // Socket error
             if (!client_conn->should_stop) { // Avoid logging error if we are stopping anyway
                 char err_buf[128];
#ifdef _WIN32
                 strerror_s(err_buf, sizeof(err_buf), sock_errno);
                 mcp_log_error("wait_for_socket_read failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                 if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                     mcp_log_error("wait_for_socket_read failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                 } else {
                     mcp_log_error("wait_for_socket_read failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                 }
#endif
             }
             mcp_log_debug("Handler %d: Exiting due to socket error (wait).", (int)client_conn->socket);
             goto client_cleanup;
        } else if (wait_result == 0) { // Timeout from select/poll
             if (tcp_data->idle_timeout_ms > 0 && time(NULL) >= deadline) {
                 mcp_log_info("Idle timeout exceeded for socket %d. Closing connection.", (int)client_conn->socket);
                 mcp_log_debug("Handler %d: Exiting due to idle timeout.", (int)client_conn->socket);
                 goto client_cleanup;
             }
             // If no idle timeout configured, or deadline not reached, just loop again
             continue;
        }
        // else: wait_result == 1 (socket is readable)


        // --- 2. Read the 4-byte Length Prefix ---
        mcp_log_debug("Attempting to read 4-byte length prefix from socket %d (hex: %p)...", 
                   (int)client_conn->socket, (void*)client_conn->socket);
        
        // Check if socket is valid before reading
        if (client_conn->socket == INVALID_SOCKET_VAL) {
            mcp_log_error("Socket is invalid before read attempt!");
            goto client_cleanup;
        }

        // Re-check state and socket validity before reading
        if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == INVALID_SOCKET_VAL) {
            mcp_log_debug("Handler %d: Detected non-active state or invalid socket before recv.", (int)client_conn->socket);
            goto client_cleanup;
        }
        
        // Use recv_exact from mcp_tcp_socket_utils.c
        read_result = recv_exact(client_conn->socket, length_buf, 4, &client_conn->should_stop);
        mcp_log_debug("recv_exact (len=4) returned: %d, sock_errno: %d", 
                   read_result, sock_errno);
        
        // Check stop signal *immediately* after recv returns
        if (client_conn->should_stop) {
             mcp_log_debug("Handler %d: Stop signal detected immediately after recv (length).", (int)client_conn->socket);
             goto client_cleanup;
        }

        if (read_result == -1) { // Socket error
             if (!client_conn->should_stop) { // Avoid logging error if we are stopping anyway
                 int error_code = sock_errno; // Get error code immediately
                 mcp_log_error("recv_exact (len=4) failed with error code: %d", error_code);
                char err_buf[128];
#ifdef _WIN32
                strerror_s(err_buf, sizeof(err_buf), sock_errno);
                mcp_log_error("recv (length) failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                    mcp_log_error("recv (length) failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                } else {
                    mcp_log_error("recv (length) failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                }
#endif
             }
             mcp_log_debug("Handler %d: Exiting due to socket error (recv length).", (int)client_conn->socket);
             goto client_cleanup;
        } else if (read_result == -3) { // Connection closed
#ifdef _WIN32
             mcp_log_info("Client disconnected socket %p while reading length", (void*)client_conn->socket);
#else
             mcp_log_info("Client disconnected socket %d while reading length", client_conn->socket);
#endif
             mcp_log_debug("Handler %d: Exiting due to client disconnect (recv length).", (int)client_conn->socket);
             goto client_cleanup;
        } else if (read_result == -2) { // Stop signal
             mcp_log_debug("Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             mcp_log_debug("Handler %d: Exiting due to stop signal (recv length).", (int)client_conn->socket);
             goto client_cleanup;
        } else if (read_result != 0) { // Should be 0 on success from recv_exact
             mcp_log_error("recv_exact (length) returned unexpected code %d for socket %d", read_result, (int)client_conn->socket);
             goto client_cleanup;
        }

        // Update activity time after successful read
        client_conn->last_activity_time = time(NULL);


        // --- 3. Decode Length (Network to Host Byte Order) ---
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);
        mcp_log_debug("Received length bytes: %02X %02X %02X %02X -> net=0x%08X -> host=%u (0x%X)",
                    (unsigned char)length_buf[0], (unsigned char)length_buf[1], (unsigned char)length_buf[2], (unsigned char)length_buf[3],
                    message_length_net, message_length_host, message_length_host);


        // --- 4. Sanity Check Length ---
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             mcp_log_error("Invalid message length received: %u (0x%X) on socket %d", message_length_host, message_length_host, (int)client_conn->socket);
             mcp_log_debug("Handler %d: Exiting due to invalid message length.", (int)client_conn->socket);
             goto client_cleanup; // Invalid length, close connection
        }
        mcp_log_debug("Validated message length: %u", message_length_host);


        // --- 5. Allocate Buffer for Message Body (Pool or Malloc) ---
        size_t required_size = message_length_host + 1; // +1 for null terminator
        size_t pool_buffer_size = mcp_buffer_pool_get_buffer_size(tcp_data->buffer_pool);

        if (required_size <= pool_buffer_size) {
            message_buf = (char*)mcp_buffer_pool_acquire(tcp_data->buffer_pool);
            if (message_buf != NULL) buffer_malloced = false;
            else {
                mcp_log_warn("Buffer pool empty, falling back to malloc for %zu bytes on socket %d", required_size, (int)client_conn->socket);
                message_buf = (char*)malloc(required_size);
                buffer_malloced = true;
            }
        } else {
            mcp_log_warn("Message size %u exceeds pool buffer size %zu, using malloc on socket %d", message_length_host, pool_buffer_size, (int)client_conn->socket);
            message_buf = (char*)malloc(required_size);
            buffer_malloced = true;
        }

        if (message_buf == NULL) {
             mcp_log_error("Failed to allocate buffer for message size %u on socket %d", message_length_host, (int)client_conn->socket);
             mcp_log_debug("Handler %d: Exiting due to buffer allocation failure.", (int)client_conn->socket);
             goto client_cleanup; // Allocation failure
        }


        // --- 6. Read the Message Body ---
        // Re-check state and socket validity before reading body
        if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == INVALID_SOCKET_VAL) {
            mcp_log_debug("Handler %d: Detected non-active state or invalid socket before recv body.", (int)client_conn->socket);
            // Need to free message_buf before exiting
            if (message_buf != NULL) { // Check if buffer was allocated before freeing
               if (buffer_malloced) free(message_buf); else mcp_buffer_pool_release(tcp_data->buffer_pool, message_buf);
            }
            message_buf = NULL;
            goto client_cleanup;
        }
        // Use recv_exact from mcp_tcp_socket_utils.c
        read_result = recv_exact(client_conn->socket, message_buf, message_length_host, &client_conn->should_stop);

        // Check stop signal *immediately* after recv returns
        if (client_conn->should_stop) {
             mcp_log_debug("Handler %d: Stop signal detected immediately after recv (body).", (int)client_conn->socket);
             // Need to free message_buf before exiting
             if (buffer_malloced) free(message_buf); else mcp_buffer_pool_release(tcp_data->buffer_pool, message_buf);
             message_buf = NULL;
             goto client_cleanup;
        }

         if (read_result == -1) { // Socket error
             if (!client_conn->should_stop) {
                char err_buf[128];
#ifdef _WIN32
                strerror_s(err_buf, sizeof(err_buf), sock_errno);
                mcp_log_error("recv (body) failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                    mcp_log_error("recv (body) failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                } else {
                    mcp_log_error("recv (body) failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                }
#endif
             }
             mcp_log_debug("Handler %d: Exiting due to socket error (recv body).", (int)client_conn->socket);
             goto client_cleanup;
         } else if (read_result == -3) { // Connection closed
#ifdef _WIN32
             mcp_log_info("Client disconnected socket %p while reading body", (void*)client_conn->socket);
#else
             mcp_log_info("Client disconnected socket %d while reading body", client_conn->socket);
#endif
             mcp_log_debug("Handler %d: Exiting due to client disconnect (recv body).", (int)client_conn->socket);
             goto client_cleanup;
         } else if (read_result == -2) { // Stop signal
             mcp_log_debug("Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             mcp_log_debug("Handler %d: Exiting due to stop signal (recv body).", (int)client_conn->socket);
             goto client_cleanup;
         } else if (read_result != 0) { // Should be 0 on success
             mcp_log_error("Incomplete message body received (%d/%u bytes) for socket %d", read_result, message_length_host, (int)client_conn->socket);
             goto client_cleanup;
         }

        // Update activity time after successful read
        client_conn->last_activity_time = time(NULL);


        // --- 7. Null-terminate and Process Message via Callback ---
        // Check if terminator already exists, add one if not
        if (message_length_host > 0 && message_buf[message_length_host - 1] != '\0') {
            message_buf[message_length_host] = '\0'; // Add terminator
            mcp_log_debug("Adding NULL terminator after message body");
        } else if (message_length_host > 0 && message_buf[message_length_host - 1] == '\0') {
            mcp_log_debug("Message already ends with NULL terminator");
        }
        
        // Check and debug non-printable characters
        size_t effective_length = message_length_host;
        if (message_length_host > 0 && message_buf[message_length_host - 1] == '\0') {
            effective_length = message_length_host - 1; // Exclude terminator
        }
        
        for (size_t i = 0; i < effective_length; i++) {
            if (message_buf[i] < 32 && message_buf[i] != '\t' && message_buf[i] != '\n' && message_buf[i] != '\r') {
                mcp_log_warn("Found control character at position %zu: 0x%02X", 
                           i, (unsigned char)message_buf[i]);
                // Replace control characters with spaces to avoid JSON parsing errors
                message_buf[i] = ' ';
            }
        }
        
        char* response_str = NULL;
        int callback_error_code = 0;
        if (transport->message_callback != NULL) {
            response_str = transport->message_callback(transport->callback_user_data, message_buf, message_length_host, &callback_error_code);
        }

        // 8. Release or free the received message buffer (before potential send)
        if (message_buf != NULL) {
            if (buffer_malloced) free(message_buf);
            else mcp_buffer_pool_release(tcp_data->buffer_pool, message_buf);
            message_buf = NULL;
        }


        // --- 9. Send Response (if any) ---
        if (response_str != NULL) {
            size_t response_len = strlen(response_str);
            if (response_len > 0 && response_len <= MAX_MCP_MESSAGE_SIZE) {
                // Add 4-byte length prefix (network byte order)
                uint32_t net_len = htonl((uint32_t)response_len);
                size_t total_send_len = sizeof(net_len) + response_len;
                char* send_buffer = (char*)malloc(total_send_len);

                if (send_buffer) {
                    memcpy(send_buffer, &net_len, sizeof(net_len));
                    memcpy(send_buffer + sizeof(net_len), response_str, response_len);

                    // Re-check state and socket validity before sending
                    if (client_conn->state != CLIENT_STATE_ACTIVE || client_conn->socket == INVALID_SOCKET_VAL) {
                        mcp_log_debug("Handler %d: Detected non-active state or invalid socket before send.", (int)client_conn->socket);
                        free(send_buffer);
                        // Need to free response_str before exiting
                        free(response_str);
                        response_str = NULL;
                        goto client_cleanup;
                    }

                    // Use send_exact from mcp_tcp_socket_utils.c
                    int send_result = send_exact(client_conn->socket, send_buffer, total_send_len, &client_conn->should_stop);
                    free(send_buffer); // Free combined buffer regardless of result

                    // Check stop signal *immediately* after send returns
                    if (client_conn->should_stop) {
                         mcp_log_debug("Handler %d: Stop signal detected immediately after send.", (int)client_conn->socket);
                         // Need to free response_str before exiting
                         free(response_str);
                         response_str = NULL;
                         goto client_cleanup;
                    }

                    if (send_result == -1) { // Socket error
                        if (!client_conn->should_stop) {
                            char err_buf[128];
#ifdef _WIN32
                            strerror_s(err_buf, sizeof(err_buf), sock_errno);
                            mcp_log_error("send_exact failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                            if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                                mcp_log_error("send_exact failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                            } else {
                                mcp_log_error("send_exact failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                            }
#endif
                        }
                        free(response_str); // Free original response string on error
                        mcp_log_debug("Handler %d: Exiting due to socket error (send).", (int)client_conn->socket);
                        goto client_cleanup;
                    } else if (send_result == -2) { // Stop signal
                         free(response_str); // Free original response string on stop
                         mcp_log_debug("Handler %d: Exiting due to stop signal (send).", (int)client_conn->socket);
                         goto client_cleanup;
                    } else if (send_result == -3) { // Connection closed
                         mcp_log_info("Client disconnected socket %d during send", (int)client_conn->socket);
                         mcp_log_debug("Handler %d: Exiting due to client disconnect (send).", (int)client_conn->socket);
                         free(response_str); // Free original response string on disconnect
                         goto client_cleanup;
                    }
                    // Update activity time after successful send
                    client_conn->last_activity_time = time(NULL);
                } else {
                    mcp_log_error("Failed to allocate send buffer for response on socket %d", (int)client_conn->socket);
                    free(response_str); // Free original response string on alloc failure
                    mcp_log_debug("Handler %d: Exiting due to send buffer allocation failure.", (int)client_conn->socket);
                    goto client_cleanup;
                }
            } else if (response_len > MAX_MCP_MESSAGE_SIZE) {
                 mcp_log_error("Response generated by callback is too large (%zu bytes) for socket %d", response_len, (int)client_conn->socket);
                 // Don't send, but still need to free the response string
            }
            // Free the original response string after handling it
            free(response_str);
            response_str = NULL;
        } else if (callback_error_code != 0) {
            mcp_log_warn("Message callback indicated error (%d) but returned no response string for socket %d", callback_error_code, (int)client_conn->socket);
            // No response_str to free in this case
        }

    } // End of main while loop

client_cleanup:
    // --- Cleanup on Exit ---
    mcp_log_debug("Handler %d: Entering cleanup.", (int)client_conn->socket);
    // Free or release buffer if loop exited unexpectedly
    if (message_buf != NULL) {
        if (buffer_malloced) free(message_buf);
        else mcp_buffer_pool_release(tcp_data->buffer_pool, message_buf);
    }

    // Ensure all resources are properly cleaned up before thread exit
    socket_t sock_to_close = INVALID_SOCKET_VAL;
    
    // Acquire mutex to update client_conn state
#ifdef _WIN32
    EnterCriticalSection(&tcp_data->client_mutex);
#else
    pthread_mutex_lock(&tcp_data->client_mutex);
#endif

    // Only clean up if slot state indicates it might still be considered active/initializing by the acceptor
    // Check state under lock
    if (client_conn->state != CLIENT_STATE_INACTIVE) {
        // Save socket to close, avoiding invalid socket operations after lock release
        sock_to_close = client_conn->socket;

        // Mark as invalid socket to prevent other threads from using it
        client_conn->socket = INVALID_SOCKET_VAL;

        // Mark slot as INACTIVE
        client_conn->state = CLIENT_STATE_INACTIVE;

#ifdef _WIN32
        // Handle Windows thread handle (CloseHandle should be safe even if NULL)
        if (client_conn->thread_handle) {
            CloseHandle(client_conn->thread_handle);
            client_conn->thread_handle = NULL;
        }
#else
        client_conn->thread_handle = 0;
#endif

        // Log that client_conn slot is now inactive
        mcp_log_debug("Client connection slot marked as INACTIVE");
    } else {
        // Log if cleanup was skipped because state was already inactive
        mcp_log_debug("Handler %d: Cleanup skipped, state already INACTIVE.", (int)client_conn->socket); // Socket might be invalid here, use cautiously
    }

#ifdef _WIN32
    LeaveCriticalSection(&tcp_data->client_mutex);
#else
    pthread_mutex_unlock(&tcp_data->client_mutex);
#endif

    // Close socket (outside of mutex)
    if (sock_to_close != INVALID_SOCKET_VAL) {
#ifdef _WIN32
        mcp_log_debug("Closing client connection socket %p", (void*)sock_to_close);
#else
        mcp_log_debug("Closing client connection socket %d", sock_to_close);
#endif
        close_socket(sock_to_close);
    }


#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
