#include "internal/connection_pool_internal.h"
#include "mcp_socket_utils.h"
#include "mcp_object_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Creates a new connection and optionally adds it to the idle list.
 *
 * This function creates a new connection to the target host and port, and
 * optionally adds it to the idle list. The pool must be locked before calling
 * this function.
 *
 * @param pool The connection pool.
 * @param add_to_idle_list Whether to add the connection to the idle list.
 * @return A pointer to the new connection, or NULL if creation failed.
 */
mcp_pooled_connection_t* create_and_add_connection(mcp_connection_pool_t* pool, bool add_to_idle_list) {
    if (!pool) {
        return NULL;
    }

    // Increment count before unlocking
    pool->total_count++;

    // Temporarily unlock while creating connection
    pool_unlock(pool);

    // Create new connection
    socket_handle_t new_sock = create_new_connection(pool->host, pool->port, pool->connect_timeout_ms);

    // Re-lock before updating state
    pool_lock(pool);

    if (new_sock == INVALID_SOCKET_HANDLE) {
        // Failed to create connection
        mcp_log_warn("Failed to create new connection to %s:%d", pool->host, pool->port);
        pool->total_count--;
        pool->total_connection_errors++;
        return NULL;
    }

    // Allocate connection structure from object pool if available
    mcp_pooled_connection_t* new_conn = NULL;
    if (pool->conn_pool) {
        new_conn = (mcp_pooled_connection_t*)mcp_object_pool_acquire(pool->conn_pool);
    }

    // Fall back to malloc if object pool is not available or empty
    if (!new_conn) {
        new_conn = (mcp_pooled_connection_t*)malloc(sizeof(mcp_pooled_connection_t));
    }

    if (!new_conn) {
        // Failed to allocate node, close the connection
        mcp_log_error("Failed to allocate node for new connection %d", (int)new_sock);

        // Temporarily unlock while closing connection
        pool_unlock(pool);
        close_connection(new_sock);
        pool_lock(pool);

        pool->total_count--;
        pool->total_connection_errors++;
        return NULL;
    }

    // Initialize connection structure
    new_conn->socket_fd = new_sock;
    new_conn->last_used_time = time(NULL);
    new_conn->prev = NULL;
    new_conn->next = NULL;

    // Initialize health check fields
    init_connection_health(new_conn);

    // Update statistics
    pool->total_connections_created++;

    // Add to idle list if requested
    if (add_to_idle_list) {
        // Add to head of idle list
        if (pool->idle_head == NULL) {
            // Empty list
            pool->idle_head = new_conn;
            pool->idle_tail = new_conn;
        } else {
            // Add to head
            new_conn->next = pool->idle_head;
            pool->idle_head->prev = new_conn;
            pool->idle_head = new_conn;
        }
        pool->idle_count++;
    }

    mcp_log_debug("Created new connection %d to %s:%d", (int)new_sock, pool->host, pool->port);
    return new_conn;
}

/**
 * @brief Removes a connection from the idle list.
 *
 * This function removes a connection from the idle list. The pool must be locked
 * before calling this function.
 *
 * @param pool The connection pool.
 * @param conn The connection to remove.
 * @param prev The previous connection in the list, or NULL if conn is the head.
 * @return true if the connection was removed, false otherwise.
 */
bool remove_idle_connection(mcp_connection_pool_t* pool, mcp_pooled_connection_t* conn, mcp_pooled_connection_t* prev) {
    if (!pool || !conn) {
        return false;
    }

    // Remove from idle list
    if (prev) {
        prev->next = conn->next;
        if (conn->next) {
            conn->next->prev = prev;
        } else {
            // This was the tail
            pool->idle_tail = prev;
        }
    } else {
        // Removing the head of the list
        pool->idle_head = conn->next;
        if (conn->next) {
            conn->next->prev = NULL;
        } else {
            // This was the only node
            pool->idle_tail = NULL;
        }
    }

    // Update counts
    pool->idle_count--;

    return true;
}

/**
 * @brief Closes a connection and frees its resources.
 *
 * This function closes a connection and frees its resources. The pool must be locked
 * before calling this function.
 *
 * @param pool The connection pool.
 * @param conn The connection to close and free.
 */
void close_and_free_connection(mcp_connection_pool_t* pool, mcp_pooled_connection_t* conn) {
    if (!pool || !conn) {
        return;
    }

    // Close the connection
    socket_handle_t sock_fd = conn->socket_fd;

    // Temporarily unlock while closing connection
    pool_unlock(pool);
    close_connection(sock_fd);
    pool_lock(pool);

    // Update statistics
    pool->total_count--;
    pool->total_connections_closed++;

    // Return connection structure to object pool if available
    if (pool->conn_pool) {
        // Clear the structure before returning to pool
        memset(conn, 0, sizeof(mcp_pooled_connection_t));
        mcp_object_pool_release(pool->conn_pool, conn);
    } else {
        // Free the connection structure
        free(conn);
    }

    mcp_log_debug("Closed and freed connection %d", (int)sock_fd);
}

