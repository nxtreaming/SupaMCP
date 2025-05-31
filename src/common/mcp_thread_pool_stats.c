/**
 * @file mcp_thread_pool_stats.c
 * @brief Statistics and adjustment functions for thread pool.
 *
 * This file implements statistics collection and smart adjustment algorithms
 * for the thread pool, including auto-adjustment based on system load.
 */
#include "internal/mcp_thread_pool_internal.h"

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
