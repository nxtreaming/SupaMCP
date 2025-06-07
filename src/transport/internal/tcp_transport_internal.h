#ifndef TCP_TRANSPORT_INTERNAL_H
#define TCP_TRANSPORT_INTERNAL_H

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
#define MAX_TCP_CLIENTS 8192                    // Maximum concurrent client connections (supports 5000+ clients)
#define POOL_BUFFER_SIZE (1024 * 16)            // 16KB buffer size (More efficient)
#define POOL_NUM_BUFFERS 1024                   // Increased number of buffers for better concurrency
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024)      // 1MB limit
#define DEFAULT_THREAD_POOL_SIZE 32             // Default number of worker threads in the pool (reasonable starting point)
#define MAX_THREAD_POOL_SIZE 512                // Maximum number of worker threads (16 clients per thread for 8192 clients)
#define CONNECTION_QUEUE_SIZE 256               // Size of the connection queue for the thread pool (reasonable buffer for burst connections)

#define MONITOR_INTERVAL_MS 1000
#define ADJUST_INTERVAL_MS 30000

// Client connection states
typedef enum {
    CLIENT_STATE_INACTIVE,
    CLIENT_STATE_INITIALIZING, // Slot assigned, thread starting
    CLIENT_STATE_ACTIVE,       // Thread running, connection active
    CLIENT_STATE_CLOSING       // Connection is being closed
} client_state_t;

// Statistics for monitoring server performance
typedef struct {
    uint64_t total_connections;       // Total number of connections accepted
    uint64_t active_connections;      // Current number of active connections
    uint64_t rejected_connections;    // Connections rejected due to limits
    uint64_t messages_received;       // Total messages received
    uint64_t messages_sent;           // Total messages sent
    uint64_t bytes_received;          // Total bytes received
    uint64_t bytes_sent;              // Total bytes sent
    uint64_t errors;                  // Total number of errors
    time_t start_time;                // Server start time
} tcp_server_stats_t;

// Structure to hold information about a single client connection on the server
typedef struct {
    socket_t socket;                  // Client socket
    struct sockaddr_in address;       // Client address
    char client_ip[INET_ADDRSTRLEN];  // Client IP as string for logging
    uint16_t client_port;             // Client port for logging
    mcp_transport_t* transport;       // Pointer back to the parent transport
    volatile bool should_stop;        // Flag to signal the handler thread to stop
    client_state_t state;             // Current state of this client slot
    time_t last_activity_time;        // Timestamp of the last read/write activity
    time_t connect_time;              // When the connection was established
    uint64_t messages_processed;      // Number of messages processed on this connection
    int client_index;                 // Index in the clients array for quick reference
} tcp_client_connection_t;

// We'll use the existing mcp_thread_pool_t instead of defining our own

// Internal data structure for the TCP server transport
typedef struct {
    char* host;                       // Host to bind to
    uint16_t port;                    // Port to listen on
    socket_t listen_socket;           // Listening socket
    volatile bool running;            // Flag indicating if the transport is running
    mcp_thread_t accept_thread;       // Thread for accepting connections
    tcp_client_connection_t* clients; // Dynamic array of client connections
    int max_clients;                  // Maximum number of concurrent clients
    mcp_mutex_t* client_mutex;        // Mutex for protecting the clients array
    mcp_buffer_pool_t* buffer_pool;   // Buffer pool for receive buffers
    uint32_t idle_timeout_ms;         // Idle timeout for client connections
    mcp_thread_pool_t* thread_pool;   // Thread pool for handling client connections
    tcp_server_stats_t stats;         // Server statistics
    mcp_thread_t cleanup_thread;      // Thread for cleaning up idle connections
    mcp_thread_t monitor_thread;      // Thread for monitoring and adjusting settings
    volatile bool cleanup_running;    // Flag indicating if the cleanup thread is running
#ifndef _WIN32
    int stop_pipe[2];                 // Pipe used to signal the accept thread to stop on POSIX
#endif
} mcp_tcp_transport_data_t;

// Thread functions
void* tcp_accept_thread_func(void* arg);
void* tcp_client_handler_thread_func(void* arg);
void* tcp_cleanup_thread_func(void* arg); // Implemented in mcp_tcp_server_utils.c

// Client handler wrapper function for thread pool
void tcp_client_handler_wrapper(void* arg);

// Client connection management
int tcp_find_free_client_slot(mcp_tcp_transport_data_t* data);
void tcp_close_client_connection(mcp_tcp_transport_data_t* data, int client_index);
void tcp_update_client_activity(tcp_client_connection_t* client);

// Statistics functions
void tcp_stats_init(tcp_server_stats_t* stats);
void tcp_stats_update_connection_accepted(tcp_server_stats_t* stats);
void tcp_stats_update_connection_rejected(tcp_server_stats_t* stats);
void tcp_stats_update_connection_closed(tcp_server_stats_t* stats);
void tcp_stats_update_message_received(tcp_server_stats_t* stats, size_t bytes);
void tcp_stats_update_message_sent(tcp_server_stats_t* stats, size_t bytes);
void tcp_stats_update_error(tcp_server_stats_t* stats);

#endif // TCP_TRANSPORT_INTERNAL_H
