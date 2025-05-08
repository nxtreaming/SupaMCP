#include "mcp_sync.h"
#include "mcp_cache_aligned.h"
#include "mcp_memory_pool.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#else
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sched.h>
#include <stdatomic.h>
#endif

// Helper function to allocate memory from pool if available, or fallback to malloc
static void* sync_alloc(size_t size) {
    if (mcp_memory_pool_system_is_initialized()) {
        return mcp_pool_alloc(size);
    } else {
        return malloc(size);
    }
}

// Helper function to free memory allocated by sync_alloc
static void sync_free(void* ptr) {
    if (mcp_memory_pool_system_is_initialized()) {
        mcp_pool_free(ptr);
    } else {
        free(ptr);
    }
}

/**
 * @brief Mutex implementation with cache line alignment.
 *
 * The mutex is cache-line aligned to prevent false sharing between different mutexes.
 */
struct mcp_mutex_s {
#ifdef _WIN32
    MCP_CACHE_ALIGNED CRITICAL_SECTION cs;
#else
    MCP_CACHE_ALIGNED pthread_mutex_t mutex;
    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE - sizeof(pthread_mutex_t)];
#endif
};

/**
 * @brief Condition variable implementation with cache line alignment.
 *
 * The condition variable is cache-line aligned to prevent false sharing.
 */
struct mcp_cond_s {
#ifdef _WIN32
    MCP_CACHE_ALIGNED CONDITION_VARIABLE cv;
#else
    MCP_CACHE_ALIGNED pthread_cond_t cond;
    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE - sizeof(pthread_cond_t)];
#endif
};

mcp_mutex_t* mcp_mutex_create(void) {
    mcp_mutex_t* mutex = (mcp_mutex_t*)sync_alloc(sizeof(mcp_mutex_t));
    if (!mutex) {
        mcp_log_error("Failed to allocate memory for mutex");
        return NULL;
    }

#ifdef _WIN32
    InitializeCriticalSection(&mutex->cs);
    // Assume success for InitializeCriticalSection as it's void
    return mutex;
#else
    if (pthread_mutex_init(&mutex->mutex, NULL) != 0) {
        mcp_log_error("Failed to initialize pthread mutex");
        sync_free(mutex);
        return NULL;
    }
    return mutex;
#endif
}

void mcp_mutex_destroy(mcp_mutex_t* mutex) {
    if (!mutex) {
        return;
    }
#ifdef _WIN32
    DeleteCriticalSection(&mutex->cs);
#else
    pthread_mutex_destroy(&mutex->mutex);
#endif
    sync_free(mutex);
}

int mcp_mutex_lock(mcp_mutex_t* mutex) {
    if (!mutex) {
        return -1;
    }
#ifdef _WIN32
    EnterCriticalSection(&mutex->cs);
    return 0;
#else
    return pthread_mutex_lock(&mutex->mutex);
#endif
}

int mcp_mutex_unlock(mcp_mutex_t* mutex) {
    if (!mutex) {
        return -1;
    }
#ifdef _WIN32
    LeaveCriticalSection(&mutex->cs);
    return 0;
#else
    return pthread_mutex_unlock(&mutex->mutex);
#endif
}

mcp_cond_t* mcp_cond_create(void) {
    mcp_cond_t* cond = (mcp_cond_t*)sync_alloc(sizeof(mcp_cond_t));
    if (!cond) {
        mcp_log_error("Failed to allocate memory for condition variable");
        return NULL;
    }

#ifdef _WIN32
    InitializeConditionVariable(&cond->cv);
    // Assume success
    return cond;
#else
    if (pthread_cond_init(&cond->cond, NULL) != 0) {
        mcp_log_error("Failed to initialize pthread condition variable");
        sync_free(cond);
        return NULL;
    }
    return cond;
#endif
}

