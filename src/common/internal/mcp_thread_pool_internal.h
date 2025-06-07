#ifndef MCP_THREAD_POOL_INTERNAL_H
#define MCP_THREAD_POOL_INTERNAL_H

#include "mcp_thread_pool.h"
#include "mcp_profiler.h"
#include "mcp_sync.h"
#include "mcp_rwlock.h"
#include "mcp_cache_aligned.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
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

// Argument struct for worker threads
typedef struct {
    struct mcp_thread_pool* pool;
    size_t worker_index;
    volatile bool should_exit;  // Explicit exit flag for this worker
    volatile bool is_active;    // Whether this worker is currently active
} worker_arg_t;

/**
 * @brief Lock-free work-stealing deque structure (Chase-Lev style inspired).
 * Simplified: Assumes single producer (owner thread pushes/pops bottom), multiple consumers (thieves steal top).
 *
 * This structure is carefully designed to avoid false sharing:
 * - top is accessed by multiple thieves (readers) and occasionally by the owner
 * - bottom is primarily accessed by the owner (writer) and occasionally by thieves
 * - Each field is placed on its own cache line to prevent false sharing
 */
#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4324) // Disable padding warning
#endif

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
    size_t max_thread_count;    /**< Maximum thread count (size of allocated arrays). */
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
bool compare_and_swap_size(volatile size_t* ptr, size_t expected, size_t desired);

// Atomic Load for size_t
size_t load_size(volatile size_t* ptr);

// Atomic Load for int
int load_int(volatile int* ptr);

// Atomic Store for int
void store_int(volatile int* ptr, int value);

// Atomic Fetch-and-Add for size_t
size_t fetch_add_size(volatile size_t* ptr, size_t value);

// Push task onto the bottom of the deque (owner thread only)
bool deque_push_bottom(work_stealing_deque_t* deque, mcp_task_t task);

// Pop task from the bottom of the deque (owner thread only)
bool deque_pop_bottom(work_stealing_deque_t* deque, mcp_task_t* task);

// Steal task from the top of the deque (thief threads only)
bool deque_steal_top(work_stealing_deque_t* deque, mcp_task_t* task);

// Get current system load metrics
int get_system_load_metrics(system_load_metrics_t* metrics);

// The worker thread function
void* thread_pool_worker(void* arg);

#endif // MCP_THREAD_POOL_INTERNAL_H
