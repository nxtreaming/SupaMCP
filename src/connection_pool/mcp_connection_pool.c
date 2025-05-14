#include "internal/connection_pool_internal.h"
#include "mcp_socket_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

mcp_connection_pool_t* mcp_connection_pool_create(
    const char* host,
    int port,
    size_t min_connections,
    size_t max_connections,
    int idle_timeout_ms,
    int connect_timeout_ms,
    int health_check_interval_ms,
    int health_check_timeout_ms)
{
    if (!host || port <= 0 || max_connections == 0 || min_connections > max_connections) {
        mcp_log_error("Error: mcp_connection_pool_create invalid arguments.");
        return NULL;
    }

    mcp_connection_pool_t* pool = (mcp_connection_pool_t*)calloc(1, sizeof(mcp_connection_pool_t));
    if (!pool) {
        mcp_log_error("Error: mcp_connection_pool_create failed to allocate pool structure.");
        return NULL;
    }

    // Use mcp_strdup from mcp_types.h (included via internal header)
    pool->host = mcp_strdup(host);
    if (!pool->host) {
        mcp_log_error("Error: mcp_connection_pool_create failed to duplicate host string.");
        free(pool);
        return NULL;
    }

    pool->port = port;
    pool->min_connections = min_connections;
    pool->max_connections = max_connections;
    pool->idle_timeout_ms = idle_timeout_ms;
    pool->connect_timeout_ms = connect_timeout_ms;
    pool->health_check_interval_ms = health_check_interval_ms;
    pool->health_check_timeout_ms = health_check_timeout_ms > 0 ? health_check_timeout_ms : 2000; // Default to 2 seconds if not specified
    pool->shutting_down = false;
    pool->idle_head = NULL;
    pool->idle_tail = NULL;
    pool->idle_count = 0;
    pool->active_count = 0;
    pool->total_count = 0;

    // Initialize performance statistics
    pool->total_connections_created = 0;
    pool->total_connections_closed = 0;
    pool->total_connection_gets = 0;
    pool->total_connection_timeouts = 0;
    pool->total_connection_errors = 0;
    pool->total_wait_time_ms = 0;
    pool->max_wait_time_ms = 0;

    // Initialize health check statistics
    pool->health_checks_performed = 0;
    pool->failed_health_checks = 0;

    if (init_sync_primitives(pool) != 0) {
        mcp_log_error("mcp_connection_pool_create failed to initialize synchronization primitives.");
        free(pool->host);
        free(pool);
        return NULL;
    }

    // Pre-populate pool with min_connections
    if (pool->min_connections > 0) {
        if (prepopulate_pool(pool) != 0) {
            mcp_log_warn("Failed to pre-populate connection pool, but continuing.");
            // Continue anyway, as the maintenance thread will try to establish connections
        }
    }

    mcp_log_info("Connection pool created for %s:%d (min:%zu, max:%zu).",
            pool->host, pool->port, pool->min_connections, pool->max_connections);

    // Start maintenance thread if needed
    if (pool->idle_timeout_ms > 0 || pool->min_connections > 0) {
        if (start_maintenance_thread(pool) != 0) {
            mcp_log_error("Failed to start maintenance thread.");
            // This is not fatal, but log as error since maintenance functions won't work
        }
    }

    return pool;
}

