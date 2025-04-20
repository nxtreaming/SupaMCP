#include "mcp_thread_pool.h"
#include "mcp_profiler.h"
#include "mcp_sync.h"
#include "mcp_rwlock.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include "mcp_types.h"
#include "mcp_log.h"

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
// GCC/Clang atomics are built-in
#include <sched.h> // For sched_yield
#endif

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4324) // Disable padding warning
#endif

/**
 * @brief Lock-free work-stealing deque structure (Chase-Lev style inspired).
 * Simplified: Assumes single producer (owner thread pushes/pops bottom), multiple consumers (thieves steal top).
 */
typedef struct {
    MCP_CACHE_ALIGNED volatile size_t top;    /**< Index for stealing (incremented by thieves). */
    MCP_CACHE_ALIGNED volatile size_t bottom; /**< Index for adding/removing by owner (incremented/decremented by owner). */
    // Capacity must be power of 2
    size_t capacity_mask;                     /**< Mask for circular buffer indexing (capacity - 1). */
    MCP_CACHE_ALIGNED mcp_task_t* buffer;     /**< Circular buffer for tasks. */
    // Padding might be needed between top/bottom/buffer depending on layout and access patterns
} work_stealing_deque_t;

#ifdef _MSC_VER
#   pragma warning(pop) // Restore warning settings
#endif

/**
 * @brief Internal structure for the thread pool using work-stealing deques.
 */
struct mcp_thread_pool {
    mcp_rwlock_t* rwlock;       /**< Read-write lock for thread pool state. */
    mcp_mutex_t* cond_mutex;    /**< Mutex for condition variable (cannot use rwlock with condition variables). */
    mcp_cond_t* notify;         /**< Condition variable to signal waiting threads (mainly for shutdown). */
    mcp_thread_t* threads;      /**< Array of worker thread handles. */
    work_stealing_deque_t* deques; /**< Array of work-stealing deques, one per thread. */
    size_t thread_count;        /**< Number of worker threads. */
    volatile int shutdown;      /**< Flag indicating if the pool is shutting down (0=no, 1=immediate, 2=graceful). */
    int started;                /**< Number of threads successfully started. */
    size_t deque_capacity;      /**< Capacity of each individual deque. */
    volatile size_t next_submit_deque; /**< Index for round-robin task submission. */
};

// Forward declaration for the worker thread function
static void* thread_pool_worker(void* arg);

// Atomic Compare-and-Swap for size_t
static inline bool compare_and_swap_size(volatile size_t* ptr, size_t expected, size_t desired) {
#ifdef _WIN32
    return InterlockedCompareExchangePointer((volatile PVOID*)ptr, (PVOID)desired, (PVOID)expected) == (PVOID)expected;
#else // GCC/Clang
    return __sync_bool_compare_and_swap(ptr, expected, desired);
#endif
}

// Atomic Load for size_t
static inline size_t load_size(volatile size_t* ptr) {
#ifdef _WIN32
    size_t value = *ptr;
    _ReadWriteBarrier();
    return value;
#else // GCC/Clang
    return __sync_fetch_and_add(ptr, 0);
#endif
}

// Atomic Load for int
static inline int load_int(volatile int* ptr) {
#ifdef _WIN32
    int value = *ptr;
    _ReadWriteBarrier();
    return value;
#else // GCC/Clang
    return __sync_fetch_and_add(ptr, 0);
#endif
}

// Atomic Store for int
static inline void store_int(volatile int* ptr, int value) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG*)ptr, (LONG)value);
#else // GCC/Clang
    __sync_lock_test_and_set(ptr, value);
#endif
}

// Atomic Fetch-and-Add for size_t
static inline size_t fetch_add_size(volatile size_t* ptr, size_t value) {
#ifdef _WIN32
    // Use appropriate Windows atomic function based on pointer size
#if defined(_WIN64)
        // 64-bit Windows
    return (size_t)InterlockedExchangeAdd64((volatile LONGLONG*)ptr, (LONGLONG)value);
#   else
        // 32-bit Windows
    return (size_t)InterlockedExchangeAdd((volatile LONG*)ptr, (LONG)value);
#   endif
#else // GCC/Clang
    return __sync_fetch_and_add(ptr, value);
#endif
}

// Argument struct for worker threads
typedef struct {
    mcp_thread_pool_t* pool;
    size_t worker_index;
} worker_arg_t;

