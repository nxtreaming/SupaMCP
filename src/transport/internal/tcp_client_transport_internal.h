#ifndef MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H
#define MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H

#include "mcp_tcp_client_transport.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

// Platform-specific includes and definitions (similar to connection pool)
#ifdef _WIN32
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    typedef HANDLE thread_handle_t;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERROR_VAL   SOCKET_ERROR
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
    typedef pthread_t thread_handle_t;
    #define INVALID_SOCKET_VAL (-1)
    #define SOCKET_ERROR_VAL   (-1)
    #define close_socket close
    #define sock_errno errno
    #define sleep_ms(ms) usleep(ms * 1000)
#endif

// Constants (can reuse from other internal headers if appropriate, but define here for clarity)
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit
#define POOL_BUFFER_SIZE (1024 * 8) // 8KB buffer size
#define POOL_NUM_BUFFERS 16         // Number of buffers in the pool

// --- Internal Structures ---

// Forward declare transport struct
typedef struct mcp_transport mcp_transport_t;

// Internal structure for TCP client transport data
typedef struct {
    char* host;
    uint16_t port;
    socket_t sock;
    bool running;
    bool connected; // Track connection state
    mcp_transport_t* transport_handle; // Pointer back to the main handle (contains callbacks)
    thread_handle_t receive_thread;
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for message buffers
} mcp_tcp_client_transport_data_t;

// --- Internal Function Prototypes ---

// From mcp_tcp_client_socket_utils.c
void initialize_winsock_client();
void cleanup_winsock_client(); // Added for consistency
int connect_to_server(mcp_tcp_client_transport_data_t* data);
int send_exact_client(socket_t sock, const char* buf, size_t len, bool* running_flag);
int recv_exact_client(socket_t sock, char* buf, size_t len, bool* running_flag);

// From mcp_tcp_client_receiver.c
#ifdef _WIN32
DWORD WINAPI tcp_client_receive_thread_func(LPVOID arg);
#else
void* tcp_client_receive_thread_func(void* arg);
#endif

#endif // MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H