void mcp_cond_destroy(mcp_cond_t* cond) {
    if (!cond) {
        return;
    }
#ifdef _WIN32
    // No explicit destruction function for CONDITION_VARIABLE
#else
    pthread_cond_destroy(&cond->cond);
#endif
    sync_free(cond);
}

int mcp_cond_wait(mcp_cond_t* cond, mcp_mutex_t* mutex) {
    if (!cond || !mutex) {
        return -1;
    }
#ifdef _WIN32
    // SleepConditionVariableCS returns TRUE on success, FALSE on failure.
    if (!SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE)) {
        return -1;
    }
    return 0;
#else
    return pthread_cond_wait(&cond->cond, &mutex->mutex);
#endif
}

/**
 * @brief Waits on a condition variable with a timeout.
 *
 * This implementation has been optimized for accuracy and performance.
 * On Windows, it uses the native SleepConditionVariableCS function.
 * On POSIX systems, it uses clock_gettime with CLOCK_MONOTONIC when available
 * for more accurate timing, falling back to gettimeofday otherwise.
 *
 * @param cond Pointer to the condition variable to wait on.
 * @param mutex Pointer to the mutex associated with the condition.
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return 0 on success, -1 on error, -2 on timeout.
 */
int mcp_cond_timedwait(mcp_cond_t* cond, mcp_mutex_t* mutex, uint32_t timeout_ms) {
    if (!cond || !mutex) {
        return -1;
    }

    // Special case: if timeout is 0, just check and return immediately
    if (timeout_ms == 0) {
        return -2; // Treat as immediate timeout
    }

#ifdef _WIN32
    // SleepConditionVariableCS returns TRUE on success, FALSE on failure/timeout.
    if (!SleepConditionVariableCS(&cond->cv, &mutex->cs, timeout_ms)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return -2;
        }
        return -1;
    }
    return 0;
#else
    struct timespec ts;

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0) && defined(CLOCK_MONOTONIC)
    // Use monotonic clock for more accurate timing when available
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1;
    }

    // Calculate absolute timeout time
    uint64_t nsec = ts.tv_nsec + (uint64_t)(timeout_ms % 1000) * 1000000;
    ts.tv_sec += timeout_ms / 1000 + nsec / 1000000000;
    ts.tv_nsec = nsec % 1000000000;

    // Use pthread_cond_timedwait with the monotonic clock
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

    int ret = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
    pthread_condattr_destroy(&attr);
#else
    // Fall back to gettimeofday if monotonic clock is not available
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Calculate absolute timeout time
    uint64_t nsec = (uint64_t)tv.tv_usec * 1000 + (uint64_t)(timeout_ms % 1000) * 1000000;
    ts.tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
    ts.tv_nsec = nsec % 1000000000;

    int ret = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &ts);
#endif

    if (ret == ETIMEDOUT) {
        return -2;
    } else if (ret != 0) {
        return -1;
    }
    return 0;
#endif
}

int mcp_cond_signal(mcp_cond_t* cond) {
    if (!cond) {
        return -1;
    }
#ifdef _WIN32
    WakeConditionVariable(&cond->cv);
    return 0;
#else
    return pthread_cond_signal(&cond->cond);
#endif
}

int mcp_cond_broadcast(mcp_cond_t* cond) {
    if (!cond) {
        return -1;
    }
#ifdef _WIN32
    WakeAllConditionVariable(&cond->cv);
    return 0;
#else
    return pthread_cond_broadcast(&cond->cond);
#endif
}

int mcp_thread_create(mcp_thread_t* thread_handle, mcp_thread_func_t start_routine, void* arg) {
    if (!thread_handle || !start_routine) {
        return -1;
    }
#ifdef _WIN32
    // Note: Windows thread function signature is different (DWORD WINAPI func(LPVOID)),
    // but casting mcp_thread_func_t often works if the calling conventions match.
    // A safer approach might involve a wrapper function.
    HANDLE handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL);
    if (handle == NULL) {
        *thread_handle = NULL;
        return -1;
    }
    *thread_handle = handle;
    return 0;
