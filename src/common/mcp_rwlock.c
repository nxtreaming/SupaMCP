/**
 * @file mcp_rwlock.c
 * @brief Implementation of cross-platform read-write locks
 */

#include "mcp_rwlock.h"
#include "mcp_log.h"
#include <stdlib.h>

#ifdef _WIN32
    // Windows implementation
    #include <windows.h>

    struct mcp_rwlock_t {
        SRWLOCK srwlock;
        bool initialized;
    };

    mcp_rwlock_t* mcp_rwlock_create(void) {
        mcp_rwlock_t* rwlock = (mcp_rwlock_t*)malloc(sizeof(mcp_rwlock_t));
        if (!rwlock) {
            mcp_log_error("Failed to allocate memory for read-write lock");
            return NULL;
        }

        if (!mcp_rwlock_init(rwlock)) {
            free(rwlock);
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
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot destroy uninitialized read-write lock");
            return false;
        }

        // SRW locks don't need explicit destruction in Windows
        rwlock->initialized = false;

        mcp_log_debug("Read-write lock destroyed");
        return true;
    }

    bool mcp_rwlock_read_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot acquire read lock on uninitialized read-write lock");
            return false;
        }

        AcquireSRWLockShared(&rwlock->srwlock);
        return true;
    }

    bool mcp_rwlock_try_read_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot try read lock on uninitialized read-write lock");
            return false;
        }

        return TryAcquireSRWLockShared(&rwlock->srwlock) != 0;
    }

    bool mcp_rwlock_read_unlock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot release read lock on uninitialized read-write lock");
            return false;
        }

        ReleaseSRWLockShared(&rwlock->srwlock);
        return true;
    }

    bool mcp_rwlock_write_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot acquire write lock on uninitialized read-write lock");
            return false;
        }

        AcquireSRWLockExclusive(&rwlock->srwlock);
        return true;
    }

    bool mcp_rwlock_try_write_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot try write lock on uninitialized read-write lock");
            return false;
        }

        return TryAcquireSRWLockExclusive(&rwlock->srwlock) != 0;
    }

    bool mcp_rwlock_write_unlock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot release write lock on uninitialized read-write lock");
            return false;
        }

        ReleaseSRWLockExclusive(&rwlock->srwlock);
        return true;
    }

    void mcp_rwlock_free(mcp_rwlock_t* rwlock) {
        if (rwlock) {
            mcp_rwlock_destroy(rwlock);
            free(rwlock);
        }
    }

#else
    // POSIX implementation
    #include <pthread.h>

    struct mcp_rwlock_t {
        pthread_rwlock_t rwlock;
        bool initialized;
    };

    mcp_rwlock_t* mcp_rwlock_create(void) {
        mcp_rwlock_t* rwlock = (mcp_rwlock_t*)malloc(sizeof(mcp_rwlock_t));
        if (!rwlock) {
            mcp_log_error("Failed to allocate memory for read-write lock");
            return NULL;
        }

        if (!mcp_rwlock_init(rwlock)) {
            free(rwlock);
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
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot destroy uninitialized read-write lock");
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
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot acquire read lock on uninitialized read-write lock");
            return false;
        }

        if (pthread_rwlock_rdlock(&rwlock->rwlock) != 0) {
            mcp_log_error("Failed to acquire read lock");
            return false;
        }

        return true;
    }

    bool mcp_rwlock_try_read_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot try read lock on uninitialized read-write lock");
            return false;
        }

        return pthread_rwlock_tryrdlock(&rwlock->rwlock) == 0;
    }

    bool mcp_rwlock_read_unlock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot release read lock on uninitialized read-write lock");
            return false;
        }

        if (pthread_rwlock_unlock(&rwlock->rwlock) != 0) {
            mcp_log_error("Failed to release read lock");
            return false;
        }

        return true;
    }

    bool mcp_rwlock_write_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot acquire write lock on uninitialized read-write lock");
            return false;
        }

        if (pthread_rwlock_wrlock(&rwlock->rwlock) != 0) {
            mcp_log_error("Failed to acquire write lock");
            return false;
        }

        return true;
    }

    bool mcp_rwlock_try_write_lock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot try write lock on uninitialized read-write lock");
            return false;
        }

        return pthread_rwlock_trywrlock(&rwlock->rwlock) == 0;
    }

    bool mcp_rwlock_write_unlock(mcp_rwlock_t* rwlock) {
        if (!rwlock || !rwlock->initialized) {
            mcp_log_error("Cannot release write lock on uninitialized read-write lock");
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
            free(rwlock);
        }
    }
#endif
