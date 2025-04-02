#include "mcp_thread_pool.h"
#include "mcp_profiler.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
// Windows-specific implementation details
#define WIN32_LEAN_AND_MEAN // Exclude less-used parts of windows.h
#include <windows.h>        // Include windows.h AFTER defining WIN32_LEAN_AND_MEAN
typedef HANDLE thread_handle_t;
typedef CRITICAL_SECTION mutex_t;
typedef CONDITION_VARIABLE condition_t;
#define mutex_init(m) (InitializeCriticalSection(m), 0)
#define mutex_lock(m) (EnterCriticalSection(m), 0)
#define mutex_unlock(m) (LeaveCriticalSection(m), 0)
#define mutex_destroy(m) (DeleteCriticalSection(m), 0)
#define cond_init(c) (InitializeConditionVariable(c), 0)
// SleepConditionVariableCS returns BOOL (0 on failure, non-zero on success).
// We need to handle its return value specifically where it's called.
#define cond_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
#define cond_signal(c) (WakeConditionVariable(c), 0)
#define cond_broadcast(c) (WakeAllConditionVariable(c), 0)
#define cond_destroy(c) (void)0 // No explicit destruction needed for CONDITION_VARIABLE
#define thread_create(th, attr, start_routine, arg) \
    ((*(th) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(start_routine), arg, 0, NULL)) == NULL ? -1 : 0)
#define thread_join(th, retval) \
    (WaitForSingleObject(th, INFINITE) == WAIT_OBJECT_0 ? (CloseHandle(th), 0) : -1)
#else
// POSIX (pthreads) implementation details
typedef pthread_t thread_handle_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t condition_t;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#define mutex_destroy(m) pthread_mutex_destroy(m)
#define cond_init(c) pthread_cond_init(c, NULL)
#define cond_wait(c, m) pthread_cond_wait(c, m)
#define cond_signal(c) pthread_cond_signal(c)
#define cond_broadcast(c) pthread_cond_broadcast(c)
#define cond_destroy(c) pthread_cond_destroy(c)
#define thread_create(th, attr, start_routine, arg) pthread_create(th, attr, start_routine, arg)
#define thread_join(th, retval) pthread_join(th, retval)
#endif

/**
 * @brief Internal structure for the thread pool.
 */
struct mcp_thread_pool {
    mutex_t lock;               /**< Mutex for protecting shared data access. */
    condition_t notify;         /**< Condition variable to signal waiting threads for new tasks. */
    condition_t queue_not_full; /**< Condition variable to signal when queue has space. */
    thread_handle_t* threads;   /**< Array of worker thread handles. */
    mcp_task_t* queue;          /**< Array representing the task queue (circular buffer). */
    size_t thread_count;        /**< Number of worker threads. */
    size_t queue_size;          /**< Maximum capacity of the task queue. */
    size_t head;                /**< Index of the next task to dequeue. */
    size_t tail;                /**< Index of the next empty slot to enqueue. */
    size_t count;               /**< Current number of tasks in the queue. */
    int shutdown;               /**< Flag indicating if the pool is shutting down (0=no, 1=immediate, 2=graceful). */
    int started;                /**< Number of threads successfully started. */
};

// Forward declaration for the worker thread function
#ifdef _WIN32
static DWORD WINAPI thread_pool_worker(LPVOID arg);
#else
static void* thread_pool_worker(void* arg);
#endif

