#ifndef MCP_SYNC_H
#define MCP_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque mutex type. Can be either standard or recursive. */
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

/**
 * @brief Creates a new recursive mutex.
 * @return Pointer to the created recursive mutex, or NULL on failure.
 * @note Caller is responsible for destroying the mutex using mcp_mutex_destroy().
 * @note A recursive mutex can be locked multiple times by the same thread without deadlocking.
 */
mcp_mutex_t* mcp_recursive_mutex_create(void);

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

/** @brief Platform-independent thread handle type. */
#ifdef _WIN32
typedef void* mcp_thread_t;
#else
#include <pthread.h>
typedef pthread_t mcp_thread_t;
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

/**
 * @brief Gets the ID of the current thread.
 * @return The ID of the current thread as an unsigned long.
 */
unsigned long mcp_get_thread_id(void);

/** @brief Opaque spinlock type. */
typedef struct mcp_spinlock_s mcp_spinlock_t;

/**
 * @brief Creates a new spinlock.
 * @return Pointer to the created spinlock, or NULL on failure.
 * @note Caller is responsible for destroying the spinlock using mcp_spinlock_destroy().
 */
mcp_spinlock_t* mcp_spinlock_create(void);

/**
 * @brief Destroys a spinlock and frees associated resources.
 * @param spinlock Pointer to the spinlock to destroy. If NULL, the function does nothing.
 */
void mcp_spinlock_destroy(mcp_spinlock_t* spinlock);

/**
 * @brief Acquires the spinlock. Spins (busy-waits) if the spinlock is already locked.
 * @param spinlock Pointer to the spinlock to acquire.
 * @return 0 on success, non-zero on error.
 * @note Spinlocks should only be held for very short durations to avoid wasting CPU time.
 */
int mcp_spinlock_lock(mcp_spinlock_t* spinlock);

/**
 * @brief Tries to acquire the spinlock without spinning.
 * @param spinlock Pointer to the spinlock to acquire.
 * @return 0 if the spinlock was acquired, 1 if the spinlock was already locked, negative value on error.
 */
int mcp_spinlock_trylock(mcp_spinlock_t* spinlock);

/**
 * @brief Releases the spinlock.
 * @param spinlock Pointer to the spinlock to release.
 * @return 0 on success, non-zero on error.
 */
int mcp_spinlock_unlock(mcp_spinlock_t* spinlock);

/** @brief Opaque thread local storage key type. */
typedef struct mcp_tls_key_s mcp_tls_key_t;

/**
 * @brief Creates a new thread local storage key.
 * @param destructor Optional destructor function that is called when a thread exits,
 *                   to free the thread-specific data. Can be NULL.
 * @return Pointer to the created TLS key, or NULL on failure.
 * @note Caller is responsible for destroying the key using mcp_tls_key_destroy().
 */
mcp_tls_key_t* mcp_tls_key_create(void (*destructor)(void*));

/**
 * @brief Destroys a thread local storage key.
 * @param key Pointer to the TLS key to destroy. If NULL, the function does nothing.
 * @note This does not free the thread-specific data associated with the key.
 *       The destructor is called for each thread's data when the thread exits.
 */
void mcp_tls_key_destroy(mcp_tls_key_t* key);

/**
 * @brief Sets the thread-specific data associated with a key.
 * @param key Pointer to the TLS key.
 * @param value Pointer to the thread-specific data to associate with the key.
 * @return 0 on success, non-zero on error.
 */
int mcp_tls_set(mcp_tls_key_t* key, void* value);

/**
 * @brief Gets the thread-specific data associated with a key.
 * @param key Pointer to the TLS key.
 * @return Pointer to the thread-specific data, or NULL if no data is associated
 *         with the key or an error occurred.
 */
void* mcp_tls_get(mcp_tls_key_t* key);

#ifdef __cplusplus
}
#endif

#endif // MCP_SYNC_H
