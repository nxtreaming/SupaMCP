#include "mcp_tcp_transport.h"
#include "mcp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

// Platform-specific socket includes
#ifdef _WIN32
// Include winsock2.h FIRST before any other includes that might pull in windows.h
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    typedef int socklen_t;
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
    // Windows doesn't have MSG_NOSIGNAL, but send/recv handle broken pipes differently
    #define SEND_FLAGS 0
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <pthread.h>
#   include <fcntl.h>
#   include <sys/select.h>
#   include <poll.h>
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define close_socket close
    #define sock_errno errno
    // Prevent SIGPIPE signal on send to closed socket
    #define SEND_FLAGS MSG_NOSIGNAL
#endif

// Max message size limit for sanity check (remains relevant)
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit
// Max number of concurrent clients (simple example)
#define MAX_TCP_CLIENTS 10
// Buffer pool configuration
#define POOL_BUFFER_SIZE (1024 * 8) // 8KB buffer size
#define POOL_NUM_BUFFERS 16         // Number of buffers in the pool

// Structure to hold info about a connected client
typedef struct {
    socket_t socket;
    struct sockaddr_in address; // Assuming IPv4 for simplicity
    bool active;
    time_t last_activity_time; // Timestamp of the last successful read/write
#ifdef _WIN32
    HANDLE thread_handle;
#else
    pthread_t thread_handle;
#endif
    mcp_transport_t* transport; // Pointer back to parent transport
    bool should_stop; // Flag to signal this specific handler thread
} tcp_client_connection_t;

// Internal structure for TCP transport data
typedef struct {
    char* host;
    uint16_t port;
    uint32_t idle_timeout_ms; // Idle timeout in milliseconds (0 = disabled)
    socket_t listen_socket;
    bool running;
    tcp_client_connection_t clients[MAX_TCP_CLIENTS];
#ifdef _WIN32
    HANDLE accept_thread;
    CRITICAL_SECTION client_mutex; // To protect access to clients array
#else
    pthread_t accept_thread;
    pthread_mutex_t client_mutex; // To protect access to clients array
    int stop_pipe[2]; // Pipe used to signal accept thread on POSIX
#endif
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for message buffers
} mcp_tcp_transport_data_t;


// --- Forward Declarations for Static Functions ---
static int tcp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data, mcp_transport_error_callback_t error_callback);
static int tcp_transport_stop(mcp_transport_t* transport);
static void tcp_transport_destroy(mcp_transport_t* transport);
#ifdef _WIN32
static DWORD WINAPI tcp_accept_thread_func(LPVOID arg);
static DWORD WINAPI tcp_client_handler_thread_func(LPVOID arg);
#else
static void* tcp_accept_thread_func(void* arg);
static void* tcp_client_handler_thread_func(void* arg);
#endif
static void initialize_winsock(); // Helper for Windows
#ifndef _WIN32
static void close_stop_pipe(mcp_tcp_transport_data_t* data); // Helper
#endif
static int wait_for_socket_read(socket_t sock, uint32_t timeout_ms, bool* should_stop);


// --- Static Implementation Functions ---

#ifdef _WIN32
static void initialize_winsock() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        fprintf(stderr, "[MCP TCP Transport] WSAStartup failed: %d\n", iResult);
        exit(EXIT_FAILURE); // Critical error
    }
}
#else
static void initialize_winsock() { /* No-op on non-Windows */ }
#endif

// Helper function to send exactly n bytes to a socket (checks client stop flag)
// Returns 0 on success, -1 on error, -2 on stop signal, -3 on connection closed/reset
static int send_exact(socket_t sock, const char* buf, size_t len, bool* client_should_stop_flag) {
    size_t total_sent = 0;
    while (total_sent < len) {
        if (client_should_stop_flag && *client_should_stop_flag) return -2; // Interrupted by stop signal

#ifdef _WIN32
        int chunk_len = (len - total_sent > INT_MAX) ? INT_MAX : (int)(len - total_sent);
        int bytes_sent = send(sock, buf + total_sent, chunk_len, SEND_FLAGS);
#else
        ssize_t bytes_sent = send(sock, buf + total_sent, len - total_sent, SEND_FLAGS);
#endif

        if (bytes_sent == SOCKET_ERROR) {
            // Check if the error is due to a broken pipe (connection closed by peer)
            int error_code = sock_errno;
#ifdef _WIN32
            if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN) {
                return -3; // Indicate connection closed/reset
            }
#else
            if (error_code == EPIPE || error_code == ECONNRESET || error_code == ENOTCONN) {
                return -3; // Indicate connection closed/reset
            }
#endif
            return -1; // Other socket error
        }
        if (bytes_sent == 0) {
             // Should not happen with blocking sockets unless len was 0
             return -1;
        }
        total_sent += bytes_sent;
    }
    return 0; // Success
}


