/**
 * @file mcp_thread_pool.c
 * @brief Core thread pool management functions.
 *
 */
#include "internal/mcp_thread_pool_internal.h"

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
    pool->max_thread_count = thread_count;  // Store original allocation size
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

    // Prevent exceeding original allocation size to avoid array bounds issues
    if (new_thread_count > pool->max_thread_count) {
        mcp_log_warn("Cannot resize thread pool to %zu threads (max: %zu), capping to maximum",
                     new_thread_count, pool->max_thread_count);
        new_thread_count = pool->max_thread_count;
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

size_t mcp_thread_pool_get_thread_count(mcp_thread_pool_t* pool) {
    if (!pool) return 0;

    mcp_rwlock_read_lock(pool->rwlock);
    size_t count = pool->thread_count;
    mcp_rwlock_read_unlock(pool->rwlock);

    return count;
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

                mcp_sleep_ms(100);  // Short sleep before retry
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

    // Per-worker statistics - use max_thread_count to avoid array bounds issues
    // Note: Statistics arrays were allocated based on max_thread_count
    for (size_t i = 0; i < pool->max_thread_count; ++i) {
        // Only log statistics for threads that were actually started
        if (i < pool->thread_count) {
            mcp_log_info("Worker %zu statistics: executed=%zu, stolen=%zu",
                         i, pool->tasks_executed[i], pool->tasks_stolen[i]);
        }
    }

    // Cleanup - first free regular memory resources
    if (pool->threads) free(pool->threads);
    if (pool->worker_args) free(pool->worker_args);
    if (pool->worker_status) free((void*)pool->worker_status);
    if (pool->tasks_stolen) free((void*)pool->tasks_stolen);
    if (pool->tasks_executed) free((void*)pool->tasks_executed);

    if (pool->deques) {
         for (size_t i = 0; i < pool->max_thread_count; ++i) {
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
