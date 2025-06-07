/**
 * @file mcp_thread_pool_tasks.c
 * @brief Task submission and waiting operations for thread pool.
 *
 * This file implements task submission and waiting functionality for the thread pool,
 * including task distribution across work-stealing deques and synchronization.
 */
#include "internal/mcp_thread_pool_internal.h"

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
    // Use max_thread_count to ensure we don't exceed allocated deque array size
    size_t target_deque_idx = fetch_add_size(&pool->next_submit_deque, 1) % pool->max_thread_count;
    work_stealing_deque_t* target_deque = &pool->deques[target_deque_idx];

    // Try to push task onto the bottom of the target deque
    if (!deque_push_bottom(target_deque, task)) {
        // First deque is full, try to find another deque with space
        bool submission_success = false;

        // Try each deque in sequence
        for (size_t i = 1; i < pool->max_thread_count; i++) {
            size_t alt_deque_idx = (target_deque_idx + i) % pool->max_thread_count;
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
        for (size_t i = 0; i < pool->max_thread_count; i++) {
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
