/**
 * @file mcp_rwlock.c
 * @brief Implementation of cross-platform read-write locks
 */

#include "mcp_rwlock.h"
#include "mcp_log.h"
#include "mcp_memory_pool.h"
#include "mcp_cache_aligned.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

/**
 * @brief Read-write lock implementation with cache line alignment.
 *
 * The rwlock is cache-line aligned to prevent false sharing between different locks.
 */
struct mcp_rwlock_t {
    MCP_CACHE_ALIGNED SRWLOCK srwlock;
    bool initialized;
    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE - sizeof(SRWLOCK) - sizeof(bool)];
};

/**
 * @brief Helper function to validate rwlock state
 *
 * @param rwlock The rwlock to validate
 * @param operation_name Name of the operation being performed (for error logging)
 * @return true if valid, false otherwise
 */
static inline bool validate_rwlock(const mcp_rwlock_t* rwlock, const char* operation_name) {
    if (!rwlock || !rwlock->initialized) {
        mcp_log_error("Cannot %s on uninitialized read-write lock", operation_name);
        return false;
    }
    return true;
}

mcp_rwlock_t* mcp_rwlock_create(void) {
    // Use memory pool allocation if available
    mcp_rwlock_t* rwlock = NULL;
    if (mcp_memory_pool_system_is_initialized()) {
        rwlock = (mcp_rwlock_t*)mcp_pool_alloc(sizeof(mcp_rwlock_t));
    } else {
        rwlock = (mcp_rwlock_t*)malloc(sizeof(mcp_rwlock_t));
    }

    if (!rwlock) {
        mcp_log_error("Failed to allocate memory for read-write lock");
        return NULL;
    }

    if (!mcp_rwlock_init(rwlock)) {
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(rwlock);
        } else {
            free(rwlock);
        }
        return NULL;
    }

    return rwlock;
}

bool mcp_rwlock_init(mcp_rwlock_t* rwlock) {
    if (!rwlock) {
        mcp_log_error("Cannot initialize NULL read-write lock");
        return false;
    }

    InitializeSRWLock(&rwlock->srwlock);
    rwlock->initialized = true;

    mcp_log_debug("Read-write lock initialized (Windows implementation)");
    return true;
}

bool mcp_rwlock_destroy(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "destroy")) {
        return false;
    }

    // SRW locks don't need explicit destruction in Windows
    rwlock->initialized = false;

    mcp_log_debug("Read-write lock destroyed");
    return true;
}

bool mcp_rwlock_read_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "acquire read lock")) {
        return false;
    }

    AcquireSRWLockShared(&rwlock->srwlock);
    return true;
}

bool mcp_rwlock_try_read_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "try read lock")) {
        return false;
    }

    return TryAcquireSRWLockShared(&rwlock->srwlock) != 0;
}

bool mcp_rwlock_read_unlock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "release read lock")) {
        return false;
    }

    #pragma warning(suppress: 26110)
    ReleaseSRWLockShared(&rwlock->srwlock);
    return true;
}

bool mcp_rwlock_write_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "acquire write lock")) {
        return false;
    }

    AcquireSRWLockExclusive(&rwlock->srwlock);
    return true;
}

bool mcp_rwlock_try_write_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "try write lock")) {
        return false;
    }

    return TryAcquireSRWLockExclusive(&rwlock->srwlock) != 0;
}

bool mcp_rwlock_write_unlock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "release write lock")) {
        return false;
    }

    #pragma warning(suppress: 26110)
    ReleaseSRWLockExclusive(&rwlock->srwlock);
    return true;
}

void mcp_rwlock_free(mcp_rwlock_t* rwlock) {
    if (rwlock) {
        mcp_rwlock_destroy(rwlock);

        // Use appropriate free function based on memory system
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(rwlock);
        } else {
            free(rwlock);
        }
    }
}

#else
#include <pthread.h>

/**
 * @brief Read-write lock implementation with cache line alignment.
 *
 * The rwlock is cache-line aligned to prevent false sharing between different locks.
 */
