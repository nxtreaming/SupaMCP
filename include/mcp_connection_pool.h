#ifndef MCP_CONNECTION_POOL_H
#define MCP_CONNECTION_POOL_H

#include <stddef.h>
#include <stdbool.h>

// Include platform-specific socket headers to define SOCKET and INVALID_SOCKET
#ifdef _WIN32
    // Minimal include to get SOCKET type, avoiding potential conflicts with windows.h
    #include <winsock2.h>
    // INVALID_SOCKET is defined in winsock2.h
#else // Linux/macOS etc.
    #include <sys/socket.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
#endif


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle representing a connection pool instance.
 */
typedef struct mcp_connection_pool mcp_connection_pool_t;

/**
 * @brief Creates a connection pool for managing connections to a target server.
 *
 * The pool maintains a set of reusable connections to avoid the overhead of
 * establishing a new connection for each request.
 *
 * @param host The hostname or IP address of the target server.
 * @param port The port number of the target server.
 * @param min_connections The minimum number of connections to keep open in the pool.
 *                        The pool will attempt to establish these connections upon creation.
 * @param max_connections The maximum number of connections allowed in the pool.
 *                        Requests may block or fail if this limit is reached and no connections are available.
 * @param idle_timeout_ms The maximum time (in milliseconds) an idle connection can remain
 *                        in the pool before being potentially closed. 0 means no timeout.
 * @param connect_timeout_ms Timeout (in milliseconds) for establishing a single new connection.
 * @param health_check_interval_ms Interval (in milliseconds) between health checks for idle connections.
 *                                 0 means health checks are disabled.
 * @param health_check_timeout_ms Timeout (in milliseconds) for health check operations.
 *                                If health_check_interval_ms is 0, this parameter is ignored.
 * @return Pointer to the created connection pool instance, or NULL on failure (e.g., allocation error, invalid arguments).
 */
mcp_connection_pool_t* mcp_connection_pool_create(
    const char* host,
    int port,
    size_t min_connections,
    size_t max_connections,
    int idle_timeout_ms,
    int connect_timeout_ms,
    int health_check_interval_ms,
    int health_check_timeout_ms);

/**
 * @brief Retrieves a connection handle from the pool.
 *
 * This function attempts to get an available connection from the pool. If no idle
 * connection is available and the pool is not at its maximum capacity, it may
 * attempt to create a new connection. If the pool is at maximum capacity and no
 * connections are idle, the call will block for up to `timeout_ms` waiting for
 * a connection to become available.
 *
 * @param pool The connection pool instance.
 * @param timeout_ms The maximum time (in milliseconds) to wait for a connection.
 *                   A value of 0 means don't wait, -1 means wait indefinitely.
 * @return A connection handle (socket descriptor) on success.
 *         Returns `INVALID_SOCKET` on failure (e.g., timeout, pool destroyed, error creating connection).
 * @note `INVALID_SOCKET` is defined platform-specifically (usually -1 on POSIX, `(SOCKET)(~0)` on Windows).
 */
SOCKET mcp_connection_pool_get(mcp_connection_pool_t* pool, int timeout_ms); // Changed return type to SOCKET

/**
 * @brief Returns a connection handle back to the pool.
 *
 * This function should be called after the client is finished using the connection
 * obtained via `mcp_connection_pool_get`.
 *
 * @param pool The connection pool instance.
 * @param connection The connection handle (socket descriptor) to return. Must not be `INVALID_SOCKET`.
 * @param is_valid Set to true if the connection is still healthy and can be reused.
 *                 Set to false if an error occurred on the connection (e.g., socket error,
 *                 protocol error), indicating it should be closed and not reused.
 * @return 0 on success, -1 on failure (e.g., invalid handle, pool destroyed).
 */
int mcp_connection_pool_release(mcp_connection_pool_t* pool, SOCKET connection, bool is_valid); // Changed connection type to SOCKET

/**
 * @brief Destroys the connection pool and closes all associated connections.
 *
 * This function gracefully closes all connections currently managed by the pool
 * and frees all allocated resources associated with the pool.
 *
 * @param pool The connection pool instance to destroy. If NULL, the function does nothing.
 */
void mcp_connection_pool_destroy(mcp_connection_pool_t* pool);

/**
 * @brief Gets statistics about the connection pool.
 *
 * @param pool The connection pool instance.
 * @param[out] total_connections Pointer to store the total number of connections (active + idle).
 * @param[out] idle_connections Pointer to store the number of idle connections available.
 * @param[out] active_connections Pointer to store the number of connections currently in use.
 * @param[out] health_checks_performed Pointer to store the total number of health checks performed (can be NULL).
 * @param[out] failed_health_checks Pointer to store the number of failed health checks (can be NULL).
 * @return 0 on success, -1 on failure (e.g., NULL pool pointer provided).
 */
int mcp_connection_pool_get_stats(mcp_connection_pool_t* pool, size_t* total_connections, size_t* idle_connections, size_t* active_connections, size_t* health_checks_performed, size_t* failed_health_checks);


#ifdef __cplusplus
}
#endif

#endif // MCP_CONNECTION_POOL_H
