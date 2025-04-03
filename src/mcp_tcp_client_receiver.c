#include "internal/mcp_tcp_client_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes needed for socket operations
#ifdef _WIN32
// Included via internal header
#else
#include <unistd.h>
#include <netinet/in.h>
#endif

/**
 * @internal
 * @brief Background thread function responsible for receiving messages from the server.
 * Reads messages using length-prefix framing, calls the message callback for processing,
 * and calls the error callback if fatal transport errors occur.
 * @param arg Pointer to the mcp_transport_t handle.
 * @return 0 on Windows, NULL on POSIX.
 */
#ifdef _WIN32
DWORD WINAPI tcp_client_receive_thread_func(LPVOID arg) {
#else
void* tcp_client_receive_thread_func(void* arg) {
#endif
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    char length_buf[4];
    uint32_t message_length_net, message_length_host;
    char* message_buf = NULL;
    bool buffer_malloced = false; // Flag to track if we used malloc fallback
    int read_result;
    bool error_signaled = false; // Track if error callback was already called in this loop iteration

    log_message(LOG_LEVEL_DEBUG, "TCP Client receive thread started for socket %d", (int)data->sock);

    // Loop while the transport is marked as running and connected
    while (data->running && data->connected) {

        // --- 1. Read Length Prefix ---
        // Use helper from mcp_tcp_client_socket_utils.c
        read_result = recv_exact_client(data->sock, length_buf, (size_t)4, &data->running);

        // Check result
        if (read_result == SOCKET_ERROR_VAL || read_result == 0) { // Error or connection closed
             if (data->running) { // Log only if not intentionally stopping
                 if (read_result == 0) { // Graceful close by server
                     log_message(LOG_LEVEL_INFO, "Server disconnected socket %d (length read).", (int)data->sock);
                 } else { // Socket error
                     char err_buf[128];
#ifdef _WIN32
                     strerror_s(err_buf, sizeof(err_buf), sock_errno);
                     log_message(LOG_LEVEL_ERROR, "recv_exact_client (length) failed for socket %p: %d (%s)", (void*)data->sock, sock_errno, err_buf);
#else
                     if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                         log_message(LOG_LEVEL_ERROR, "recv_exact_client (length) failed for socket %d: %d (%s)", data->sock, sock_errno, err_buf);
                     } else {
                         log_message(LOG_LEVEL_ERROR, "recv_exact_client (length) failed for socket %d: %d (strerror_r failed)", data->sock, sock_errno);
                     }
#endif
                 }
             }
             data->connected = false; // Mark as disconnected
             // Signal transport error back to the main client logic if running
             if (data->running && transport->error_callback) {
                 transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                 error_signaled = true; // Avoid signaling multiple times if body read also fails
             }
             break; // Exit receive loop
        } else if (read_result == -2) { // Interrupted by stop signal
             log_message(LOG_LEVEL_DEBUG, "Client receive thread for socket %d interrupted by stop signal (length read).", (int)data->sock);
             break; // Exit receive loop
        }
        // read_result == 1 means success reading length


        // --- 2. Decode Length ---
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);


        // --- 3. Sanity Check Length ---
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "Invalid message length received from server: %u on socket %d", message_length_host, (int)data->sock);
             data->connected = false; // Treat as fatal error
             // Signal error back to client logic if running and not already signaled
             if (data->running && transport->error_callback && !error_signaled) {
                 transport->error_callback(transport->callback_user_data, MCP_ERROR_PARSE_ERROR); // Or a specific framing error?
                 error_signaled = true;
             }
             break; // Exit receive loop
        }


        // --- 4. Allocate Buffer (Pool or Malloc) ---
        size_t required_size = message_length_host + 1; // +1 for null terminator
        size_t pool_buffer_size = mcp_buffer_pool_get_buffer_size(data->buffer_pool);

        if (required_size <= pool_buffer_size) {
            // Try to acquire from pool
            message_buf = (char*)mcp_buffer_pool_acquire(data->buffer_pool);
            if (message_buf != NULL) {
                buffer_malloced = false;
                // log_message(LOG_LEVEL_DEBUG, "Acquired buffer from pool for socket %d", (int)data->sock);
            } else {
                // Pool empty, fallback to malloc
                log_message(LOG_LEVEL_WARN, "Buffer pool empty, falling back to malloc for %zu bytes on socket %d", required_size, (int)data->sock);
                message_buf = (char*)malloc(required_size);
                buffer_malloced = true;
            }
        } else {
            // Message too large for pool buffers, use malloc
            log_message(LOG_LEVEL_WARN, "Message size %u exceeds pool buffer size %zu, using malloc on socket %d", message_length_host, pool_buffer_size, (int)data->sock);
            message_buf = (char*)malloc(required_size);
            buffer_malloced = true;
        }

        // Check allocation result
        if (message_buf == NULL) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer for message size %u on socket %d", message_length_host, (int)data->sock);
             data->connected = false; // Treat as fatal error
             // Signal error back to client logic if running and not already signaled
             if (data->running && transport->error_callback && !error_signaled) {
                 transport->error_callback(transport->callback_user_data, MCP_ERROR_INTERNAL_ERROR); // Allocation error
                 error_signaled = true;
             }
             break; // Exit receive loop
        }


        // --- 5. Read Message Body ---
        // Use helper from mcp_tcp_client_socket_utils.c
        read_result = recv_exact_client(data->sock, message_buf, (size_t)message_length_host, &data->running);

         // Check result
         if (read_result == SOCKET_ERROR_VAL || read_result == 0) { // Error or connection closed
             if (data->running) { // Log only if not intentionally stopping
                 if (read_result == 0) { // Graceful close by server
                     log_message(LOG_LEVEL_INFO, "Server disconnected socket %d while reading body.", (int)data->sock);
                 } else { // Socket error
                     char err_buf[128];
#ifdef _WIN32
                     strerror_s(err_buf, sizeof(err_buf), sock_errno);
                     log_message(LOG_LEVEL_ERROR, "recv_exact_client (body) failed for socket %p: %d (%s)", (void*)data->sock, sock_errno, err_buf);
#else
                     if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                         log_message(LOG_LEVEL_ERROR, "recv_exact_client (body) failed for socket %d: %d (%s)", data->sock, sock_errno, err_buf);
                     } else {
                         log_message(LOG_LEVEL_ERROR, "recv_exact_client (body) failed for socket %d: %d (strerror_r failed)", data->sock, sock_errno);
                     }
#endif
                 }
              }
              // Free or release buffer before exiting
              if (buffer_malloced) {
                  free(message_buf);
              } else {
                  mcp_buffer_pool_release(data->buffer_pool, message_buf);
              }
              message_buf = NULL;
              data->connected = false;
              // Signal transport error back to the main client logic if running and not already signaled
              if (data->running && transport->error_callback && !error_signaled) {
                  transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                  error_signaled = true;
              }
              break; // Exit receive loop
          } else if (read_result == -2) { // Interrupted by stop signal
              log_message(LOG_LEVEL_DEBUG, "Client receive thread for socket %d interrupted by stop signal (body read).", (int)data->sock);
              // Free or release buffer before exiting
              if (buffer_malloced) {
                  free(message_buf);
              } else {
                  mcp_buffer_pool_release(data->buffer_pool, message_buf);
              }
              message_buf = NULL;
              break; // Exit receive loop
          }
         // read_result == 1 means success reading body


        // --- 6. Process Message via Callback ---
        message_buf[message_length_host] = '\0';
        if (transport->message_callback != NULL) {
            int callback_error_code = 0;
            // Callback expects to return a response string, but client callback doesn't send anything back here.
            // We just need to process the incoming message (likely a response).
            char* unused_response = transport->message_callback(transport->callback_user_data, message_buf, message_length_host, &callback_error_code);
            free(unused_response); // Free the NULL response from the client callback
            if (callback_error_code != 0) {
                 log_message(LOG_LEVEL_WARN, "Client message callback indicated error (%d) processing data from socket %d", callback_error_code, (int)data->sock);
                  // Decide if we should continue or disconnect? For now, continue.
             }
        }


        // --- 7. Release/Free Message Buffer ---
        if (message_buf != NULL) { // Check if buffer was allocated
            if (buffer_malloced) {
                free(message_buf);
            } else {
                mcp_buffer_pool_release(data->buffer_pool, message_buf);
                // log_message(LOG_LEVEL_DEBUG, "Released buffer to pool for socket %d", (int)data->sock);
            }
            message_buf = NULL; // Avoid double free/release in cleanup
        }

    } // End of main while loop

    log_message(LOG_LEVEL_DEBUG, "TCP Client receive thread exiting for socket %d.", (int)data->sock);
    data->connected = false; // Ensure disconnected state

    // Free buffer if loop exited unexpectedly (should have been handled above, but double-check)
    if (message_buf != NULL) {
        if (buffer_malloced) {
            free(message_buf);
        } else {
            mcp_buffer_pool_release(data->buffer_pool, message_buf);
        }
    }


#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
