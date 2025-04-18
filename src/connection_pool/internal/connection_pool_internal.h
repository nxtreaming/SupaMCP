#ifndef MCP_CONNECTION_POOL_INTERNAL_H
#define MCP_CONNECTION_POOL_INTERNAL_H

#include "mcp_connection_pool.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_sync.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

// Platform-specific includes for sockets
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_handle_t;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_ERROR_HANDLE SOCKET_ERROR
#else // Linux/macOS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
typedef int socket_handle_t;
#define INVALID_SOCKET_HANDLE (-1)
#define SOCKET_ERROR_HANDLE (-1)
#endif

// --- Internal Structures ---

/**
 * @internal
 * @brief Represents a single connection stored within the pool.
 */
typedef struct mcp_pooled_connection {
    socket_handle_t socket_fd;          // Use consistent type alias
    time_t last_used_time;              // Timestamp when the connection was last returned to the pool
    time_t last_health_check;           // Timestamp of the last health check
    int health_score;                   // Health score (0-100, 100 is perfectly healthy)
    bool is_being_checked;              // Flag to indicate if connection is currently being checked
    struct mcp_pooled_connection* next; // Link for idle list
} mcp_pooled_connection_t;

/**
 * @internal
 * @brief Internal structure for the connection pool.
 */
struct mcp_connection_pool {
    char* host;                         // Target host
    int port;                           // Target port
    size_t min_connections;             // Min number of connections
    size_t max_connections;             // Max number of connections
    int idle_timeout_ms;                // Idle connection timeout
    int connect_timeout_ms;             // Timeout for establishing connections
    int health_check_interval_ms;       // Interval between health checks (0 = disabled)
    int health_check_timeout_ms;        // Timeout for health check operations

    mcp_mutex_t* mutex;                 // Mutex for thread safety (using abstracted type)
    mcp_cond_t* cond_var;               // Condition variable for waiting clients (using abstracted type)

    mcp_pooled_connection_t* idle_list; // Linked list of idle connections
    size_t idle_count;                  // Number of idle connections
    size_t active_count;                // Number of connections currently in use
    size_t total_count;                 // Total connections created (idle + active)

    // Health check statistics
    size_t health_checks_performed;     // Total number of health checks performed
    size_t failed_health_checks;        // Number of failed health checks

    bool shutting_down;                 // Flag indicating pool destruction is in progress

    // Thread for managing idle timeouts/min connections and health checks
    mcp_thread_t maintenance_thread;     // Thread handle for maintenance thread
};

// --- Internal Function Prototypes ---

// From mcp_connection_pool_socket.c
socket_handle_t create_new_connection(const char* host, int port, int connect_timeout_ms);
void close_connection(socket_handle_t socket_fd);

// From mcp_connection_pool_sync.c
int init_sync_primitives(mcp_connection_pool_t* pool);
void destroy_sync_primitives(mcp_connection_pool_t* pool);
void pool_lock(mcp_connection_pool_t* pool);
void pool_unlock(mcp_connection_pool_t* pool);
void pool_signal(mcp_connection_pool_t* pool);
void pool_broadcast(mcp_connection_pool_t* pool);
int pool_wait(mcp_connection_pool_t* pool, int timeout_ms);

// From mcp_connection_pool_utils.c
long long get_current_time_ms();

// From mcp_connection_pool_maintenance.c
int prepopulate_pool(mcp_connection_pool_t* pool);
int start_maintenance_thread(mcp_connection_pool_t* pool);
void stop_maintenance_thread(mcp_connection_pool_t* pool);
void* pool_maintenance_thread_func(void* arg);

// From mcp_connection_pool_health.c
bool check_connection_health(socket_handle_t socket_fd, int timeout_ms);
int perform_health_checks(mcp_connection_pool_t* pool);
void init_connection_health(mcp_pooled_connection_t* conn);

#endif // MCP_CONNECTION_POOL_INTERNAL_H
