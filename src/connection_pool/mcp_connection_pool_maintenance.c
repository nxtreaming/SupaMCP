#include "internal/connection_pool_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

// Thread function for pool maintenance
void* pool_maintenance_thread_func(void* arg) {
    mcp_connection_pool_t* pool = (mcp_connection_pool_t*)arg;
    if (!pool) {
        mcp_log_error("Maintenance thread started with NULL pool.");
        return NULL;
    }

    mcp_log_info("Connection pool maintenance thread started for %s:%d.", pool->host, pool->port);

    // Main maintenance loop
    while (true) {
        // Sleep for a reasonable interval (e.g., 1 second)
        #ifdef _WIN32
        Sleep(1000);
        #else
        usleep(1000000); // 1 second in microseconds
        #endif

        // Check if we should exit
        pool_lock(pool);
        if (pool->shutting_down) {
            pool_unlock(pool);
            break;
        }

        // Get current time for idle timeout checks
        time_t current_time = time(NULL);

        // 1. Check and close idle connections that have timed out
        if (pool->idle_timeout_ms > 0) {
            mcp_pooled_connection_t* prev = NULL;
            mcp_pooled_connection_t* current = pool->idle_list;

            while (current) {
                // Check if this connection has timed out
                double idle_time_sec = difftime(current_time, current->last_used_time);
                if (idle_time_sec * 1000 > pool->idle_timeout_ms) {
                    // Connection has timed out, remove it from the list
                    mcp_pooled_connection_t* to_remove = current;

                    if (prev) {
                        prev->next = current->next;
                        current = current->next;
                    } else {
                        // Removing the head of the list
                        pool->idle_list = current->next;
                        current = pool->idle_list;
                    }

                    // Close the connection and free the node
                    mcp_log_debug("Closing idle connection %d due to timeout (idle for %.1f seconds).",
                                 (int)to_remove->socket_fd, idle_time_sec);
                    close_connection(to_remove->socket_fd);
                    free(to_remove);

                    // Update counts
                    pool->idle_count--;
                    pool->total_count--;
                } else {
                    // Connection is still within timeout, move to next
                    prev = current;
                    current = current->next;
                }
            }
        }

        // 2. Perform health checks on idle connections if enabled
        if (pool->health_check_interval_ms > 0) {
            // Temporarily unlock the pool while performing health checks
            pool_unlock(pool);

            // Perform health checks
            int failed_checks = perform_health_checks(pool);
            if (failed_checks > 0) {
                mcp_log_warn("Health check: %d connections failed health check and were removed.", failed_checks);
            }

            // Re-lock the pool
            pool_lock(pool);

            // Check if we need to exit after health checks
            if (pool->shutting_down) {
                pool_unlock(pool);
                break;
            }
        }

        // 3. Ensure minimum connections are maintained
        if (pool->min_connections > 0 && pool->total_count < pool->min_connections) {
            size_t connections_to_add = pool->min_connections - pool->total_count;
            mcp_log_debug("Maintaining minimum connections: adding %zu connections.", connections_to_add);

            for (size_t i = 0; i < connections_to_add && pool->total_count < pool->max_connections; i++) {
                // Increment count before unlocking
                pool->total_count++;

                // Temporarily unlock while creating connection
                pool_unlock(pool);

                // Create new connection
                socket_handle_t new_sock = create_new_connection(pool->host, pool->port, pool->connect_timeout_ms);

                // Re-lock before updating state
                pool_lock(pool);

                if (new_sock != INVALID_SOCKET_HANDLE) {
                    // Add to idle list
                    mcp_pooled_connection_t* new_conn = (mcp_pooled_connection_t*)malloc(sizeof(mcp_pooled_connection_t));
                    if (new_conn) {
                        new_conn->socket_fd = new_sock;
                        new_conn->last_used_time = time(NULL);
                        // Initialize health check fields
                        init_connection_health(new_conn);
                        new_conn->next = pool->idle_list;
                        pool->idle_list = new_conn;
                        pool->idle_count++;
                        mcp_log_debug("Added new connection %d to maintain minimum pool size.", (int)new_sock);
                    } else {
                        // Failed to allocate node, close the connection
                        mcp_log_error("Failed to allocate node for new connection %d.", (int)new_sock);
                        close_connection(new_sock);
                        pool->total_count--; // Decrement since we couldn't add it
                    }
                } else {
                    // Failed to create connection
                    mcp_log_warn("Failed to create new connection to maintain minimum pool size.");
                    pool->total_count--; // Decrement since creation failed
                }
            }
        }

        pool_unlock(pool);
    }

    mcp_log_info("Connection pool maintenance thread exiting.");
    return NULL;
}

