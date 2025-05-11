#ifndef MCP_OBJECT_POOL_H
#define MCP_OBJECT_POOL_H

#include "mcp_sync.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generic object pool structure.
 *
 * This structure manages a pool of pre-allocated objects of a fixed size.
 * It is designed to reduce the overhead of frequent memory allocation and deallocation.
 * The pool is thread-safe if compiled with thread support.
 */
typedef struct mcp_object_pool_s mcp_object_pool_t;

/**
 * @brief Creates a new object pool.
 *
 * Allocates and initializes an object pool for objects of the specified size.
 *
 * @param object_size The size of each object in the pool.
 * @param initial_capacity The initial number of objects to pre-allocate.
 * @param max_capacity The maximum number of objects the pool can hold (0 for unlimited, within memory constraints).
 * @return A pointer to the newly created object pool, or NULL on failure.
 */
mcp_object_pool_t* mcp_object_pool_create(size_t object_size, size_t initial_capacity, size_t max_capacity);

/**
 * @brief Destroys an object pool.
 *
 * Frees all resources associated with the object pool, including any remaining objects.
 *
 * @param pool A pointer to the object pool to destroy.
 */
void mcp_object_pool_destroy(mcp_object_pool_t* pool);

/**
 * @brief Acquires an object from the pool.
 *
 * Retrieves an available object from the pool. If the pool is empty and has not reached
 * its maximum capacity, a new object may be allocated.
 *
 * @param pool A pointer to the object pool.
 * @return A pointer to an acquired object, or NULL if the pool is empty and cannot grow.
 *         The returned object's memory is NOT initialized (it contains previous data or garbage).
 */
void* mcp_object_pool_acquire(mcp_object_pool_t* pool);

/**
 * @brief Releases an object back to the pool.
 *
 * Returns a previously acquired object to the pool, making it available for reuse.
 *
 * @param pool A pointer to the object pool.
 * @param obj A pointer to the object to release. Must have been previously acquired from this pool.
 * @return true if the object was successfully released, false otherwise (e.g., invalid object or pool).
 */
bool mcp_object_pool_release(mcp_object_pool_t* pool, void* obj);

/**
 * @brief Gets the number of currently available objects in the pool.
 *
 * @param pool A pointer to the object pool.
 * @return The number of free objects currently in the pool.
 */
size_t mcp_object_pool_get_free_count(mcp_object_pool_t* pool);

/**
 * @brief Gets the total number of objects managed by the pool (both free and acquired).
 *
 * @param pool A pointer to the object pool.
 * @return The total number of objects currently managed by the pool.
 */
size_t mcp_object_pool_get_total_count(mcp_object_pool_t* pool);

/**
 * @brief Gets detailed statistics about the object pool.
 *
 * @param pool A pointer to the object pool.
 * @param total_objects Pointer to store the total number of objects (can be NULL).
 * @param free_objects Pointer to store the number of free objects (can be NULL).
 * @param current_usage Pointer to store the current number of objects in use (can be NULL).
 * @param peak_usage Pointer to store the peak number of objects in use (can be NULL).
 * @return true if statistics were successfully retrieved, false otherwise.
 */
bool mcp_object_pool_get_stats(mcp_object_pool_t* pool,
                              size_t* total_objects,
                              size_t* free_objects,
                              size_t* current_usage,
                              size_t* peak_usage);

#ifdef __cplusplus
}
#endif

#endif // MCP_OBJECT_POOL_H
