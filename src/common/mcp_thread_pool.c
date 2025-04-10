#include "mcp_thread_pool.h"
#include "mcp_profiler.h"
#include "mcp_sync.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include "mcp_types.h"

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#   ifdef _MSC_VER
    #pragma warning( disable : 4324 )
#   endif
#else
// GCC/Clang atomics are built-in
#endif

/**
 * @brief Bounded queue structure using platform atomics with cache line padding.
 */
typedef struct {
    mcp_task_t* buffer;     /**< The underlying circular buffer. */
    size_t capacity;        /**< Capacity of the buffer (must be power of 2 for simple masking). */

    // Align head to cache line and add padding
    MCP_CACHE_ALIGNED volatile size_t head;   /**< Index for the next dequeue operation. */
    char head_padding[CACHE_LINE_SIZE - sizeof(volatile size_t)]; /**< Pad to prevent false sharing with tail. */

    // Align tail to cache line and add padding
    MCP_CACHE_ALIGNED volatile size_t tail;   /**< Index for the next enqueue operation. */
    char tail_padding[CACHE_LINE_SIZE - sizeof(volatile size_t)]; /**< Pad to prevent false sharing with other data. */

} bounded_queue_t;

/**
 * @brief Internal structure for the thread pool using platform atomics.
 */
struct mcp_thread_pool {
    mcp_mutex_t* lock;          /**< Mutex primarily for shutdown signaling and thread management. */
    mcp_cond_t* notify;         /**< Condition variable to signal waiting threads (mainly for shutdown). */
    mcp_thread_t* threads;      /**< Array of worker thread handles. */
    bounded_queue_t queue;      /**< Task queue accessed with platform atomics. */
    size_t thread_count;        /**< Number of worker threads. */
    volatile int shutdown;      /**< Flag indicating if the pool is shutting down (0=no, 1=immediate, 2=graceful). */
    int started;                /**< Number of threads successfully started. */
};

// --- Platform Atomic Wrappers (Simplified) ---

// Atomic Compare-and-Swap for size_t
static inline bool compare_and_swap_size(volatile size_t* ptr, size_t expected, size_t desired) {
#ifdef _WIN32
    // Use InterlockedCompareExchangePointer for pointer-sized types (like size_t)
    // It requires PVOID*, so we cast.
    return InterlockedCompareExchangePointer((volatile PVOID*)ptr, (PVOID)desired, (PVOID)expected) == (PVOID)expected;
#else // GCC/Clang
    return __sync_bool_compare_and_swap(ptr, expected, desired);
#endif
}

// Atomic Load for size_t
static inline size_t load_size(volatile size_t* ptr) {
#ifdef _WIN32
    // Simple read might suffice with volatile, but use Interlocked for safety/fencing if needed.
    // For this queue, volatile read is likely okay, paired with CAS release/acquire.
    // Alternatively, use InterlockedCompareExchange with expected == *ptr to get current value atomically.
    size_t value = *ptr;
    _ReadWriteBarrier(); // Prevent compiler reordering around volatile read
    return value;
#else // GCC/Clang
    return __sync_fetch_and_add(ptr, 0); // Atomic load
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
    // InterlockedExchange works for LONG (32-bit int)
    InterlockedExchange((volatile LONG*)ptr, (LONG)value);
#else // GCC/Clang
    __sync_lock_test_and_set(ptr, value); // Provides release semantics
#endif
}


// Forward declaration for the worker thread function
static void* thread_pool_worker(void* arg);

/**
 * @brief Creates a new thread pool using platform atomics.
 */
mcp_thread_pool_t* mcp_thread_pool_create(size_t thread_count, size_t queue_size) {
    if (thread_count == 0 || queue_size == 0) {
        fprintf(stderr, "Thread pool creation failed: thread_count and queue_size must be > 0.\n");
        return NULL;
    }

    mcp_thread_pool_t* pool = (mcp_thread_pool_t*)malloc(sizeof(mcp_thread_pool_t));
    if (pool == NULL) {
        perror("Failed to allocate memory for thread pool");
        return NULL;
    }

    // Initialize pool structure
    pool->thread_count = 0;
    pool->shutdown = 0; // Initialize volatile int
    pool->started = 0;
    pool->lock = NULL;
    pool->notify = NULL;
    pool->threads = NULL;
    pool->queue.buffer = NULL;

    // Adjust queue capacity to the next power of 2
    size_t adjusted_capacity = 1;
    while (adjusted_capacity < queue_size) {
        adjusted_capacity <<= 1;
    }
    pool->queue.capacity = adjusted_capacity;
    fprintf(stdout, "Thread pool queue capacity adjusted to %zu (power of 2)\n", adjusted_capacity);

    // Allocate memory
    pool->threads = (mcp_thread_t*)malloc(sizeof(mcp_thread_t) * thread_count);
    pool->queue.buffer = (mcp_task_t*)malloc(sizeof(mcp_task_t) * pool->queue.capacity);
    pool->lock = mcp_mutex_create();
    pool->notify = mcp_cond_create();

    if (pool->lock == NULL || pool->notify == NULL ||
        pool->threads == NULL || pool->queue.buffer == NULL)
    {
        fprintf(stderr, "Thread pool creation failed: Failed to initialize sync primitives or allocate memory.\n");
        if (pool->threads) free(pool->threads);
        if (pool->queue.buffer) free(pool->queue.buffer);
        mcp_mutex_destroy(pool->lock);
        mcp_cond_destroy(pool->notify);
        free(pool);
        return NULL;
    }

    // Initialize queue indices
    pool->queue.head = 0;
    pool->queue.tail = 0;

    // Start worker threads
    for (size_t i = 0; i < thread_count; ++i) {
        if (mcp_thread_create(&(pool->threads[i]), thread_pool_worker, (void*)pool) != 0) {
            perror("Failed to create worker thread");
            store_int(&pool->shutdown, 1); // Immediate shutdown using atomic store
            mcp_thread_pool_destroy(pool);
            return NULL;
        }
        pool->thread_count++;
        pool->started++;
    }

    return pool;
}

