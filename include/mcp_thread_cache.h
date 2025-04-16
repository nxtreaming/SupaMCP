#ifndef MCP_THREAD_CACHE_H
#define MCP_THREAD_CACHE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread-local cache configuration
 */
typedef struct {
    size_t small_cache_size;     /**< Maximum number of small blocks in thread-local cache */
    size_t medium_cache_size;    /**< Maximum number of medium blocks in thread-local cache */
    size_t large_cache_size;     /**< Maximum number of large blocks in thread-local cache */
    bool adaptive_sizing;        /**< Whether to enable adaptive cache sizing */
    double growth_threshold;     /**< Hit ratio threshold for growing cache (0.0-1.0) */
    double shrink_threshold;     /**< Hit ratio threshold for shrinking cache (0.0-1.0) */
    size_t min_cache_size;       /**< Minimum cache size for adaptive sizing */
    size_t max_cache_size;       /**< Maximum cache size for adaptive sizing */
} mcp_thread_cache_config_t;

/**
 * @brief Thread-local cache statistics
 */
typedef struct {
    // Cache occupancy
    size_t small_cache_count;    /**< Number of small blocks in thread-local cache */
    size_t medium_cache_count;   /**< Number of medium blocks in thread-local cache */
    size_t large_cache_count;    /**< Number of large blocks in thread-local cache */

    // Cache configuration
    size_t small_max_size;       /**< Maximum number of small blocks in thread-local cache */
    size_t medium_max_size;      /**< Maximum number of medium blocks in thread-local cache */
    size_t large_max_size;       /**< Maximum number of large blocks in thread-local cache */
    bool adaptive_sizing;        /**< Whether adaptive cache sizing is enabled */

    // Hit/miss statistics
    size_t cache_hits;           /**< Number of cache hits */
    size_t misses_small;         /**< Number of cache misses for small blocks */
    size_t misses_medium;        /**< Number of cache misses for medium blocks */
    size_t misses_large;         /**< Number of cache misses for large blocks */
    size_t misses_other;         /**< Number of cache misses for other block sizes */
    size_t cache_flushes;        /**< Number of cache flushes */
    double hit_ratio;           /**< Cache hit ratio (0.0-1.0) */
} mcp_thread_cache_stats_t;

/**
 * @brief Initializes the thread-local cache for the current thread with default settings
 *
 * @return true if initialization was successful, false otherwise
 */
bool mcp_thread_cache_init(void);

/**
 * @brief Initializes the thread-local cache for the current thread with custom configuration
 *
 * @param config Pointer to a configuration structure
 * @return true if initialization was successful, false otherwise
 */
bool mcp_thread_cache_init_with_config(const mcp_thread_cache_config_t* config);

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
 * @param size Size of the memory block (optional, can be 0 if unknown)
 * @note Safe to call with NULL
 * @note If size is 0, the function will try to determine the size from the block header
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

/**
 * @brief Configures the thread-local cache with new settings
 *
 * @param config Pointer to a configuration structure
 * @return true if configuration was successful, false otherwise
 */
bool mcp_thread_cache_configure(const mcp_thread_cache_config_t* config);

/**
 * @brief Enables or disables adaptive cache sizing
 *
 * @param enable Whether to enable adaptive sizing
 * @return true if the operation was successful, false otherwise
 */
bool mcp_thread_cache_enable_adaptive_sizing(bool enable);

/**
 * @brief Adjusts the cache size based on hit ratio statistics
 *
 * This function is called automatically when adaptive sizing is enabled,
 * but can also be called manually to trigger an immediate adjustment.
 *
 * @return true if the adjustment was successful, false otherwise
 */
bool mcp_thread_cache_adjust_size(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_THREAD_CACHE_H */
