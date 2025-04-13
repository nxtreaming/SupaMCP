#ifndef MCP_THREAD_CACHE_H
#define MCP_THREAD_CACHE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread-local cache statistics
 */
typedef struct {
    size_t small_cache_count;    /**< Number of small blocks in thread-local cache */
    size_t medium_cache_count;   /**< Number of medium blocks in thread-local cache */
    size_t large_cache_count;    /**< Number of large blocks in thread-local cache */
    size_t cache_hits;           /**< Number of cache hits */
    size_t cache_misses;         /**< Number of cache misses */
    size_t cache_flushes;        /**< Number of cache flushes */
} mcp_thread_cache_stats_t;

/**
 * @brief Initializes the thread-local cache for the current thread
 *
 * @return true if initialization was successful, false otherwise
 */
bool mcp_thread_cache_init(void);

/**
 * @brief Cleans up the thread-local cache for the current thread
 */
void mcp_thread_cache_cleanup(void);

/**
 * @brief Allocates memory from the thread-local cache
 *
 * @param size Size of memory to allocate (in bytes)
 * @return Pointer to the allocated memory, or NULL if allocation failed
 * @note Falls back to mcp_pool_alloc if the cache is empty
 */
void* mcp_thread_cache_alloc(size_t size);

/**
 * @brief Frees memory to the thread-local cache
 *
 * @param ptr Pointer to the memory to free
 * @param size Size of the memory block (needed to determine the appropriate cache)
 * @note Safe to call with NULL
 */
void mcp_thread_cache_free(void* ptr, size_t size);

/**
 * @brief Gets statistics for the thread-local cache
 *
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_thread_cache_get_stats(mcp_thread_cache_stats_t* stats);

/**
 * @brief Flushes the thread-local cache, returning all blocks to the global pools
 */
void mcp_thread_cache_flush(void);

/**
 * @brief Checks if the thread-local cache is initialized for the current thread
 *
 * @return true if the thread-local cache is initialized, false otherwise
 */
bool mcp_thread_cache_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_THREAD_CACHE_H */
