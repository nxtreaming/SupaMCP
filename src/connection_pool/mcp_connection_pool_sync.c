#include "internal/connection_pool_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes needed for sync primitives
#ifdef _WIN32
// Included via internal header
#else
#include <pthread.h>
#include <sys/time.h>
#endif

// --- Synchronization Primitive Helpers ---

int init_sync_primitives(mcp_connection_pool_t* pool) {
    #ifdef _WIN32
        InitializeCriticalSection(&pool->mutex);
        InitializeConditionVariable(&pool->cond_var);
        return 0; // No error code defined for these in basic usage
    #else
        if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
            mcp_log_error("pthread_mutex_init failed: %s", strerror(errno));
            return -1;
        }
        if (pthread_cond_init(&pool->cond_var, NULL) != 0) {
            mcp_log_error("pthread_cond_init failed: %s", strerror(errno));
            pthread_mutex_destroy(&pool->mutex); // Clean up mutex
            return -1;
        }
        return 0;
    #endif
}

void destroy_sync_primitives(mcp_connection_pool_t* pool) {
     #ifdef _WIN32
        DeleteCriticalSection(&pool->mutex);
        // Condition variables don't need explicit destruction on Windows
    #else
        // It's good practice to check return codes, though errors during destroy are less common to handle
        int mutex_ret = pthread_mutex_destroy(&pool->mutex);
        int cond_ret = pthread_cond_destroy(&pool->cond_var);
        if (mutex_ret != 0) {
             mcp_log_warn("pthread_mutex_destroy failed: %s", strerror(mutex_ret));
        }
        if (cond_ret != 0) {
             mcp_log_warn("pthread_cond_destroy failed: %s", strerror(cond_ret));
        }
    #endif
}

void pool_lock(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    // TODO: Add error checking for pthread_mutex_lock?
    pthread_mutex_lock(&pool->mutex);
#endif
}

void pool_unlock(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    // TODO: Add error checking for pthread_mutex_unlock?
    pthread_mutex_unlock(&pool->mutex);
#endif
}

void pool_signal(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    WakeConditionVariable(&pool->cond_var);
#else
    // TODO: Add error checking for pthread_cond_signal?
    pthread_cond_signal(&pool->cond_var);
#endif
}

void pool_broadcast(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    WakeAllConditionVariable(&pool->cond_var);
#else
    // TODO: Add error checking for pthread_cond_broadcast?
    pthread_cond_broadcast(&pool->cond_var);
#endif
}

// Helper function to wait on the condition variable
// Returns 0 on success (signaled), 1 on timeout, -1 on error
int pool_wait(mcp_connection_pool_t* pool, int timeout_ms) {
#ifdef _WIN32
    // SleepConditionVariableCS takes a relative timeout in milliseconds
    DWORD win_timeout = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (!SleepConditionVariableCS(&pool->cond_var, &pool->mutex, win_timeout)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return 1; // Timeout
        } else {
             mcp_log_error("SleepConditionVariableCS failed: %lu", GetLastError());
            return -1; // Error
        }
    }
    return 0; // Signaled (or spurious wakeup)
#else // POSIX
    int rc = 0;
    if (timeout_ms < 0) { // Wait indefinitely
        rc = pthread_cond_wait(&pool->cond_var, &pool->mutex);
    } else { // Wait with timeout
        struct timespec deadline;
        // Need get_current_time_ms and calculate_deadline, or reimplement time logic here
        // For now, assume calculate_deadline is available (will move later)
        struct timeval tv;
        gettimeofday(&tv, NULL);
        long long nsec = tv.tv_usec * 1000 + (long long)(timeout_ms % 1000) * 1000000;
        deadline.tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
        deadline.tv_nsec = nsec % 1000000000;

        rc = pthread_cond_timedwait(&pool->cond_var, &pool->mutex, &deadline);
    }

    if (rc == ETIMEDOUT) {
        return 1; // Timeout
    } else if (rc != 0) {
        mcp_log_error("pthread_cond_timedwait/wait failed: %s", strerror(rc));
        return -1; // Error
    }
    return 0; // Signaled (or spurious wakeup)
#endif
}