// Push task onto the bottom of the deque (owner thread only)
static bool deque_push_bottom(work_stealing_deque_t* deque, mcp_task_t task) {
    size_t b = load_size(&deque->bottom);
    // Do not need to load top for push, only check against capacity later if needed
    // size_t t = load_size(&deque->top);
    // size_t size = b - t;
    // if (size >= (deque->capacity_mask + 1)) { return false; } // Check happens implicitly below

    size_t index = b & deque->capacity_mask;
    deque->buffer[index] = task;

    // Ensure buffer write is visible before bottom increment (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    // Increment bottom (owner only, volatile write is sufficient)
    deque->bottom = b + 1;
    return true;
}

// Pop task from the bottom of the deque (owner thread only)
static bool deque_pop_bottom(work_stealing_deque_t* deque, mcp_task_t* task) {
    size_t b = load_size(&deque->bottom);
    if (b == 0) return false; // Nothing to pop if bottom is 0
    b = b - 1;
    deque->bottom = b; // Volatile write

    // Ensure bottom write is visible before reading top (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    size_t t = load_size(&deque->top);
    long size = (long)b - (long)t; // Use signed difference

    if (size < 0) {
        // Deque was empty or became empty due to concurrent steal
        deque->bottom = t; // Reset bottom to match top
        return false;
    }

    // Get task from bottom
    size_t index = b & deque->capacity_mask;
    *task = deque->buffer[index];

    if (size == 0) {
        // Last item case: Race with thieves stealing top
        if (!compare_and_swap_size(&deque->top, t, t + 1)) {
            // Thief stole the item first
            deque->bottom = t + 1; // Acknowledge thief won
            return false; // Failed to pop
        }
        // Successfully took the last item
        deque->bottom = t + 1; // Reset bottom
    }
    // If size > 0, we successfully popped without contention on the last item

    return true;
}

// Steal task from the top of the deque (thief threads only)
static bool deque_steal_top(work_stealing_deque_t* deque, mcp_task_t* task) {
    size_t t = load_size(&deque->top);

    // Ensure top read happens before reading bottom (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    size_t b = load_size(&deque->bottom);

    if ((long)t >= (long)b) { // Use signed comparison
        // Deque appears empty
        return false;
    }

    // Get task from top
    size_t index = t & deque->capacity_mask;
    *task = deque->buffer[index]; // Read task data first

    // Ensure task read happens before CAS (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    // Attempt to increment top using CAS
    if (compare_and_swap_size(&deque->top, t, t + 1)) {
        // Successfully stole the item
        return true;
    } else {
        // Another thief or the owner modified top/bottom concurrently
        return false;
    }
}

/**
 * @brief Creates a new thread pool using work-stealing deques.
 */
