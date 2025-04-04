#include "internal/tcp_client_transport_internal.h"
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

// Define global flag variable
bool reconnection_in_progress = false;

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

    // Add initial connection health check
    log_message(LOG_LEVEL_DEBUG, "TCP Client receive thread started for socket %d", (int)data->sock);
    
    // Wait before sending handshake to ensure server is ready
    sleep_ms(1000); // Add wait time to ensure server is ready
    
    // Check if this is a thread startup during reconnection
    if (reconnection_in_progress) {
        log_message(LOG_LEVEL_INFO, "Skipping initial ping due to reconnection");
        reconnection_in_progress = false; // Reset flag
        
        // Only enter receive mode, do not send ping
        goto receive_loop;
    }
    
    // Send ping on first connection
    log_message(LOG_LEVEL_INFO, "Preparing client ping message...");
    
    // Ensure connection is established
    if (!data->connected) {
        log_message(LOG_LEVEL_ERROR, "Cannot send ping, socket not connected");
        return 0;
    }
    
    // 1. Define standard ping message
    static const char ping_content[] = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":0}";
    uint32_t ping_length = (uint32_t)strlen(ping_content);
    
    // 2. Calculate message length (including NULL terminator)
    const uint32_t content_length = ping_length + 1; // +1 for terminator
    
    // 3. Convert to network byte order (Big-Endian)
    const uint32_t length_network_order = htonl(content_length);
    
    // 4. Create a single buffer containing length prefix and message content
    size_t total_send_len = 4 + content_length; // 4 byte length prefix + message content (including terminator)
    char* send_buffer = (char*)malloc(total_send_len);
    
    if (!send_buffer) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer for ping message");
        return 0;
    }
    
    // 5. Fill the buffer with length prefix and message content (including terminator)
    memcpy(send_buffer, &length_network_order, 4);
    memcpy(send_buffer + 4, ping_content, ping_length); // Copy content first
    send_buffer[4 + ping_length] = '\0'; // Explicitly add terminator
    
    // 6. Detailed logging
    log_message(LOG_LEVEL_DEBUG, "Ping message content (%u bytes): '%s'", content_length, ping_content);
    log_message(LOG_LEVEL_DEBUG, "Preparing combined message (total %zu bytes)", total_send_len);
    log_message(LOG_LEVEL_DEBUG, "Length prefix: %02X %02X %02X %02X", 
                (unsigned char)send_buffer[0], 
                (unsigned char)send_buffer[1],
                (unsigned char)send_buffer[2],
                (unsigned char)send_buffer[3]);
    
    // 7. Send complete message in one operation (length prefix + message content)
    if (data->connected) {
        int send_status = send_exact_client(data->sock, send_buffer, total_send_len, &data->running);
        free(send_buffer); // Free buffer
        
        if (send_status != 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to send ping message (status: %d)", send_status);
            return 0;
        }
        
        log_message(LOG_LEVEL_INFO, "Ping message sent successfully");
    } else {
        free(send_buffer); // Free buffer
        log_message(LOG_LEVEL_ERROR, "Cannot send ping, connection already closed");
    }

