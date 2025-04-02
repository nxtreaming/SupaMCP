#include "mcp_types.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_buffer_pool.h"
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
    #define sleep_ms(ms) Sleep(ms)
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <pthread.h>
#   include <fcntl.h>
#   include <netdb.h> // For getaddrinfo
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define close_socket close
    #define sock_errno errno
    #define sleep_ms(ms) usleep(ms * 1000)
#endif

// Max message size limit for sanity check (remains relevant)
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit
// Buffer pool configuration (use same constants as server for consistency)
#define POOL_BUFFER_SIZE (1024 * 8) // 8KB buffer size
#define POOL_NUM_BUFFERS 16         // Number of buffers in the pool

// Internal structure for TCP client transport data
typedef struct {
    char* host;
    uint16_t port;
    socket_t sock;
    bool running;
    bool connected; // Track connection state
    mcp_transport_t* transport_handle; // Pointer back to the main handle (contains callbacks)
#ifdef _WIN32
    HANDLE receive_thread;
#else
    pthread_t receive_thread;
#endif
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for message buffers
} mcp_tcp_client_transport_data_t;


// --- Forward Declarations for Static Functions ---
static int tcp_client_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data, mcp_transport_error_callback_t error_callback);
static int tcp_client_transport_stop(mcp_transport_t* transport);
static int tcp_client_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static void tcp_client_transport_destroy(mcp_transport_t* transport);
#ifdef _WIN32
static DWORD WINAPI tcp_client_receive_thread_func(LPVOID arg);
#else
static void* tcp_client_receive_thread_func(void* arg);
#endif
static void initialize_winsock_client(); // Helper for Windows
static int connect_to_server(mcp_tcp_client_transport_data_t* data); // Connection helper
static int send_exact_client(socket_t sock, const char* buf, size_t len, bool* running_flag);
static int recv_exact_client(socket_t sock, char* buf, size_t len, bool* running_flag);


// --- Static Implementation Functions ---

/**
 * @internal
 * @brief Initializes Winsock if on Windows. No-op otherwise.
 */
#ifdef _WIN32
static void initialize_winsock_client() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        fprintf(stderr, "[MCP TCP Client] WSAStartup failed: %d\n", iResult);
        // Don't exit here, let create function handle failure
    }
}
#else
static void initialize_winsock_client() { /* No-op on non-Windows */ }
#endif

/**
 * @internal
 * @brief Helper function to reliably send a specified number of bytes over a socket.
 * Handles potential partial sends. Checks the running flag for interruption.
 * @param sock The socket descriptor.
 * @param buf Buffer containing the data to send.
 * @param len Number of bytes to send.
 * @param running_flag Pointer to the transport's running flag for interruption check.
 * @return 0 on success, -1 on socket error, -2 if interrupted by stop signal.
 */