/**
 * @brief Maintenance thread function for the connection pool.
 *
 * This function runs in a separate thread and performs maintenance tasks for the
 * connection pool, such as closing idle connections that have timed out, performing
 * health checks, and ensuring the minimum number of connections is maintained.
 *
 * @param arg Pointer to the connection pool.
 * @return NULL.
 */
void* pool_maintenance_thread_func(void* arg) {
    mcp_connection_pool_t* pool = (mcp_connection_pool_t*)arg;
    if (!pool) {
        mcp_log_error("Maintenance thread started with NULL pool.");
        return NULL;
    }

    mcp_log_info("Connection pool maintenance thread started for %s:%d.", pool->host, pool->port);

    while (true) {
        // Sleep between maintenance cycles
        mcp_sleep_ms(1000);

        // Check if we should exit
        pool_lock(pool);
        if (pool->shutting_down) {
            pool_unlock(pool);
            break;
        }

        // Get current time for idle timeout checks and statistics
        time_t current_time = time(NULL);

        // Start timing for performance measurement (millisecond precision)
        long long maintenance_start_ms = mcp_get_time_ms();

        // Update maintenance statistics
        pool->maintenance_cycles++;
        pool->last_maintenance_time = current_time;

        // 1. Check and close idle connections that have timed out
        if (pool->idle_timeout_ms > 0) {
            mcp_pooled_connection_t* prev = NULL;
            mcp_pooled_connection_t* current = pool->idle_head;

            while (current) {
                // Check if this connection has timed out
                double idle_time_sec = difftime(current_time, current->last_used_time);
                if (idle_time_sec * 1000 > pool->idle_timeout_ms) {
                    // Connection has timed out, remove it from the list
                    mcp_pooled_connection_t* to_remove = current;

                    // Remove from idle list
                    if (remove_idle_connection(pool, to_remove, prev)) {
                        // Get next connection before closing current one
                        current = (prev) ? prev->next : pool->idle_head;

                        // Close and free the connection
                        mcp_log_debug("Closing idle connection %d due to timeout (idle for %.1f seconds).",
                                     (int)to_remove->socket_fd, idle_time_sec);
                        close_and_free_connection(pool, to_remove);
                    } else {
                        // Something went wrong, move to next
                        prev = current;
                        current = current->next;
                    }
                } else {
                    // Connection is still within timeout, move to next
                    prev = current;
                    current = current->next;
                }
            }
        }

        // 2. Perform health checks on idle connections if enabled
        if (pool->health_check_interval_ms > 0) {
            // Check if it's time to perform health checks
            if (pool->last_health_check_time == 0 ||
                difftime(current_time, pool->last_health_check_time) * 1000 >= pool->health_check_interval_ms) {

                // Temporarily unlock the pool while performing health checks
                pool_unlock(pool);

                // Perform health checks
                int failed_checks = perform_health_checks(pool);
                if (failed_checks > 0) {
                    mcp_log_warn("Health check: %d connections failed health check and were removed.", failed_checks);
                }

                // Re-lock the pool
                pool_lock(pool);

                // Update health check timestamp
                pool->last_health_check_time = current_time;

                // Check if we need to exit after health checks
                if (pool->shutting_down) {
                    pool_unlock(pool);
                    break;
                }
            }
        }

        // 3. Ensure minimum connections are maintained
        if (pool->min_connections > 0 && pool->total_count < pool->min_connections) {
            size_t connections_to_add = pool->min_connections - pool->total_count;
            mcp_log_debug("Maintaining minimum connections: adding %zu connections.", connections_to_add);

            for (size_t i = 0; i < connections_to_add && pool->total_count < pool->max_connections; i++) {
                // Create and add a new connection to the idle list
                mcp_pooled_connection_t* new_conn = create_and_add_connection(pool, true);

                if (new_conn) {
                    mcp_log_debug("Added new connection %d to maintain minimum pool size.", (int)new_conn->socket_fd);
                }
                // Error handling is done inside create_and_add_connection
            }
        }

        // Calculate maintenance cycle time and update statistics
        long long maintenance_end_ms = mcp_get_time_ms();
        long long cycle_time_ms = maintenance_end_ms - maintenance_start_ms;

        pool->total_maintenance_time_ms += cycle_time_ms;
        if (cycle_time_ms > pool->max_maintenance_time_ms) {
            pool->max_maintenance_time_ms = cycle_time_ms;
        }

        if (cycle_time_ms > 100) { // Log slow maintenance cycles
            mcp_log_warn("Slow maintenance cycle: %lld ms", cycle_time_ms);
        }

        pool_unlock(pool);
    }

    mcp_log_info("Connection pool maintenance thread exiting.");
    return NULL;
}

/**
 * @brief Pre-populates the connection pool with the minimum number of connections.
 *
 * This function creates the minimum number of connections specified in the pool
 * configuration and adds them to the idle list.
 *
 * @param pool The connection pool.
 * @return 0 on success, -1 on failure.
 */
