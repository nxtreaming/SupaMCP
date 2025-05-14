#include "internal/connection_pool_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 * @brief Initializes synchronization primitives for a connection pool.
 *
 * This function creates a mutex and condition variable for the pool.
 *
 * @param pool The connection pool.
 * @return 0 on success, -1 on failure.
 */
int init_sync_primitives(mcp_connection_pool_t* pool) {
    pool->mutex = mcp_mutex_create();
    if (!pool->mutex) {
        mcp_log_error("Failed to create connection pool mutex.");
        return -1;
    }
    pool->cond_var = mcp_cond_create();
    if (!pool->cond_var) {
        mcp_log_error("Failed to create connection pool condition variable.");
        mcp_mutex_destroy(pool->mutex);
        pool->mutex = NULL;
        return -1;
    }
    return 0;
}

/**
 * @brief Destroys synchronization primitives for a connection pool.
 *
 * This function destroys the mutex and condition variable for the pool.
 *
 * @param pool The connection pool.
 */
void destroy_sync_primitives(mcp_connection_pool_t* pool) {
    // Destroy functions are safe to call even if pointer is NULL
    mcp_mutex_destroy(pool->mutex);
    mcp_cond_destroy(pool->cond_var);
    pool->mutex = NULL;
    pool->cond_var = NULL;
}

/**
 * @brief Waits on the pool's condition variable.
 *
 * This function waits on the pool's condition variable for a specified timeout.
 * It simplifies the handling of timeouts and error conditions.
 *
 * @param pool The connection pool.
 * @param timeout_ms Timeout in milliseconds. Negative value means wait indefinitely.
 * @return 0 on success (signaled), 1 on timeout, -1 on error.
 */
int pool_wait(mcp_connection_pool_t* pool, int timeout_ms) {
    int result;
    if (timeout_ms < 0) {
        result = mcp_cond_wait(pool->cond_var, pool->mutex);
    } else {
        result = mcp_cond_timedwait(pool->cond_var, pool->mutex, (uint32_t)timeout_ms);
    }

    if (result == 0) {
        return 0; // Signaled
    } else if (result == ETIMEDOUT) {
        return 1; // Timeout
    } else {
        mcp_log_error("mcp_cond_wait/timedwait failed with code: %d", result);
        return -1; // Error
    }
}
