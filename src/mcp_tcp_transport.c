#include "mcp_tcp_transport.h"
#include "mcp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

// Platform-specific socket includes
#ifdef _WIN32
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    typedef int socklen_t;
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <pthread.h>
#   include <fcntl.h>
#   include <sys/select.h> // For select()
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define close_socket close
    #define sock_errno errno
#endif

// Max message size limit for sanity check
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit
// Max number of concurrent clients (simple example)
#define MAX_TCP_CLIENTS 10

// Structure to hold info about a connected client
typedef struct {
    socket_t socket;
    struct sockaddr_in address; // Assuming IPv4 for simplicity
    bool active;
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
} mcp_tcp_transport_data_t;


// --- Forward Declarations for Static Functions ---
// Note: tcp_transport_start signature matches the updated mcp_transport.h/mcp_transport_internal.h
static int tcp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data, mcp_transport_error_callback_t error_callback);
static int tcp_transport_stop(mcp_transport_t* transport);
// static int tcp_transport_send(mcp_transport_t* transport, const void* data, size_t size); // Removed - sending handled by client handler
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
static int send_exact(socket_t sock, const char* buf, size_t len, bool* client_should_stop_flag) {
    size_t total_sent = 0;
    while (total_sent < len) {
        if (client_should_stop_flag && *client_should_stop_flag) return -2; // Interrupted by stop signal

        // Note: send() typically takes int/ssize_t for length. We might need to cast
        // or send in chunks if len > INT_MAX (highly unlikely for JSON-RPC).
        // For simplicity, assume len fits in send()'s length parameter type.
        // On Windows, send takes int. On POSIX, it takes size_t.
#ifdef _WIN32
        int chunk_len = (len - total_sent > INT_MAX) ? INT_MAX : (int)(len - total_sent);
        int bytes_sent = send(sock, buf + total_sent, chunk_len, 0);
#else
        ssize_t bytes_sent = send(sock, buf + total_sent, len - total_sent, 0);
#endif

        if (bytes_sent == SOCKET_ERROR) {
            return -1; // Socket error
        }
        total_sent += bytes_sent;
    }
    return (int)total_sent; // Should be equal to len if successful
}


