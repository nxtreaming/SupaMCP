#ifndef MCP_TCP_TRANSPORT_INTERNAL_H
#define MCP_TCP_TRANSPORT_INTERNAL_H

// Platform-specific includes MUST come before standard headers that might include windows.h
#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERROR_VAL   SOCKET_ERROR
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
    #define SEND_FLAGS 0
    #include <windows.h>
    #define sleep_ms(ms) Sleep(ms)
#else // POSIX
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <poll.h>
#   include <sys/uio.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VAL (-1)
    #define SOCKET_ERROR_VAL   (-1)
    #define close_socket close
    #define sock_errno errno
    #define SEND_FLAGS MSG_NOSIGNAL
    #define sleep_ms(ms) usleep(ms * 1000)
#endif

// Now include project headers and standard libraries
#include "mcp_transport.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_buffer_pool.h"
#include "mcp_sync.h"
#include <mcp_thread_pool.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


// Constants
#define MAX_TCP_CLIENTS 64 // Max concurrent client connections for the server
#define POOL_BUFFER_SIZE (1024 * 64) // 64KB buffer size (Increased)
#define POOL_NUM_BUFFERS 64         // Increased number of buffers to match max clients
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit

// Client connection states
typedef enum {
    CLIENT_STATE_INACTIVE,
    CLIENT_STATE_INITIALIZING, // Slot assigned, thread starting
    CLIENT_STATE_ACTIVE        // Thread running, connection active
} client_state_t;

// Structure to hold information about a single client connection on the server
typedef struct {
    socket_t socket;
    struct sockaddr_in address;
    mcp_thread_t thread_handle; // Use abstracted thread type
    mcp_transport_t* transport; // Pointer back to the parent transport
    volatile bool should_stop;  // Flag to signal the handler thread to stop
    client_state_t state;       // Current state of this client slot
    time_t last_activity_time;  // Timestamp of the last read/write activity
} tcp_client_connection_t;

// Internal data structure for the TCP server transport
typedef struct {
    char* host;
    uint16_t port;
    socket_t listen_socket;
    volatile bool running;
    mcp_thread_t accept_thread; // Use abstracted thread type
    tcp_client_connection_t clients[MAX_TCP_CLIENTS];
    mcp_mutex_t* client_mutex; // Use abstracted mutex type
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for receive buffers
    uint32_t idle_timeout_ms; // Idle timeout for client connections
#ifndef _WIN32
    int stop_pipe[2]; // Pipe used to signal the accept thread to stop on POSIX
#endif
} mcp_tcp_transport_data_t;


// --- Internal Function Prototypes ---

// From mcp_tcp_socket_utils.c
void initialize_winsock();
void cleanup_winsock();
// Updated signatures to use volatile bool* for stop flags
int send_exact(socket_t sock, const char* buf, size_t len, volatile bool* client_should_stop_flag);
int recv_exact(socket_t sock, char* buf, int len, volatile bool* client_should_stop_flag);
int wait_for_socket_read(socket_t sock, uint32_t timeout_ms, volatile bool* should_stop);
// Declarations for vectored send helpers from mcp_tcp_socket_utils.c
#ifdef _WIN32
int send_vectors_windows(socket_t sock, WSABUF* buffers, DWORD buffer_count, size_t total_len, volatile bool* stop_flag);
#else
int send_vectors_posix(socket_t sock, struct iovec* iov, int iovcnt, size_t total_len, volatile bool* stop_flag);
#endif
#ifndef _WIN32
void close_stop_pipe(mcp_tcp_transport_data_t* data); // Declaration for POSIX helper
#endif


// From mcp_tcp_acceptor.c
void* tcp_accept_thread_func(void* arg); // Use correct thread function signature

// From mcp_tcp_client_handler.c
void* tcp_client_handler_thread_func(void* arg); // Use correct thread function signature


#endif // MCP_TCP_TRANSPORT_INTERNAL_H
