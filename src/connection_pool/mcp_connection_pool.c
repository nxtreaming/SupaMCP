#include "internal/connection_pool_internal.h" 
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
    int connect_timeout_ms)
{
    // Use fprintf for initial checks as logging might not be ready
    if (!host || port <= 0 || max_connections == 0 || min_connections > max_connections) {
        fprintf(stderr, "Error: mcp_connection_pool_create invalid arguments.\n");
        return NULL;
    }

    mcp_connection_pool_t* pool = (mcp_connection_pool_t*)calloc(1, sizeof(mcp_connection_pool_t));
    if (!pool) {
        fprintf(stderr, "Error: mcp_connection_pool_create failed to allocate pool structure.\n");
        return NULL;
    }

    // Use mcp_strdup from mcp_types.h (included via internal header)
    pool->host = mcp_strdup(host);
    if (!pool->host) {
        fprintf(stderr, "Error: mcp_connection_pool_create failed to duplicate host string.\n");
        free(pool);
        return NULL;
    }

    pool->port = port;
    pool->min_connections = min_connections;
    pool->max_connections = max_connections;
    pool->idle_timeout_ms = idle_timeout_ms;
    pool->connect_timeout_ms = connect_timeout_ms;
    pool->shutting_down = false;
    pool->idle_list = NULL;
    pool->idle_count = 0;
    pool->active_count = 0;
    pool->total_count = 0;

    // Call helper from mcp_connection_pool_sync.c
    if (init_sync_primitives(pool) != 0) {
        mcp_log_error("mcp_connection_pool_create failed to initialize synchronization primitives.");
        free(pool->host);
        free(pool);
        return NULL;
    }

    // TODO: Pre-populate pool with min_connections (potentially in a background thread)
    mcp_log_info("Connection pool created for %s:%d (min:%zu, max:%zu).",
            pool->host, pool->port, pool->min_connections, pool->max_connections);

    // TODO: Start maintenance thread if idle_timeout_ms > 0 or min_connections > 0

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
        start_time_ms = get_current_time_ms(); // Use helper from utils
    }

    pool_lock(pool); // Use helper from sync

    while (sock == INVALID_SOCKET_HANDLE) {
        if (pool->shutting_down) {
            mcp_log_warn("mcp_connection_pool_get: Pool is shutting down.");
            pool_unlock(pool); // Use helper from sync
            return INVALID_SOCKET_HANDLE;
        }

        // 1. Try to get an idle connection
        if (pool->idle_list) {
            mcp_pooled_connection_t* pooled_conn = pool->idle_list;
            pool->idle_list = pooled_conn->next;
            pool->idle_count--;
            pool->active_count++;
            sock = pooled_conn->socket_fd;

            // TODO: Implement idle timeout check more robustly if needed
            // (Requires comparing pooled_conn->last_used_time with current time)

            free(pooled_conn); // Free the list node structure
            mcp_log_debug("Reusing idle connection %d.", (int)sock);
            pool_unlock(pool); // Use helper from sync
            return sock;
        }

        // 2. If no idle connections, try to create a new one if allowed
        if (pool->total_count < pool->max_connections) {
            size_t current_total = pool->total_count; // Read before unlocking
            pool->total_count++; // Optimistically increment total count
            pool_unlock(pool);   // Unlock while creating connection

            mcp_log_debug("Attempting to create new connection (%zu/%zu).", current_total + 1, pool->max_connections);
            // Use helper from socket utils
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
                if (timeout_ms == 0) { // Don't wait if timeout is 0
                    pool_unlock(pool); // Use helper from sync
                    return INVALID_SOCKET_HANDLE;
                }
                // Fall through to wait below
            }
        }

        // 3. If pool is full or creation failed, wait if timeout allows
        if (sock == INVALID_SOCKET_HANDLE) {
             if (timeout_ms == 0) { // Don't wait
                 mcp_log_warn("mcp_connection_pool_get: Pool full and timeout is 0.");
                 pool_unlock(pool); // Use helper from sync
                 return INVALID_SOCKET_HANDLE;
             }

             int wait_timeout = timeout_ms;
             if (timeout_ms > 0) {
                 long long elapsed_ms = get_current_time_ms() - start_time_ms; // Use helper from utils
                 wait_timeout = timeout_ms - (int)elapsed_ms;
                 if (wait_timeout <= 0) {
                     mcp_log_warn("mcp_connection_pool_get: Timed out waiting for connection.");
                     pool_unlock(pool); // Use helper from sync
                     return INVALID_SOCKET_HANDLE; // Overall timeout expired
                 }
             }

             mcp_log_debug("Waiting for connection (timeout: %d ms)...", wait_timeout);
             int wait_result = pool_wait(pool, wait_timeout); // Use helper from sync

             if (wait_result == 1) { // Timeout occurred during wait
                 mcp_log_warn("mcp_connection_pool_get: Timed out waiting for condition.");
                 pool_unlock(pool); // Use helper from sync
                 return INVALID_SOCKET_HANDLE;
             } else if (wait_result == -1) { // Error during wait
                 mcp_log_error("mcp_connection_pool_get: Error waiting for condition.");
                 pool_unlock(pool); // Use helper from sync
                 return INVALID_SOCKET_HANDLE;
             }
             // If wait_result is 0, we were signaled or had a spurious wakeup, loop continues
             mcp_log_debug("Woke up from wait, retrying get.");
        }
    } // End while loop

    pool_unlock(pool); // Use helper from sync
    return sock;
}