#else
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, start_routine, arg);
    if (ret != 0) {
        *thread_handle = 0;
        return -1;
    }
    *thread_handle = tid;
    return 0;
#endif
}

int mcp_thread_join(mcp_thread_t thread_handle, void** retval) {
#ifdef _WIN32
    // WaitForSingleObject waits for the thread to terminate.
    // Getting the actual return value is more complex and not directly supported here.
    if (WaitForSingleObject((HANDLE)thread_handle, INFINITE) == WAIT_FAILED) {
        return -1;
    }
    // Close the handle after joining
    CloseHandle((HANDLE)thread_handle);
    if (retval) *retval = NULL;
    return 0;
#else
    return pthread_join((pthread_t)thread_handle, retval);
#endif
}

/**
 * @brief Yields the execution of the current thread.
 */
void mcp_thread_yield(void) {
#ifdef _WIN32
    // Allows another ready thread on the same processor to run
    SwitchToThread();
#else
    // Yields processor to another thread
    sched_yield();
#endif
}

/**
 * @brief Spinlock implementation.
 *
 * The spinlock is implemented using atomic operations for maximum performance.
 * It is cache-line aligned to prevent false sharing between different spinlocks.
 */
struct mcp_spinlock_s {
    // The lock state: 0 = unlocked, 1 = locked
    // Aligned to cache line to prevent false sharing
    MCP_CACHE_ALIGNED volatile int lock;
};

/**
 * @brief Creates a new spinlock.
 */
mcp_spinlock_t* mcp_spinlock_create(void) {
    mcp_spinlock_t* spinlock = (mcp_spinlock_t*)sync_alloc(sizeof(mcp_spinlock_t));
    if (!spinlock) {
        mcp_log_error("Failed to allocate memory for spinlock");
        return NULL;
    }

    // Initialize to unlocked state
    spinlock->lock = 0;
    return spinlock;
}

/**
 * @brief Destroys a spinlock.
 */
void mcp_spinlock_destroy(mcp_spinlock_t* spinlock) {
    if (spinlock) {
        sync_free(spinlock);
    }
}

/**
 * @brief Acquires the spinlock with exponential backoff.
 *
 * This implementation uses an exponential backoff strategy to reduce
 * contention and CPU usage when multiple threads are trying to acquire
 * the same spinlock.
 */
int mcp_spinlock_lock(mcp_spinlock_t* spinlock) {
    if (!spinlock) {
        return -1;
    }

    // Start with a small number of iterations before yielding
    unsigned int spin_count = 0;
    const unsigned int YIELD_THRESHOLD = 16;
    const unsigned int MAX_YIELD_COUNT = 16;
    unsigned int yield_count = 0;

    while (1) {
#ifdef _WIN32
        // Try to acquire the lock using InterlockedCompareExchange
        if (InterlockedCompareExchange((volatile LONG*)&spinlock->lock, 1, 0) == 0) {
            // Successfully acquired the lock
            return 0;
        }
#else
        int expected = 0;
        // Try to acquire the lock using atomic compare-and-swap
        if (atomic_compare_exchange_strong((atomic_int*)&spinlock->lock, &expected, 1)) {
            // Successfully acquired the lock
            return 0;
        }
#endif

        // Didn't get the lock, implement backoff strategy
        if (spin_count < YIELD_THRESHOLD) {
            // Short spin with pause instruction to reduce CPU consumption
            for (unsigned int i = 0; i < (1u << spin_count); i++) {
#ifdef _WIN32
                // Pause instruction for better performance in spin loops
                _mm_pause();
#else
                // Equivalent to pause on x86, or compiler barrier on other architectures
                __asm__ __volatile__("pause" ::: "memory");
#endif
            }
            spin_count++;
        } else {
            // After spinning for a while, yield to other threads
            mcp_thread_yield();
            yield_count++;

            // If we've yielded too many times, reset the spin count to
            // avoid getting stuck in a long yield cycle
            if (yield_count > MAX_YIELD_COUNT) {
                spin_count = 0;
                yield_count = 0;
            }
        }
    }
}