// Helper function to read exactly n bytes from a socket (checks client stop flag)
// Returns: 0 on success, -1 on error, -2 on stop signal, -3 on connection closed
static int recv_exact(socket_t sock, char* buf, int len, bool* client_should_stop_flag) {
    int total_read = 0;
    while (total_read < len) {
        if (client_should_stop_flag && *client_should_stop_flag) return -2; // Interrupted by stop signal

        int bytes_read = recv(sock, buf + total_read, len - total_read, 0);
        if (bytes_read == SOCKET_ERROR) {
            return -1; // Socket error
        } else if (bytes_read == 0) {
            return -3;  // Connection closed gracefully
        }
        total_read += bytes_read;
    }
    return 0; // Success
}

/**
 * @internal
 * @brief Waits for readability on a socket or a stop signal.
 * Uses poll() on POSIX and select() on Windows for simplicity.
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. 0 means no timeout (wait indefinitely).
 * @param should_stop Pointer to the thread's stop flag.
 * @return 1 if socket is readable, 0 if timeout occurred, -1 on error, -2 if stop signal received.
 */
static int wait_for_socket_read(socket_t sock, uint32_t timeout_ms, bool* should_stop) {
    if (should_stop && *should_stop) return -2;

#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    struct timeval tv;
    struct timeval* tv_ptr = NULL;

    if (timeout_ms > 0) {
        // Use a smaller timeout for select to check should_stop more often
        uint32_t select_timeout_ms = (timeout_ms < 500) ? timeout_ms : 500; // Check every 500ms max
        tv.tv_sec = select_timeout_ms / 1000;
        tv.tv_usec = (select_timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int result = select(0, &read_fds, NULL, NULL, tv_ptr);

    if (should_stop && *should_stop) return -2; // Check again after select

    if (result == SOCKET_ERROR) {
        return -1; // select error
    } else if (result == 0) {
        return 0; // timeout (or intermediate check)
    } else {
        return 1; // readable
    }
#else
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    // Use a smaller timeout for poll to check should_stop more often
    int poll_timeout = (timeout_ms == 0) ? -1 : ((timeout_ms < 500) ? (int)timeout_ms : 500); // Check every 500ms max

    int result = poll(&pfd, 1, poll_timeout);

    if (should_stop && *should_stop) return -2; // Check again after poll

    if (result < 0) {
        if (errno == EINTR) return -2; // Interrupted, treat like stop signal
        return -1; // poll error
    } else if (result == 0) {
        return 0; // timeout (or intermediate check)
    } else {
        if (pfd.revents & POLLIN) {
            return 1; // readable
        } else {
            // Other event (e.g., POLLERR, POLLHUP), treat as error/closed
            return -1;
        }
    }
#endif
}


// Thread function to handle a single client connection
#ifdef _WIN32
static DWORD WINAPI tcp_client_handler_thread_func(LPVOID arg) {
#else
static void* tcp_client_handler_thread_func(void* arg) {
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
        // Calculate overall deadline for this read operation
        time_t deadline = 0;
        if (tcp_data->idle_timeout_ms > 0) {
            deadline = client_conn->last_activity_time + (tcp_data->idle_timeout_ms / 1000) + ((tcp_data->idle_timeout_ms % 1000) > 0 ? 1 : 0);
        }

        // 1. Wait for data or timeout
        uint32_t wait_ms = tcp_data->idle_timeout_ms; // Start with full timeout for select/poll
        if (wait_ms == 0) wait_ms = 500; // If no idle timeout, check stop flag periodically

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

        // 2. Read the 4-byte length prefix
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

        // 3. Decode length (Network to Host byte order)
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);

        // 4. Sanity check length
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "Invalid message length received: %u on socket %d", message_length_host, (int)client_conn->socket);
             goto client_cleanup; // Invalid length, close connection
        }

        // 5. Allocate buffer for message body using buffer pool or malloc
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

        // 6. Read the message body
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

        // 7. Null-terminate and process the message via callback
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

        // 9. If callback returned a response string, send it back
        if (response_str != NULL) {
            size_t response_len = strlen(response_str);
            if (response_len > 0 && response_len <= MAX_MCP_MESSAGE_SIZE) {
                uint32_t net_len = htonl((uint32_t)response_len);
                size_t total_send_len = sizeof(net_len) + response_len;
                char* send_buffer = (char*)malloc(total_send_len);

                if (send_buffer) {
                    memcpy(send_buffer, &net_len, sizeof(net_len));
                    memcpy(send_buffer + sizeof(net_len), response_str, response_len);

                    int send_result = send_exact(client_conn->socket, send_buffer, total_send_len, &client_conn->should_stop);
                    free(send_buffer); // Free send buffer regardless of result

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
                        free(response_str);
                        goto client_cleanup;
                    } else if (send_result == -2) { // Stop signal
                         free(response_str);
                         goto client_cleanup;
                    } else if (send_result == -3) { // Connection closed
                         log_message(LOG_LEVEL_INFO, "Client disconnected socket %d during send", (int)client_conn->socket);
                         free(response_str);
                         goto client_cleanup;
                    }
                    // Update activity time after successful send
                    client_conn->last_activity_time = time(NULL);
                } else {
                    log_message(LOG_LEVEL_ERROR, "Failed to allocate send buffer for response on socket %d", (int)client_conn->socket);
                    free(response_str);
                    goto client_cleanup;
                }
            } else if (response_len > MAX_MCP_MESSAGE_SIZE) {
                 log_message(LOG_LEVEL_ERROR, "Response generated by callback is too large (%zu bytes) for socket %d", response_len, (int)client_conn->socket);
            }
            free(response_str);
            response_str = NULL;
        } else if (callback_error_code != 0) {
            log_message(LOG_LEVEL_WARN, "Message callback indicated error (%d) but returned no response string for socket %d", callback_error_code, (int)client_conn->socket);
        }

    } // End of main while loop