socket_handle_t mcp_connection_pool_get(mcp_connection_pool_t* pool, int timeout_ms) {
    if (!pool) {
        mcp_log_error("mcp_connection_pool_get: Pool is NULL.");
        return INVALID_SOCKET_HANDLE;
    }

    socket_handle_t sock = INVALID_SOCKET_HANDLE;
    long long start_time_ms = 0; // For tracking overall timeout
    if (timeout_ms > 0) {
        start_time_ms = mcp_get_time_ms();
    }

    pool_lock(pool);

    while (sock == INVALID_SOCKET_HANDLE) {
        if (pool->shutting_down) {
            mcp_log_warn("mcp_connection_pool_get: Pool is shutting down.");
            pool_unlock(pool);
            return INVALID_SOCKET_HANDLE;
        }

        // 1. Try to get an idle connection
        if (pool->idle_head) {
            // Get connection from head of idle list (most recently used)
            mcp_pooled_connection_t* pooled_conn = pool->idle_head;

            // Update head pointer
            pool->idle_head = pooled_conn->next;

            // Update tail pointer if this was the last connection
            if (pool->idle_head == NULL) {
                pool->idle_tail = NULL;
            } else {
                // Update prev pointer of new head
                pool->idle_head->prev = NULL;
            }

            // Update counts
            pool->idle_count--;
            pool->active_count++;
            pool->total_connection_gets++;

            // Get socket handle
            sock = pooled_conn->socket_fd;

            // Update connection use count
            pooled_conn->use_count++;

            // Check if the connection has timed out
            if (pool->idle_timeout_ms > 0) {
                time_t current_time = time(NULL);
                double idle_time_sec = difftime(current_time, pooled_conn->last_used_time);

                if (idle_time_sec * 1000 > pool->idle_timeout_ms) {
                    // Connection has timed out, close it and try to get another one
                    mcp_log_debug("Idle connection %d timed out (idle for %.1f seconds), closing.",
                                 (int)sock, idle_time_sec);
                    mcp_socket_close(sock);
                    free(pooled_conn);

                    // Update counts but keep total the same
                    pool->idle_count--;
                    pool->total_count--;

                    // Continue the loop to get another connection
                    continue;
                }
            }

            // Perform a quick health check if enabled
            if (pool->health_check_interval_ms > 0) {
                // Temporarily unlock the pool while checking connection health
                pool_unlock(pool);

                // Check connection health
                bool is_healthy = check_connection_health(sock, pool->health_check_timeout_ms);

                // Re-lock the pool
                pool_lock(pool);

                // Update health check statistics
                pool->health_checks_performed++;

                if (!is_healthy) {
                    // Connection is unhealthy, close it and try to get another one
                    mcp_log_warn("Connection %d failed health check, closing.", (int)sock);
                    mcp_socket_close(sock);
                    free(pooled_conn);

                    // Update counts and statistics
                    pool->idle_count--;
                    pool->total_count--;
                    pool->failed_health_checks++;

                    // Continue the loop to get another connection
                    continue;
                }
            }

            free(pooled_conn); // Free the list node structure
            mcp_log_debug("Reusing idle connection %d.", (int)sock);
            pool_unlock(pool);
            return sock;
        }

        // 2. If no idle connections, try to create a new one if allowed
        if (pool->total_count < pool->max_connections) {
            size_t current_total = pool->total_count; // Read before unlocking
            pool->total_count++; // Optimistically increment total count
            pool_unlock(pool);   // Unlock while creating connection

            mcp_log_debug("Attempting to create new connection (%zu/%zu).", current_total + 1, pool->max_connections);
            socket_handle_t new_sock = create_new_connection(pool->host, pool->port, pool->connect_timeout_ms);

            pool_lock(pool); // Re-lock before checking result and updating state
            if (new_sock != INVALID_SOCKET_HANDLE) {
                pool->active_count++;
                sock = new_sock; // Success! Loop will terminate.
                mcp_log_debug("Created new connection %d.", (int)sock);
            } else {
                pool->total_count--; // Creation failed, decrement total count
                mcp_log_warn("Failed to create new connection.");
                // If creation fails, we might need to wait if timeout allows
                if (timeout_ms == 0) {
                    pool_unlock(pool);
                    return INVALID_SOCKET_HANDLE;
                }
                // Fall through to wait below
            }
        }

        // 3. If pool is full or creation failed, wait if timeout allows
        if (sock == INVALID_SOCKET_HANDLE) {
             if (timeout_ms == 0) {
                 mcp_log_warn("mcp_connection_pool_get: Pool full and timeout is 0.");
                 pool_unlock(pool);
                 return INVALID_SOCKET_HANDLE;
             }

             int wait_timeout = timeout_ms;
             if (timeout_ms > 0) {
                 long long elapsed_ms = mcp_get_time_ms() - start_time_ms;
                 wait_timeout = timeout_ms - (int)elapsed_ms;
                 if (wait_timeout <= 0) {
                     mcp_log_warn("mcp_connection_pool_get: Timed out waiting for connection.");
                     pool_unlock(pool);
                     return INVALID_SOCKET_HANDLE; // Overall timeout expired
                 }
             }

             mcp_log_debug("Waiting for connection (timeout: %d ms)...", wait_timeout);
             int wait_result = pool_wait(pool, wait_timeout);
             if (wait_result == 1) {
                 mcp_log_warn("mcp_connection_pool_get: Timed out waiting for condition.");
                 pool_unlock(pool);
                 return INVALID_SOCKET_HANDLE;
             } else if (wait_result == -1) {
                 mcp_log_error("mcp_connection_pool_get: Error waiting for condition.");
                 pool_unlock(pool);
                 return INVALID_SOCKET_HANDLE;
             }
             // If wait_result is 0, we were signaled or had a spurious wakeup, loop continues
             mcp_log_debug("Woke up from wait, retrying get.");
        }
    } // End while loop

    pool_unlock(pool);
    return sock;
}