// Function to pre-populate the pool with min_connections
int prepopulate_pool(mcp_connection_pool_t* pool) {
    if (!pool || pool->min_connections == 0) {
        return 0; // Nothing to do
    }

    mcp_log_info("Pre-populating connection pool with %zu connections.", pool->min_connections);

    // Lock the pool before modifying it
    pool_lock(pool);

    size_t success_count = 0;
    for (size_t i = 0; i < pool->min_connections && pool->total_count < pool->max_connections; i++) {
        // Increment count before unlocking
        pool->total_count++;

        // Temporarily unlock while creating connection
        pool_unlock(pool);

        // Create new connection
        socket_handle_t new_sock = create_new_connection(pool->host, pool->port, pool->connect_timeout_ms);

        // Re-lock before updating state
        pool_lock(pool);

        if (new_sock != INVALID_SOCKET_HANDLE) {
            // Add to idle list
            mcp_pooled_connection_t* new_conn = (mcp_pooled_connection_t*)malloc(sizeof(mcp_pooled_connection_t));
            if (new_conn) {
                new_conn->socket_fd = new_sock;
                new_conn->last_used_time = time(NULL);
                // Initialize health check fields
                init_connection_health(new_conn);
                new_conn->next = pool->idle_list;
                pool->idle_list = new_conn;
                pool->idle_count++;
                success_count++;
                mcp_log_debug("Pre-populated pool with connection %d (%zu/%zu).",
                             (int)new_sock, success_count, pool->min_connections);
            } else {
                // Failed to allocate node, close the connection
                mcp_log_error("Failed to allocate node for pre-populated connection %d.", (int)new_sock);
                close_connection(new_sock);
                pool->total_count--; // Decrement since we couldn't add it
            }
        } else {
            // Failed to create connection
            mcp_log_warn("Failed to create connection during pre-population.");
            pool->total_count--; // Decrement since creation failed
        }
    }

    pool_unlock(pool);

    mcp_log_info("Pre-populated pool with %zu/%zu connections.", success_count, pool->min_connections);
    return (success_count > 0) ? 0 : -1; // Return success if at least one connection was created
}

// Function to start the maintenance thread
int start_maintenance_thread(mcp_connection_pool_t* pool) {
    if (!pool) {
        return -1;
    }

    // Only start maintenance thread if we need idle timeout checks or min connections maintenance
    if (pool->idle_timeout_ms <= 0 && pool->min_connections == 0) {
        mcp_log_debug("No maintenance thread needed (idle_timeout_ms=%d, min_connections=%zu).",
                     pool->idle_timeout_ms, pool->min_connections);
        return 0; // No maintenance needed
    }

    // Create and start the maintenance thread
    if (mcp_thread_create(&pool->maintenance_thread, pool_maintenance_thread_func, pool) != 0) {
        mcp_log_error("Failed to create connection pool maintenance thread.");
        return -1;
    }

    mcp_log_info("Started connection pool maintenance thread.");
    return 0;
}

// Function to stop the maintenance thread
void stop_maintenance_thread(mcp_connection_pool_t* pool) {
    if (!pool || !pool->maintenance_thread) {
        return;
    }

    // Signal thread to exit (shutting_down flag should already be set)
    mcp_log_debug("Waiting for maintenance thread to exit...");

    // Wait for thread to exit
    mcp_thread_join(pool->maintenance_thread, NULL);
    pool->maintenance_thread = 0; // Reset handle

    mcp_log_info("Maintenance thread stopped.");
}