mcp_thread_pool_t* mcp_thread_pool_create(size_t thread_count, size_t queue_size) {
    if (thread_count == 0 || queue_size == 0) {
        mcp_log_error("Thread pool creation failed: thread_count and queue_size must be > 0");
        return NULL;
    }

    mcp_thread_pool_t* pool = (mcp_thread_pool_t*)malloc(sizeof(mcp_thread_pool_t));
    if (pool == NULL) {
        mcp_log_error("Failed to allocate memory for thread pool");
        return NULL;
    }

    // Initialize pool structure
    pool->thread_count = 0; // Set later after threads start
    pool->shutdown = 0;
    pool->started = 0;
    pool->rwlock = NULL;
    pool->cond_mutex = NULL;
    pool->notify = NULL;
    pool->threads = NULL;
    pool->deques = NULL;
    pool->next_submit_deque = 0;

    // Adjust queue capacity to the next power of 2
    size_t adjusted_capacity = 1;
    while (adjusted_capacity < queue_size) {
        adjusted_capacity <<= 1;
    }
    pool->deque_capacity = adjusted_capacity;
    mcp_log_info("Thread pool deque capacity set to %zu (power of 2)", adjusted_capacity);

    // Allocate memory for threads, deques array, and sync primitives
    pool->threads = (mcp_thread_t*)malloc(sizeof(mcp_thread_t) * thread_count);
    pool->deques = (work_stealing_deque_t*)malloc(sizeof(work_stealing_deque_t) * thread_count);
    pool->rwlock = mcp_rwlock_create();
    pool->cond_mutex = mcp_mutex_create();
    pool->notify = mcp_cond_create();

    // Allocate buffers for each deque
    bool allocation_failed = false;
    if (pool->deques != NULL) {
        for (size_t i = 0; i < thread_count; ++i) {
            // Use aligned allocation if possible
#ifdef  _WIN32
            pool->deques[i].buffer = (mcp_task_t*)_aligned_malloc(sizeof(mcp_task_t) * pool->deque_capacity, CACHE_LINE_SIZE);
#elif defined(__GNUC__) || defined(__clang__)
            if (posix_memalign((void**)&pool->deques[i].buffer, CACHE_LINE_SIZE, sizeof(mcp_task_t) * pool->deque_capacity) != 0) {
                pool->deques[i].buffer = NULL;
            }
#else
            pool->deques[i].buffer = (mcp_task_t*)malloc(sizeof(mcp_task_t) * pool->deque_capacity);
#endif
            if (pool->deques[i].buffer == NULL) {
                allocation_failed = true;
                for (size_t j = 0; j < i; ++j) { // Free previously allocated buffers
                    #ifdef _WIN32
                        _aligned_free(pool->deques[j].buffer);
                    #else
                        free(pool->deques[j].buffer);
                    #endif
                }
                break;
            }
            // Initialize deque state
            pool->deques[i].capacity_mask = pool->deque_capacity - 1;
            pool->deques[i].top = 0;
            pool->deques[i].bottom = 0;
        }
    } else {
        allocation_failed = true;
    }

    if (pool->rwlock == NULL || pool->cond_mutex == NULL || pool->notify == NULL || pool->threads == NULL || allocation_failed) {
        mcp_log_error("Thread pool creation failed: Failed to initialize sync primitives or allocate memory for deques.");
        if (pool->threads) free(pool->threads);
        if (pool->deques) {
             for (size_t i = 0; i < thread_count; ++i) {
                 if (pool->deques[i].buffer) {
                    #ifdef _WIN32
                        _aligned_free(pool->deques[i].buffer);
                    #else
                        free(pool->deques[i].buffer);
                    #endif
                 }
             }
             free(pool->deques);
        }
        mcp_rwlock_free(pool->rwlock);
        mcp_mutex_destroy(pool->cond_mutex);
        mcp_cond_destroy(pool->notify);
        free(pool);
        return NULL;
    }

    // Start worker threads, passing index
    allocation_failed = false;
    for (size_t i = 0; i < thread_count; ++i) {
        worker_arg_t* arg = (worker_arg_t*)malloc(sizeof(worker_arg_t));
        if (!arg) {
             mcp_log_error("Failed to allocate memory for worker arg");
             allocation_failed = true;
             break;
        }
        arg->pool = pool;
        arg->worker_index = i;

        if (mcp_thread_create(&(pool->threads[i]), thread_pool_worker, (void*)arg) != 0) {
            mcp_log_error("Failed to create worker thread");
            free(arg);
            allocation_failed = true;
            break;
        }
        pool->thread_count++;
        pool->started++;
    }

    // If any allocation or thread creation failed after partial success
    if (allocation_failed) {
         store_int(&pool->shutdown, 1);
         if (mcp_mutex_lock(pool->cond_mutex) == 0) {
             mcp_cond_broadcast(pool->notify);
             mcp_mutex_unlock(pool->cond_mutex);
         }
         for (size_t i = 0; i < pool->started; ++i) {
             mcp_thread_join(pool->threads[i], NULL);
             // TODO: Free worker_arg_t for joined threads
         }
         // Full cleanup
         if (pool->threads) free(pool->threads);
         if (pool->deques) {
             for (size_t i = 0; i < thread_count; ++i) {
                 if (pool->deques[i].buffer) {
#ifdef _WIN32
                     _aligned_free(pool->deques[i].buffer);
#else
                     free(pool->deques[i].buffer);
#endif
                 }
             }
             free(pool->deques);
         }
         mcp_rwlock_free(pool->rwlock);
         mcp_mutex_destroy(pool->cond_mutex);
         mcp_cond_destroy(pool->notify);
         free(pool);
         return NULL;
    }

    return pool;
}

/**
 * @brief Adds a new task to the thread pool (distributes to a deque).
 */