struct mcp_rwlock_t {
    MCP_CACHE_ALIGNED pthread_rwlock_t rwlock;
    bool initialized;
    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE - sizeof(pthread_rwlock_t) - sizeof(bool)];
};

/**
 * @brief Helper function to validate rwlock state
 *
 * @param rwlock The rwlock to validate
 * @param operation_name Name of the operation being performed (for error logging)
 * @return true if valid, false otherwise
 */
static inline bool validate_rwlock(const mcp_rwlock_t* rwlock, const char* operation_name) {
    if (!rwlock || !rwlock->initialized) {
        mcp_log_error("Cannot %s on uninitialized read-write lock", operation_name);
        return false;
    }
    return true;
}

mcp_rwlock_t* mcp_rwlock_create(void) {
    // Use memory pool allocation if available
    mcp_rwlock_t* rwlock = NULL;
    if (mcp_memory_pool_system_is_initialized()) {
        rwlock = (mcp_rwlock_t*)mcp_pool_alloc(sizeof(mcp_rwlock_t));
    } else {
        rwlock = (mcp_rwlock_t*)malloc(sizeof(mcp_rwlock_t));
    }

    if (!rwlock) {
        mcp_log_error("Failed to allocate memory for read-write lock");
        return NULL;
    }

    if (!mcp_rwlock_init(rwlock)) {
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(rwlock);
        } else {
            free(rwlock);
        }
        return NULL;
    }

    return rwlock;
}

bool mcp_rwlock_init(mcp_rwlock_t* rwlock) {
    if (!rwlock) {
        mcp_log_error("Cannot initialize NULL read-write lock");
        return false;
    }

    if (pthread_rwlock_init(&rwlock->rwlock, NULL) != 0) {
        mcp_log_error("Failed to initialize read-write lock");
        return false;
    }

    rwlock->initialized = true;

    mcp_log_debug("Read-write lock initialized (POSIX implementation)");
    return true;
}

bool mcp_rwlock_destroy(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "destroy")) {
        return false;
    }

    if (pthread_rwlock_destroy(&rwlock->rwlock) != 0) {
        mcp_log_error("Failed to destroy read-write lock");
        return false;
    }

    rwlock->initialized = false;

    mcp_log_debug("Read-write lock destroyed");
    return true;
}

bool mcp_rwlock_read_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "acquire read lock")) {
        return false;
    }

    if (pthread_rwlock_rdlock(&rwlock->rwlock) != 0) {
        mcp_log_error("Failed to acquire read lock");
        return false;
    }

    return true;
}

bool mcp_rwlock_try_read_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "try read lock")) {
        return false;
    }

    return pthread_rwlock_tryrdlock(&rwlock->rwlock) == 0;
}

bool mcp_rwlock_read_unlock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "release read lock")) {
        return false;
    }

    if (pthread_rwlock_unlock(&rwlock->rwlock) != 0) {
        mcp_log_error("Failed to release read lock");
        return false;
    }

    return true;
}

bool mcp_rwlock_write_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "acquire write lock")) {
        return false;
    }

    if (pthread_rwlock_wrlock(&rwlock->rwlock) != 0) {
        mcp_log_error("Failed to acquire write lock");
        return false;
    }

    return true;
}

bool mcp_rwlock_try_write_lock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "try write lock")) {
        return false;
    }

    return pthread_rwlock_trywrlock(&rwlock->rwlock) == 0;
}

bool mcp_rwlock_write_unlock(mcp_rwlock_t* rwlock) {
    if (!validate_rwlock(rwlock, "release write lock")) {
        return false;
    }

    if (pthread_rwlock_unlock(&rwlock->rwlock) != 0) {
        mcp_log_error("Failed to release write lock");
        return false;
    }

    return true;
}

void mcp_rwlock_free(mcp_rwlock_t* rwlock) {
    if (rwlock) {
        mcp_rwlock_destroy(rwlock);

        // Use appropriate free function based on memory system
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(rwlock);
        } else {
            free(rwlock);
        }
    }
}
#endif
