#ifndef MCP_TCP_TRANSPORT_INTERNAL_H
#define MCP_TCP_TRANSPORT_INTERNAL_H

#include "mcp_tcp_transport.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Platform-specific socket includes and definitions
#ifdef _WIN32
// Include winsock2.h FIRST
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    typedef int socklen_t;
    typedef HANDLE thread_handle_t;
    typedef CRITICAL_SECTION mutex_t;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERROR_VAL   SOCKET_ERROR
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
    #define SEND_FLAGS 0 // No MSG_NOSIGNAL on Windows
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <pthread.h>
#   include <fcntl.h>
#   include <sys/select.h>
#   include <poll.h>
#   include <errno.h>
    typedef int socket_t;
    typedef pthread_t thread_handle_t;
    typedef pthread_mutex_t mutex_t;
    #define INVALID_SOCKET_VAL (-1)
    #define SOCKET_ERROR_VAL   (-1)
    #define close_socket close
    #define sock_errno errno
    #define SEND_FLAGS MSG_NOSIGNAL // Prevent SIGPIPE
#endif

// Constants
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit
#define MAX_TCP_CLIENTS 10
#define POOL_BUFFER_SIZE (1024 * 8) // 8KB buffer size
#define POOL_NUM_BUFFERS 16         // Number of buffers in the pool

// --- Structures ---

// Forward declare transport struct
typedef struct mcp_transport mcp_transport_t;

// Enum for client connection state
typedef enum {
    CLIENT_STATE_INACTIVE,     // Slot is free
    CLIENT_STATE_INITIALIZING, // Slot assigned, thread being created
    CLIENT_STATE_ACTIVE        // Handler thread running
} client_state_t;

// Structure to hold info about a connected client
typedef struct {
    socket_t socket;
    struct sockaddr_in address; // Assuming IPv4
    client_state_t state;       // Current state of the connection slot
    time_t last_activity_time;
    thread_handle_t thread_handle;
    mcp_transport_t* transport; // Pointer back to parent transport
    bool should_stop;           // Flag to signal this specific handler thread
} tcp_client_connection_t;

// Internal structure for TCP transport data
typedef struct {
    char* host;
    uint16_t port;
    uint32_t idle_timeout_ms;
    socket_t listen_socket;
    bool running;
    tcp_client_connection_t clients[MAX_TCP_CLIENTS];
    thread_handle_t accept_thread;
    mutex_t client_mutex;
#ifndef _WIN32
    int stop_pipe[2]; // Pipe used to signal accept thread on POSIX
#endif
    mcp_buffer_pool_t* buffer_pool;
} mcp_tcp_transport_data_t;


// --- Internal Function Prototypes ---

// From mcp_tcp_socket_utils.c
void initialize_winsock();
void cleanup_winsock(); // Added for WSACleanup
int send_exact(socket_t sock, const char* buf, size_t len, bool* client_should_stop_flag);
int recv_exact(socket_t sock, char* buf, int len, bool* client_should_stop_flag);
int wait_for_socket_read(socket_t sock, uint32_t timeout_ms, bool* should_stop);
#ifndef _WIN32
void close_stop_pipe(mcp_tcp_transport_data_t* data);
#endif

// From mcp_tcp_client_handler.c
#ifdef _WIN32
DWORD WINAPI tcp_client_handler_thread_func(LPVOID arg);
#else
void* tcp_client_handler_thread_func(void* arg);
#endif

// From mcp_tcp_acceptor.c
#ifdef _WIN32
DWORD WINAPI tcp_accept_thread_func(LPVOID arg);
#else
void* tcp_accept_thread_func(void* arg);
#endif

// From mcp_tcp_transport.c (These remain static but need prototypes if called internally, though likely not)
// static int tcp_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data, mcp_transport_error_callback_t error_callback);
// static int tcp_transport_stop(mcp_transport_t* transport);
// static void tcp_transport_destroy(mcp_transport_t* transport);


#endif // MCP_TCP_TRANSPORT_INTERNAL_H
