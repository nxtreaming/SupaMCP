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
#   include <ws2tcpip.h> // For InetNtop
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    typedef int socklen_t;
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h> // For inet_ntop
#   include <unistd.h>
#   include <pthread.h>
#   include <fcntl.h> // For non-blocking socket
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define close_socket close
    #define sock_errno errno
#endif

// Max line length for reading from socket (simple framing)
#define MAX_SOCKET_LINE_LENGTH 4096
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
#endif
} mcp_tcp_transport_data_t;


// --- Forward Declarations for Static Functions ---
static int tcp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data);
static int tcp_transport_stop(mcp_transport_t* transport);
static int tcp_transport_send(mcp_transport_t* transport, const void* data, size_t size); // Note: Server send needs target client
static void tcp_transport_destroy(mcp_transport_t* transport);
#ifdef _WIN32
static DWORD WINAPI tcp_accept_thread_func(LPVOID arg);
static DWORD WINAPI tcp_client_handler_thread_func(LPVOID arg);
#else
static void* tcp_accept_thread_func(void* arg);
static void* tcp_client_handler_thread_func(void* arg);
#endif
static void initialize_winsock(); // Helper for Windows


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

// Placeholder: Actual send needs to target a specific client connection
static int tcp_transport_send(mcp_transport_t* transport, const void* data_to_send, size_t size) {
    (void)transport;
    (void)data_to_send;
    (void)size;
    log_message(LOG_LEVEL_WARN, "Server-side TCP send not fully implemented (needs client target).");
    // In a real server, you'd need a mechanism to map a response back to the
    // correct client socket and send it there, likely involving locking the client list.
    return -1; // Not implemented for server broadcast
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
    char buffer[MAX_SOCKET_LINE_LENGTH];
    int bytes_received;

#ifdef _WIN32
    log_message(LOG_LEVEL_DEBUG, "Client handler started for socket %p", (void*)client_conn->socket);
#else
    log_message(LOG_LEVEL_DEBUG, "Client handler started for socket %d", client_conn->socket);
#endif

    // Use recv directly for better control
    while (tcp_data->running && client_conn->active) {
        bytes_received = recv(client_conn->socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received == SOCKET_ERROR) {
            // Only log error if we are supposed to be running
            if (tcp_data->running && client_conn->active) {
                char err_buf[128];
#ifdef _WIN32
                strerror_s(err_buf, sizeof(err_buf), sock_errno);
                log_message(LOG_LEVEL_ERROR, "recv failed for socket %p: %d (%s)", (void*)client_conn->socket, sock_errno, err_buf);
#else
                // Use XSI-compliant strerror_r
                if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                    log_message(LOG_LEVEL_ERROR, "recv failed for socket %d: %d (%s)", client_conn->socket, sock_errno, err_buf);
                } else {
                    log_message(LOG_LEVEL_ERROR, "recv failed for socket %d: %d (strerror_r failed)", client_conn->socket, sock_errno);
                }
#endif
            }
            break;
        } else if (bytes_received == 0) {
#ifdef _WIN32
            log_message(LOG_LEVEL_INFO, "Client disconnected socket %p", (void*)client_conn->socket);
#else
            log_message(LOG_LEVEL_INFO, "Client disconnected socket %d", client_conn->socket);
#endif
            break; // Connection closed by client
        } else {
            buffer[bytes_received] = '\0'; // Null-terminate received data
            // TODO: Handle message framing - this assumes newline termination for now
            // This simple approach will break if messages don't have newlines or are sent fragmented.
            char *line_start = buffer;
            char *line_end;
            while((line_end = strchr(line_start, '\n')) != NULL) {
                *line_end = '\0'; // Null-terminate the line
                size_t len = line_end - line_start;
                if (len > 0 && transport->message_callback != NULL) {
                     if (transport->message_callback(transport->callback_user_data, line_start, len) != 0) {
                        log_message(LOG_LEVEL_WARN, "Message callback failed for data from socket %d", (int)client_conn->socket);
                     }
                }
                line_start = line_end + 1; // Move to the start of the next potential line
                 if (*line_start == '\0') break; // End of buffer
            }
             // TODO: Need buffer management for partial lines received without newline
             // If line_start still points to data, it's a partial line.
        }
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
    // Close the thread handle on Windows? Only if not detached.
    // If CreateThread was used, CloseHandle is needed eventually.
    // CloseHandle(client_conn->thread_handle); // Maybe do this in stop/destroy?
    client_conn->thread_handle = NULL;
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

    while (data->running) {
        client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

        if (!data->running) break; // Check flag again after blocking accept

        if (client_socket == INVALID_SOCKET) {
            if (data->running) { // Avoid error message if stopped intentionally
                 char err_buf[128];
#ifdef _WIN32
                 strerror_s(err_buf, sizeof(err_buf), sock_errno);
                 log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
#else
                 if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                    log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
                 } else {
                    log_message(LOG_LEVEL_ERROR, "accept failed: %d (strerror_r failed)", sock_errno);
                 }
#endif
            }
            // Consider adding a small delay before retrying on certain errors?
            continue;
        }

        // Convert client IP to string
        const char* client_ip = NULL;
#ifdef _WIN32
        // InetNtop requires Ws2_32.lib, already linked via pragma
        if (InetNtop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
             client_ip = client_ip_str;
        } else {
             log_message(LOG_LEVEL_WARN, "InetNtop failed: %d", sock_errno);
             client_ip = "?.?.?.?";
        }
#else
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
            client_ip = client_ip_str;
        } else {
             char err_buf[128];
#ifdef _WIN32
             strerror_s(err_buf, sizeof(err_buf), sock_errno);
             log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (%s)", sock_errno, err_buf);
#else
             if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                 log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (%s)", sock_errno, err_buf);
             } else {
                 log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (strerror_r failed)", sock_errno);
             }
