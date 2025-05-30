#ifndef CONNECTION_POOL_INTERNAL_H
#define CONNECTION_POOL_INTERNAL_H

#include "mcp_connection_pool.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_sync.h"
#include "mcp_cache_aligned.h"
#include "mcp_rwlock.h"
#include "mcp_object_pool.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

// Maximum number of DNS cache entries
#define DNS_CACHE_SIZE 16

// DNS cache entry expiration time in seconds
#define DNS_CACHE_EXPIRY 300 // 5 minutes

// Maximum hostname length (including port and null terminator)
#define DNS_CACHE_MAX_HOSTNAME 256

// Platform-specific includes for sockets
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_handle_t;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define SOCKET_ERROR_HANDLE SOCKET_ERROR
#else
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
    int use_count;                      // Number of times this connection has been used
    struct mcp_pooled_connection* next; // Link for idle list
    struct mcp_pooled_connection* prev; // Link for idle list (double-linked)

    // Padding to ensure cache line alignment and prevent false sharing
    char padding[MCP_CACHE_LINE_SIZE -
                (sizeof(socket_handle_t) + 2 * sizeof(time_t) + sizeof(int) +
                 sizeof(bool) + sizeof(int) + 2 * sizeof(void*)) % MCP_CACHE_LINE_SIZE];
} MCP_CACHE_ALIGNED mcp_pooled_connection_t;

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
    mcp_rwlock_t* rwlock;               // Read-write lock for better concurrency

    // Object pool for connection structures
    mcp_object_pool_t* conn_pool;       // Object pool for connection structures

    // Double-linked list of idle connections (head and tail for efficient operations)
    mcp_pooled_connection_t* idle_head; // Head of idle list
    mcp_pooled_connection_t* idle_tail; // Tail of idle list
    size_t idle_count;                  // Number of idle connections
    size_t active_count;                // Number of connections currently in use
    size_t total_count;                 // Total connections created (idle + active)

    // Performance statistics
    size_t total_connections_created;   // Total number of connections created over lifetime
    size_t total_connections_closed;    // Total number of connections closed over lifetime
    size_t total_connection_gets;       // Total number of successful connection gets
    size_t total_connection_timeouts;   // Total number of connection get timeouts
    size_t total_connection_errors;     // Total number of connection errors
    long long total_wait_time_ms;       // Total time spent waiting for connections
    long long max_wait_time_ms;         // Maximum time spent waiting for a connection

    // Maintenance statistics
    size_t maintenance_cycles;          // Total number of maintenance cycles run
    time_t last_maintenance_time;       // Timestamp of last maintenance cycle
    long long total_maintenance_time_ms;// Total time spent in maintenance
    long long max_maintenance_time_ms;  // Maximum time spent in a single maintenance cycle

    // Health check statistics
    size_t health_checks_performed;     // Total number of health checks performed
    size_t failed_health_checks;        // Number of failed health checks
    time_t last_health_check_time;      // Timestamp of last health check

    bool shutting_down;                 // Flag indicating pool destruction is in progress

    // Thread for managing idle timeouts/min connections and health checks
    mcp_thread_t maintenance_thread;     // Thread handle for maintenance thread
};

// --- Internal Function Prototypes ---

// From mcp_connection_pool_socket.c
socket_handle_t create_new_connection(const char* host, int port, int connect_timeout_ms);

// From mcp_connection_pool_sync.c
int init_sync_primitives(mcp_connection_pool_t* pool);
void destroy_sync_primitives(mcp_connection_pool_t* pool);
int pool_wait(mcp_connection_pool_t* pool, int timeout_ms);

// Macros to replace the removed functions
#define pool_lock(pool) mcp_mutex_lock((pool)->mutex)
#define pool_unlock(pool) mcp_mutex_unlock((pool)->mutex)
#define pool_signal(pool) mcp_cond_signal((pool)->cond_var)
#define pool_broadcast(pool) mcp_cond_broadcast((pool)->cond_var)

// From mcp_connection_pool_maintenance.c
int prepopulate_pool(mcp_connection_pool_t* pool);
int start_maintenance_thread(mcp_connection_pool_t* pool);
void stop_maintenance_thread(mcp_connection_pool_t* pool);
void* pool_maintenance_thread_func(void* arg);
mcp_pooled_connection_t* create_and_add_connection(mcp_connection_pool_t* pool, bool add_to_idle_list);
bool remove_idle_connection(mcp_connection_pool_t* pool, mcp_pooled_connection_t* conn, mcp_pooled_connection_t* prev);
void close_and_free_connection(mcp_connection_pool_t* pool, mcp_pooled_connection_t* conn);

// From mcp_connection_pool_health.c
bool check_connection_health(socket_handle_t socket_fd, int timeout_ms);
int perform_health_checks(mcp_connection_pool_t* pool);
void init_connection_health(mcp_pooled_connection_t* conn);
int update_connection_health_score(mcp_pooled_connection_t* conn, bool is_healthy);
bool is_connection_healthy_by_score(mcp_pooled_connection_t* conn);

// DNS cache structure and functions
typedef struct {
    char hostname[DNS_CACHE_MAX_HOSTNAME];  // Fixed-size buffer for hostname (key)
    struct addrinfo* addr_info;             // Resolved address info
    time_t timestamp;                       // When this entry was created/updated
    int ref_count;                          // Reference count for this entry
    mcp_mutex_t* mutex;                     // Mutex for thread-safe access to this entry

    // Statistics
    uint32_t hit_count;                     // Number of cache hits for this entry

    // Padding to align to cache line boundary (fixed size for simplicity)
    char padding[MCP_CACHE_LINE_SIZE];
} MCP_CACHE_ALIGNED dns_cache_entry_t;

typedef struct {
    MCP_CACHE_ALIGNED dns_cache_entry_t entries[DNS_CACHE_SIZE];  // Fixed-size array of cache entries
    mcp_mutex_t* mutex;                                           // Mutex for thread-safe access to the cache
    mcp_rwlock_t* rwlock;                                         // Read-write lock for better concurrency
    bool initialized;                                             // Whether the cache has been initialized

    // Statistics
    uint32_t hits;                                                // Total number of cache hits
    uint32_t misses;                                              // Total number of cache misses
    uint32_t evictions;                                           // Total number of cache evictions

    // Padding to align to cache line boundary (fixed size for simplicity)
    char padding[MCP_CACHE_LINE_SIZE];
} MCP_CACHE_ALIGNED dns_cache_t;

// Global DNS cache
extern dns_cache_t g_dns_cache;

// DNS cache functions
void dns_cache_init();
void dns_cache_cleanup();
struct addrinfo* dns_cache_get(const char* hostname, int port, const struct addrinfo* hints);
void dns_cache_release(struct addrinfo* addr_info);
void dns_cache_clear();

#endif // CONNECTION_POOL_INTERNAL_H