/**
 * @brief Creates a new thread pool.
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
    pool->thread_count = 0; // Will be set later after successful thread creation
    pool->queue_size = queue_size;
    pool->head = pool->tail = pool->count = 0;
    pool->shutdown = 0; // Not shutting down
    pool->started = 0;  // No threads started yet

    // Allocate memory for threads and queue
    pool->threads = (thread_handle_t*)malloc(sizeof(thread_handle_t) * thread_count);
    pool->queue = (mcp_task_t*)malloc(sizeof(mcp_task_t) * queue_size);

    // Initialize mutex and condition variables
    if (mutex_init(&pool->lock) != 0 ||
        cond_init(&pool->notify) != 0 ||
        cond_init(&pool->queue_not_full) != 0 ||
        pool->threads == NULL || pool->queue == NULL)
    {
        fprintf(stderr, "Thread pool creation failed: Failed to initialize sync primitives or allocate memory.\n");
        // Cleanup allocated resources
        if (pool->threads) free(pool->threads);
        if (pool->queue) free(pool->queue);
        // Attempt to destroy initialized sync primitives (ignore errors here)
        mutex_destroy(&pool->lock);
        cond_destroy(&pool->notify);
        cond_destroy(&pool->queue_not_full);
        free(pool);
        return NULL;
    }

    // Start worker threads
    for (size_t i = 0; i < thread_count; ++i) {
        if (thread_create(&(pool->threads[i]), NULL, thread_pool_worker, (void*)pool) != 0) {
            perror("Failed to create worker thread");
            // If thread creation fails, trigger shutdown and cleanup
            // Set shutdown flag before calling destroy to prevent deadlock
            pool->shutdown = 1; // Immediate shutdown
            mcp_thread_pool_destroy(pool); // Use destroy for proper cleanup
            return NULL;
        }
        pool->thread_count++;
        pool->started++;
    }

    return pool;
}

/**
 * @brief Adds a new task to the thread pool's queue.
 */
int mcp_thread_pool_add_task(mcp_thread_pool_t* pool, void (*function)(void*), void* argument) {
    int err = 0;
    if (pool == NULL || function == NULL) {
        return -1; // Invalid arguments
    }
    PROFILE_START("mcp_thread_pool_add_task");

    if (mutex_lock(&pool->lock) != 0) {
        return -1; // Failed to lock mutex
    }

    // Wait if the queue is full and we are not shutting down
    while (pool->count == pool->queue_size && !pool->shutdown) {
#ifdef _WIN32
        // SleepConditionVariableCS returns BOOL (0 on failure)
        if (!SleepConditionVariableCS(&pool->queue_not_full, &pool->lock, INFINITE)) {
             fprintf(stderr, "Error waiting on queue_not_full condition variable: %lu\n", GetLastError());
             mutex_unlock(&pool->lock);
             return -1;
        }
#else
        if (pthread_cond_wait(&pool->queue_not_full, &pool->lock) != 0) {
             perror("Error waiting on queue_not_full condition variable");
             mutex_unlock(&pool->lock);
             return -1;
        }
#endif
    }

    if (pool->shutdown) {
        err = -1; // Pool is shutting down, cannot add task
    } else {
        // Add task to the queue
        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].argument = argument;
        pool->tail = (pool->tail + 1) % pool->queue_size;
        pool->count++;

        // Signal a waiting worker thread that a task is available
        if (cond_signal(&pool->notify) != 0) {
            err = -1; // Failed to signal
            fprintf(stderr, "Warning: Failed to signal worker thread after adding task.\n");
        }
    }

    if (mutex_unlock(&pool->lock) != 0) {
        err = -1; // Failed to unlock mutex
    }
    PROFILE_END("mcp_thread_pool_add_task");

    return err;
}

/**
 * @brief Destroys the thread pool.
 */