#endif
            client_ip = "?.?.?.?";
        }
#endif

#ifdef _WIN32
        log_message(LOG_LEVEL_INFO, "Accepted connection from %s:%d on socket %p",
               client_ip, ntohs(client_addr.sin_port), (void*)client_socket);
#else
         log_message(LOG_LEVEL_INFO, "Accepted connection from %s:%d on socket %d",
               client_ip, ntohs(client_addr.sin_port), client_socket);
#endif

        // Find an empty slot for the new client
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

            // Create a handler thread for this client
#ifdef _WIN32
            client_conn->thread_handle = CreateThread(NULL, 0, tcp_client_handler_thread_func, client_conn, 0, NULL);
            if (client_conn->thread_handle == NULL) {
                log_message(LOG_LEVEL_ERROR, "Failed to create handler thread for client %d.", client_index);
                close_socket(client_socket);
                client_conn->active = false; // Free the slot
            }
#else
            if (pthread_create(&client_conn->thread_handle, NULL, tcp_client_handler_thread_func, client_conn) != 0) {
                 log_message(LOG_LEVEL_ERROR, "Failed to create handler thread: %s", strerror(errno));
                 close_socket(client_socket);
                 client_conn->active = false; // Free the slot
            } else {
                 pthread_detach(client_conn->thread_handle); // Detach thread so resources are cleaned up automatically
            }
#endif
        } else {
            log_message(LOG_LEVEL_WARN, "Max clients (%d) reached, rejecting connection from %s:%d",
                    MAX_TCP_CLIENTS, client_ip, ntohs(client_addr.sin_port));
            close_socket(client_socket);
        }
    }

    log_message(LOG_LEVEL_INFO, "Accept thread exiting.");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}


static int tcp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data) {
    (void)message_callback; // Stored in transport struct
    (void)user_data;        // Stored in transport struct

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

    // Initialize mutex/critical section
#ifdef _WIN32
    InitializeCriticalSection(&data->client_mutex);
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

    // Close the listening socket to interrupt the accept() call
    if (data->listen_socket != INVALID_SOCKET) {
        close_socket(data->listen_socket);
        data->listen_socket = INVALID_SOCKET;
    }

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
        // Send a signal or use other IPC to wake up accept if blocked indefinitely?
        // For now, rely on closing the socket and joining.
        pthread_join(data->accept_thread, NULL);
        data->accept_thread = 0;
    }
#endif
    log_message(LOG_LEVEL_DEBUG, "Accept thread stopped.");

    // Close all active client connections and signal handler threads
#ifdef _WIN32
    EnterCriticalSection(&data->client_mutex);
#else
    pthread_mutex_lock(&data->client_mutex);
#endif
    for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
        if (data->clients[i].active) {
            data->clients[i].active = false; // Signal handler thread to stop
            // Shutdown might be cleaner than just close_socket
#ifdef _WIN32
            shutdown(data->clients[i].socket, SD_BOTH);
#else
            shutdown(data->clients[i].socket, SHUT_RDWR);
#endif
            close_socket(data->clients[i].socket);
#ifdef _WIN32
            // Wait for handler threads?
            if (data->clients[i].thread_handle) {
                 WaitForSingleObject(data->clients[i].thread_handle, 1000); // Wait 1 sec
                 CloseHandle(data->clients[i].thread_handle);
                 data->clients[i].thread_handle = NULL;
            }
#else
            // Threads were detached, cannot join. Assume they exit on socket close/error.
#endif
        }
    }
#ifdef _WIN32
    LeaveCriticalSection(&data->client_mutex);
    DeleteCriticalSection(&data->client_mutex);
#else
    pthread_mutex_unlock(&data->client_mutex);
    pthread_mutex_destroy(&data->client_mutex);
#endif
    log_message(LOG_LEVEL_INFO, "TCP Transport stopped.");

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

    tcp_data->host = strdup(host);
    if (tcp_data->host == NULL) {
        free(tcp_data);
        free(transport);
        return NULL;
    }

    tcp_data->port = port;
    tcp_data->listen_socket = INVALID_SOCKET;
    tcp_data->running = false;
    // Client array is zero-initialized by calloc

    // Initialize function pointers
    transport->start = tcp_transport_start;
    transport->stop = tcp_transport_stop;
    transport->send = tcp_transport_send; // Assign placeholder send
    transport->destroy = tcp_transport_destroy;
    transport->transport_data = tcp_data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;

    return transport;
}