receive_loop:
    // Main message reception loop
    while (data->running) {
        // Check connection status
        if (!data->connected) {
            log_message(LOG_LEVEL_INFO, "Connection lost, exiting receive thread");
            break;
        }

        // --- 1. Read length prefix ---
        log_message(LOG_LEVEL_DEBUG, "Waiting to receive length prefix from server...");
        read_result = recv_exact_client(data->sock, length_buf, 4, &data->running);
        log_message(LOG_LEVEL_DEBUG, "recv_exact_client for length returned: %d", read_result);

        // Check result
        if (read_result == SOCKET_ERROR_VAL || read_result == 0) { // Error or connection closed
             if (data->running) { // Only log during abnormal stops
                 if (read_result == 0) { // Server gracefully closed the connection
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
             if (data->running && transport->error_callback) {
                 transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                 error_signaled = true;
             }
             break;
        } else if (read_result == -2) { // Stop signal interrupted
             log_message(LOG_LEVEL_DEBUG, "Client receive thread for socket %d interrupted by stop signal.", (int)data->sock);
             break;
        }

        // --- 2. Decode length ---
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);
        log_message(LOG_LEVEL_DEBUG, "Received length: %u bytes", message_length_host);

        // --- 3. Check length validity ---
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "Invalid message length from server: %u", message_length_host);
             data->connected = false;
             if (data->running && transport->error_callback && !error_signaled) {
                 transport->error_callback(transport->callback_user_data, MCP_ERROR_PARSE_ERROR);
                 error_signaled = true;
             }
             break;
        }

        // --- 4. Allocate message buffer ---
        size_t required_size = message_length_host + 1; // +1 for null terminator
        size_t pool_buffer_size = mcp_buffer_pool_get_buffer_size(data->buffer_pool);

        if (required_size <= pool_buffer_size) {
            message_buf = (char*)mcp_buffer_pool_acquire(data->buffer_pool);
            if (message_buf != NULL) {
                buffer_malloced = false;
            } else {
                log_message(LOG_LEVEL_WARN, "Buffer pool empty, using malloc for %zu bytes", required_size);
                message_buf = (char*)malloc(required_size);
                buffer_malloced = true;
            }
        } else {
            log_message(LOG_LEVEL_WARN, "Message size %u exceeds pool buffer size %zu", message_length_host, pool_buffer_size);
            message_buf = (char*)malloc(required_size);
            buffer_malloced = true;
        }

        if (message_buf == NULL) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer for message size %u", message_length_host);
             data->connected = false;
             if (data->running && transport->error_callback && !error_signaled) {
                 transport->error_callback(transport->callback_user_data, MCP_ERROR_INTERNAL_ERROR);
                 error_signaled = true;
             }
             break;
        }

        // --- 5. Read message body ---
        read_result = recv_exact_client(data->sock, message_buf, message_length_host, &data->running);

        if (read_result == SOCKET_ERROR_VAL || read_result == 0) {
             if (data->running) {
                 if (read_result == 0) {
                     log_message(LOG_LEVEL_INFO, "Server disconnected socket %d while reading body.", (int)data->sock);
                 } else {
                     char err_buf[128];
#ifdef _WIN32
                     strerror_s(err_buf, sizeof(err_buf), sock_errno);
                     log_message(LOG_LEVEL_ERROR, "recv_exact_client (body) failed: %d (%s)", sock_errno, err_buf);
#else
                     if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                         log_message(LOG_LEVEL_ERROR, "recv_exact_client (body) failed: %d (%s)", sock_errno, err_buf);
                     } else {
                         log_message(LOG_LEVEL_ERROR, "recv_exact_client (body) failed: %d", sock_errno);
                     }
#endif
                 }
              }
              
              // Clean up buffer
              if (buffer_malloced) {
                  free(message_buf);
              } else {
                  mcp_buffer_pool_release(data->buffer_pool, message_buf);
              }
              message_buf = NULL;
              data->connected = false;
              
              // Notify error callback
              if (data->running && transport->error_callback && !error_signaled) {
                  transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
                  error_signaled = true;
              }
              break;
          } else if (read_result == -2) { // Stop signal
              log_message(LOG_LEVEL_DEBUG, "Client receive thread interrupted by stop signal.");
              
              // Clean up buffer
              if (buffer_malloced) {
                  free(message_buf);
              } else {
                  mcp_buffer_pool_release(data->buffer_pool, message_buf);
              }
              message_buf = NULL;
              break;
          }

        // --- 6. Process received message ---
        // Ensure NULL termination
        message_buf[message_length_host] = '\0';
        
        // Print received message content (for debugging only)
        log_message(LOG_LEVEL_DEBUG, "Received message from server: '%s'", message_buf);
        
        if (transport->message_callback != NULL) {
            int callback_error_code = 0;
            char* unused_response = transport->message_callback(
                transport->callback_user_data, 
                message_buf, 
                message_length_host, 
                &callback_error_code
            );
            
            free(unused_response); // Client doesn't need this response
            
            if (callback_error_code != 0) {
                 log_message(LOG_LEVEL_WARN, "Client message callback error: %d", callback_error_code);
             }
        }

        // --- 7. Release message buffer ---
        if (message_buf != NULL) {
            if (buffer_malloced) {
                free(message_buf);
            } else {
                mcp_buffer_pool_release(data->buffer_pool, message_buf);
            }
            message_buf = NULL;
        }

    } // End of main loop

    log_message(LOG_LEVEL_DEBUG, "TCP Client receive thread exiting for socket %d", (int)data->sock);
    data->connected = false;

    // Final check if buffer was released
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
