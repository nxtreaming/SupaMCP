#ifndef MCP_TCP_POOL_TRANSPORT_INTERNAL_H
#define MCP_TCP_POOL_TRANSPORT_INTERNAL_H

#include <mcp_transport.h>
#include <mcp_connection_pool.h>
#include <mcp_buffer_pool.h>
#include <mcp_types.h>
#include <mcp_log.h>
#include <mcp_socket_utils.h>
#include <mcp_framing.h>
#include <mcp_string_utils.h>
#include <stdbool.h>

// Include the internal transport header to get access to the mcp_transport struct definition
#include "internal/transport_internal.h"

// Platform-specific socket handle type
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_handle_t;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_ERROR_HANDLE SOCKET_ERROR
// EINPROGRESS is already defined in errno.h
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
typedef int socket_handle_t;
#define INVALID_SOCKET_HANDLE (-1)
#define SOCKET_ERROR_HANDLE (-1)
#endif

// Forward declaration for framing functions
// Use the existing mcp_framing_recv_message function
#define MAX_MESSAGE_SIZE (16 * 1024 * 1024)

// Internal structure for TCP pool transport data
typedef struct {
    char* host;                     // Target host
    uint16_t port;                  // Target port
    size_t min_connections;         // Min number of connections in the pool
    size_t max_connections;         // Max number of connections in the pool
    int idle_timeout_ms;            // Idle connection timeout
    int connect_timeout_ms;         // Timeout for establishing connections
    int request_timeout_ms;         // Timeout for request-response cycle
    volatile bool running;          // Flag indicating if transport is running
    mcp_connection_pool_t* connection_pool; // Connection pool
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for message buffers
} mcp_tcp_pool_transport_data_t;

#endif // MCP_TCP_POOL_TRANSPORT_INTERNAL_H
