/**
 * @file mcp_thread_pool_worker.c
 * @brief Worker thread implementation for thread pool.
 *
 * This file implements the worker thread function that handles task execution,
 * work stealing, and thread lifecycle management.
 */

#include "internal/mcp_thread_pool_internal.h"

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4324) // Disable warning for structure padding
#endif

// Thread-local data structure with cache line alignment to prevent false sharing
// between different worker threads' local variables
typedef struct MCP_CACHE_ALIGNED {
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
} worker_locals_t;

#ifdef _MSC_VER
#   pragma warning(pop)
#endif

/**
 * @brief The worker thread function using work-stealing deques.
 */
void* thread_pool_worker(void* arg) {
    worker_locals_t worker_locals = {0};

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
                fetch_add_size(&worker_locals.pool->tasks_executed[worker_locals.my_index], 1);
                fetch_add_size(&worker_locals.pool->tasks_stolen[worker_locals.my_index], 1);
                fetch_add_size(&worker_locals.pool->active_tasks, (size_t)-1); // Decrement

                // Mark worker as idle
                worker_locals.pool->worker_status[worker_locals.my_index] = 0;
                worker_locals.worker_data->is_active = false;

                continue;
            }
        }

        // 4. No work found, increment steal attempts and wait
        worker_locals.steal_attempts++;

        // Exponential backoff with jitter to reduce contention
        if (worker_locals.steal_attempts > 5) {
            // Calculate timeout with exponential backoff (capped at 100ms)
            unsigned int timeout_ms = 1 << worker_locals.backoff_shift; // 1, 2, 4, 8, 16, 32, 64ms
            if (timeout_ms > 100) {
                timeout_ms = 100; // Cap at 100ms
            } else {
                worker_locals.backoff_shift++;
                if (worker_locals.backoff_shift > 6) { // Cap backoff shift
                    worker_locals.backoff_shift = 6;
                }
            }

            // Add some jitter to reduce thundering herd
            timeout_ms += (rand() % 10); // Add 0-9ms jitter

            // Wait on condition variable with timeout
            if (mcp_mutex_lock(worker_locals.pool->cond_mutex) == 0) {
                // Double-check for shutdown while holding the mutex
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
