#include "mcp_tcp_transport_internal.h"
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
    log_message(LOG_LEVEL_DEBUG, "Client handler started for socket %p", (void*)client_conn->socket);
#else
    log_message(LOG_LEVEL_DEBUG, "Client handler started for socket %d", client_conn->socket);
#endif

    // Main receive loop with length prefix framing and idle timeout
    while (!client_conn->should_stop && client_conn->active) {

        // --- Calculate Deadline for Idle Timeout ---
        time_t deadline = 0;
        if (tcp_data->idle_timeout_ms > 0) {
            deadline = client_conn->last_activity_time + (tcp_data->idle_timeout_ms / 1000) + ((tcp_data->idle_timeout_ms % 1000) > 0 ? 1 : 0);
        }


        // --- 1. Wait for Data or Timeout ---
        uint32_t wait_ms = tcp_data->idle_timeout_ms; // Start with full timeout for select/poll
        if (wait_ms == 0) wait_ms = 500; // If no idle timeout, check stop flag periodically

        // Use wait_for_socket_read from mcp_tcp_socket_utils.c
        int wait_result = wait_for_socket_read(client_conn->socket, wait_ms, &client_conn->should_stop);

        if (wait_result == -2) { // Stop signal
             log_message(LOG_LEVEL_DEBUG, "Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             goto client_cleanup;
        } else if (wait_result == -1) { // Socket error
             if (!client_conn->should_stop) {
                 char err_buf[128];
#ifdef _WIN32
                 strerror_s(err_buf, sizeof(err_buf), sock_errno);
                 log_message(LOG_LEVEL_ERROR, "wait_for_socket_read failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                 if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                     log_message(LOG_LEVEL_ERROR, "wait_for_socket_read failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                 } else {
                     log_message(LOG_LEVEL_ERROR, "wait_for_socket_read failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                 }
#endif
             }
             goto client_cleanup;
        } else if (wait_result == 0) { // Timeout from select/poll
             if (tcp_data->idle_timeout_ms > 0 && time(NULL) >= deadline) {
                 log_message(LOG_LEVEL_INFO, "Idle timeout exceeded for socket %d. Closing connection.", (int)client_conn->socket);
                 goto client_cleanup;
             }
             // If no idle timeout configured, or deadline not reached, just loop again
             continue;
        }
        // else: wait_result == 1 (socket is readable)


        // --- 2. Read the 4-byte Length Prefix ---
        // Use recv_exact from mcp_tcp_socket_utils.c
        read_result = recv_exact(client_conn->socket, length_buf, 4, &client_conn->should_stop);

        if (read_result == -1) { // Socket error
             if (!client_conn->should_stop) {
                char err_buf[128];
#ifdef _WIN32
                strerror_s(err_buf, sizeof(err_buf), sock_errno);
                log_message(LOG_LEVEL_ERROR, "recv (length) failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                    log_message(LOG_LEVEL_ERROR, "recv (length) failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                } else {
                    log_message(LOG_LEVEL_ERROR, "recv (length) failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                }
#endif
             }
             goto client_cleanup;
        } else if (read_result == -3) { // Connection closed
#ifdef _WIN32
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %p while reading length", (void*)client_conn->socket);
#else
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %d while reading length", client_conn->socket);
#endif
             goto client_cleanup;
        } else if (read_result == -2) { // Stop signal
             log_message(LOG_LEVEL_DEBUG, "Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             goto client_cleanup;
        } else if (read_result != 0) { // Should be 0 on success from recv_exact
             log_message(LOG_LEVEL_ERROR, "recv_exact (length) returned unexpected code %d for socket %d", read_result, (int)client_conn->socket);
             goto client_cleanup;
        }

        // Update activity time after successful read
        client_conn->last_activity_time = time(NULL);


        // --- 3. Decode Length (Network to Host Byte Order) ---
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);


        // --- 4. Sanity Check Length ---
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "Invalid message length received: %u on socket %d", message_length_host, (int)client_conn->socket);
             goto client_cleanup; // Invalid length, close connection
        }


        // --- 5. Allocate Buffer for Message Body (Pool or Malloc) ---
        size_t required_size = message_length_host + 1; // +1 for null terminator
        size_t pool_buffer_size = mcp_buffer_pool_get_buffer_size(tcp_data->buffer_pool);

        if (required_size <= pool_buffer_size) {
            message_buf = (char*)mcp_buffer_pool_acquire(tcp_data->buffer_pool);
            if (message_buf != NULL) buffer_malloced = false;
            else {
                log_message(LOG_LEVEL_WARN, "Buffer pool empty, falling back to malloc for %zu bytes on socket %d", required_size, (int)client_conn->socket);
                message_buf = (char*)malloc(required_size);
                buffer_malloced = true;
            }
        } else {
            log_message(LOG_LEVEL_WARN, "Message size %u exceeds pool buffer size %zu, using malloc on socket %d", message_length_host, pool_buffer_size, (int)client_conn->socket);
            message_buf = (char*)malloc(required_size);
            buffer_malloced = true;
        }

        if (message_buf == NULL) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer for message size %u on socket %d", message_length_host, (int)client_conn->socket);
             goto client_cleanup; // Allocation failure
        }


        // --- 6. Read the Message Body ---
        // Use recv_exact from mcp_tcp_socket_utils.c
        read_result = recv_exact(client_conn->socket, message_buf, message_length_host, &client_conn->should_stop);

         if (read_result == -1) { // Socket error
             if (!client_conn->should_stop) {
                char err_buf[128];
#ifdef _WIN32
                strerror_s(err_buf, sizeof(err_buf), sock_errno);
                log_message(LOG_LEVEL_ERROR, "recv (body) failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                    log_message(LOG_LEVEL_ERROR, "recv (body) failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                } else {
                    log_message(LOG_LEVEL_ERROR, "recv (body) failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                }
#endif
             }
             goto client_cleanup;
         } else if (read_result == -3) { // Connection closed
#ifdef _WIN32
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %p while reading body", (void*)client_conn->socket);
#else
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %d while reading body", client_conn->socket);
#endif
             goto client_cleanup;
         } else if (read_result == -2) { // Stop signal
             log_message(LOG_LEVEL_DEBUG, "Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             goto client_cleanup;
         } else if (read_result != 0) { // Should be 0 on success
             log_message(LOG_LEVEL_ERROR, "Incomplete message body received (%d/%u bytes) for socket %d", read_result, message_length_host, (int)client_conn->socket);
             goto client_cleanup;
         }

        // Update activity time after successful read
        client_conn->last_activity_time = time(NULL);


        // --- 7. Null-terminate and Process Message via Callback ---
        message_buf[message_length_host] = '\0';
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

                    // Use send_exact from mcp_tcp_socket_utils.c
                    int send_result = send_exact(client_conn->socket, send_buffer, total_send_len, &client_conn->should_stop);
                    free(send_buffer); // Free combined buffer regardless of result

                    if (send_result == -1) { // Socket error
                        if (!client_conn->should_stop) {
                            char err_buf[128];
#ifdef _WIN32
                            strerror_s(err_buf, sizeof(err_buf), sock_errno);
                            log_message(LOG_LEVEL_ERROR, "send_exact failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                            if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                                log_message(LOG_LEVEL_ERROR, "send_exact failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                            } else {
                                log_message(LOG_LEVEL_ERROR, "send_exact failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                            }
#endif
                        }
                        free(response_str); // Free original response string on error
                        goto client_cleanup;
                    } else if (send_result == -2) { // Stop signal
                         free(response_str); // Free original response string on stop
                         goto client_cleanup;
                    } else if (send_result == -3) { // Connection closed
                         log_message(LOG_LEVEL_INFO, "Client disconnected socket %d during send", (int)client_conn->socket);
                         free(response_str); // Free original response string on disconnect
                         goto client_cleanup;
                    }
                    // Update activity time after successful send
                    client_conn->last_activity_time = time(NULL);
                } else {
                    log_message(LOG_LEVEL_ERROR, "Failed to allocate send buffer for response on socket %d", (int)client_conn->socket);
                    free(response_str); // Free original response string on alloc failure
                    goto client_cleanup;
                }
            } else if (response_len > MAX_MCP_MESSAGE_SIZE) {
                 log_message(LOG_LEVEL_ERROR, "Response generated by callback is too large (%zu bytes) for socket %d", response_len, (int)client_conn->socket);
                 // Don't send, but still need to free the response string
            }
            // Free the original response string after handling it
            free(response_str);
            response_str = NULL;
        } else if (callback_error_code != 0) {
            log_message(LOG_LEVEL_WARN, "Message callback indicated error (%d) but returned no response string for socket %d", callback_error_code, (int)client_conn->socket);
            // No response_str to free in this case
        }

    } // End of main while loop

client_cleanup:
    // --- Cleanup on Exit ---
    // Free or release buffer if loop exited unexpectedly
    if (message_buf != NULL) {
        if (buffer_malloced) free(message_buf);
        else mcp_buffer_pool_release(tcp_data->buffer_pool, message_buf);
    }

 #ifdef _WIN32
     log_message(LOG_LEVEL_DEBUG, "Closing client connection socket %p", (void*)client_conn->socket);
 #else
     log_message(LOG_LEVEL_DEBUG, "Closing client connection socket %d", client_conn->socket);
 #endif
    close_socket(client_conn->socket);

    // Mark slot as inactive (needs mutex protection)
#ifdef _WIN32
    EnterCriticalSection(&tcp_data->client_mutex);
#else
    pthread_mutex_lock(&tcp_data->client_mutex);
#endif
    client_conn->active = false;
#ifdef _WIN32
    if (client_conn->thread_handle) {
        CloseHandle(client_conn->thread_handle); // Close handle now that thread is exiting
        client_conn->thread_handle = NULL;
    }
#else
    client_conn->thread_handle = 0;
#endif
#ifdef _WIN32
    LeaveCriticalSection(&tcp_data->client_mutex);
#else
    pthread_mutex_unlock(&tcp_data->client_mutex);
#endif


#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