client_cleanup:
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

#ifndef _WIN32
// Helper to close stop pipe FDs
static void close_stop_pipe(mcp_tcp_transport_data_t* data) {
    if (data->stop_pipe[0] != -1) {
        close(data->stop_pipe[0]);
        data->stop_pipe[0] = -1;
    }
    if (data->stop_pipe[1] != -1) {
        close(data->stop_pipe[1]);
        data->stop_pipe[1] = -1;
    }
}
#endif


// Thread function to accept incoming connections
#ifdef _WIN32
static DWORD WINAPI tcp_accept_thread_func(LPVOID arg) {
#else
static void* tcp_accept_thread_func(void* arg) {
#endif
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr)); // Initialize client_addr
    socklen_t client_addr_len = sizeof(client_addr);
    socket_t client_socket = INVALID_SOCKET; // Initialize to invalid
    char client_ip_str[INET6_ADDRSTRLEN]; // Use INET6 for future-proofing

    log_message(LOG_LEVEL_INFO, "Accept thread started, listening on %s:%d", data->host, data->port);

#ifdef _WIN32
    // Windows: Use select with a timeout to allow checking data->running periodically
    fd_set read_fds;
    struct timeval tv;
    while (data->running) {
        FD_ZERO(&read_fds);
        // Only set FD if socket is valid (it might be closed during stop)
        if (data->listen_socket != INVALID_SOCKET) {
            FD_SET(data->listen_socket, &read_fds);
        } else {
            Sleep(100); // Avoid busy-waiting if socket is closed
            continue;
        }
        tv.tv_sec = 1; // Check every second
        tv.tv_usec = 0;

        int activity = select(0, &read_fds, NULL, NULL, &tv);

        if (!data->running) break; // Check flag again after select

        if (activity == SOCKET_ERROR) {
             if (data->running) {
                 int error_code = sock_errno;
                 if (error_code != WSAEINTR && error_code != WSAENOTSOCK && error_code != WSAEINVAL) { // Ignore interrupt/socket closed
                     char err_buf[128];
                     strerror_s(err_buf, sizeof(err_buf), error_code);
                     log_message(LOG_LEVEL_ERROR, "select failed in accept thread: %d (%s)", error_code, err_buf);
                 } else {
                     log_message(LOG_LEVEL_DEBUG, "Select interrupted likely due to stop signal.");
                     break;
                 }
             } else {
                 break;
             }
             continue;
        }

        if (activity > 0 && FD_ISSET(data->listen_socket, &read_fds)) {
            // Accept connection
            client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

            if (client_socket == INVALID_SOCKET) {
                 if (data->running) {
                     char err_buf[128];
                     strerror_s(err_buf, sizeof(err_buf), sock_errno);
                     log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
                 }
                 continue;
            }
            // --- Common connection handling logic (Windows) ---
            const char* client_ip = NULL;
            if (InetNtop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
                 client_ip = client_ip_str;
            } else {
                 log_message(LOG_LEVEL_WARN, "InetNtop failed: %d", sock_errno);
                 client_ip = "?.?.?.?";
            }
            log_message(LOG_LEVEL_INFO, "Accepted connection from %s:%d on socket %p",
                   client_ip, ntohs(client_addr.sin_port), (void*)client_socket);

            // Find an empty slot and launch handler thread (common logic below)
            goto handle_connection;
        }
        // If activity == 0, it was a timeout, loop continues to check data->running

#else // POSIX
    struct pollfd fds[2];
    fds[0].fd = data->listen_socket;
    fds[0].events = POLLIN;
    fds[1].fd = data->stop_pipe[0]; // Read end of stop pipe
    fds[1].events = POLLIN;

    while (data->running) {
        int activity = poll(fds, 2, -1); // Wait indefinitely

        if (!data->running) break; // Check flag again after poll

        if (activity < 0 && errno != EINTR) {
            log_message(LOG_LEVEL_ERROR, "poll error in accept thread: %s", strerror(errno));
            continue;
        }

        // Check if stop pipe has data
        if (fds[1].revents & POLLIN) {
            log_message(LOG_LEVEL_DEBUG, "Stop signal received in accept thread.");
            char buf[16];
            while (read(data->stop_pipe[0], buf, sizeof(buf)) > 0); // Drain pipe
            break; // Exit loop
        }

        // Check for incoming connection
        if (fds[0].revents & POLLIN) {
            client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

            if (client_socket == INVALID_SOCKET) {
                 if (data->running) {
                     char err_buf[128];
                     if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                        log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
                     } else {
                        log_message(LOG_LEVEL_ERROR, "accept failed: %d (strerror_r failed)", sock_errno);
                     }
                 }
                 continue;
            }
            // --- Common connection handling logic (POSIX) ---
            const char* client_ip = NULL;
            if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
                client_ip = client_ip_str;
            } else {
                 char err_buf[128];
                 if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                     log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (%s)", sock_errno, err_buf);
                 } else {
                     log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (strerror_r failed)", sock_errno);
                 }
                client_ip = "?.?.?.?";
            }
            log_message(LOG_LEVEL_INFO, "Accepted connection from %s:%d on socket %d",
                   client_ip, ntohs(client_addr.sin_port), client_socket);

            // Find an empty slot and launch handler thread (common logic below)
            goto handle_connection;
        }
