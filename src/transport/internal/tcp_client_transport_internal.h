#ifndef MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H
#define MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H

// Platform-specific includes MUST come before standard headers that might include windows.h
#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <winsock2.h> // Include before windows.h (potentially included by stdio/stdlib)
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VAL INVALID_SOCKET
    #define SOCKET_ERROR_VAL   SOCKET_ERROR
    #define close_socket closesocket
    #define sock_errno WSAGetLastError()
    #define sleep_ms(ms) Sleep(ms)
    #include <windows.h>
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <netdb.h>
#   include <sys/uio.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VAL (-1)
    #define SOCKET_ERROR_VAL   (-1)
    #define close_socket close
    #define sock_errno errno
    #define sleep_ms(ms) usleep(ms * 1000)
#endif

// Now include project headers and standard libraries
#include "mcp_tcp_client_transport.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_buffer_pool.h"
#include "mcp_sync.h"
#include <mcp_thread_pool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>


// 全局变量，用于控制重连逻辑
extern bool reconnection_in_progress;


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
    mcp_thread_t receive_thread; // Use abstracted thread type
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for message buffers
} mcp_tcp_client_transport_data_t;

// --- Internal Function Prototypes ---

// From mcp_tcp_client_socket_utils.c
void initialize_winsock_client();
void cleanup_winsock_client(); // Added for consistency
int connect_to_server(mcp_tcp_client_transport_data_t* data);
int send_exact_client(socket_t sock, const char* buf, size_t len, bool* running_flag);
int recv_exact_client(socket_t sock, char* buf, size_t len, bool* running_flag);
// Declarations for vectored send helpers
#ifdef _WIN32
int send_vectors_client_windows(socket_t sock, WSABUF* buffers, DWORD buffer_count, size_t total_len, bool* running_flag);
#else
int send_vectors_client_posix(socket_t sock, struct iovec* iov, int iovcnt, size_t total_len, bool* running_flag);
#endif


// From mcp_tcp_client_receiver.c
// Use the abstracted thread function signature
void* tcp_client_receive_thread_func(void* arg);

#endif // MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H