static int send_exact_client(socket_t sock, const char* buf, size_t len, bool* running_flag) {
    size_t total_sent = 0;
    while (total_sent < len) {
        // Check if the transport has been stopped
        if (running_flag && !(*running_flag)) return -2; // Interrupted by stop signal

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
    return 0;
}

/**
 * @internal
 * @brief Helper function to reliably receive a specified number of bytes from a socket.
 * Handles potential partial reads. Checks the running flag for interruption.
 * @param sock The socket descriptor.
 * @param buf Buffer to store the received data.
 * @param len Number of bytes to receive.
 * @param running_flag Pointer to the transport's running flag for interruption check.
 * @return 1 on success, 0 on graceful connection close, -1 on socket error, -2 if interrupted by stop signal.
 */
static int recv_exact_client(socket_t sock, char* buf, size_t len, bool* running_flag) {
    size_t total_read = 0;
    while (total_read < len) {
        // Check if the transport has been stopped
        if (running_flag && !(*running_flag)) return -2; // Interrupted by stop signal

#ifdef _WIN32
        int chunk_len = (len - total_read > INT_MAX) ? INT_MAX : (int)(len - total_read);
        int bytes_read = recv(sock, buf + total_read, chunk_len, 0);
#else
         ssize_t bytes_read = recv(sock, buf + total_read, len - total_read, 0);
#endif

        if (bytes_read == SOCKET_ERROR) {
            return -1; // Socket error
        } else if (bytes_read == 0) {
            return 0;  // Connection closed gracefully
        }
        total_read += bytes_read;
    }
    return 1;
}

/**
 * @internal
 * @brief Background thread function responsible for receiving messages from the server.
 * Reads messages using length-prefix framing, calls the message callback for processing,
 * and calls the error callback if fatal transport errors occur.
 * @param arg Pointer to the mcp_transport_t handle.
 * @return 0 on Windows, NULL on POSIX.
 */
#ifdef _WIN32
static DWORD WINAPI tcp_client_receive_thread_func(LPVOID arg) {
#else
static void* tcp_client_receive_thread_func(void* arg) {
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
        // 1. Read the 4-byte length prefix (network byte order)
        read_result = recv_exact_client(data->sock, length_buf, (size_t)4, &data->running);

        // Check result of reading the length prefix
        if (read_result == SOCKET_ERROR || read_result == 0) { // Error or connection closed
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

        // 2. Decode length (Network to Host byte order)
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);

        // 3. Sanity check length
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

        // 4. Allocate buffer for message body using buffer pool or malloc
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

        // 5. Read the message body
        read_result = recv_exact_client(data->sock, message_buf, (size_t)message_length_host, &data->running);

         // Check result of reading the message body
         if (read_result == SOCKET_ERROR || read_result == 0) { // Error or connection closed
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

        // 6. Null-terminate the received data and pass it to the message callback
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

        // 7. Free or release the message buffer
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

/**
 * @internal
 * @brief Establishes a TCP connection to the configured server host and port.
 * Uses getaddrinfo for hostname resolution and attempts connection.
 * @param data Pointer to the transport's internal data structure containing host, port, and socket field.
 * @return 0 on successful connection, -1 on failure.
 */
static int connect_to_server(mcp_tcp_client_transport_data_t* data) {
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char port_str[6]; // Max port length is 5 digits + null terminator

    snprintf(port_str, sizeof(port_str), "%u", data->port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // Force IPv4 to match server listener
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(data->host, port_str, &hints, &servinfo)) != 0) {
        log_message(LOG_LEVEL_ERROR, "getaddrinfo failed: %s", gai_strerror(rv));
        return -1;
    }

    // Loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((data->sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET) {
            log_message(LOG_LEVEL_WARN, "Client socket creation failed: %d", sock_errno);
            continue;
        }

        // Revert cast for connect
        if (connect(data->sock, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) {
            log_message(LOG_LEVEL_WARN, "Client connect failed: %d", sock_errno);
            close_socket(data->sock);
            data->sock = INVALID_SOCKET;
            continue;
        }

        break; // If we get here, we successfully connected
    }

    freeaddrinfo(servinfo); // All done with this structure

    if (p == NULL) {
        log_message(LOG_LEVEL_ERROR, "Client failed to connect to %s:%u", data->host, data->port);
        return -1;
    }

    log_message(LOG_LEVEL_INFO, "Client connected to %s:%u on socket %d", data->host, data->port, (int)data->sock);
    data->connected = true;
    return 0;
 }


 static int tcp_client_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
     if (transport == NULL || transport->transport_data == NULL) return -1;
     mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

     // Store callbacks and user data in the generic transport handle
     transport->message_callback = message_callback;
     transport->callback_user_data = user_data;
     transport->error_callback = error_callback;

    if (data->running) return 0; // Already running

    initialize_winsock_client(); // Initialize Winsock if on Windows

    // Attempt to connect
    if (connect_to_server(data) != 0) {
        return -1; // Connection failed
    }

    data->running = true;

    // Start receive thread
#ifdef _WIN32
    data->receive_thread = CreateThread(NULL, 0, tcp_client_receive_thread_func, transport, 0, NULL);
    if (data->receive_thread == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create client receive thread (Error: %lu).", GetLastError());
        close_socket(data->sock);
        data->sock = INVALID_SOCKET;
        data->connected = false;
        data->running = false;
        return -1;
    }
#else
     if (pthread_create(&data->receive_thread, NULL, tcp_client_receive_thread_func, transport) != 0) {
        char err_buf[128];
         if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to create client receive thread: %s", err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "Failed to create client receive thread: %d (strerror_r failed)", errno);
         }
        close_socket(data->sock);
        data->sock = INVALID_SOCKET;
        data->connected = false;
        data->running = false;
        return -1;
    }
#endif

    log_message(LOG_LEVEL_INFO, "TCP Client Transport started for %s:%d", data->host, data->port);
    return 0;
}