/**
 * @brief Tries to acquire the spinlock without spinning.
 */
int mcp_spinlock_trylock(mcp_spinlock_t* spinlock) {
    if (!spinlock) {
        return -1;
    }

#ifdef _WIN32
    // Try to acquire the lock using InterlockedCompareExchange
    if (InterlockedCompareExchange((volatile LONG*)&spinlock->lock, 1, 0) == 0) {
        // Successfully acquired the lock
        return 0;
    }
#else
    int expected = 0;
    // Try to acquire the lock using atomic compare-and-swap
    if (atomic_compare_exchange_strong((atomic_int*)&spinlock->lock, &expected, 1)) {
        // Successfully acquired the lock
        return 0;
    }
#endif

    // Lock was already held
    return 1;
}

/**
 * @brief Releases the spinlock.
 */
int mcp_spinlock_unlock(mcp_spinlock_t* spinlock) {
    if (!spinlock) {
        return -1;
    }

#ifdef _WIN32
    // Memory barrier to ensure all writes are visible before releasing the lock
    MemoryBarrier();
    // Release the lock
    spinlock->lock = 0;
#else
    // Release the lock with a memory barrier
    atomic_store_explicit((atomic_int*)&spinlock->lock, 0, memory_order_release);
#endif

    return 0;
}

/**
 * @brief Thread local storage key implementation.
 *
 * This structure is cache-line aligned to prevent false sharing.
 */
struct mcp_tls_key_s {
#ifdef _WIN32
    MCP_CACHE_ALIGNED DWORD tls_index;
    void (*destructor)(void*);
#else
    MCP_CACHE_ALIGNED pthread_key_t key;
    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE - sizeof(pthread_key_t)];
#endif
};

/**
 * @brief Creates a new thread local storage key.
 */
mcp_tls_key_t* mcp_tls_key_create(void (*destructor)(void*)) {
    mcp_tls_key_t* key = (mcp_tls_key_t*)sync_alloc(sizeof(mcp_tls_key_t));
    if (!key) {
        mcp_log_error("Failed to allocate memory for TLS key");
        return NULL;
    }

#ifdef _WIN32
    // On Windows, we need to manually handle destructors
    key->tls_index = TlsAlloc();
    if (key->tls_index == TLS_OUT_OF_INDEXES) {
        mcp_log_error("Failed to allocate TLS index");
        sync_free(key);
        return NULL;
    }
    key->destructor = destructor;
#else
    // On POSIX, pthread_key_create handles destructors
    if (pthread_key_create(&key->key, destructor) != 0) {
        mcp_log_error("Failed to create pthread TLS key");
        sync_free(key);
        return NULL;
    }
#endif

    return key;
}

/**
 * @brief Destroys a thread local storage key.
 */
void mcp_tls_key_destroy(mcp_tls_key_t* key) {
    if (!key) {
        return;
    }

#ifdef _WIN32
    TlsFree(key->tls_index);
#else
    pthread_key_delete(key->key);
#endif

    sync_free(key);
}

/**
 * @brief Sets the thread-specific data associated with a key.
 */
int mcp_tls_set(mcp_tls_key_t* key, void* value) {
    if (!key) {
        return -1;
    }

#ifdef _WIN32
    if (!TlsSetValue(key->tls_index, value)) {
        return -1;
    }
#else
    if (pthread_setspecific(key->key, value) != 0) {
        return -1;
    }
#endif

    return 0;
}

/**
 * @brief Gets the thread-specific data associated with a key.
 */
void* mcp_tls_get(mcp_tls_key_t* key) {
    if (!key) {
        return NULL;
    }

#ifdef _WIN32
    return TlsGetValue(key->tls_index);
#else
    return pthread_getspecific(key->key);
#endif
}