#endif // Platform-specific accept loop end

handle_connection: {} // Label for common connection handling logic
            // --- Common logic for handling accepted connection ---
            int client_index = -1;
#ifdef _WIN32
            EnterCriticalSection(&data->client_mutex);
#else
            pthread_mutex_lock(&data->client_mutex);
#endif
            for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
                if (!data->clients[i].active) {
                    client_index = i;
                    data->clients[i].active = true; // Mark as active immediately
                    break;
                }
            }
#ifdef _WIN32
            LeaveCriticalSection(&data->client_mutex);
#else
            pthread_mutex_unlock(&data->client_mutex);
#endif

            if (client_index != -1) {
                tcp_client_connection_t* client_conn = &data->clients[client_index];
                client_conn->socket = client_socket;
                client_conn->address = client_addr;
                client_conn->transport = transport; // Link back
                client_conn->should_stop = false; // Ensure flag is reset
                client_conn->last_activity_time = time(NULL); // Initialize activity time

                // Create a handler thread for this client
#ifdef _WIN32
                client_conn->thread_handle = CreateThread(NULL, 0, tcp_client_handler_thread_func, client_conn, 0, NULL);
                if (client_conn->thread_handle == NULL) {
                    log_message(LOG_LEVEL_ERROR, "Failed to create handler thread for client %d.", client_index);
                    close_socket(client_socket);
                    // Safely mark slot inactive again
                    EnterCriticalSection(&data->client_mutex);
                    client_conn->active = false;
                    LeaveCriticalSection(&data->client_mutex);
                }
#else
                if (pthread_create(&client_conn->thread_handle, NULL, tcp_client_handler_thread_func, client_conn) != 0) {
                     log_message(LOG_LEVEL_ERROR, "Failed to create handler thread: %s", strerror(errno));
                     close_socket(client_socket);
                     // Safely mark slot inactive again
                     pthread_mutex_lock(&data->client_mutex);
                     client_conn->active = false;
                     pthread_mutex_unlock(&data->client_mutex);
                } else {
                     pthread_detach(client_conn->thread_handle); // Detach thread
                }
#endif
            } else {
                // This uses client_ip which might be out of scope if the goto was inside the #ifdef block
                // Re-declare and get IP here if needed, or restructure the #ifdefs
                const char* reject_client_ip = NULL;
                if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
                    reject_client_ip = client_ip_str;
                } else {
                    reject_client_ip = "?.?.?.?";
                }
                log_message(LOG_LEVEL_WARN, "Max clients (%d) reached, rejecting connection from %s:%d",
                        MAX_TCP_CLIENTS, reject_client_ip, ntohs(client_addr.sin_port));
                close_socket(client_socket);
            }