// Helper function to read exactly n bytes from a socket (checks client stop flag)
static int recv_exact(socket_t sock, char* buf, int len, bool* client_should_stop_flag) {
    int total_read = 0;
    while (total_read < len) {
        if (client_should_stop_flag && *client_should_stop_flag) return -2; // Interrupted by stop signal

        int bytes_read = recv(sock, buf + total_read, len - total_read, 0);
        if (bytes_read == SOCKET_ERROR) {
            return -1; // Socket error
        } else if (bytes_read == 0) {
            return 0;  // Connection closed gracefully
        }
        total_read += bytes_read;
    }
    return total_read; // Should be equal to len if successful
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
    int read_result;
    client_conn->should_stop = false; // Initialize stop flag for this handler

#ifdef _WIN32
    log_message(LOG_LEVEL_DEBUG, "Client handler started for socket %p", (void*)client_conn->socket);
#else
    log_message(LOG_LEVEL_DEBUG, "Client handler started for socket %d", client_conn->socket);
#endif

    // Main receive loop with length prefix framing
    while (!client_conn->should_stop && client_conn->active) { // Check both flags
        // 1. Read the 4-byte length prefix
        // Use select/poll for non-blocking read with timeout and stop signal check?
        // For simplicity, stick with blocking recv_exact for now, stop signal checked inside.
        read_result = recv_exact(client_conn->socket, length_buf, 4, &client_conn->should_stop); // Pass client stop flag

        if (read_result == SOCKET_ERROR) {
             if (!client_conn->should_stop) { // Avoid error log if stopping
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
        } else if (read_result == 0) { // Connection closed gracefully
#ifdef _WIN32
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %p while reading length", (void*)client_conn->socket);
#else
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %d while reading length", client_conn->socket);
#endif
             goto client_cleanup;
        } else if (read_result == -2) { // Interrupted by stop signal
             log_message(LOG_LEVEL_DEBUG, "Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             goto client_cleanup;
        } else if (read_result != 4) { // Should not happen with recv_exact logic
             log_message(LOG_LEVEL_ERROR, "Incomplete length received (%d bytes) for socket %d", read_result, (int)client_conn->socket);
             goto client_cleanup; // Should not happen with recv_exact logic
        }


        // 2. Decode length (Network to Host byte order)
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);

        // 3. Sanity check length
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "Invalid message length received: %u on socket %d", message_length_host, (int)client_conn->socket);
             goto client_cleanup; // Invalid length, close connection
        }

        // 4. Allocate buffer for message body
        message_buf = (char*)malloc(message_length_host + 1); // +1 for null terminator
        if (message_buf == NULL) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer for message size %u on socket %d", message_length_host, (int)client_conn->socket);
             goto client_cleanup; // Allocation failure
        }

        // 5. Read the message body
        read_result = recv_exact(client_conn->socket, message_buf, message_length_host, &client_conn->should_stop);

         if (read_result == SOCKET_ERROR) {
             if (!client_conn->should_stop) { // Avoid error log if stopping
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
             goto client_cleanup; // Exit thread on error
         } else if (read_result == 0) { // Connection closed gracefully
#ifdef _WIN32
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %p while reading body", (void*)client_conn->socket);
#else
             log_message(LOG_LEVEL_INFO, "Client disconnected socket %d while reading body", client_conn->socket);
#endif
             goto client_cleanup; // Exit thread on disconnect
         } else if (read_result == -2) { // Interrupted by stop signal
             log_message(LOG_LEVEL_DEBUG, "Client handler for socket %d interrupted by stop signal.", (int)client_conn->socket);
             goto client_cleanup;
         } else if (read_result != (int)message_length_host) { // Should not happen
             log_message(LOG_LEVEL_ERROR, "Incomplete message body received (%d/%u bytes) for socket %d", read_result, message_length_host, (int)client_conn->socket);
             goto client_cleanup; // Should not happen with recv_exact logic
         }


        // 6. Null-terminate and process the message via callback
        message_buf[message_length_host] = '\0';
        char* response_str = NULL;
        int callback_error_code = 0;
        if (transport->message_callback != NULL) {
            response_str = transport->message_callback(transport->callback_user_data, message_buf, message_length_host, &callback_error_code);
            // response_str is malloc'd by the callback, or NULL
        }

        // 7. Free the received message buffer
        free(message_buf);
        message_buf = NULL;

        // 8. If callback returned a response string, send it back
        if (response_str != NULL) {
            size_t response_len = strlen(response_str);
            if (response_len > 0 && response_len <= MAX_MCP_MESSAGE_SIZE) {
                uint32_t net_len = htonl((uint32_t)response_len);
                size_t total_send_len = sizeof(net_len) + response_len;
                char* send_buffer = (char*)malloc(total_send_len);

                if (send_buffer) {
                    memcpy(send_buffer, &net_len, sizeof(net_len));
                    memcpy(send_buffer + sizeof(net_len), response_str, response_len);

                    // Pass size_t total_send_len directly
                    int send_result = send_exact(client_conn->socket, send_buffer, total_send_len, &client_conn->should_stop);
                    if (send_result == SOCKET_ERROR) { // send_exact returns -1 on error
                        if (!client_conn->should_stop) { // Avoid error log if stopping
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
                        free(send_buffer);
                        free(response_str); // Free callback response
                        goto client_cleanup; // Error sending response
                    } else if (send_result == -2) { // Interrupted by stop signal
                         free(send_buffer);
                         free(response_str);
                         goto client_cleanup;
                    }
                    free(send_buffer);
                } else {
                    log_message(LOG_LEVEL_ERROR, "Failed to allocate send buffer for response on socket %d", (int)client_conn->socket);
                    // Continue without sending response? Or disconnect? Disconnect for safety.
                    free(response_str);
                    goto client_cleanup;
                }
            } else if (response_len > MAX_MCP_MESSAGE_SIZE) {
                 log_message(LOG_LEVEL_ERROR, "Response generated by callback is too large (%zu bytes) for socket %d", response_len, (int)client_conn->socket);
                 // Disconnect client?
            }
            // Free the response string returned by the callback
            free(response_str);
            response_str = NULL;
        } else if (callback_error_code != 0) {
            // Callback indicated an error but didn't return a response string
            log_message(LOG_LEVEL_WARN, "Message callback indicated error (%d) but returned no response string for socket %d", callback_error_code, (int)client_conn->socket);
            // Decide if we should disconnect? For now, continue.
        }
        // If response_str was NULL and no error, it was likely a notification - no response needed.

    } // End of main while loop

client_cleanup:
    // Free buffer if loop exited unexpectedly
    free(message_buf);

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
    // Pthreads were detached, no need to join or manage handle here.
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
    socklen_t client_addr_len = sizeof(client_addr);
    socket_t client_socket;
    char client_ip_str[INET6_ADDRSTRLEN]; // Use INET6 for future-proofing

    log_message(LOG_LEVEL_INFO, "Accept thread started, listening on %s:%d", data->host, data->port);

#ifdef _WIN32
    // Windows: Use WSAEventSelect or similar for robust stop signal handling?
    // For now, stick to closing the socket to unblock accept.
    while (data->running) {
        client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

        if (!data->running) break; // Check flag again after blocking accept

        if (client_socket == INVALID_SOCKET) {
            if (data->running) { // Avoid error message if stopped intentionally
                 // Check if error is due to socket closure during stop
                 int error_code = sock_errno;
                 if (error_code != WSAEINTR && error_code != WSAENOTSOCK && error_code != WSAEINVAL) {
                     char err_buf[128];
                     strerror_s(err_buf, sizeof(err_buf), error_code);
                     log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", error_code, err_buf);
                 } else {
                      log_message(LOG_LEVEL_DEBUG, "Accept interrupted likely due to stop signal.");
                      break; // Exit loop if socket closed during stop
                 }
            } else {
                 break; // Exit if not running
            }
            continue; // Try again if it was a different error
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

#else // POSIX
    fd_set read_fds;
    int max_fd = (data->stop_pipe[0] > data->listen_socket) ? data->stop_pipe[0] : data->listen_socket;

    while (data->running) {
        FD_ZERO(&read_fds);
        FD_SET(data->stop_pipe[0], &read_fds); // Monitor stop pipe
        FD_SET(data->listen_socket, &read_fds); // Monitor listen socket

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (!data->running) break; // Check flag again after select

        if (activity < 0 && errno != EINTR) {
            log_message(LOG_LEVEL_ERROR, "select error in accept thread: %s", strerror(errno));
            continue; // Or break? Decide on error handling policy
        }

        // Check if stop pipe has data
        if (FD_ISSET(data->stop_pipe[0], &read_fds)) {
            log_message(LOG_LEVEL_DEBUG, "Stop signal received in accept thread.");
            // Drain the pipe
            char buf[16];
            while (read(data->stop_pipe[0], buf, sizeof(buf)) > 0);
            break; // Exit loop
        }

        // Check for incoming connection
        if (FD_ISSET(data->listen_socket, &read_fds)) {
            client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

            if (client_socket == INVALID_SOCKET) {
                 if (data->running) { // Avoid error message if stopped intentionally
                     char err_buf[128];
                     if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                        log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
                     } else {
                        log_message(LOG_LEVEL_ERROR, "accept failed: %d (strerror_r failed)", sock_errno);
                     }
                 }
                 continue; // Don't exit thread on accept error, just try again
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

#endif // Platform-specific accept loop end

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
                log_message(LOG_LEVEL_WARN, "Max clients (%d) reached, rejecting connection from %s:%d",
                        MAX_TCP_CLIENTS, client_ip, ntohs(client_addr.sin_port));
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
            // close_socket(data->clients[i].socket); // Keep or remove? Keep for safety.
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
     free(data);
     transport->transport_data = NULL;
     // Generic destroy will free the transport struct itself
}


// --- Public Creation Function ---

mcp_transport_t* mcp_transport_tcp_create(const char* host, uint16_t port) {
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
    tcp_data->listen_socket = INVALID_SOCKET;
    tcp_data->running = false;
    // Client array is zero-initialized by calloc
#ifndef _WIN32
    tcp_data->stop_pipe[0] = -1; // Initialize pipe FDs
    tcp_data->stop_pipe[1] = -1;
#endif

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
