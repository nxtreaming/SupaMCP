#ifndef MCP_SYNC_H
#define MCP_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Mutex ---

/** @brief Opaque mutex type. */
typedef struct mcp_mutex_s mcp_mutex_t;

/**
 * @brief Creates a new mutex.
 * @return Pointer to the created mutex, or NULL on failure.
 * @note Caller is responsible for destroying the mutex using mcp_mutex_destroy().
 */
mcp_mutex_t* mcp_mutex_create(void);

/**
 * @brief Destroys a mutex and frees associated resources.
 * @param mutex Pointer to the mutex to destroy. If NULL, the function does nothing.
 */
void mcp_mutex_destroy(mcp_mutex_t* mutex);

/**
 * @brief Locks the specified mutex. Blocks if the mutex is already locked.
 * @param mutex Pointer to the mutex to lock.
 * @return 0 on success, non-zero on error.
 */
int mcp_mutex_lock(mcp_mutex_t* mutex);

/**
 * @brief Unlocks the specified mutex.
 * @param mutex Pointer to the mutex to unlock.
 * @return 0 on success, non-zero on error.
 */
int mcp_mutex_unlock(mcp_mutex_t* mutex);

// --- Condition Variable ---

/** @brief Opaque condition variable type. */
typedef struct mcp_cond_s mcp_cond_t;

/**
 * @brief Creates a new condition variable.
 * @return Pointer to the created condition variable, or NULL on failure.
 * @note Caller is responsible for destroying the condition variable using mcp_cond_destroy().
 */
mcp_cond_t* mcp_cond_create(void);

/**
 * @brief Destroys a condition variable and frees associated resources.
 * @param cond Pointer to the condition variable to destroy. If NULL, the function does nothing.
 */
void mcp_cond_destroy(mcp_cond_t* cond);

/**
 * @brief Waits indefinitely on a condition variable.
 * Atomically unlocks the mutex and waits. Re-locks the mutex before returning.
 * @param cond Pointer to the condition variable to wait on.
 * @param mutex Pointer to the mutex associated with the condition.
 * @return 0 on success, non-zero on error.
 */
int mcp_cond_wait(mcp_cond_t* cond, mcp_mutex_t* mutex);

/**
 * @brief Waits on a condition variable for a specified duration.
 * Atomically unlocks the mutex and waits. Re-locks the mutex before returning.
 * @param cond Pointer to the condition variable to wait on.
 * @param mutex Pointer to the mutex associated with the condition.
 * @param timeout_ms Maximum time to wait in milliseconds.
 * @return 0 on success, -1 on error, -2 on timeout.
 */
int mcp_cond_timedwait(mcp_cond_t* cond, mcp_mutex_t* mutex, uint32_t timeout_ms);

/**
 * @brief Wakes up one thread waiting on the condition variable.
 * @param cond Pointer to the condition variable to signal.
 * @return 0 on success, non-zero on error.
 */
int mcp_cond_signal(mcp_cond_t* cond);

/**
 * @brief Wakes up all threads waiting on the condition variable.
 * @param cond Pointer to the condition variable to broadcast.
 * @return 0 on success, non-zero on error.
 */
int mcp_cond_broadcast(mcp_cond_t* cond);

// --- Thread ---

/** @brief Platform-independent thread handle type. */
#ifdef _WIN32
typedef void* mcp_thread_t; // Use HANDLE (void*)
#else
// 在 macOS 上，pthread_t 是一个指针类型，而不是 unsigned long
// 使用直接的 pthread_t 类型可以避免类型转换问题
#include <pthread.h>
typedef pthread_t mcp_thread_t; // 直接使用 pthread_t 类型
#endif

/** @brief Thread function signature. */
typedef void* (*mcp_thread_func_t)(void* arg);

/**
 * @brief Creates and starts a new thread.
 * @param thread_handle Pointer to store the handle of the created thread.
 * @param start_routine The function the new thread will execute.
 * @param arg The argument to pass to the start_routine.
 * @return 0 on success, non-zero on failure.
 */
int mcp_thread_create(mcp_thread_t* thread_handle, mcp_thread_func_t start_routine, void* arg);

/**
 * @brief Waits for a specific thread to terminate.
 * @param thread_handle The handle of the thread to wait for.
 * @param retval Pointer to store the return value of the thread function (optional, can be NULL).
 *               Note: Retrieving the actual void* return value is not directly supported on Windows via this abstraction.
 * @return 0 on success, non-zero on failure.
 */
int mcp_thread_join(mcp_thread_t thread_handle, void** retval);

/**
 * @brief Yields the execution of the current thread, allowing other threads to run.
 */
void mcp_thread_yield(void);

#ifdef __cplusplus
}
#endif

#endif // MCP_SYNC_H