#ifndef _WIN32 // Closing brace for the 'if (FD_ISSET(data->listen_socket, &read_fds))' block in POSIX case
        }
#endif
    } // End of while(data->running) loop

    log_message(LOG_LEVEL_INFO, "Accept thread exiting.");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}


// Note: Update tcp_transport_start signature to match interface
static int tcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    // Store callbacks
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (data->running) return 0; // Already running

    initialize_winsock(); // Initialize Winsock if on Windows

    // Create listening socket
    data->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (data->listen_socket == INVALID_SOCKET) {
        char err_buf[128];
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), sock_errno);
        log_message(LOG_LEVEL_ERROR, "socket creation failed: %d (%s)", sock_errno, err_buf);
#else
         if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "socket creation failed: %d (%s)", sock_errno, err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "socket creation failed: %d (strerror_r failed)", sock_errno);
         }
#endif
        return -1;
    }

    // Allow address reuse
    int optval = 1;
    setsockopt(data->listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    // Bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->port);
    if (inet_pton(AF_INET, data->host, &server_addr.sin_addr) <= 0) {
         log_message(LOG_LEVEL_ERROR, "Invalid address/ Address not supported: %s", data->host);
         close_socket(data->listen_socket);
         return -1;
    }

    if (bind(data->listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        char err_buf[128];
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), sock_errno);
        log_message(LOG_LEVEL_ERROR, "bind failed on %s:%d: %d (%s)", data->host, data->port, sock_errno, err_buf);
#else
         if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
             log_message(LOG_LEVEL_ERROR, "bind failed on %s:%d: %d (%s)", data->host, data->port, sock_errno, err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "bind failed on %s:%d: %d (strerror_r failed)", data->host, data->port, sock_errno);
         }
#endif
        close_socket(data->listen_socket);
        return -1;
    }

    // Listen
    if (listen(data->listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        char err_buf[128];
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), sock_errno);
        log_message(LOG_LEVEL_ERROR, "listen failed: %d (%s)", sock_errno, err_buf);