int mcp_connection_pool_release(mcp_connection_pool_t* pool, socket_handle_t connection, bool is_valid) {
     if (!pool || connection == INVALID_SOCKET_HANDLE) {
        mcp_log_error("mcp_connection_pool_release: Invalid arguments (pool=%p, connection=%d).", (void*)pool, (int)connection);
        return -1;
    }

    pool_lock(pool); // Use helper from sync

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
        close_connection(connection); // Use helper from socket utils
        pool->total_count--;
        // No signal needed, broadcast happens in destroy
    } else if (!is_valid) {
        mcp_log_warn("Closing invalid connection %d.", (int)connection);
        close_connection(connection); // Use helper from socket utils
        pool->total_count--;
        // Signal potentially waiting getters that a slot might be free for creation
        pool_signal(pool); // Use helper from sync
    } else {
        // Add valid connection back to idle list
        mcp_pooled_connection_t* pooled_conn = (mcp_pooled_connection_t*)malloc(sizeof(mcp_pooled_connection_t));
        if (pooled_conn) {
            pooled_conn->socket_fd = connection;
            pooled_conn->last_used_time = time(NULL); // Record return time
            pooled_conn->next = pool->idle_list;
            pool->idle_list = pooled_conn;
            pool->idle_count++;
            mcp_log_debug("Returned connection %d to idle pool.", (int)connection);
            // Signal one waiting getter that a connection is available
            pool_signal(pool); // Use helper from sync
        } else {
            // Failed to allocate node, just close the connection
             mcp_log_error("Failed to allocate node for idle connection %d, closing.", (int)connection);
             close_connection(connection); // Use helper from socket utils
             pool->total_count--;
             // Signal potentially waiting getters that a slot might be free for creation
             pool_signal(pool); // Use helper from sync
        }
    }

    pool_unlock(pool); // Use helper from sync
    return 0;
}

void mcp_connection_pool_destroy(mcp_connection_pool_t* pool) {
    if (!pool) {
        return;
    }

    mcp_log_info("Destroying connection pool for %s:%d.", pool->host, pool->port);

    // 1. Signal shutdown and wake waiters
    pool_lock(pool); // Use helper from sync
    if (pool->shutting_down) { // Avoid double destroy
        pool_unlock(pool); // Use helper from sync
        return;
    }
    pool->shutting_down = true;
    pool_broadcast(pool); // Use helper from sync
    pool_unlock(pool); // Use helper from sync

    // 2. Optional: Join maintenance thread if it exists
    // TODO: Implement maintenance thread join logic if added

    // 3. Close idle connections and free resources
    pool_lock(pool); // Use helper from sync
    mcp_log_info("Closing %zu idle connections.", pool->idle_count);
    mcp_pooled_connection_t* current = pool->idle_list;
    while(current) {
        mcp_pooled_connection_t* next = current->next;
        close_connection(current->socket_fd); // Use helper from socket utils
        free(current);
        current = next;
    }
    pool->idle_list = NULL;
    pool->idle_count = 0;

    // Note: Active connections are not explicitly waited for here.
    mcp_log_info("%zu connections were active during shutdown.", pool->active_count);
    pool->total_count = 0; // Reset counts
    pool->active_count = 0;

    pool_unlock(pool); // Use helper from sync

    // 4. Destroy sync primitives and free memory
    destroy_sync_primitives(pool); // Use helper from sync
    free(pool->host);
    free(pool);

    mcp_log_info("Connection pool destroyed.");
}

int mcp_connection_pool_get_stats(mcp_connection_pool_t* pool, size_t* total_connections, size_t* idle_connections, size_t* active_connections) {
    if (!pool || !total_connections || !idle_connections || !active_connections) {
        mcp_log_error("mcp_connection_pool_get_stats: Received NULL pointer argument.");
        return -1;
    }

    pool_lock(pool); // Use helper from sync

    *total_connections = pool->total_count;
    *idle_connections = pool->idle_count;
    *active_connections = pool->active_count;

    pool_unlock(pool); // Use helper from sync

    return 0;
}