int prepopulate_pool(mcp_connection_pool_t* pool) {
    if (!pool || pool->min_connections == 0) {
        return 0;
    }

    mcp_log_info("Pre-populating connection pool with %zu connections.", pool->min_connections);

    // Start timing for performance measurement (millisecond precision)
    long long prepopulate_start_ms = mcp_get_time_ms();

    // Lock the pool before modifying it
    pool_lock(pool);

    // Create object pool for connection structures if not already created
    if (!pool->conn_pool && pool->max_connections > 0) {
        // Create object pool with initial capacity equal to min_connections
        // and max capacity equal to max_connections
        pool->conn_pool = mcp_object_pool_create(
            sizeof(mcp_pooled_connection_t),
            pool->min_connections,
            pool->max_connections
        );

        if (!pool->conn_pool) {
            mcp_log_warn("Failed to create connection object pool, falling back to malloc/free");
        } else {
            mcp_log_info("Created connection object pool with initial capacity %zu, max capacity %zu",
                        pool->min_connections, pool->max_connections);
        }
    }

    size_t success_count = 0;
    for (size_t i = 0; i < pool->min_connections && pool->total_count < pool->max_connections; i++) {
        // Create and add a new connection to the idle list
        mcp_pooled_connection_t* new_conn = create_and_add_connection(pool, true);

        if (new_conn) {
            success_count++;
            mcp_log_debug("Pre-populated pool with connection %d (%zu/%zu).",
                         (int)new_conn->socket_fd, success_count, pool->min_connections);
        }
        // Error handling is done inside create_and_add_connection
    }

    // Calculate and log the time taken
    long long prepopulate_end_ms = mcp_get_time_ms();
    long long total_time_ms = prepopulate_end_ms - prepopulate_start_ms;

    pool_unlock(pool);

    mcp_log_info("Pre-populated pool with %zu/%zu connections in %lld ms.",
                success_count, pool->min_connections, total_time_ms);

    return (success_count > 0) ? 0 : -1; // Return success if at least one connection was created
}

/**
 * @brief Starts the maintenance thread for the connection pool.
 *
 * This function creates and starts a thread that performs maintenance tasks for
 * the connection pool, such as closing idle connections that have timed out,
 * performing health checks, and ensuring the minimum number of connections is
 * maintained.
 *
 * @param pool The connection pool.
 * @return 0 on success, -1 on failure.
 */
int start_maintenance_thread(mcp_connection_pool_t* pool) {
    if (!pool) {
        mcp_log_error("Cannot start maintenance thread for NULL pool");
        return -1;
    }

    // Only start maintenance thread if we need idle timeout checks, health checks, or min connections maintenance
    if (pool->idle_timeout_ms <= 0 && pool->min_connections == 0 && pool->health_check_interval_ms <= 0) {
        mcp_log_debug("No maintenance thread needed (idle_timeout_ms=%d, min_connections=%zu, health_check_interval_ms=%d).",
                     pool->idle_timeout_ms, pool->min_connections, pool->health_check_interval_ms);
        return 0;
    }

    // Initialize maintenance statistics
    pool->maintenance_cycles = 0;
    pool->last_maintenance_time = 0;
    pool->total_maintenance_time_ms = 0;
    pool->max_maintenance_time_ms = 0;
    pool->last_health_check_time = 0;

    // Create and start the maintenance thread
    if (mcp_thread_create(&pool->maintenance_thread, pool_maintenance_thread_func, pool) != 0) {
        mcp_log_error("Failed to create connection pool maintenance thread.");
        return -1;
    }

    mcp_log_info("Started connection pool maintenance thread for %s:%d.", pool->host, pool->port);
    return 0;
}

/**
 * @brief Stops the maintenance thread for the connection pool.
 *
 * This function signals the maintenance thread to exit and waits for it to
 * terminate.
 *
 * @param pool The connection pool.
 */
void stop_maintenance_thread(mcp_connection_pool_t* pool) {
    if (!pool || !pool->maintenance_thread) {
        return;
    }

    // Signal thread to exit (shutting_down flag should already be set)
    mcp_log_debug("Waiting for maintenance thread to exit...");

    // Start timing for performance measurement (millisecond precision)
    long long stop_thread_start_ms = mcp_get_time_ms();

    // Wait for thread to exit
    mcp_thread_join(pool->maintenance_thread, NULL);
    pool->maintenance_thread = 0;

    // Calculate and log the time taken
    long long stop_thread_end_ms = mcp_get_time_ms();
    long long total_time_ms = stop_thread_end_ms - stop_thread_start_ms;

    mcp_log_info("Maintenance thread stopped after %lld ms. Total cycles: %zu, Avg time per cycle: %.2f ms",
                total_time_ms, pool->maintenance_cycles,
                pool->maintenance_cycles > 0 ?
                    (double)pool->total_maintenance_time_ms / pool->maintenance_cycles : 0);
}