#else
         if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "listen failed: %d (%s)", sock_errno, err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "listen failed: %d (strerror_r failed)", sock_errno);
         }
#endif
        close_socket(data->listen_socket);
        return -1;
    }

    data->running = true;

    // Initialize mutex/critical section and stop pipe
#ifdef _WIN32
    InitializeCriticalSection(&data->client_mutex);
    // No pipe needed for Windows
#else
    if (pthread_mutex_init(&data->client_mutex, NULL) != 0) {
        char err_buf[128];
        if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
             log_message(LOG_LEVEL_ERROR, "Mutex init failed: %s", err_buf);
        } else {
             log_message(LOG_LEVEL_ERROR, "Mutex init failed: %d (strerror_r failed)", errno);
        }
        close_socket(data->listen_socket);
        data->running = false;
        return -1;
    }
    // Create stop pipe
    data->stop_pipe[0] = -1; // Initialize FDs
    data->stop_pipe[1] = -1;
    if (pipe(data->stop_pipe) != 0) {
        log_message(LOG_LEVEL_ERROR, "Stop pipe creation failed: %s", strerror(errno));
        close_socket(data->listen_socket);
        pthread_mutex_destroy(&data->client_mutex);
        data->running = false;
        return -1;
    }
    // Set read end to non-blocking
    int flags = fcntl(data->stop_pipe[0], F_GETFL, 0);
    if (flags == -1 || fcntl(data->stop_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
         log_message(LOG_LEVEL_ERROR, "Failed to set stop pipe read end non-blocking: %s", strerror(errno));
         close_stop_pipe(data);
         close_socket(data->listen_socket);
         pthread_mutex_destroy(&data->client_mutex);
         data->running = false;
         return -1;
    }
#endif

    // Start accept thread
#ifdef _WIN32
    data->accept_thread = CreateThread(NULL, 0, tcp_accept_thread_func, transport, 0, NULL);
    if (data->accept_thread == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create accept thread (Error: %lu).", GetLastError());
        close_socket(data->listen_socket);
        DeleteCriticalSection(&data->client_mutex); // Clean up mutex
        data->running = false;
        return -1;
    }
#else
     if (pthread_create(&data->accept_thread, NULL, tcp_accept_thread_func, transport) != 0) {
        char err_buf[128];
         if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to create accept thread: %s", err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "Failed to create accept thread: %d (strerror_r failed)", errno);
         }
        close_socket(data->listen_socket);
        pthread_mutex_destroy(&data->client_mutex);
        close_stop_pipe(data); // Clean up pipe on failure
        data->running = false;
        return -1;
    }
#endif

    log_message(LOG_LEVEL_INFO, "TCP Transport started listening on %s:%d", data->host, data->port);
    return 0;
}

static int tcp_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (!data->running) return 0;

    log_message(LOG_LEVEL_INFO, "Stopping TCP Transport...");
    data->running = false;

    // Signal and close the listening socket/pipe to interrupt accept/select
#ifdef _WIN32
    // Close the listening socket to interrupt accept()
    if (data->listen_socket != INVALID_SOCKET) {
        shutdown(data->listen_socket, SD_BOTH); // Try shutdown first
        close_socket(data->listen_socket);
        data->listen_socket = INVALID_SOCKET;
    }
#else
    // Write to the stop pipe to interrupt select()
    if (data->stop_pipe[1] != -1) {
        char dummy = 's';
        ssize_t written = write(data->stop_pipe[1], &dummy, 1);
        if (written <= 0) {
             log_message(LOG_LEVEL_WARN, "Failed to write to stop pipe during stop: %s", strerror(errno));
        }
        // No need to close write end immediately, close_stop_pipe handles it
    }
    // Also close the listening socket
    if (data->listen_socket != INVALID_SOCKET) {
        shutdown(data->listen_socket, SHUT_RDWR); // Try shutdown first
        close_socket(data->listen_socket);
        data->listen_socket = INVALID_SOCKET;
    }
#endif


    // Wait for the accept thread to finish
#ifdef _WIN32
    if (data->accept_thread) {
        // Consider a timeout?
        WaitForSingleObject(data->accept_thread, 2000); // Wait 2 seconds
        CloseHandle(data->accept_thread);
        data->accept_thread = NULL;
    }