/**
 * @brief Adds a new task to the thread pool's queue using platform atomics.
 */
int mcp_thread_pool_add_task(mcp_thread_pool_t* pool, void (*function)(void*), void* argument) {
    if (pool == NULL || function == NULL) {
        return -1;
    }
    PROFILE_START("mcp_thread_pool_add_task");

    if (load_int(&pool->shutdown) != 0) {
        PROFILE_END("mcp_thread_pool_add_task");
        return -1;
    }

    size_t current_tail;
    size_t new_tail;

    do {
        current_tail = load_size(&pool->queue.tail);
        size_t current_head = load_size(&pool->queue.head);

        if (current_tail - current_head >= pool->queue.capacity) {
            fprintf(stderr, "Warning: Platform atomic thread pool queue is full.\n");
            PROFILE_END("mcp_thread_pool_add_task");
            return -1; // Queue full
        }

        new_tail = current_tail + 1;
    } while (!compare_and_swap_size(&pool->queue.tail, current_tail, new_tail));

    size_t index = current_tail & (pool->queue.capacity - 1);
    pool->queue.buffer[index].function = function;
    pool->queue.buffer[index].argument = argument;

    // Memory barrier might be needed here if CAS doesn't provide release semantics
    // On x86/x64, CAS usually implies a full barrier. On ARM, might need explicit fence.
    // The platform wrappers should ideally handle necessary barriers.

    PROFILE_END("mcp_thread_pool_add_task");
    return 0;
}

/**
 * @brief Destroys the thread pool using platform atomics.
 */
int mcp_thread_pool_destroy(mcp_thread_pool_t* pool) {
    if (pool == NULL) {
        return -1;
    }

    int err = 0;
    int current_shutdown_state = load_int(&pool->shutdown);

    if (current_shutdown_state != 0) {
        return -1; // Already shutting down
    }

    // Initiate graceful shutdown atomically
#ifdef _WIN32
    if (InterlockedCompareExchange((volatile LONG*)&pool->shutdown, 2, 0) != 0) {
        return -1; // Another thread initiated shutdown
    }
#else
    if (!__sync_bool_compare_and_swap(&pool->shutdown, 0, 2)) {
        return -1; // Another thread initiated shutdown
    }
#endif

    // Wake up worker threads (using mutex/condvar)
    if (mcp_mutex_lock(pool->lock) == 0) {
        if (mcp_cond_broadcast(pool->notify) != 0) {
            err = -1;
            fprintf(stderr, "Warning: Failed to broadcast shutdown signal.\n");
        }
        mcp_mutex_unlock(pool->lock);
    } else {
        err = -1;
        fprintf(stderr, "Error: Failed to lock mutex for shutdown broadcast.\n");
    }

    // Join threads
    for (size_t i = 0; i < pool->started; ++i) {
        if (mcp_thread_join(pool->threads[i], NULL) != 0) {
            perror("Failed to join thread");
            err = -1;
        }
    }

    // Cleanup
    mcp_mutex_destroy(pool->lock);
    mcp_cond_destroy(pool->notify);
    if (pool->threads) free(pool->threads);
    if (pool->queue.buffer) free(pool->queue.buffer);
    free(pool);

    return err;
}

/**
 * @brief The worker thread function using platform atomics.
 */
static void* thread_pool_worker(void* arg) {
    mcp_thread_pool_t* pool = (mcp_thread_pool_t*)arg;
    mcp_task_t task = { .function = NULL, .argument = NULL }; // Initialize task
    size_t current_head;
    size_t new_head;

    while (1) {
        // Attempt lock-free dequeue
        do {
            current_head = load_size(&pool->queue.head);
            size_t current_tail = load_size(&pool->queue.tail);

            if (current_head == current_tail) {
                // Queue is empty
                task.function = NULL;
                int shutdown_status = load_int(&pool->shutdown);
                if (shutdown_status != 0) {
                    return NULL; // Exit thread if shutting down and queue empty
                }
                break; // Break inner loop to yield/wait
            }

            new_head = current_head + 1;
        } while (!compare_and_swap_size(&pool->queue.head, current_head, new_head));

        // Check if we actually dequeued something (tail might have caught up)
        if (load_size(&pool->queue.tail) != current_head) { // Check if tail moved past our claimed head
             size_t index = current_head & (pool->queue.capacity - 1);
             // Read task data - Need appropriate memory barrier if CAS doesn't provide acquire semantics
             task = pool->queue.buffer[index];
        } else {
             // Tail caught up, effectively empty
             task.function = NULL;
        }


        if (task.function != NULL) {
            PROFILE_START("thread_pool_task_execution");
            (*(task.function))(task.argument);
            PROFILE_END("thread_pool_task_execution");
        } else {
            // Queue was empty or became empty
            int shutdown_status = load_int(&pool->shutdown);
            if (shutdown_status != 0) {
                 // Double check queue empty for graceful shutdown
                 if (load_size(&pool->queue.head) == load_size(&pool->queue.tail)) {
                     return NULL; // Exit thread
                 }
            } else {
                 // Queue empty, not shutting down -> yield
                 mcp_thread_yield();
                 // Could add a short sleep or use the mutex/condvar for longer waits here
            }
        }
    } // End while(1)

    return NULL;
}
