#ifndef MCP_THREAD_LOCAL_H
#define MCP_THREAD_LOCAL_H

#include "mcp_arena.h"
#include "mcp_object_cache.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the thread-local arena with the given initial size.
 *
 * This function should be called once per thread that needs to use the
 * thread-local arena before calling mcp_arena_alloc with a NULL arena pointer.
 *
 * @param initial_size The initial size in bytes for the arena's buffer.
 *                     If 0, MCP_ARENA_DEFAULT_SIZE will be used.
 * @return 0 on success, -1 on failure (e.g., allocation error, already initialized).
 */
int mcp_arena_init_current_thread(size_t initial_size);

/**
 * @brief Get the current thread's arena.
 *
 * Returns the arena previously initialized by mcp_arena_init_current_thread().
 *
 * @return Pointer to the thread-local arena, or NULL if not initialized or
 *         if thread-local storage is not supported/failed.
 */
mcp_arena_t* mcp_arena_get_current(void);

/**
 * @brief Reset the current thread's arena.
 *
 * Makes all memory previously allocated from the thread-local arena available
 * again without freeing the underlying blocks. Useful for reusing the arena
 * within a thread's lifecycle (e.g., per request).
 */
void mcp_arena_reset_current_thread(void);

/**
 * @brief Destroy the current thread's arena.
 *
 * Frees all memory blocks associated with the thread-local arena. This should
 * be called when the thread is exiting or no longer needs the arena to prevent
 * memory leaks.
 */
void mcp_arena_destroy_current_thread(void);

/**
 * @brief Initialize the thread-local object cache system.
 *
 * This function should be called once per thread that needs to use the
 * thread-local object cache system.
 *
 * @return true on success, false on failure.
 */
bool mcp_thread_cache_init_current_thread(void);

/**
 * @brief Initialize a specific object cache type for the current thread.
 *
 * @param type The type of objects to cache.
 * @param config Configuration for the cache, or NULL for default configuration.
 * @return true on success, false on failure.
 */
bool mcp_thread_cache_init_type(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config);

/**
 * @brief Allocate an object from the thread-local cache.
 *
 * @param type The type of object to allocate.
 * @param size Size of the object to allocate.
 * @return Pointer to the allocated object, or NULL if allocation failed.
 */
void* mcp_thread_cache_alloc_object(mcp_object_cache_type_t type, size_t size);

/**
 * @brief Free an object to the thread-local cache.
 *
 * @param type The type of object to free.
 * @param ptr Pointer to the object to free.
 * @param size Size of the object (optional, can be 0 if unknown).
 */
void mcp_thread_cache_free_object(mcp_object_cache_type_t type, void* ptr, size_t size);

/**
 * @brief Get statistics for a thread-local object cache.
 *
 * @param type The type of object cache to get statistics for.
 * @param stats Pointer to a statistics structure to fill.
 * @return true if statistics were successfully retrieved, false otherwise.
 */
bool mcp_thread_cache_get_object_stats(mcp_object_cache_type_t type, mcp_object_cache_stats_t* stats);

/**
 * @brief Flush a thread-local object cache.
 *
 * @param type The type of object cache to flush.
 */
void mcp_thread_cache_flush_object_cache(mcp_object_cache_type_t type);

/**
 * @brief Clean up all thread-local object caches for the current thread.
 *
 * This should be called when the thread is exiting or no longer needs the
 * thread-local object caches to prevent memory leaks.
 */
void mcp_thread_cache_cleanup_current_thread(void);

/**
 * @brief Get statistics about thread-local storage usage.
 *
 * This function provides detailed statistics about the usage of thread-local
 * storage, including thread ID, arena allocations, resets, and cache operations.
 *
 * @param thread_id Thread identifier
 * @param arena_allocations Number of arena allocations
 * @param arena_resets Number of arena resets
 * @param arena_destroys Number of arena destroys
 * @param cache_allocations Number of cache allocations
 * @param cache_frees Number of cache frees
 * @param cache_hits Number of cache hits
 * @param cache_misses Number of cache misses
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_thread_local_get_stats(
    unsigned long* thread_id,
    size_t* arena_allocations,
    size_t* arena_resets,
    size_t* arena_destroys,
    size_t* cache_allocations,
    size_t* cache_frees,
    size_t* cache_hits,
    size_t* cache_misses
);

/**
 * @brief Check if thread-local storage is initialized.
 *
 * This function checks if the thread-local storage has been initialized
 * for the current thread.
 *
 * @return true if thread-local storage is initialized, false otherwise
 */
bool mcp_thread_local_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_THREAD_LOCAL_H */