int mcp_thread_pool_destroy(mcp_thread_pool_t* pool) {
    if (pool == NULL) {
        return -1;
    }

    int err = 0;

    if (mutex_lock(&pool->lock) != 0) {
        return -1; // Failed to lock mutex
    }

    // Check if already shutting down or already destroyed
    if (pool->shutdown) {
        mutex_unlock(&pool->lock);
        return -1; // Already initiated shutdown
    }

    // Initiate graceful shutdown (wait for queue to empty)
    pool->shutdown = 2;

    // Wake up all worker threads so they can check the shutdown flag
    if (cond_broadcast(&pool->notify) != 0 || cond_broadcast(&pool->queue_not_full) != 0) {
        err = -1; // Failed to broadcast, but continue shutdown
        fprintf(stderr, "Warning: Failed to broadcast shutdown signal to worker threads.\n");
    }

    // Unlock mutex to allow workers to acquire lock and exit
    if (mutex_unlock(&pool->lock) != 0) {
        err = -1; // Failed to unlock mutex, critical error?
        fprintf(stderr, "Error: Failed to unlock mutex during thread pool destroy.\n");
        // Proceed with joining anyway, might deadlock if a thread holds the lock
    }

    // Join all worker threads
    for (size_t i = 0; i < pool->started; ++i) { // Only join started threads
        if (thread_join(pool->threads[i], NULL) != 0) {
            perror("Failed to join thread");
            err = -1;
        }
    }

    // Cleanup resources (mutex should not be locked here)
    mutex_destroy(&pool->lock);
    cond_destroy(&pool->notify);
    cond_destroy(&pool->queue_not_full);
    if (pool->threads) free(pool->threads);
    if (pool->queue) free(pool->queue);
    free(pool);

    return err;
}

/**
 * @brief The worker thread function.
 */
#ifdef _WIN32
static DWORD WINAPI thread_pool_worker(LPVOID arg) {
#else
static void* thread_pool_worker(void* arg) {
#endif
    mcp_thread_pool_t* pool = (mcp_thread_pool_t*)arg;
    mcp_task_t task;

    while (1) {
        // Lock mutex to access shared data
        if (mutex_lock(&pool->lock) != 0) {
            fprintf(stderr, "Worker thread failed to lock mutex. Exiting.\n");
#ifdef _WIN32
            return (DWORD)-1;
#else
            return (void*)(intptr_t)-1; // Indicate error
#endif
        }

        // Wait for a task or shutdown signal
        while (pool->count == 0 && !pool->shutdown) {
#ifdef _WIN32
            if (!SleepConditionVariableCS(&pool->notify, &pool->lock, INFINITE)) {
                 fprintf(stderr, "Worker thread failed waiting on condition variable: %lu. Exiting.\n", GetLastError());
                 mutex_unlock(&pool->lock);
                 return (DWORD)-1;
            }
#else
            if (pthread_cond_wait(&pool->notify, &pool->lock) != 0) {
                 perror("Worker thread failed waiting on condition variable. Exiting.");
                 mutex_unlock(&pool->lock);
                 return (void*)(intptr_t)-1; // Indicate error
            }
#endif
        }

        // Check shutdown conditions (after waking up)
        if (pool->shutdown == 1 || (pool->shutdown == 2 && pool->count == 0)) {
            // Immediate shutdown OR Graceful shutdown and queue is empty
            break; // Exit loop (mutex will be unlocked below)
        }

        // Dequeue task if available (check count again after waking)
        if (pool->count > 0) {
            task.function = pool->queue[pool->head].function;
            task.argument = pool->queue[pool->head].argument;
            pool->head = (pool->head + 1) % pool->queue_size;
            pool->count--;

            // Signal that the queue might no longer be full
            cond_signal(&pool->queue_not_full);

            // Unlock mutex before executing task
            if (mutex_unlock(&pool->lock) != 0) {
                 fprintf(stderr, "Worker thread failed to unlock mutex before task execution. Exiting.\n");
#ifdef _WIN32
                 return (DWORD)-1;
#else
                 return (void*)(intptr_t)-1; // Indicate error
#endif
            }

            // Execute the task
            PROFILE_START("thread_pool_task_execution");
            (*(task.function))(task.argument);
            PROFILE_END("thread_pool_task_execution");

            // Task execution finished, loop back to acquire lock
        } else {
            // Spurious wakeup or shutdown initiated while waiting, unlock and loop/exit
            mutex_unlock(&pool->lock);
        }
    } // End while(1)

    // Unlock mutex before exiting thread
    mutex_unlock(&pool->lock);

#ifdef _WIN32
    return 0;
#else
    pthread_exit(NULL);
    return NULL; // Keep compiler happy
#endif
}
