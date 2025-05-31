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
#include "mcp_sys_utils.h"

#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <sched.h>
#endif

// Constants for thread pool management
#define MIN_THREAD_COUNT 2  // Minimum number of threads to maintain

// Smart adjustment constants
#define HIGH_LOAD_THRESHOLD 0.8     // 80% utilization considered high load
#define LOW_LOAD_THRESHOLD 0.2      // 20% utilization considered low load
#define QUEUE_PRESSURE_THRESHOLD 0.5 // 50% queue full considered pressure
#define ADJUSTMENT_COOLDOWN_MS 10000  // 10 seconds between adjustments

// System load monitoring structure
typedef struct {
    double cpu_usage_percent;      // Current CPU usage (0.0 - 100.0)
    size_t available_memory_mb;    // Available memory in MB
    uint64_t last_update_time;     // Last time metrics were updated
    bool metrics_valid;            // Whether metrics are valid
} system_load_metrics_t;

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4324) // Disable padding warning
#endif

/**
 * @brief Lock-free work-stealing deque structure (Chase-Lev style inspired).
 * Simplified: Assumes single producer (owner thread pushes/pops bottom), multiple consumers (thieves steal top).
 *
 * This structure is carefully designed to avoid false sharing:
 * - top is accessed by multiple thieves (readers) and occasionally by the owner
 * - bottom is primarily accessed by the owner (writer) and occasionally by thieves
 * - Each field is placed on its own cache line to prevent false sharing
 */
typedef struct {
    // Top index - accessed by multiple thieves
    MCP_CACHE_ALIGNED volatile size_t top;    /**< Index for stealing (incremented by thieves). */

    // Padding to ensure top and bottom are on different cache lines
    char pad1[MCP_CACHE_LINE_SIZE - sizeof(size_t)];

    // Bottom index - accessed primarily by owner
    MCP_CACHE_ALIGNED volatile size_t bottom; /**< Index for adding/removing by owner (incremented/decremented by owner). */

    // Padding to separate bottom from other fields
    char pad2[MCP_CACHE_LINE_SIZE - sizeof(size_t)];

    // Capacity must be power of 2
    MCP_CACHE_ALIGNED size_t capacity_mask;   /**< Mask for circular buffer indexing (capacity - 1). */

    // Buffer pointer - aligned to cache line
    MCP_CACHE_ALIGNED mcp_task_t* buffer;     /**< Circular buffer for tasks. */

    // Final padding to ensure no false sharing with adjacent structures
    char pad3[MCP_CACHE_LINE_SIZE - sizeof(size_t) - sizeof(mcp_task_t*)];
} work_stealing_deque_t;

#ifdef _MSC_VER
#   pragma warning(pop) // Restore warning settings
#endif

// Forward declaration for the worker thread function
static void* thread_pool_worker(void* arg);

// Argument struct for worker threads
typedef struct {
    struct mcp_thread_pool* pool;
    size_t worker_index;
    volatile bool should_exit;  // Explicit exit flag for this worker
    volatile bool is_active;    // Whether this worker is currently active
} worker_arg_t;

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4324) // Disable warning for structure padding
#endif
/**
 * @brief Internal structure for the thread pool using work-stealing deques.
 *
 * This structure is carefully designed to avoid false sharing between frequently accessed fields.
 * Fields are grouped by access patterns and separated by cache line padding.
 */
struct mcp_thread_pool {
    // Group 1: Synchronization primitives (rarely modified after initialization)
    mcp_rwlock_t* rwlock;       /**< Read-write lock for thread pool state. */
    mcp_mutex_t* cond_mutex;    /**< Mutex for condition variable (cannot use rwlock with condition variables). */
    mcp_cond_t* notify;         /**< Condition variable to signal waiting threads (mainly for shutdown). */

    // Group 2: Thread management (rarely modified after initialization)
    mcp_thread_t* threads;      /**< Array of worker thread handles. */
    worker_arg_t** worker_args; /**< Array of worker thread arguments for cleanup. */
    size_t thread_count;        /**< Number of worker threads. */
    int started;                /**< Number of threads successfully started. */

    // Group 3: Deque management (rarely modified after initialization)
    work_stealing_deque_t* deques; /**< Array of work-stealing deques, one per thread. */
    size_t deque_capacity;      /**< Capacity of each individual deque. */

    // Group 4: Shutdown flag (occasionally modified, frequently read)
    MCP_CACHE_ALIGNED volatile int shutdown_flag; /**< Flag indicating if the pool is shutting down (0=no, 1=immediate, 2=graceful). */
    char pad1[MCP_CACHE_LINE_SIZE - sizeof(int)]; /**< Padding to separate shutdown_flag from next_submit_deque */

    // Group 5: Task submission counter (frequently modified by submitters)
    MCP_CACHE_ALIGNED volatile size_t next_submit_deque; /**< Index for round-robin task submission. */
    char pad2[MCP_CACHE_LINE_SIZE - sizeof(size_t)]; /**< Padding to separate next_submit_deque from statistics */

    // Group 6: Statistics - each on its own cache line to prevent false sharing
    // These are frequently updated by different threads
    MCP_CACHE_ALIGNED volatile size_t tasks_submitted;  /**< Total number of tasks submitted. */
    char pad3[MCP_CACHE_LINE_SIZE - sizeof(size_t)];

    MCP_CACHE_ALIGNED volatile size_t tasks_completed;  /**< Total number of tasks completed. */
    char pad4[MCP_CACHE_LINE_SIZE - sizeof(size_t)];

    MCP_CACHE_ALIGNED volatile size_t tasks_failed;     /**< Total number of tasks that failed to be submitted. */
    char pad5[MCP_CACHE_LINE_SIZE - sizeof(size_t)];