#else
    if (data->accept_thread) {
        // Join the accept thread
        pthread_join(data->accept_thread, NULL);
        data->accept_thread = 0;
    }
#endif
    log_message(LOG_LEVEL_DEBUG, "Accept thread stopped.");

    // Signal handler threads to stop and close connections
#ifdef _WIN32
    EnterCriticalSection(&data->client_mutex);
#else
    pthread_mutex_lock(&data->client_mutex);
#endif
    for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
        if (data->clients[i].active) {
            data->clients[i].should_stop = true; // Signal handler thread to stop
            // Shutdown might be cleaner than just close_socket to unblock recv
#ifdef _WIN32
            shutdown(data->clients[i].socket, SD_BOTH);
#else
            shutdown(data->clients[i].socket, SHUT_RDWR);
#endif
            // Note: Closing the socket here might be redundant if shutdown works,
            // but it ensures the resource is released. The handler thread will
            // also call close_socket upon exiting its loop.
            close_socket(data->clients[i].socket);

#ifdef _WIN32
            // Wait for handler threads?
            if (data->clients[i].thread_handle) {
                 WaitForSingleObject(data->clients[i].thread_handle, 1000); // Wait 1 sec
                 CloseHandle(data->clients[i].thread_handle);
                 data->clients[i].thread_handle = NULL;
            }
#else
            // Threads were detached, cannot join. Assume they exit on socket close/error/stop signal.
#endif
        } // end if(data->clients[i].active)
    } // end for loop

    // Clean up mutex and stop pipe
#ifdef _WIN32
    LeaveCriticalSection(&data->client_mutex);
    DeleteCriticalSection(&data->client_mutex);
#else
    pthread_mutex_unlock(&data->client_mutex);
    pthread_mutex_destroy(&data->client_mutex);
    close_stop_pipe(data); // Close pipe FDs
#endif

    log_message(LOG_LEVEL_INFO, "TCP Transport stopped.");

    // Cleanup Winsock on Windows
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

static void tcp_transport_destroy(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) return;
     mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

     tcp_transport_stop(transport); // Ensure everything is stopped and cleaned

     free(data->host);
     // Destroy the buffer pool
     mcp_buffer_pool_destroy(data->buffer_pool);
     free(data);
     transport->transport_data = NULL;
     // Generic destroy will free the transport struct itself
}


// --- Public Creation Function ---

mcp_transport_t* mcp_transport_tcp_create(const char* host, uint16_t port, uint32_t idle_timeout_ms) {
    if (host == NULL) return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) return NULL;

    mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)calloc(1, sizeof(mcp_tcp_transport_data_t)); // Use calloc for zero-init
    if (tcp_data == NULL) {
        free(transport);
         return NULL;
     }

     tcp_data->host = mcp_strdup(host); // Use helper
     if (tcp_data->host == NULL) {
         free(tcp_data);
         free(transport);
         return NULL;
     }

     tcp_data->port = port;
     tcp_data->idle_timeout_ms = idle_timeout_ms; // Store timeout
     tcp_data->listen_socket = INVALID_SOCKET;
     tcp_data->running = false;
     tcp_data->buffer_pool = NULL; // Initialize pool pointer
     // Client array is zero-initialized by calloc
 #ifndef _WIN32
     tcp_data->stop_pipe[0] = -1; // Initialize pipe FDs
     tcp_data->stop_pipe[1] = -1;
 #endif

     // Create the buffer pool
     tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
     if (tcp_data->buffer_pool == NULL) {
         log_message(LOG_LEVEL_ERROR, "Failed to create buffer pool for TCP transport.");
         free(tcp_data->host);
         free(tcp_data);
         free(transport);
         return NULL;
     }

     // Initialize function pointers
    transport->start = tcp_transport_start;
    transport->stop = tcp_transport_stop;
    transport->send = NULL; // Set send to NULL, it's not used by server transport
    transport->receive = NULL; // Set receive to NULL, not used by server transport
    transport->destroy = tcp_transport_destroy;
    transport->transport_data = tcp_data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL; // Initialize error callback

    return transport;
}