int mcp_thread_pool_add_task(mcp_thread_pool_t* pool, void (*function)(void*), void* argument) {
    if (pool == NULL || function == NULL) {
        return -1;
    }
    PROFILE_START("mcp_thread_pool_add_task");

    // Use read lock to check shutdown state - allows multiple threads to check concurrently
    mcp_rwlock_read_lock(pool->rwlock);
    int shutdown_state = pool->shutdown;
    mcp_rwlock_read_unlock(pool->rwlock);

    if (shutdown_state != 0) {
        PROFILE_END("mcp_thread_pool_add_task");
        return -1;
    }

    mcp_task_t task = { .function = function, .argument = argument };

    // Simple round-robin submission for now
    // Note: This part is NOT thread-safe if multiple producers call add_task!
    size_t target_deque_idx = fetch_add_size(&pool->next_submit_deque, 1) % pool->thread_count;
    work_stealing_deque_t* target_deque = &pool->deques[target_deque_idx];

    // Push task onto the bottom of the target deque
    if (!deque_push_bottom(target_deque, task)) {
        // Deque is full - Try submitting to the next deque? Or fail?
        // For simplicity, fail for now.
        mcp_log_error("Warning: Target deque %zu is full during task submission.", target_deque_idx);
        PROFILE_END("mcp_thread_pool_add_task");
        return -1; // Indicate queue full
    }

    // Optional: Signal a potentially idle worker?
    // If using the hybrid wait approach in workers, signal here.
    // if (mcp_mutex_lock(pool->lock) == 0) {
    //     mcp_cond_signal(pool->notify);
    //     mcp_mutex_unlock(pool->lock);
    // }

    PROFILE_END("mcp_thread_pool_add_task");
    return 0;
}

/**
 * @brief Waits for all currently queued tasks to complete.
 *
 * This function blocks until all tasks in the queue at the time of the call
 * have been processed, or until the specified timeout is reached.
 *
 * @param pool The thread pool instance.
 * @param timeout_ms Maximum time to wait in milliseconds. Use 0 for no timeout.
 * @return 0 on success, -1 on failure or timeout.
 */
int mcp_thread_pool_wait(mcp_thread_pool_t* pool, unsigned int timeout_ms) {
    if (pool == NULL) {
        return -1;
    }

    // Use read lock to check shutdown state - allows multiple threads to check concurrently
    mcp_rwlock_read_lock(pool->rwlock);
    int shutdown_state = pool->shutdown;
    mcp_rwlock_read_unlock(pool->rwlock);

    if (shutdown_state != 0) {
        return -1; // Pool is shutting down
    }

    // Check if all deques are empty
    bool all_empty = true;
    unsigned int wait_time = 0;
    unsigned int sleep_interval = 10; // 10ms sleep interval

    while (wait_time < timeout_ms || timeout_ms == 0) {
        all_empty = true;

        // Check all deques
        for (size_t i = 0; i < pool->thread_count; i++) {
            size_t bottom = load_size(&pool->deques[i].bottom);
            size_t top = load_size(&pool->deques[i].top);

            if (bottom > top) {
                all_empty = false;
                break;
            }
        }

        if (all_empty) {
            return 0; // All tasks completed
        }

        // Sleep for a short interval
#ifdef _WIN32
        Sleep(sleep_interval);
#else
        struct timespec ts;
        ts.tv_sec = sleep_interval / 1000;
        ts.tv_nsec = (sleep_interval % 1000) * 1000000;
        nanosleep(&ts, NULL);
#endif

        wait_time += sleep_interval;
    }

    return -1; // Timeout
}

/**
 * @brief Destroys the thread pool using work-stealing deques.
 */
