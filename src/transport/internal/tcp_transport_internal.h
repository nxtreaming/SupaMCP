#ifndef MCP_TCP_TRANSPORT_INTERNAL_H
#define MCP_TCP_TRANSPORT_INTERNAL_H

#include "mcp_socket_utils.h"
#include "mcp_transport.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
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
#define MAX_TCP_CLIENTS 64                      // Max concurrent client connections for the server
#define POOL_BUFFER_SIZE (1024 * 64)            // 64KB buffer size (Increased)
#define POOL_NUM_BUFFERS 64                     // Increased number of buffers to match max clients
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024)      // 1MB limit

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
    volatile bool should_stop;  // Added volatile back // Flag to signal the handler thread to stop
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

void* tcp_accept_thread_func(void* arg);
void* tcp_client_handler_thread_func(void* arg);

#endif // MCP_TCP_TRANSPORT_INTERNAL_H