    MCP_CACHE_ALIGNED volatile size_t active_tasks;     /**< Number of tasks currently being processed. */
    char pad6[MCP_CACHE_LINE_SIZE - sizeof(size_t)];

    // Group 7: Worker state tracking arrays (accessed by different threads)
    MCP_CACHE_ALIGNED volatile int* worker_status;      /**< Status of each worker thread (0=idle, 1=active). */
    MCP_CACHE_ALIGNED volatile size_t* tasks_stolen;    /**< Number of tasks stolen by each worker. */
    MCP_CACHE_ALIGNED volatile size_t* tasks_executed;  /**< Number of tasks executed by each worker. */
};
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

// Atomic Compare-and-Swap for size_t
static inline bool compare_and_swap_size(volatile size_t* ptr, size_t expected, size_t desired) {
#ifdef _WIN32
    return InterlockedCompareExchangePointer((volatile PVOID*)ptr, (PVOID)desired, (PVOID)expected) == (PVOID)expected;
#else
    return __sync_bool_compare_and_swap(ptr, expected, desired);
#endif
}

// Atomic Load for size_t
static inline size_t load_size(volatile size_t* ptr) {
#ifdef _WIN32
    size_t value = *ptr;
    _ReadWriteBarrier();
    return value;
#else
    return __sync_fetch_and_add(ptr, 0);
#endif
}

// Atomic Load for int
static inline int load_int(volatile int* ptr) {
#ifdef _WIN32
    int value = *ptr;
    _ReadWriteBarrier();
    return value;
#else
    return __sync_fetch_and_add(ptr, 0);
#endif
}

// Atomic Store for int
static inline void store_int(volatile int* ptr, int value) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG*)ptr, (LONG)value);
#else
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
#else
    return __sync_fetch_and_add(ptr, value);
#endif
}

