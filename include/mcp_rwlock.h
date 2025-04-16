/**
 * @file mcp_rwlock.h
 * @brief Cross-platform read-write lock implementation
 *
 * This file provides a platform-independent interface for read-write locks,
 * which allow multiple readers to access a resource simultaneously while
 * ensuring exclusive access for writers.
 */

#ifndef MCP_RWLOCK_H
#define MCP_RWLOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read-write lock type
 *
 * This is an opaque type that represents a read-write lock.
 * The actual implementation depends on the platform.
 */
typedef struct mcp_rwlock_t mcp_rwlock_t;

/**
 * @brief Create a new read-write lock
 *
 * @return Pointer to the newly created read-write lock, or NULL if creation failed
 */
mcp_rwlock_t* mcp_rwlock_create(void);

/**
 * @brief Initialize a read-write lock
 *
 * @param rwlock Pointer to the read-write lock to initialize
 * @return true if initialization was successful, false otherwise
 */
bool mcp_rwlock_init(mcp_rwlock_t* rwlock);

/**
 * @brief Destroy a read-write lock
 *
 * @param rwlock Pointer to the read-write lock to destroy
 * @return true if destruction was successful, false otherwise
 */
bool mcp_rwlock_destroy(mcp_rwlock_t* rwlock);

/**
 * @brief Free a read-write lock created with mcp_rwlock_create
 *
 * @param rwlock Pointer to the read-write lock to free
 */
void mcp_rwlock_free(mcp_rwlock_t* rwlock);

/**
 * @brief Acquire a read lock
 *
 * Multiple threads can hold a read lock simultaneously, but no thread can
 * hold a write lock while any thread holds a read lock.
 *
 * @param rwlock Pointer to the read-write lock
 * @return true if the read lock was acquired, false otherwise
 */
bool mcp_rwlock_read_lock(mcp_rwlock_t* rwlock);

/**
 * @brief Try to acquire a read lock without blocking
 *
 * @param rwlock Pointer to the read-write lock
 * @return true if the read lock was acquired, false otherwise
 */
bool mcp_rwlock_try_read_lock(mcp_rwlock_t* rwlock);

/**
 * @brief Release a read lock
 *
 * @param rwlock Pointer to the read-write lock
 * @return true if the read lock was released, false otherwise
 */
bool mcp_rwlock_read_unlock(mcp_rwlock_t* rwlock);

/**
 * @brief Acquire a write lock
 *
 * Only one thread can hold a write lock at a time, and no thread can hold
 * a read lock while any thread holds a write lock.
 *
 * @param rwlock Pointer to the read-write lock
 * @return true if the write lock was acquired, false otherwise
 */
bool mcp_rwlock_write_lock(mcp_rwlock_t* rwlock);

/**
 * @brief Try to acquire a write lock without blocking
 *
 * @param rwlock Pointer to the read-write lock
 * @return true if the write lock was acquired, false otherwise
 */
bool mcp_rwlock_try_write_lock(mcp_rwlock_t* rwlock);

/**
 * @brief Release a write lock
 *
 * @param rwlock Pointer to the read-write lock
 * @return true if the write lock was released, false otherwise
 */
bool mcp_rwlock_write_unlock(mcp_rwlock_t* rwlock);

#ifdef __cplusplus
}
#endif

#endif /* MCP_RWLOCK_H */