int mcp_thread_pool_destroy(mcp_thread_pool_t* pool) {
    if (pool == NULL) {
        return -1;
    }

    int err = 0;
    // Use read lock to check current shutdown state
    mcp_rwlock_read_lock(pool->rwlock);
    int current_shutdown_state = pool->shutdown;
    mcp_rwlock_read_unlock(pool->rwlock);

    if (current_shutdown_state != 0) {
        return -1; // Already shutting down
    }

    // Use write lock to set shutdown state - ensures exclusive access
    mcp_rwlock_write_lock(pool->rwlock);

    // Double-check shutdown state after acquiring lock
    if (pool->shutdown != 0) {
        mcp_rwlock_write_unlock(pool->rwlock);
        return -1; // Another thread initiated shutdown
    }

    // Set shutdown state to graceful shutdown (2)
    pool->shutdown = 2;
    mcp_rwlock_write_unlock(pool->rwlock);

    // Wake up worker threads
    if (mcp_mutex_lock(pool->cond_mutex) == 0) {
        if (mcp_cond_broadcast(pool->notify) != 0) {
            err = -1;
            mcp_log_warn("Warning: Failed to broadcast shutdown signal");
        }
        mcp_mutex_unlock(pool->cond_mutex);
    } else {
        err = -1;
        mcp_log_error("Error: Failed to lock mutex for shutdown broadcast.");
    }

    // Join threads
    for (size_t i = 0; i < pool->started; ++i) {
        if (mcp_thread_join(pool->threads[i], NULL) != 0) {
            mcp_log_error("Failed to join thread");
            err = -1;
        }
        // TODO: Free worker_arg_t associated with this thread
    }

    // Cleanup
    mcp_rwlock_free(pool->rwlock);
    mcp_mutex_destroy(pool->cond_mutex);
    mcp_cond_destroy(pool->notify);
    if (pool->threads) free(pool->threads);
    if (pool->deques) {
         for (size_t i = 0; i < pool->thread_count; ++i) {
              if (pool->deques[i].buffer) {
#ifdef _WIN32
                  _aligned_free(pool->deques[i].buffer);
#else
                  free(pool->deques[i].buffer);
#endif
              }
         }
         free(pool->deques);
    }
    free(pool);

    return err;
}

/**
 * @brief The worker thread function using work-stealing deques.
 */
static void* thread_pool_worker(void* arg) {
    worker_arg_t* worker_data = (worker_arg_t*)arg;
    mcp_thread_pool_t* pool = worker_data->pool;
    size_t my_index = worker_data->worker_index;
    work_stealing_deque_t* my_deque = &pool->deques[my_index];
    mcp_task_t task;
    unsigned int steal_attempts = 0; // Counter for steal attempts

    // Free the argument struct now that we've copied the data
    free(arg);

    while (1) {
        // 1. Try to pop from own deque
        if (deque_pop_bottom(my_deque, &task)) {
            steal_attempts = 0;
            PROFILE_START("thread_pool_task_execution");
            (*(task.function))(task.argument);
            PROFILE_END("thread_pool_task_execution");
            continue;
        }

        // 2. Own deque is empty, check for shutdown
        // Use read lock to check shutdown state - allows multiple threads to check concurrently
        mcp_rwlock_read_lock(pool->rwlock);
        int shutdown_status = pool->shutdown;
        mcp_rwlock_read_unlock(pool->rwlock);

        if (shutdown_status != 0) {
            // Simplified exit: If shutdown signaled and own deque empty, exit.
            // A more robust graceful shutdown would check if *all* deques are empty.
           return NULL;
        }

        // 3. Try to steal from another random deque
        if (pool->thread_count > 1) {
            // Simple random victim selection
            size_t victim_index = rand() % pool->thread_count;
            if (victim_index == my_index) {
                victim_index = (victim_index + 1) % pool->thread_count;
            }
            work_stealing_deque_t* victim_deque = &pool->deques[victim_index];

            if (deque_steal_top(victim_deque, &task)) {
                steal_attempts = 0;
                PROFILE_START("thread_pool_task_execution_steal");
                (*(task.function))(task.argument);
                PROFILE_END("thread_pool_task_execution_steal");
                continue;
            }
        }

        // 4. Failed to pop and failed to steal
        steal_attempts++;

        // Backoff strategy
        if (steal_attempts < 5) {
             mcp_thread_yield();
        } else {
             // Longer wait using mutex/condvar for shutdown signal
             if (mcp_mutex_lock(pool->cond_mutex) == 0) {
                 // Use read lock to check shutdown state - allows multiple threads to check concurrently
                 mcp_rwlock_read_lock(pool->rwlock);
                 int current_shutdown = pool->shutdown;
                 mcp_rwlock_read_unlock(pool->rwlock);

                 if (current_shutdown != 0) {
                     mcp_mutex_unlock(pool->cond_mutex);
                     return NULL;
                 }
                 mcp_cond_timedwait(pool->notify, pool->cond_mutex, 100); // Wait 100ms
                 mcp_mutex_unlock(pool->cond_mutex);
             }
             steal_attempts = 0;
        }

    } // End while(1)

    return NULL;
}
