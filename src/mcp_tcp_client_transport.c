#include "mcp_types.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_transport_internal.h"
#include "mcp_log.h"
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

// Max message size limit for sanity check
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit

// Internal structure for TCP client transport data
typedef struct {
    char* host;
    uint16_t port;
    socket_t sock;
    bool running;
    bool connected; // Track connection state
    mcp_transport_t* transport_handle; // Pointer back to the main handle
#ifdef _WIN32
    HANDLE receive_thread;
#else
    pthread_t receive_thread;
#endif
} mcp_tcp_client_transport_data_t;


// --- Forward Declarations for Static Functions ---
static int tcp_client_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data);
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

// Helper function to send exactly n bytes to a socket
static int send_exact_client(socket_t sock, const char* buf, size_t len, bool* running_flag) {
    size_t total_sent = 0;
    while (total_sent < len) {
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
    return 0; // Return 0 on success, -1 on error, -2 on interrupt
}

// Helper function to read exactly n bytes from a socket
static int recv_exact_client(socket_t sock, char* buf, size_t len, bool* running_flag) {
    size_t total_read = 0;
    while (total_read < len) {
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
    return 1; // Return 1 on success, 0 on close, -1 on error, -2 on interrupt
}

// Thread function to handle receiving messages from the server
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
    int read_result;

    log_message(LOG_LEVEL_DEBUG, "TCP Client receive thread started for socket %d", (int)data->sock);

    while (data->running && data->connected) {
        // 1. Read the 4-byte length prefix
        read_result = recv_exact_client(data->sock, length_buf, (size_t)4, &data->running); // Cast 4 to size_t

        if (read_result == SOCKET_ERROR || read_result == 0) { // Error or connection closed
             if (data->running) { // Avoid error log if stopping intentionally
                 if (read_result == 0) {
                     log_message(LOG_LEVEL_INFO, "Server disconnected socket %d.", (int)data->sock);
                 } else {
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
             break; // Exit thread
        } else if (read_result == -2) {
             log_message(LOG_LEVEL_DEBUG, "Client receive thread for socket %d interrupted by stop signal.", (int)data->sock);
             break; // Interrupted by stop
        }
        // read_result == 1 means success

        // 2. Decode length (Network to Host byte order)
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);

        // 3. Sanity check length
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "Invalid message length received from server: %u on socket %d", message_length_host, (int)data->sock);
             data->connected = false; // Mark as disconnected
             break; // Invalid length, stop processing
        }

        // 4. Allocate buffer for message body
        message_buf = (char*)malloc(message_length_host + 1); // +1 for null terminator
        if (message_buf == NULL) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer for message size %u on socket %d", message_length_host, (int)data->sock);
             data->connected = false; // Mark as disconnected
             break; // Allocation failure
        }

        // 5. Read the message body
        read_result = recv_exact_client(data->sock, message_buf, (size_t)message_length_host, &data->running); // Cast uint32_t to size_t

         if (read_result == SOCKET_ERROR || read_result == 0) { // Error or connection closed
             if (data->running) {
                 if (read_result == 0) {
                     log_message(LOG_LEVEL_INFO, "Server disconnected socket %d while reading body.", (int)data->sock);
                 } else {
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
             free(message_buf);
             data->connected = false;
             break; // Exit thread
         } else if (read_result == -2) {
             log_message(LOG_LEVEL_DEBUG, "Client receive thread for socket %d interrupted by stop signal.", (int)data->sock);
             free(message_buf);
             break; // Interrupted by stop
         }
         // read_result == 1 means success

        // 6. Null-terminate and process the message via callback
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

        // 7. Free the message buffer for the next message
        free(message_buf);
        message_buf = NULL;

    } // End of main while loop

    log_message(LOG_LEVEL_DEBUG, "TCP Client receive thread exiting for socket %d.", (int)data->sock);
    data->connected = false; // Ensure disconnected state

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// Connect to the server
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


static int tcp_client_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data) {
    (void)message_callback; // Stored in transport struct
    (void)user_data;        // Stored in transport struct

    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

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
// Send data with length prefix framing (caller prepares the frame)
static int tcp_client_transport_send(mcp_transport_t* transport, const void* data_to_send, size_t size) {
    if (transport == NULL || transport->transport_data == NULL || data_to_send == NULL || size == 0) {
        return -1;
    }
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected) {
        log_message(LOG_LEVEL_ERROR, "Cannot send: TCP client transport not running or not connected.");
        return -1;
    }

    // Directly send the buffer prepared by the caller (which includes the length prefix)
    int send_status = send_exact_client(data->sock, (const char*)data_to_send, size, &data->running);

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

    tcp_data->host = strdup(host);
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

    // Initialize function pointers
    transport->start = tcp_client_transport_start;
    transport->stop = tcp_client_transport_stop;
    transport->send = tcp_client_transport_send; // Use the client send function
    transport->receive = NULL; // Synchronous receive not supported/used by client transport
    transport->destroy = tcp_client_transport_destroy;
    transport->transport_data = tcp_data;
    transport->message_callback = NULL; // Set by start
    transport->callback_user_data = NULL; // Set by start

    return transport;
}
