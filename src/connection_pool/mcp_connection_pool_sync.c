#include "internal/connection_pool_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes are no longer needed here

// --- Synchronization Primitive Helpers ---

int init_sync_primitives(mcp_connection_pool_t* pool) {
    pool->mutex = mcp_mutex_create();
    if (!pool->mutex) {
        mcp_log_error("Failed to create connection pool mutex.");
        return -1;
    }
    pool->cond_var = mcp_cond_create();
    if (!pool->cond_var) {
        mcp_log_error("Failed to create connection pool condition variable.");
        mcp_mutex_destroy(pool->mutex); // Clean up mutex
        pool->mutex = NULL;
        return -1;
    }
    return 0;
}

void destroy_sync_primitives(mcp_connection_pool_t* pool) {
    // Destroy functions are safe to call even if pointer is NULL
    mcp_mutex_destroy(pool->mutex);
    mcp_cond_destroy(pool->cond_var);
    pool->mutex = NULL;
    pool->cond_var = NULL;
}

void pool_lock(mcp_connection_pool_t* pool) {
    if (mcp_mutex_lock(pool->mutex) != 0) {
        // Log error, but what else can we do? Abort?
        mcp_log_error("Failed to lock connection pool mutex!");
    }
}

void pool_unlock(mcp_connection_pool_t* pool) {
    if (mcp_mutex_unlock(pool->mutex) != 0) {
        // Log error
        mcp_log_error("Failed to unlock connection pool mutex!");
    }
}

void pool_signal(mcp_connection_pool_t* pool) {
    if (mcp_cond_signal(pool->cond_var) != 0) {
        // Log error
        mcp_log_error("Failed to signal connection pool condition variable!");
    }
}

void pool_broadcast(mcp_connection_pool_t* pool) {
    if (mcp_cond_broadcast(pool->cond_var) != 0) {
        // Log error
        mcp_log_error("Failed to broadcast connection pool condition variable!");
    }
}

// Helper function to wait on the condition variable
// Returns 0 on success (signaled), 1 on timeout, -1 on error
int pool_wait(mcp_connection_pool_t* pool, int timeout_ms) {
    int result;
    if (timeout_ms < 0) { // Wait indefinitely
        result = mcp_cond_wait(pool->cond_var, pool->mutex);
    } else { // Wait with timeout
        result = mcp_cond_timedwait(pool->cond_var, pool->mutex, (uint32_t)timeout_ms);
    }

    if (result == 0) {
        return 0; // Signaled
    } else if (result == ETIMEDOUT) { // Check for timeout specifically if needed by abstraction
        return 1; // Timeout
    } else {
        mcp_log_error("mcp_cond_wait/timedwait failed with code: %d", result);
        return -1; // Error
    }
}