int mcp_connection_pool_release(mcp_connection_pool_t* pool, socket_handle_t connection, bool is_valid) {
    if (!pool || connection == INVALID_SOCKET_HANDLE) {
        mcp_log_error("mcp_connection_pool_release: Invalid arguments (pool=%p, connection=%d).", (void*)pool, (int)connection);
        return -1;
    }

    pool_lock(pool);

    // Find the connection in the active list - this requires tracking active connections,
    // which the current simple implementation doesn't do explicitly.
    // For now, we just decrement the active count assuming the caller provides a valid active handle.
    // A more robust implementation would track active handles.
    if (pool->active_count == 0) {
        mcp_log_warn("mcp_connection_pool_release: Releasing connection %d but active count is zero.", (int)connection);
        // Proceeding anyway, but indicates a potential issue in usage or tracking.
    } else {
        pool->active_count--;
    }

    if (pool->shutting_down) {
        mcp_log_info("Pool shutting down, closing connection %d.", (int)connection);
        mcp_socket_close(connection);
        pool->total_count--;
        // No signal needed, broadcast happens in destroy
    } else if (!is_valid) {
        mcp_log_warn("Closing invalid connection %d.", (int)connection);
        mcp_socket_close(connection);
        pool->total_count--;
        // Signal potentially waiting getters that a slot might be free for creation
        pool_signal(pool);
    } else {
        // Add valid connection back to idle list
        mcp_pooled_connection_t* pooled_conn = (mcp_pooled_connection_t*)malloc(sizeof(mcp_pooled_connection_t));
        if (pooled_conn) {
            // Initialize connection node
            pooled_conn->socket_fd = connection;
            pooled_conn->last_used_time = time(NULL); // Record return time
            pooled_conn->use_count = 1; // First use
            pooled_conn->next = NULL; // Will be at head of list
            pooled_conn->prev = NULL;

            // Initialize health check fields
            init_connection_health(pooled_conn);

            // Add to head of idle list (most recently used)
            if (pool->idle_head == NULL) {
                // Empty list
                pool->idle_head = pooled_conn;
                pool->idle_tail = pooled_conn;
            } else {
                // Add to head
                pooled_conn->next = pool->idle_head;
                pool->idle_head->prev = pooled_conn;
                pool->idle_head = pooled_conn;
            }

            // Update counts
            pool->idle_count++;

            mcp_log_debug("Returned connection %d to idle pool.", (int)connection);

            // Signal one waiting getter that a connection is available
            pool_signal(pool);
        } else {
            // Failed to allocate node, just close the connection
            mcp_log_error("Failed to allocate node for idle connection %d, closing.", (int)connection);
            mcp_socket_close(connection);
            pool->total_count--;
            // Signal potentially waiting getters that a slot might be free for creation
            pool_signal(pool);
        }
    }

    pool_unlock(pool);
    return 0;
}

