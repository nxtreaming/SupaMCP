#ifndef MCP_THREAD_POOL_H
#define MCP_THREAD_POOL_H

#include <stddef.h>
#include <stdbool.h>

typedef struct mcp_thread_pool mcp_thread_pool_t;

/**
 * @brief Structure representing a task to be executed by the thread pool.
 */
typedef struct {
    void (*function)(void*); /**< Pointer to the function to execute. */
    void* argument;          /**< Argument to pass to the function. */
} mcp_task_t;

/**
 * @brief Creates a new thread pool.
 *
 * @param thread_count The number of worker threads to create in the pool.
 * @param queue_size The maximum number of tasks that can be queued.
 * @return A pointer to the newly created thread pool, or NULL on failure.
 *         The caller is responsible for destroying the pool using mcp_thread_pool_destroy().
 */
mcp_thread_pool_t* mcp_thread_pool_create(size_t thread_count, size_t queue_size);

/**
 * @brief Dynamically adjusts the thread pool size
 * @param pool The thread pool to adjust
 * @param new_thread_count The new number of threads
 * @return 0 on success, -1 on error
 */
int mcp_thread_pool_resize(mcp_thread_pool_t* pool, size_t new_thread_count);

/**
 * @brief Gets the current system's recommended thread count
 * @return Recommended number of threads (usually 2 * num_cores + 1)
 */
size_t mcp_get_optimal_thread_count(void);

/**
 * @brief Auto-adjusts thread pool size based on system load
 * @param pool The thread pool to adjust
 * @return 0 on success, -1 on error
 */
int mcp_thread_pool_auto_adjust(mcp_thread_pool_t* pool);

/**
 * @brief Smart auto-adjustment with load monitoring and context
 * @param pool The thread pool to adjust
 * @param context Optional context data (e.g., TCP transport stats)
 * @return 0 on success, -1 on error
 */
int mcp_thread_pool_smart_adjust(mcp_thread_pool_t* pool, void* context);

/**
 * @brief Gets the current thread count of the pool
 * @param pool The thread pool instance
 * @return Current number of threads, or 0 if pool is NULL
 */
size_t mcp_thread_pool_get_thread_count(mcp_thread_pool_t* pool);

/**
 * @brief Adds a new task to the thread pool's queue.
 *
 * This function is thread-safe.
 *
 * @param pool The thread pool instance.
 * @param function The function pointer for the task to execute.
 * @param argument The argument to pass to the task function.
 * @return 0 on success, -1 on failure (e.g., pool is shutting down, queue is full).
 */
int mcp_thread_pool_add_task(mcp_thread_pool_t* pool, void (*function)(void*), void* argument);

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
int mcp_thread_pool_wait(mcp_thread_pool_t* pool, unsigned int timeout_ms);

/**
 * @brief Destroys the thread pool.
 *
 * Waits for all queued tasks to complete and joins all worker threads
 * before freeing resources.
 *
 * @param pool The thread pool instance to destroy.
 * @return 0 on success, -1 on failure.
 */
int mcp_thread_pool_destroy(mcp_thread_pool_t* pool);

/**
 * @brief Gets statistics from the thread pool.
 *
 * This function retrieves statistics about the thread pool's operation.
 *
 * @param pool The thread pool instance.
 * @param submitted Pointer to store the number of submitted tasks. Can be NULL.
 * @param completed Pointer to store the number of completed tasks. Can be NULL.
 * @param failed Pointer to store the number of failed task submissions. Can be NULL.
 * @param active Pointer to store the number of currently active tasks. Can be NULL.
 * @return 0 on success, -1 on failure.
 */
int mcp_thread_pool_get_stats(mcp_thread_pool_t* pool, size_t* submitted, size_t* completed,
                              size_t* failed, size_t* active);

#endif // MCP_THREAD_POOL_H