// worker_arg_t is already defined above

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
    if (b == 0) return false;
    b = b - 1;
    // Volatile write
    deque->bottom = b;

    // Ensure bottom write is visible before reading top (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    size_t t = load_size(&deque->top);
    // Use signed difference
    long size = (long)b - (long)t;

    if (size < 0) {
        // Deque was empty or became empty due to concurrent steal
        // Reset bottom to match top
        deque->bottom = t;
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
    pool->thread_count = 0;
    pool->shutdown_flag = 0;
    pool->started = 0;
    pool->rwlock = NULL;
    pool->cond_mutex = NULL;
    pool->notify = NULL;
    pool->threads = NULL;
    pool->deques = NULL;
    pool->worker_args = NULL;
    pool->worker_status = NULL;
    pool->tasks_stolen = NULL;
    pool->tasks_executed = NULL;
    pool->next_submit_deque = 0;

    // Initialize statistics
    pool->tasks_submitted = 0;
    pool->tasks_completed = 0;
    pool->tasks_failed = 0;
    pool->active_tasks = 0;

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
    pool->worker_args = (worker_arg_t**)malloc(sizeof(worker_arg_t*) * thread_count);
    pool->worker_status = (volatile int*)calloc(thread_count, sizeof(int));
    pool->tasks_stolen = (volatile size_t*)calloc(thread_count, sizeof(size_t));
    pool->tasks_executed = (volatile size_t*)calloc(thread_count, sizeof(size_t));
    pool->rwlock = mcp_rwlock_create();
    pool->cond_mutex = mcp_mutex_create();
    pool->notify = mcp_cond_create();

    // Allocate buffers for each deque
    bool allocation_failed = false;
    if (pool->deques != NULL) {
        for (size_t i = 0; i < thread_count; ++i) {
            // Use aligned allocation if possible
#ifdef  _WIN32
            pool->deques[i].buffer = (mcp_task_t*)_aligned_malloc(sizeof(mcp_task_t) * pool->deque_capacity, MCP_CACHE_LINE_SIZE);
#elif defined(__GNUC__) || defined(__clang__)
            if (posix_memalign((void**)&pool->deques[i].buffer, MCP_CACHE_LINE_SIZE, sizeof(mcp_task_t) * pool->deque_capacity) != 0) {
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

    if (pool->rwlock == NULL || pool->cond_mutex == NULL || pool->notify == NULL ||
        pool->threads == NULL || pool->worker_args == NULL || pool->worker_status == NULL ||
        pool->tasks_stolen == NULL || pool->tasks_executed == NULL || allocation_failed) {

        mcp_log_error("Thread pool creation failed: Failed to initialize sync primitives or allocate memory.");

        if (pool->threads) free(pool->threads);
        if (pool->worker_args) free(pool->worker_args);
        if (pool->worker_status) free((void*)pool->worker_status);
        if (pool->tasks_stolen) free((void*)pool->tasks_stolen);
        if (pool->tasks_executed) free((void*)pool->tasks_executed);

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
        arg->should_exit = false;  // Initialize exit flag
        arg->is_active = false;    // Initialize active flag

        // Store the worker argument for later cleanup
        pool->worker_args[i] = arg;

        if (mcp_thread_create(&(pool->threads[i]), thread_pool_worker, arg) != 0) {
            mcp_log_error("Failed to create worker thread");
            free(arg);
            pool->worker_args[i] = NULL;
            allocation_failed = true;
            break;
        }
        pool->thread_count++;
        pool->started++;
    }

    // If any allocation or thread creation failed after partial success
    if (allocation_failed) {
         store_int(&pool->shutdown_flag, 1);
         if (mcp_mutex_lock(pool->cond_mutex) == 0) {
             mcp_cond_broadcast(pool->notify);
             mcp_mutex_unlock(pool->cond_mutex);
         }

         // Join and cleanup worker threads with retry mechanism
         for (size_t i = 0; i < pool->started; ++i) {
             int join_attempts = 0;
             const int max_join_attempts = 3;
             bool join_success = false;

             while (join_attempts < max_join_attempts && !join_success) {
                 if (mcp_thread_join(pool->threads[i], NULL) == 0) {
                     join_success = true;
                 } else {
                     mcp_log_warn("Warning: Failed to join thread %zu during cleanup (attempt %d of %d)",
                                 i, join_attempts + 1, max_join_attempts);
                     join_attempts++;

                     // Short sleep before retry
                     #ifdef _WIN32
                     Sleep(100);
                     #else
                     struct timespec ts;
                     ts.tv_sec = 0;
                     ts.tv_nsec = 100000000; // 100ms
                     nanosleep(&ts, NULL);
                     #endif
                 }
             }

             if (!join_success) {
                 mcp_log_error("Error: Failed to join thread %zu during cleanup after %d attempts",
                              i, max_join_attempts);
                 // Continue with cleanup anyway
             }

             // Worker threads free their own arguments when they exit normally
         }

         // Free any worker arguments that weren't passed to threads
         for (size_t i = pool->started; i < thread_count; ++i) {
             if (pool->worker_args[i]) {
                 free(pool->worker_args[i]);
             }
         }

         // Full cleanup - first free regular memory resources
         if (pool->threads) free(pool->threads);
         if (pool->worker_args) free(pool->worker_args);
         if (pool->worker_status) free((void*)pool->worker_status);
         if (pool->tasks_stolen) free((void*)pool->tasks_stolen);
         if (pool->tasks_executed) free((void*)pool->tasks_executed);

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

         // Free synchronization primitives last
         if (pool->rwlock) mcp_rwlock_free(pool->rwlock);
         if (pool->cond_mutex) mcp_mutex_destroy(pool->cond_mutex);
         if (pool->notify) mcp_cond_destroy(pool->notify);
         free(pool);
         return NULL;
    }

    return pool;
}

size_t mcp_get_optimal_thread_count(void) {
    size_t num_cores = 4; // Default fallback

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    num_cores = sysinfo.dwNumberOfProcessors;
#else
    num_cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    // 2 * num_cores + 1 is a good balance for I/O bound workloads
    return (2 * num_cores) + 1;
}

// Get current system load metrics
static int get_system_load_metrics(system_load_metrics_t* metrics) {
    if (!metrics) return -1;

    uint64_t current_time = mcp_get_time_ms();

    // Update metrics every 5 seconds to avoid overhead
    if (metrics->metrics_valid && (current_time - metrics->last_update_time) < 5000) {
        return 0; // Use cached metrics
    }

#ifdef _WIN32
    // Windows implementation
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        metrics->available_memory_mb = (size_t)(mem_status.ullAvailPhys / (1024 * 1024));
    } else {
        metrics->available_memory_mb = 1024; // 1GB fallback
    }

    // Simple CPU usage estimation (not perfect but sufficient)
    static ULARGE_INTEGER last_idle, last_kernel, last_user;
    FILETIME idle_time, kernel_time, user_time;

    if (GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idle_time.dwLowDateTime;
        idle.HighPart = idle_time.dwHighDateTime;
        kernel.LowPart = kernel_time.dwLowDateTime;
        kernel.HighPart = kernel_time.dwHighDateTime;
        user.LowPart = user_time.dwLowDateTime;
        user.HighPart = user_time.dwHighDateTime;

        if (last_idle.QuadPart != 0) {
            ULONGLONG idle_diff = idle.QuadPart - last_idle.QuadPart;
            ULONGLONG kernel_diff = kernel.QuadPart - last_kernel.QuadPart;
            ULONGLONG user_diff = user.QuadPart - last_user.QuadPart;
            ULONGLONG total_diff = kernel_diff + user_diff;

            if (total_diff > 0) {
                metrics->cpu_usage_percent = 100.0 - (100.0 * idle_diff / total_diff);
            } else {
                metrics->cpu_usage_percent = 0.0;
            }
        } else {
            metrics->cpu_usage_percent = 50.0; // Initial estimate
        }

        last_idle = idle;
        last_kernel = kernel;
        last_user = user;
    } else {
        metrics->cpu_usage_percent = 50.0; // Fallback
    }

#else
    // POSIX implementation
    // Get available memory
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        metrics->available_memory_mb = (size_t)((pages * page_size) / (1024 * 1024));
    } else {
        metrics->available_memory_mb = 1024; // 1GB fallback
    }

    // Simple CPU load estimation using load average
    double load_avg[3];
    if (getloadavg(load_avg, 1) != -1) {
        size_t num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (num_cores > 0) {
            // Convert load average to percentage (rough estimate)
            metrics->cpu_usage_percent = (load_avg[0] / num_cores) * 100.0;
            if (metrics->cpu_usage_percent > 100.0) {
                metrics->cpu_usage_percent = 100.0;
            }
        } else {
            metrics->cpu_usage_percent = 50.0;
        }
    } else {
        metrics->cpu_usage_percent = 50.0; // Fallback
    }
#endif

    metrics->last_update_time = current_time;
    metrics->metrics_valid = true;

    return 0;
}

int mcp_thread_pool_resize(mcp_thread_pool_t* pool, size_t new_thread_count) {
    if (!pool || new_thread_count == 0) {
        return -1;
    }

    // Take write lock for thread-safe resizing
    mcp_rwlock_write_lock(pool->rwlock);

    // If new size is same as current, nothing to do
    if (new_thread_count == pool->thread_count) {
        mcp_rwlock_write_unlock(pool->rwlock);
        return 0;
    }

    // Add minimum thread count validation
    if (new_thread_count < MIN_THREAD_COUNT) {
        new_thread_count = MIN_THREAD_COUNT;
    }

    // If shrinking
    if (new_thread_count < pool->thread_count) {
        // First, explicitly signal threads that should exit
        for (size_t i = new_thread_count; i < pool->thread_count; i++) {
            if (pool->worker_args[i] != NULL) {
                pool->worker_args[i]->should_exit = true;
                mcp_log_debug("Signaling worker %zu to exit during pool shrink", i);
            }
        }

        // Set new thread count after signaling exit flags
        pool->thread_count = new_thread_count;

        // Signal all threads to check their exit flags
        mcp_mutex_lock(pool->cond_mutex);
        mcp_cond_broadcast(pool->notify);
        mcp_mutex_unlock(pool->cond_mutex);

        // Worker threads will exit when they see their should_exit flag is true
        mcp_log_debug("Pool shrunk from %zu to %zu threads", pool->thread_count + (pool->thread_count - new_thread_count), new_thread_count);
    }
    // If expanding
    else {
        // Create new worker threads
        size_t threads_to_add = new_thread_count - pool->thread_count;
        for (size_t i = 0; i < threads_to_add; i++) {
            worker_arg_t* arg = (worker_arg_t*)malloc(sizeof(worker_arg_t));
            if (!arg) {
                mcp_log_error("Failed to allocate memory for worker arg");
                pool->thread_count += i; // Update to actual number of threads
                mcp_rwlock_write_unlock(pool->rwlock);
                return -1;
            }
            arg->pool = pool;
            arg->worker_index = pool->thread_count + i;
            arg->should_exit = false;  // Initialize exit flag
            arg->is_active = false;    // Initialize active flag

            // Store the worker argument for cleanup
            pool->worker_args[arg->worker_index] = arg;

            if (mcp_thread_create(&(pool->threads[arg->worker_index]), thread_pool_worker, arg) != 0) {
                mcp_log_error("Failed to create worker thread");
                free(arg);
                pool->worker_args[arg->worker_index] = NULL;
                pool->thread_count += i; // Update to actual number of threads
                mcp_rwlock_write_unlock(pool->rwlock);
                return -1;
            }
        }
        pool->thread_count = new_thread_count;
    }

    mcp_rwlock_write_unlock(pool->rwlock);
    return 0;
}

int mcp_thread_pool_auto_adjust(mcp_thread_pool_t* pool) {
    if (!pool) return -1;

    size_t optimal_threads = mcp_get_optimal_thread_count();
    return mcp_thread_pool_resize(pool, optimal_threads);
}

int mcp_thread_pool_smart_adjust(mcp_thread_pool_t* pool, void* context) {
    (void)context; // Unused parameter, can be used for additional context if needed
    if (!pool) return -1;

    static system_load_metrics_t metrics = {0};
    static uint64_t last_adjustment_time = 0;

    uint64_t current_time = mcp_get_time_ms();

    // Enforce cooldown period between adjustments
    if (last_adjustment_time != 0 &&
        (current_time - last_adjustment_time) < ADJUSTMENT_COOLDOWN_MS) {
        return 0; // Skip adjustment, too soon
    }

    // Get current system load metrics
    if (get_system_load_metrics(&metrics) != 0) {
        mcp_log_warn("Failed to get system load metrics, falling back to basic auto-adjust");
        return mcp_thread_pool_auto_adjust(pool);
    }

    // Get current pool statistics
    size_t submitted, completed, failed, active_tasks;
    if (mcp_thread_pool_get_stats(pool, &submitted, &completed, &failed, &active_tasks) != 0) {
        mcp_log_warn("Failed to get thread pool stats, falling back to basic auto-adjust");
        return mcp_thread_pool_auto_adjust(pool);
    }

    // Get current thread count and queue info
    size_t current_threads = mcp_thread_pool_get_thread_count(pool);
    size_t optimal_threads = mcp_get_optimal_thread_count();

    // Calculate utilization metrics
    double thread_utilization = current_threads > 0 ? (double)active_tasks / current_threads : 0.0;

    // Calculate queue pressure from work-stealing deques
    mcp_rwlock_read_lock(pool->rwlock);
    size_t total_queued_tasks = 0;
    size_t total_queue_capacity = 0;
    size_t thread_count_local = pool->thread_count;

    // Sum up tasks in all deques
    for (size_t i = 0; i < thread_count_local; i++) {
        if (pool->deques) {
            size_t deque_top = load_size(&pool->deques[i].top);
            size_t deque_bottom = load_size(&pool->deques[i].bottom);

            // Calculate current queue size for this deque
            if (deque_bottom >= deque_top) {
                total_queued_tasks += (deque_bottom - deque_top);
            }

            // Add deque capacity
            total_queue_capacity += pool->deque_capacity;
        }
    }
    mcp_rwlock_read_unlock(pool->rwlock);

    double queue_pressure = total_queue_capacity > 0 ? (double)total_queued_tasks / total_queue_capacity : 0.0;

    // Decision logic for thread count adjustment
    size_t target_threads = current_threads;
    const char* reason = "no change";

    // High load conditions - increase threads
    if ((metrics.cpu_usage_percent < 80.0) && // CPU not maxed out
        (metrics.available_memory_mb > 100) && // Sufficient memory
        ((thread_utilization > HIGH_LOAD_THRESHOLD) ||
         (queue_pressure > QUEUE_PRESSURE_THRESHOLD))) {

        // Increase threads, but not beyond optimal * 1.5
        size_t max_threads = optimal_threads + (optimal_threads / 2);
        if (current_threads < max_threads) {
            target_threads = current_threads + 1;
            reason = "high load/queue pressure";
        }
    }
    // Low load conditions - decrease threads
    else if ((thread_utilization < LOW_LOAD_THRESHOLD) &&
             (queue_pressure < 0.1) && // Very low queue pressure
             (current_threads > MIN_THREAD_COUNT)) {

        // Decrease threads, but not below minimum
        target_threads = current_threads - 1;
        if (target_threads < MIN_THREAD_COUNT) {
            target_threads = MIN_THREAD_COUNT;
        }
        reason = "low load";
    }
    // Memory pressure - reduce threads
    else if (metrics.available_memory_mb < 50) { // Less than 50MB available
        if (current_threads > MIN_THREAD_COUNT) {
            target_threads = current_threads - 1;
            if (target_threads < MIN_THREAD_COUNT) {
                target_threads = MIN_THREAD_COUNT;
            }
            reason = "memory pressure";
        }
    }
    // CPU pressure - reduce threads if we have too many
    else if ((metrics.cpu_usage_percent > 95.0) &&
             (current_threads > optimal_threads)) {
        target_threads = optimal_threads;
        reason = "CPU pressure";
    }

    // Apply the adjustment if needed
    int result = 0;
    if (target_threads != current_threads) {
        result = mcp_thread_pool_resize(pool, target_threads);
        if (result == 0) {
            last_adjustment_time = current_time;
            mcp_log_info("Smart thread pool adjustment: %zu -> %zu threads (%s) "
                        "[CPU: %.1f%%, Mem: %zuMB, Thread util: %.1f%%, Queue: %.1f%%]",
                        current_threads, target_threads, reason,
                        metrics.cpu_usage_percent, metrics.available_memory_mb,
                        thread_utilization * 100.0, queue_pressure * 100.0);
        } else {
            mcp_log_warn("Failed to adjust thread pool from %zu to %zu threads",
                        current_threads, target_threads);
        }
    } else {
        mcp_log_debug("Smart adjustment: no change needed "
                     "[CPU: %.1f%%, Mem: %zuMB, Thread util: %.1f%%, Queue: %.1f%%]",
                     metrics.cpu_usage_percent, metrics.available_memory_mb,
                     thread_utilization * 100.0, queue_pressure * 100.0);
    }

    return result;
}

size_t mcp_thread_pool_get_thread_count(mcp_thread_pool_t* pool) {
    if (!pool) return 0;

    mcp_rwlock_read_lock(pool->rwlock);
    size_t count = pool->thread_count;
    mcp_rwlock_read_unlock(pool->rwlock);

    return count;
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
    int shutdown_state = pool->shutdown_flag;
    mcp_rwlock_read_unlock(pool->rwlock);

    if (shutdown_state != 0) {
        PROFILE_END("mcp_thread_pool_add_task");
        return -1;
    }

    mcp_task_t task = { .function = function, .argument = argument };

    // Thread-safe round-robin submission using atomic fetch-and-add
    size_t target_deque_idx = fetch_add_size(&pool->next_submit_deque, 1) % pool->thread_count;
    work_stealing_deque_t* target_deque = &pool->deques[target_deque_idx];

    // Try to push task onto the bottom of the target deque
    if (!deque_push_bottom(target_deque, task)) {
        // First deque is full, try to find another deque with space
        bool submission_success = false;

        // Try each deque in sequence
        for (size_t i = 1; i < pool->thread_count; i++) {
            size_t alt_deque_idx = (target_deque_idx + i) % pool->thread_count;
            work_stealing_deque_t* alt_deque = &pool->deques[alt_deque_idx];

            if (deque_push_bottom(alt_deque, task)) {
                submission_success = true;
                target_deque_idx = alt_deque_idx; // Update for signaling
                break;
            }
        }

        if (!submission_success) {
            // All deques are full
            mcp_log_error("All deques are full during task submission. Consider increasing queue size.");
            fetch_add_size(&pool->tasks_failed, 1);
            PROFILE_END("mcp_thread_pool_add_task");
            return -1; // Indicate queue full
        }
    }

    // Update statistics
    fetch_add_size(&pool->tasks_submitted, 1);

    // Signal a potentially idle worker
    if (mcp_mutex_lock(pool->cond_mutex) == 0) {
        // Always broadcast to ensure no worker misses the signal
        mcp_cond_broadcast(pool->notify); // Wake up all waiting workers
        mcp_mutex_unlock(pool->cond_mutex);
    }

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
    int shutdown_state = pool->shutdown_flag;
    mcp_rwlock_read_unlock(pool->rwlock);

    if (shutdown_state != 0) {
        return -1; // Pool is shutting down
    }

    // Capture the current number of submitted tasks
    size_t tasks_to_wait_for = pool->tasks_submitted;

    // If no tasks have been submitted, return immediately
    if (tasks_to_wait_for == 0) {
        return 0;
    }

    // Calculate the number of tasks that should be completed
    size_t target_completed = tasks_to_wait_for - pool->tasks_failed;

    // If all tasks have already completed, return immediately
    if (pool->tasks_completed >= target_completed) {
        return 0;
    }

    // Wait for tasks to complete using condition variable
    if (mcp_mutex_lock(pool->cond_mutex) != 0) {
        return -1; // Failed to lock mutex
    }

    unsigned int wait_time = 0;
    unsigned int sleep_interval = 10; // 10ms sleep interval
    int result = 0;

    while ((pool->tasks_completed < target_completed) &&
           (wait_time < timeout_ms || timeout_ms == 0)) {

        // Check if all deques are empty and no tasks are active
        bool all_empty = true;
        for (size_t i = 0; i < pool->thread_count; i++) {
            size_t bottom = load_size(&pool->deques[i].bottom);
            size_t top = load_size(&pool->deques[i].top);

            if (bottom > top) {
                all_empty = false;
                break;
            }
        }

        // If all deques are empty and no tasks are active, we're done
        if (all_empty && pool->active_tasks == 0) {
            result = 0;
            break;
        }

        // Wait for a signal or timeout
        int wait_result = mcp_cond_timedwait(pool->notify, pool->cond_mutex, sleep_interval);

        // Check for errors in wait
        if (wait_result < 0 && wait_result != -2) { // -2 is timeout which is expected
            result = -1;
            break;
        }

        wait_time += sleep_interval;
    }

    mcp_mutex_unlock(pool->cond_mutex);

    // If we timed out and tasks are still not complete
    if (wait_time >= timeout_ms && timeout_ms > 0 && pool->tasks_completed < target_completed) {
        return -1; // Timeout
    }

    return result;
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
    int current_shutdown_state = pool->shutdown_flag;
    mcp_rwlock_read_unlock(pool->rwlock);

    if (current_shutdown_state != 0) {
        return -1; // Already shutting down
    }

    // Use write lock to set shutdown state - ensures exclusive access
    mcp_rwlock_write_lock(pool->rwlock);

    // Double-check shutdown state after acquiring lock
    if (pool->shutdown_flag != 0) {
        mcp_rwlock_write_unlock(pool->rwlock);
        return -1; // Another thread initiated shutdown
    }

    // Set shutdown state to graceful shutdown (2)
    pool->shutdown_flag = 2;
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

    // Join threads with timeout and retry mechanism
    for (size_t i = 0; i < pool->started; ++i) {
        int join_attempts = 0;
        const int max_join_attempts = 3;
        bool join_success = false;

        while (join_attempts < max_join_attempts && !join_success) {
            if (mcp_thread_join(pool->threads[i], NULL) == 0) {
                join_success = true;
            } else {
                mcp_log_warn("Warning: Failed to join thread %zu (attempt %d of %d)",
                            i, join_attempts + 1, max_join_attempts);
                join_attempts++;

                // Short sleep before retry
                #ifdef _WIN32
                Sleep(100);
                #else
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 100000000; // 100ms
                nanosleep(&ts, NULL);
                #endif
            }
        }

        if (!join_success) {
            mcp_log_error("Error: Failed to join thread %zu after %d attempts", i, max_join_attempts);
            err = -1;

            // We'll continue with cleanup even if join failed
            // The thread might still be running, but we need to clean up resources
        }

        // Note: Worker threads free their own arguments when they exit normally
        // If join failed, we might leak the worker_arg, but that's better than
        // potentially freeing memory that's still in use by a running thread
    }

    // Log final statistics
    mcp_log_info("Thread pool statistics: submitted=%zu, completed=%zu, failed=%zu",
                 pool->tasks_submitted, pool->tasks_completed, pool->tasks_failed);

    // Per-worker statistics
    for (size_t i = 0; i < pool->thread_count; ++i) {
        mcp_log_info("Worker %zu statistics: executed=%zu, stolen=%zu",
                     i, pool->tasks_executed[i], pool->tasks_stolen[i]);
    }

    // Cleanup - first free regular memory resources
    if (pool->threads) free(pool->threads);
    if (pool->worker_args) free(pool->worker_args);
    if (pool->worker_status) free((void*)pool->worker_status);
    if (pool->tasks_stolen) free((void*)pool->tasks_stolen);
    if (pool->tasks_executed) free((void*)pool->tasks_executed);

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

    // Free synchronization primitives last
    if (pool->rwlock) mcp_rwlock_free(pool->rwlock);
    if (pool->cond_mutex) mcp_mutex_destroy(pool->cond_mutex);
    if (pool->notify) mcp_cond_destroy(pool->notify);

    free(pool);
    return err;
}

/**
 * @brief The worker thread function using work-stealing deques.
 */
static void* thread_pool_worker(void* arg) {
    // Thread-local data structure with cache line alignment to prevent false sharing
    // between different worker threads' local variables
#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4324) // Disable warning for structure padding
#endif
    struct MCP_CACHE_ALIGNED {
        worker_arg_t* worker_data;
        mcp_thread_pool_t* pool;
        size_t my_index;
        work_stealing_deque_t* my_deque;
        mcp_task_t task;
        unsigned int steal_attempts;
        size_t last_victim_index;
        unsigned int scan_interval;
        unsigned int backoff_shift;
        // Padding to ensure this structure occupies complete cache lines
        char padding[MCP_CACHE_LINE_SIZE - (
            sizeof(worker_arg_t*) + sizeof(mcp_thread_pool_t*) + sizeof(size_t) +
            sizeof(work_stealing_deque_t*) + sizeof(mcp_task_t) + sizeof(unsigned int) * 3 +
            sizeof(size_t)) % MCP_CACHE_LINE_SIZE];
    } worker_locals;
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

    // Initialize worker locals
    worker_locals.worker_data = (worker_arg_t*)arg;
    worker_locals.pool = worker_locals.worker_data->pool;
    worker_locals.my_index = worker_locals.worker_data->worker_index;
    worker_locals.my_deque = &worker_locals.pool->deques[worker_locals.my_index];
    worker_locals.steal_attempts = 0;
    worker_locals.last_victim_index = (worker_locals.my_index + 1) % worker_locals.pool->thread_count;
    worker_locals.scan_interval = 0;
    worker_locals.backoff_shift = 0;

    // Mark this worker as the owner of its argument
    worker_locals.pool->worker_args[worker_locals.my_index] = worker_locals.worker_data;

    while (1) {
        // Check if this thread should exit - use explicit exit flag first, then fallback to index check
        bool should_exit_explicit = worker_locals.worker_data->should_exit;
        bool should_exit_index = false;

        // Only check index-based exit if explicit flag is not set (for backward compatibility)
        if (!should_exit_explicit) {
            mcp_rwlock_read_lock(worker_locals.pool->rwlock);
            should_exit_index = (worker_locals.my_index >= worker_locals.pool->thread_count);
            mcp_rwlock_read_unlock(worker_locals.pool->rwlock);
        }

        if (should_exit_explicit || should_exit_index) {
            // Mark worker as inactive before exiting
            worker_locals.pool->worker_status[worker_locals.my_index] = 0;
            worker_locals.worker_data->is_active = false;

            if (should_exit_explicit) {
                mcp_log_debug("Worker %zu exiting due to explicit exit signal", worker_locals.my_index);
            } else {
                mcp_log_debug("Worker %zu exiting due to pool shrink (index >= thread_count)", worker_locals.my_index);
            }
            break;  // Let the function's cleanup code handle the rest
        }
        
        // 1. Try to pop from own deque
        if (deque_pop_bottom(worker_locals.my_deque, &worker_locals.task)) {
            worker_locals.steal_attempts = 0;
            worker_locals.backoff_shift = 0; // Reset exponential backoff

            // Mark worker as active
            worker_locals.pool->worker_status[worker_locals.my_index] = 1;
            worker_locals.worker_data->is_active = true;
            fetch_add_size(&worker_locals.pool->active_tasks, 1);

            PROFILE_START("thread_pool_task_execution");
            (*(worker_locals.task.function))(worker_locals.task.argument);
            PROFILE_END("thread_pool_task_execution");

            // Update statistics
            fetch_add_size(&worker_locals.pool->tasks_completed, 1);
            fetch_add_size(&worker_locals.pool->tasks_executed[worker_locals.my_index], 1);
            fetch_add_size(&worker_locals.pool->active_tasks, (size_t)-1); // Decrement

            // Mark worker as idle
            worker_locals.pool->worker_status[worker_locals.my_index] = 0;
            worker_locals.worker_data->is_active = false;

            continue;
        }

        // 2. Own deque is empty, check for shutdown
        // Use read lock to check shutdown state - allows multiple threads to check concurrently
        mcp_rwlock_read_lock(worker_locals.pool->rwlock);
        int shutdown_status = worker_locals.pool->shutdown_flag;
        mcp_rwlock_read_unlock(worker_locals.pool->rwlock);

        if (shutdown_status != 0) {
            // For immediate shutdown (1), exit right away
            if (shutdown_status == 1) {
                break; // Exit the loop and clean up
            }

            // For graceful shutdown (2), check if all deques are empty
            bool all_empty = true;

            // Check all deques
            for (size_t i = 0; i < worker_locals.pool->thread_count; i++) {
                size_t bottom = load_size(&worker_locals.pool->deques[i].bottom);
                size_t top = load_size(&worker_locals.pool->deques[i].top);

                if (bottom > top) {
                    all_empty = false;
                    break;
                }
            }

            // If all deques are empty and no active tasks, we can exit
            if (all_empty && load_size(&worker_locals.pool->active_tasks) == 0) {
                break; // Exit the loop and clean up
            }

            // Otherwise continue trying to steal and process remaining tasks
        }

        // 3. Try to steal using an optimized strategy
        if (worker_locals.pool->thread_count > 1) {
            size_t victim_index = worker_locals.my_index; // Default to own index (will be changed)

            // Increment scan interval counter
            worker_locals.scan_interval++;

            // Every 8 attempts, do a full scan to find the deque with most tasks
            // This balances between quick targeted stealing and thorough load balancing
            if (worker_locals.scan_interval >= 8) {
                worker_locals.scan_interval = 0;
                size_t max_tasks = 0;

                // Full scan of all deques
                for (size_t i = 0; i < worker_locals.pool->thread_count; i++) {
                    if (i == worker_locals.my_index) continue; // Skip own deque

                    size_t bottom = load_size(&worker_locals.pool->deques[i].bottom);
                    size_t top = load_size(&worker_locals.pool->deques[i].top);
                    size_t tasks = (bottom > top) ? (bottom - top) : 0;

                    if (tasks > max_tasks) {
                        max_tasks = tasks;
                        victim_index = i;
                    }
                }

                // If we found a deque with tasks, update the last victim index
                if (max_tasks > 0) {
                    worker_locals.last_victim_index = victim_index;
                }
                // If no tasks found, we'll try the last successful victim or random
            } else {
                // First try the last successful victim
                size_t bottom = load_size(&worker_locals.pool->deques[worker_locals.last_victim_index].bottom);
                size_t top = load_size(&worker_locals.pool->deques[worker_locals.last_victim_index].top);

                // If last victim has no tasks, try a random victim
                if (bottom <= top) {
                    // Try a random victim, but not ourselves
                    do {
                        victim_index = rand() % worker_locals.pool->thread_count;
                    } while (victim_index == worker_locals.my_index);
                } else {
                    // Last victim still has tasks
                    victim_index = worker_locals.last_victim_index;
                }
            }

            work_stealing_deque_t* victim_deque = &worker_locals.pool->deques[victim_index];

            if (deque_steal_top(victim_deque, &worker_locals.task)) {
                worker_locals.steal_attempts = 0;
                worker_locals.backoff_shift = 0; // Reset exponential backoff

                // Update last successful victim index
                worker_locals.last_victim_index = victim_index;

                // Mark worker as active
                worker_locals.pool->worker_status[worker_locals.my_index] = 1;
                worker_locals.worker_data->is_active = true;
                fetch_add_size(&worker_locals.pool->active_tasks, 1);

                PROFILE_START("thread_pool_task_execution_steal");
                (*(worker_locals.task.function))(worker_locals.task.argument);
                PROFILE_END("thread_pool_task_execution_steal");

                // Update statistics
                fetch_add_size(&worker_locals.pool->tasks_completed, 1);
                fetch_add_size(&worker_locals.pool->tasks_stolen[worker_locals.my_index], 1);
                fetch_add_size(&worker_locals.pool->active_tasks, (size_t)-1); // Decrement

                // Mark worker as idle
                worker_locals.pool->worker_status[worker_locals.my_index] = 0;
                worker_locals.worker_data->is_active = false;

                continue;
            }
        }

        // 4. Failed to pop and failed to steal
        worker_locals.steal_attempts++;

        // Exponential backoff strategy
        if (worker_locals.steal_attempts <= 3) {
            // For first few attempts, just yield to give other threads a chance
            mcp_thread_yield();
            // Reset backoff shift for next time
            worker_locals.backoff_shift = 0;
        } else if (worker_locals.steal_attempts <= 10) {
            // For attempts 4-10, use exponential backoff with yield
            // Calculate backoff time: 2^backoff_shift milliseconds (1, 2, 4, 8, 16, 32, 64)
            unsigned int backoff_time = 1u << worker_locals.backoff_shift;

            // Yield a number of times proportional to backoff time
            for (unsigned int i = 0; i < backoff_time && i < 20; i++) {
                mcp_thread_yield();
            }

            // Increase shift for next backoff, max 6 (2^6 = 64ms equivalent of yields)
            if (worker_locals.backoff_shift < 6) {
                worker_locals.backoff_shift++;
            }
        } else {
            // For attempts > 10, use condition variable with exponential timeout
            // Reset backoff shift if it's too large
            if (worker_locals.backoff_shift > 8) {
                worker_locals.backoff_shift = 3; // Start from 8ms again
            }

            // Calculate timeout: 2^backoff_shift milliseconds, capped at 200ms
            unsigned int timeout_ms = 1u << worker_locals.backoff_shift;
            if (timeout_ms > 200) timeout_ms = 200;

            // Increase shift for next backoff, max 8 (2^8 = 256ms, but capped at 200ms)
            worker_locals.backoff_shift++;

            // Longer wait using mutex/condvar for shutdown signal
            if (mcp_mutex_lock(worker_locals.pool->cond_mutex) == 0) {
                // Check for explicit exit signal first
                if (worker_locals.worker_data->should_exit) {
                    mcp_mutex_unlock(worker_locals.pool->cond_mutex);
                    break; // Exit the loop and clean up
                }

                // Use read lock to check shutdown state - allows multiple threads to check concurrently
                mcp_rwlock_read_lock(worker_locals.pool->rwlock);
                int current_shutdown = worker_locals.pool->shutdown_flag;
                mcp_rwlock_read_unlock(worker_locals.pool->rwlock);

                if (current_shutdown != 0) {
                    // For immediate shutdown (1), exit right away
                    if (current_shutdown == 1) {
                        mcp_mutex_unlock(worker_locals.pool->cond_mutex);
                        break; // Exit the loop and clean up
                    }

                    // For graceful shutdown (2), check if all deques are empty
                    bool all_empty = true;

                    // Check all deques
                    for (size_t i = 0; i < worker_locals.pool->thread_count; i++) {
                        size_t bottom = load_size(&worker_locals.pool->deques[i].bottom);
                        size_t top = load_size(&worker_locals.pool->deques[i].top);

                        if (bottom > top) {
                            all_empty = false;
                            break;
                        }
                    }

                    // If all deques are empty and no active tasks, we can exit
                    if (all_empty && load_size(&worker_locals.pool->active_tasks) == 0) {
                        mcp_mutex_unlock(worker_locals.pool->cond_mutex);
                        break; // Exit the loop and clean up
                    }

                    // Otherwise continue waiting for tasks
                }

                // Wait for a signal or timeout with exponential backoff
                int wait_result = mcp_cond_timedwait(worker_locals.pool->notify, worker_locals.pool->cond_mutex, timeout_ms);

                // If we timed out, check if there are tasks in any deque
                if (wait_result == -2) { // -2 is timeout
                    bool found_tasks = false;
                    for (size_t i = 0; i < worker_locals.pool->thread_count && !found_tasks; i++) {
                        size_t bottom = load_size(&worker_locals.pool->deques[i].bottom);
                        size_t top = load_size(&worker_locals.pool->deques[i].top);
                        if (bottom > top) {
                            found_tasks = true;
                        }
                    }

                    // If we found tasks but timed out, there might be a missed signal
                    if (found_tasks) {
                        // Reset steal_attempts to try stealing again immediately
                        worker_locals.steal_attempts = 0;
                        worker_locals.backoff_shift = 0;
                    }
                } else if (wait_result == 0) {
                    // If we were signaled, reset backoff
                    worker_locals.backoff_shift = 0;
                    worker_locals.steal_attempts = 0;
                }

                mcp_mutex_unlock(worker_locals.pool->cond_mutex);
            }

            // If we've been trying for a very long time with no success,
            // occasionally reset the steal counter to avoid getting stuck in long backoffs
            if (worker_locals.steal_attempts > 30) {
                worker_locals.steal_attempts = 5;
                worker_locals.backoff_shift = 0;
            }
        }
    } // End while(1)

    // Clean up worker argument
    free(worker_locals.worker_data);
    worker_locals.pool->worker_args[worker_locals.my_index] = NULL;

    return NULL;
}

/**
 * @brief Gets statistics from the thread pool.
 *
 * This function retrieves statistics about the thread pool's operation.
 *
 * @param pool The thread pool instance.
 * @param submitted Pointer to store the number of submitted tasks.
 * @param completed Pointer to store the number of completed tasks.
 * @param failed Pointer to store the number of failed task submissions.
 * @param active Pointer to store the number of currently active tasks.
 * @return 0 on success, -1 on failure.
 */
int mcp_thread_pool_get_stats(mcp_thread_pool_t* pool, size_t* submitted, size_t* completed,
                              size_t* failed, size_t* active) {
    if (pool == NULL) {
        return -1;
    }

    // Use read lock to ensure consistent state
    mcp_rwlock_read_lock(pool->rwlock);

    if (submitted) *submitted = pool->tasks_submitted;
    if (completed) *completed = pool->tasks_completed;
    if (failed) *failed = pool->tasks_failed;
    if (active) *active = pool->active_tasks;

    mcp_rwlock_read_unlock(pool->rwlock);

    return 0;
}