void mcp_connection_pool_destroy(mcp_connection_pool_t* pool) {
    if (!pool) {
        return;
    }

    mcp_log_info("Destroying connection pool for %s:%d.", pool->host, pool->port);

    // 1. Signal shutdown and wake waiters
    pool_lock(pool);
    if (pool->shutting_down) {
        pool_unlock(pool);
        return;
    }
    pool->shutting_down = true;
    pool_broadcast(pool);
    pool_unlock(pool);

    // 2. Stop and join maintenance thread if it exists
    stop_maintenance_thread(pool);

    // 3. Close idle connections and free resources
    pool_lock(pool);
    mcp_log_info("Closing %zu idle connections.", pool->idle_count);
    mcp_pooled_connection_t* current = pool->idle_head;
    while(current) {
        mcp_pooled_connection_t* next = current->next;
        mcp_socket_close(current->socket_fd);
        pool->total_connections_closed++;
        free(current);
        current = next;
    }
    pool->idle_head = NULL;
    pool->idle_tail = NULL;
    pool->idle_count = 0;

    // Note: Active connections are not explicitly waited for here.
    mcp_log_info("%zu connections were active during shutdown.", pool->active_count);
    pool->total_count = 0;
    pool->active_count = 0;

    pool_unlock(pool);

    // 4. Destroy sync primitives and free memory
    destroy_sync_primitives(pool);
    free(pool->host);
    free(pool);

    mcp_log_info("Connection pool destroyed.");
}

// Using the extended statistics structure defined in mcp_connection_pool.h

int mcp_connection_pool_get_stats(mcp_connection_pool_t* pool, size_t* total_connections, size_t* idle_connections, size_t* active_connections, size_t* health_checks_performed, size_t* failed_health_checks) {
    if (!pool || !total_connections || !idle_connections || !active_connections) {
        mcp_log_error("mcp_connection_pool_get_stats: Received NULL pointer argument.");
        return -1;
    }

    pool_lock(pool);

    *total_connections = pool->total_count;
    *idle_connections = pool->idle_count;
    *active_connections = pool->active_count;

    // Health check statistics are optional
    if (health_checks_performed) {
        *health_checks_performed = pool->health_checks_performed;
    }

    if (failed_health_checks) {
        *failed_health_checks = pool->failed_health_checks;
    }

    pool_unlock(pool);

    return 0;
}

/**
 * @brief Get extended statistics from the connection pool
 *
 * @param pool The connection pool
 * @param stats Pointer to a statistics structure to fill
 * @return 0 on success, -1 on failure
 */
int mcp_connection_pool_get_extended_stats(mcp_connection_pool_t* pool, mcp_connection_pool_extended_stats_t* stats) {
    if (!pool || !stats) {
        mcp_log_error("mcp_connection_pool_get_extended_stats: Received NULL pointer argument.");
        return -1;
    }

    pool_lock(pool);

    // Basic stats
    stats->total_connections = pool->total_count;
    stats->idle_connections = pool->idle_count;
    stats->active_connections = pool->active_count;

    // Health check stats
    stats->health_checks_performed = pool->health_checks_performed;
    stats->failed_health_checks = pool->failed_health_checks;

    // Performance stats
    stats->total_connections_created = pool->total_connections_created;
    stats->total_connections_closed = pool->total_connections_closed;
    stats->total_connection_gets = pool->total_connection_gets;
    stats->total_connection_timeouts = pool->total_connection_timeouts;
    stats->total_connection_errors = pool->total_connection_errors;
    stats->total_wait_time_ms = pool->total_wait_time_ms;
    stats->max_wait_time_ms = pool->max_wait_time_ms;

    // Calculate average wait time
    if (pool->total_connection_gets > 0) {
        stats->avg_wait_time_ms = (double)pool->total_wait_time_ms / pool->total_connection_gets;
    } else {
        stats->avg_wait_time_ms = 0.0;
    }

    pool_unlock(pool);

    return 0;
}
