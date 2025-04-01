#ifndef MCP_THREAD_POOL_H
#define MCP_THREAD_POOL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

// Forward declaration
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
 * @brief Destroys the thread pool.
 *
 * Waits for all queued tasks to complete and joins all worker threads
 * before freeing resources.
 *
 * @param pool The thread pool instance to destroy.
 * @return 0 on success, -1 on failure.
 */
int mcp_thread_pool_destroy(mcp_thread_pool_t* pool);

#endif // MCP_THREAD_POOL_H