static int tcp_client_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running) return 0;

    log_message(LOG_LEVEL_INFO, "Stopping TCP Client Transport...");
    data->running = false; // Signal thread to stop

    // Close the socket to interrupt recv()
    if (data->sock != INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(data->sock, SD_BOTH);
#else
        shutdown(data->sock, SHUT_RDWR);
#endif
        close_socket(data->sock);
        data->sock = INVALID_SOCKET;
    }
    data->connected = false;

    // Wait for the receive thread to finish
#ifdef _WIN32
    if (data->receive_thread) {
        WaitForSingleObject(data->receive_thread, 2000); // Wait 2 seconds
        CloseHandle(data->receive_thread);
        data->receive_thread = NULL;
    }
#else
    if (data->receive_thread) {
        pthread_join(data->receive_thread, NULL);
        data->receive_thread = 0;
    }
#endif
    log_message(LOG_LEVEL_DEBUG, "Client receive thread stopped.");

    log_message(LOG_LEVEL_INFO, "TCP Client Transport stopped.");

    // Cleanup Winsock on Windows
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}

// Send data with length prefix framing
// Send data with length prefix framing (transport adds the frame)
static int tcp_client_transport_send(mcp_transport_t* transport, const void* payload_data, size_t payload_size) {
    if (transport == NULL || transport->transport_data == NULL || payload_data == NULL || payload_size == 0) {
        return -1;
    }
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected) {
        log_message(LOG_LEVEL_ERROR, "Cannot send: TCP client transport not running or not connected.");
        return -1;
    }

    // Check payload size limit
    if (payload_size > MAX_MCP_MESSAGE_SIZE) {
        log_message(LOG_LEVEL_ERROR, "Cannot send: Payload size (%zu) exceeds limit (%d).", payload_size, MAX_MCP_MESSAGE_SIZE);
        return -1;
    }

    // Prepare buffer with 4-byte length prefix + payload
    uint32_t net_len = htonl((uint32_t)payload_size);
    size_t total_size = sizeof(net_len) + payload_size;
    char* send_buffer = (char*)malloc(total_size);
    if (!send_buffer) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate send buffer for TCP client transport.");
        return -1;
    }

    // Copy length prefix and payload
    memcpy(send_buffer, &net_len, sizeof(net_len));
    memcpy(send_buffer + sizeof(net_len), payload_data, payload_size);

    // Send the combined buffer
    int send_status = send_exact_client(data->sock, send_buffer, total_size, &data->running);

    free(send_buffer); // Free the temporary buffer

    if (send_status != 0) {
        log_message(LOG_LEVEL_ERROR, "send_exact_client failed (status: %d)", send_status);
        // Assume connection is broken if send failed
        data->connected = false;
        // Consider stopping the transport? For now, just return error.
        return -1;
    }

    return 0; // Success
}


static void tcp_client_transport_destroy(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) return;
     mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

     tcp_client_transport_stop(transport); // Ensure everything is stopped and cleaned

     free(data->host);
     // Destroy the buffer pool
     mcp_buffer_pool_destroy(data->buffer_pool);
     free(data);
     transport->transport_data = NULL;
     // Generic destroy will free the transport struct itself
}


// --- Public Creation Function ---

mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port) {
    if (host == NULL) return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) return NULL;

    mcp_tcp_client_transport_data_t* tcp_data = (mcp_tcp_client_transport_data_t*)calloc(1, sizeof(mcp_tcp_client_transport_data_t)); // Use calloc for zero-init
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
     tcp_data->sock = INVALID_SOCKET;
     tcp_data->running = false;
     tcp_data->connected = false;
     tcp_data->transport_handle = transport; // Link back
     tcp_data->buffer_pool = NULL; // Initialize pool pointer

     // Create the buffer pool
     tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
     if (tcp_data->buffer_pool == NULL) {
         log_message(LOG_LEVEL_ERROR, "Failed to create buffer pool for TCP client transport.");
         free(tcp_data->host);
         free(tcp_data);
         free(transport);
         return NULL;
     }

     // Initialize function pointers
    transport->start = tcp_client_transport_start;
    transport->stop = tcp_client_transport_stop;
    transport->send = tcp_client_transport_send; // Use the client send function
    transport->receive = NULL; // Synchronous receive not supported/used by client transport
     transport->destroy = tcp_client_transport_destroy;
     transport->transport_data = tcp_data;
     transport->message_callback = NULL; // Set by start
     transport->callback_user_data = NULL; // Set by start
     transport->error_callback = NULL; // Set by start

     return transport;
 }
