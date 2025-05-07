#include "mcp_cache_aligned.h"
#include "mcp_thread_cache.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

// Default cache sizes
#define DEFAULT_SMALL_CACHE_SIZE 16
#define DEFAULT_MEDIUM_CACHE_SIZE 8
#define DEFAULT_LARGE_CACHE_SIZE 4

// Adaptive sizing constants
#define MIN_CACHE_SIZE 4
#define MAX_CACHE_SIZE 64
#define DEFAULT_GROWTH_THRESHOLD 0.8  // Grow cache if hit ratio > 80%
#define DEFAULT_SHRINK_THRESHOLD 0.3  // Shrink cache if hit ratio < 30%
#define DEFAULT_ADJUSTMENT_INTERVAL 100  // Adjust every 100 operations

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4324) // Disable warning for structure padding
#endif
// Thread-local cache state structure with cache line alignment to prevent false sharing
typedef MCP_CACHE_ALIGNED struct {
    // Thread identification
    unsigned long thread_id;                  // Thread identifier for debugging and analysis

    // Cache arrays
    void* small_cache[MAX_CACHE_SIZE];        // Small block cache
    void* medium_cache[MAX_CACHE_SIZE];       // Medium block cache
    void* large_cache[MAX_CACHE_SIZE];        // Large block cache

    // Cache counters
    size_t small_count;                       // Number of small blocks in cache
    size_t medium_count;                      // Number of medium blocks in cache
    size_t large_count;                       // Number of large blocks in cache

    // Cache configuration
    size_t small_max_size;                    // Maximum small cache size
    size_t medium_max_size;                   // Maximum medium cache size
    size_t large_max_size;                    // Maximum large cache size
    size_t min_cache_size;                    // Minimum cache size for adaptive sizing
    size_t max_cache_size;                    // Maximum cache size for adaptive sizing
    size_t adjustment_interval;               // Operations between adaptive size adjustments
    size_t operations_since_adjustment;       // Counter for adaptive sizing

    // Statistics
    size_t cache_hits;                        // Number of cache hits
    size_t misses_small;                      // Number of small block misses
    size_t misses_medium;                     // Number of medium block misses
    size_t misses_large;                      // Number of large block misses
    size_t misses_other;                      // Number of other size misses
    size_t cache_flushes;                     // Number of cache flushes

    // Configuration flags
    bool initialized;                         // Whether cache is initialized
    bool adaptive_sizing;                     // Whether adaptive sizing is enabled

    // Adaptive sizing thresholds
    double growth_threshold;                  // Hit ratio threshold for growing cache
    double shrink_threshold;                  // Hit ratio threshold for shrinking cache

    // Padding to ensure the structure size is a multiple of the cache line size
    char padding[MCP_CACHE_LINE_SIZE - (
        sizeof(unsigned long) +               // thread_id
        3 * sizeof(void*[MAX_CACHE_SIZE]) +   // cache arrays
        13 * sizeof(size_t) +                 // counters and configuration
        2 * sizeof(bool) +                    // flags
        2 * sizeof(double)                    // thresholds
    ) % MCP_CACHE_LINE_SIZE];
} thread_cache_state_t;
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

// Function to get current thread ID in a platform-independent way
static unsigned long get_current_thread_id(void) {
#ifdef _WIN32
    return (unsigned long)GetCurrentThreadId();
#else
    return (unsigned long)pthread_self();
#endif
}

// Thread-local cache state
#ifdef _WIN32
__declspec(thread) static thread_cache_state_t tls_cache_state = {0};
#else
__thread static thread_cache_state_t tls_cache_state = {0};
#endif

// Convenience macros to access the thread-local state
#define tls_small_cache          tls_cache_state.small_cache
#define tls_medium_cache         tls_cache_state.medium_cache
#define tls_large_cache          tls_cache_state.large_cache
#define tls_small_count          tls_cache_state.small_count
#define tls_medium_count         tls_cache_state.medium_count
#define tls_large_count          tls_cache_state.large_count
#define tls_small_max_size       tls_cache_state.small_max_size
#define tls_medium_max_size      tls_cache_state.medium_max_size
#define tls_large_max_size       tls_cache_state.large_max_size
#define tls_min_cache_size       tls_cache_state.min_cache_size
#define tls_max_cache_size       tls_cache_state.max_cache_size
#define tls_adjustment_interval  tls_cache_state.adjustment_interval
#define tls_operations_since_adjustment tls_cache_state.operations_since_adjustment
#define tls_cache_hits           tls_cache_state.cache_hits
#define tls_misses_small         tls_cache_state.misses_small
#define tls_misses_medium        tls_cache_state.misses_medium
#define tls_misses_large         tls_cache_state.misses_large
#define tls_misses_other         tls_cache_state.misses_other
#define tls_cache_flushes        tls_cache_state.cache_flushes
#define tls_cache_initialized    tls_cache_state.initialized
#define tls_adaptive_sizing      tls_cache_state.adaptive_sizing
#define tls_growth_threshold     tls_cache_state.growth_threshold
#define tls_shrink_threshold     tls_cache_state.shrink_threshold
#define tls_thread_id            tls_cache_state.thread_id

/**
 * @brief Initializes the thread-local cache for the current thread with default settings
 *
 * This function is optimized to:
 * 1. Set thread ID for better debugging and tracking
 * 2. Initialize all cache arrays to NULL
 * 3. Set default configuration values
 *
 * @return true if initialization was successful, false otherwise
 */
bool mcp_thread_cache_init(void) {
    // Fast path for already initialized cache
    if (tls_cache_initialized) {
        return true;
    }

    // Check if the memory pool system is initialized
    // We don't initialize it here to avoid circular dependencies
    if (!mcp_memory_pool_system_is_initialized()) {
        mcp_log_warn("Thread cache initialized but memory pool system is not initialized");
        // Continue anyway, we'll fall back to malloc/free when needed
    }

    // Set thread ID for tracking
    tls_thread_id = get_current_thread_id();

    // Initialize cache arrays to NULL
    memset(tls_small_cache, 0, sizeof(tls_small_cache));
    memset(tls_medium_cache, 0, sizeof(tls_medium_cache));
    memset(tls_large_cache, 0, sizeof(tls_large_cache));

    // Reset cache counters
    tls_small_count = 0;
    tls_medium_count = 0;
    tls_large_count = 0;
    tls_cache_hits = 0;
    tls_misses_small = 0;
    tls_misses_medium = 0;
    tls_misses_large = 0;
    tls_misses_other = 0;
    tls_cache_flushes = 0;
    tls_operations_since_adjustment = 0;

    // Set default configuration
    tls_small_max_size = DEFAULT_SMALL_CACHE_SIZE;
    tls_medium_max_size = DEFAULT_MEDIUM_CACHE_SIZE;
    tls_large_max_size = DEFAULT_LARGE_CACHE_SIZE;
    tls_adaptive_sizing = false;
    tls_growth_threshold = DEFAULT_GROWTH_THRESHOLD;
    tls_shrink_threshold = DEFAULT_SHRINK_THRESHOLD;
    tls_min_cache_size = MIN_CACHE_SIZE;
    tls_max_cache_size = MAX_CACHE_SIZE;
    tls_adjustment_interval = DEFAULT_ADJUSTMENT_INTERVAL;

    tls_cache_initialized = true;
    mcp_log_debug("Thread-local cache initialized with default configuration for thread %lu", tls_thread_id);

    return true;
}

/**
 * @brief Initializes the thread-local cache for the current thread with custom configuration
 *
 * This function is optimized to:
 * 1. Handle common cases efficiently (already initialized, NULL config)
 * 2. Validate and clamp configuration values to safe ranges
 * 3. Set thread ID for better debugging and tracking
 *
 * @param config Pointer to a configuration structure
 * @return true if initialization was successful, false otherwise
 */
bool mcp_thread_cache_init_with_config(const mcp_thread_cache_config_t* config) {
    // Handle common cases
    if (tls_cache_initialized) {
        // Already initialized, reconfigure instead
        return mcp_thread_cache_configure(config);
    }

    if (!config) {
        // Fall back to default initialization
        return mcp_thread_cache_init();
    }

    // Check if the memory pool system is initialized
    if (!mcp_memory_pool_system_is_initialized()) {
        mcp_log_warn("Thread cache initialized but memory pool system is not initialized");
        // Continue anyway, we'll fall back to malloc/free when needed
    }

    // Set thread ID for tracking
    tls_thread_id = get_current_thread_id();

    // Initialize cache arrays to NULL
    memset(tls_small_cache, 0, sizeof(tls_small_cache));
    memset(tls_medium_cache, 0, sizeof(tls_medium_cache));
    memset(tls_large_cache, 0, sizeof(tls_large_cache));

    // Reset cache counters
    tls_small_count = 0;
    tls_medium_count = 0;
    tls_large_count = 0;
    tls_cache_hits = 0;
    tls_misses_small = 0;
    tls_misses_medium = 0;
    tls_misses_large = 0;
    tls_misses_other = 0;
    tls_cache_flushes = 0;
    tls_operations_since_adjustment = 0;

    // Apply configuration
    tls_small_max_size = config->small_cache_size;
    tls_medium_max_size = config->medium_cache_size;
    tls_large_max_size = config->large_cache_size;
    tls_adaptive_sizing = config->adaptive_sizing;
    tls_growth_threshold = config->growth_threshold;
    tls_shrink_threshold = config->shrink_threshold;
    tls_min_cache_size = config->min_cache_size;
    tls_max_cache_size = config->max_cache_size;
    tls_adjustment_interval = DEFAULT_ADJUSTMENT_INTERVAL; // Use default for this

    // Validate and clamp configuration values
    if (tls_small_max_size > MAX_CACHE_SIZE) tls_small_max_size = MAX_CACHE_SIZE;
    if (tls_medium_max_size > MAX_CACHE_SIZE) tls_medium_max_size = MAX_CACHE_SIZE;
    if (tls_large_max_size > MAX_CACHE_SIZE) tls_large_max_size = MAX_CACHE_SIZE;

    if (tls_small_max_size < MIN_CACHE_SIZE) tls_small_max_size = MIN_CACHE_SIZE;
    if (tls_medium_max_size < MIN_CACHE_SIZE) tls_medium_max_size = MIN_CACHE_SIZE;
    if (tls_large_max_size < MIN_CACHE_SIZE) tls_large_max_size = MIN_CACHE_SIZE;

    if (tls_growth_threshold < 0.0) tls_growth_threshold = 0.0;
    if (tls_growth_threshold > 1.0) tls_growth_threshold = 1.0;

    if (tls_shrink_threshold < 0.0) tls_shrink_threshold = 0.0;
    if (tls_shrink_threshold > 1.0) tls_shrink_threshold = 1.0;

    if (tls_min_cache_size < 1) tls_min_cache_size = 1;
    if (tls_max_cache_size < tls_min_cache_size) tls_max_cache_size = tls_min_cache_size;

    tls_cache_initialized = true;
    mcp_log_debug("Thread-local cache initialized with custom configuration for thread %lu", tls_thread_id);

    return true;
}

/**
 * @brief Cleans up the thread-local cache for the current thread
 *
 * This function is optimized to:
 * 1. Check if cache is initialized before attempting cleanup
 * 2. Flush all cached memory blocks back to their respective pools
 * 3. Log cleanup with thread ID for better tracking
 */
void mcp_thread_cache_cleanup(void) {
    // Fast path for uninitialized cache
    if (!tls_cache_initialized) {
        return;
    }

    // Get thread ID for logging (in case it wasn't set)
    if (tls_thread_id == 0) {
        tls_thread_id = get_current_thread_id();
    }

    // Flush all caches
    mcp_thread_cache_flush();

    // Log statistics before cleanup
    mcp_log_debug("Thread-local cache stats for thread %lu: hits=%zu, misses=%zu, hit ratio=%.2f%%",
                 tls_thread_id,
                 tls_cache_hits,
                 tls_misses_small + tls_misses_medium + tls_misses_large + tls_misses_other,
                 (tls_cache_hits * 100.0) / (tls_cache_hits + tls_misses_small + tls_misses_medium +
                                           tls_misses_large + tls_misses_other + 0.001));

    tls_cache_initialized = false;
    mcp_log_debug("Thread-local cache cleaned up for thread %lu", tls_thread_id);
}

bool mcp_thread_cache_is_initialized(void) {
    return tls_cache_initialized;
}

/**
 * @brief Allocates memory from the thread-local cache
 *
 * This function is optimized to:
 * 1. Provide fast paths for common cases
 * 2. Track statistics for performance monitoring
 * 3. Adaptively adjust cache sizes based on usage patterns
 *
 * @param size Size of memory to allocate (in bytes)
 * @return Pointer to the allocated memory, or NULL if allocation failed
 */
void* mcp_thread_cache_alloc(size_t size) {
    // Fast path for zero-size allocation
    if (size == 0) {
        return NULL;
    }

    // Fast path for uninitialized cache
    if (!tls_cache_initialized) {
        // Fall back to direct allocation
        if (mcp_memory_pool_system_is_initialized()) {
            return mcp_pool_alloc(size);
        } else {
            return malloc(size);
        }
    }

    // Increment operation counter for adaptive sizing
    tls_operations_since_adjustment++;

    // Fast path for small blocks (most common case)
    if (size <= SMALL_BLOCK_SIZE) {
        if (tls_small_count > 0) {
            tls_cache_hits++;
            return tls_small_cache[--tls_small_count];
        }
        tls_misses_small++;
    }
    // Medium blocks
    else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count > 0) {
            tls_cache_hits++;
            return tls_medium_cache[--tls_medium_count];
        }
        tls_misses_medium++;
    }
    // Large blocks
    else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count > 0) {
            tls_cache_hits++;
            return tls_large_cache[--tls_large_count];
        }
        tls_misses_large++;
    }
    // Oversized blocks
    else {
        tls_misses_other++;
    }

    // Check if we need to adjust cache sizes
    if (tls_adaptive_sizing && tls_operations_since_adjustment >= tls_adjustment_interval) {
        mcp_thread_cache_adjust_size();
    }

    // Cache miss, allocate from the pool if initialized, otherwise use malloc
    void* ptr;
    if (mcp_memory_pool_system_is_initialized()) {
        ptr = mcp_pool_alloc(size);
    } else {
        ptr = malloc(size);
    }

    // Log allocation for debugging in case of memory issues
    if (ptr == NULL) {
        mcp_log_error("Failed to allocate %zu bytes from thread cache (thread %lu)",
                     size, tls_thread_id);
    }

    return ptr;
}

/**
 * @brief Frees memory to the thread-local cache
 *
 * This function is optimized to:
 * 1. Provide fast paths for common cases
 * 2. Determine block size if not provided
 * 3. Cache blocks for future reuse when possible
 *
 * @param ptr Pointer to the memory to free
 * @param size Size of the memory block (optional, can be 0 if unknown)
 */
void mcp_thread_cache_free(void* ptr, size_t size) {
    // Fast path for NULL pointer
    if (!ptr) return;

    // Fast path for uninitialized cache
    if (!tls_cache_initialized) {
        // Determine if this is a pool-allocated block
        size_t block_size = mcp_pool_get_block_size(ptr);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(ptr);
        } else {
            // It's a malloc-allocated block, use free
            free(ptr);
        }
        return;
    }

    // Increment operation counter for adaptive sizing
    tls_operations_since_adjustment++;

    // If size is 0, try to determine it from the block
    if (size == 0) {
        size = mcp_pool_get_block_size(ptr);
        // If we still don't know the size, we can't cache it
        if (size == 0) {
            free(ptr);
            return;
        }
    }

    // Fast path for small blocks (most common case)
    if (size <= SMALL_BLOCK_SIZE) {
        if (tls_small_count < tls_small_max_size) {
            tls_small_cache[tls_small_count++] = ptr;
            return;
        }
    }
    // Medium blocks
    else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count < tls_medium_max_size) {
            tls_medium_cache[tls_medium_count++] = ptr;
            return;
        }
    }
    // Large blocks
    else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count < tls_large_max_size) {
            tls_large_cache[tls_large_count++] = ptr;
            return;
        }
    }
    // Note: Oversized blocks are never cached

    // Check if we need to adjust cache sizes
    if (tls_adaptive_sizing && tls_operations_since_adjustment >= tls_adjustment_interval) {
        mcp_thread_cache_adjust_size();
    }

    // Cache is full or size is too large, determine if this is a pool block
    size_t block_size = mcp_pool_get_block_size(ptr);
    if (block_size > 0) {
        // It's a pool-allocated block, return it to the pool
        mcp_pool_free(ptr);
    } else {
        // It's a malloc-allocated block, use free
        free(ptr);
    }
}

/**
 * @brief Gets statistics for the thread-local cache
 *
 * This function is optimized to:
 * 1. Validate input parameters
 * 2. Provide comprehensive statistics including thread ID
 * 3. Calculate derived metrics like hit ratio
 *
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_thread_cache_get_stats(mcp_thread_cache_stats_t* stats) {
    // Validate parameters
    if (!stats) {
        return false;
    }

    // Check if cache is initialized
    if (!tls_cache_initialized) {
        return false;
    }

    // Get thread ID if not already set
    if (tls_thread_id == 0) {
        tls_thread_id = get_current_thread_id();
    }

    // Thread identification
    stats->thread_id = tls_thread_id;

    // Cache occupancy
    stats->small_cache_count = tls_small_count;
    stats->medium_cache_count = tls_medium_count;
    stats->large_cache_count = tls_large_count;

    // Cache configuration
    stats->small_max_size = tls_small_max_size;
    stats->medium_max_size = tls_medium_max_size;
    stats->large_max_size = tls_large_max_size;
    stats->adaptive_sizing = tls_adaptive_sizing;

    // Hit/miss statistics
    stats->cache_hits = tls_cache_hits;
    stats->misses_small = tls_misses_small;
    stats->misses_medium = tls_misses_medium;
    stats->misses_large = tls_misses_large;
    stats->misses_other = tls_misses_other;
    stats->cache_flushes = tls_cache_flushes;

    // Calculate hit ratio
    size_t total_operations = stats->cache_hits + stats->misses_small +
                             stats->misses_medium + stats->misses_large +
                             stats->misses_other;
    if (total_operations > 0) {
        stats->hit_ratio = (double)stats->cache_hits / (double)total_operations;
    } else {
        stats->hit_ratio = 0.0;
    }

    // Calculate total misses
    stats->total_misses = stats->misses_small + stats->misses_medium +
                         stats->misses_large + stats->misses_other;

    // Calculate total operations
    stats->total_operations = total_operations;

    return true;
}

bool mcp_thread_cache_configure(const mcp_thread_cache_config_t* config) {
    if (!config || !tls_cache_initialized) {
        return false;
    }

    // Apply configuration
    tls_small_max_size = config->small_cache_size;
    tls_medium_max_size = config->medium_cache_size;
    tls_large_max_size = config->large_cache_size;
    tls_adaptive_sizing = config->adaptive_sizing;
    tls_growth_threshold = config->growth_threshold;
    tls_shrink_threshold = config->shrink_threshold;
    tls_min_cache_size = config->min_cache_size;
    tls_max_cache_size = config->max_cache_size;

    // Validate and clamp configuration values
    if (tls_small_max_size > MAX_CACHE_SIZE) tls_small_max_size = MAX_CACHE_SIZE;
    if (tls_medium_max_size > MAX_CACHE_SIZE) tls_medium_max_size = MAX_CACHE_SIZE;
    if (tls_large_max_size > MAX_CACHE_SIZE) tls_large_max_size = MAX_CACHE_SIZE;

    if (tls_small_max_size < MIN_CACHE_SIZE) tls_small_max_size = MIN_CACHE_SIZE;
    if (tls_medium_max_size < MIN_CACHE_SIZE) tls_medium_max_size = MIN_CACHE_SIZE;
    if (tls_large_max_size < MIN_CACHE_SIZE) tls_large_max_size = MIN_CACHE_SIZE;

    if (tls_growth_threshold < 0.0) tls_growth_threshold = 0.0;
    if (tls_growth_threshold > 1.0) tls_growth_threshold = 1.0;

    if (tls_shrink_threshold < 0.0) tls_shrink_threshold = 0.0;
    if (tls_shrink_threshold > 1.0) tls_shrink_threshold = 1.0;

    if (tls_min_cache_size < 1) tls_min_cache_size = 1;
    if (tls_max_cache_size < tls_min_cache_size) tls_max_cache_size = tls_min_cache_size;

    mcp_log_debug("Thread-local cache reconfigured");
    return true;
}

bool mcp_thread_cache_enable_adaptive_sizing(bool enable) {
    if (!tls_cache_initialized) {
        return false;
    }

    tls_adaptive_sizing = enable;
    mcp_log_debug("Thread-local cache adaptive sizing %s", enable ? "enabled" : "disabled");
    return true;
}

bool mcp_thread_cache_adjust_size(void) {
    if (!tls_cache_initialized) {
        return false;
    }

    // Reset operation counter
    tls_operations_since_adjustment = 0;

    // If adaptive sizing is disabled, do nothing
    if (!tls_adaptive_sizing) {
        return true;
    }

    // Calculate hit ratios for each cache size
    double small_hit_ratio = 0.0;
    double medium_hit_ratio = 0.0;
    double large_hit_ratio = 0.0;

    size_t small_ops = tls_cache_hits + tls_misses_small;
    size_t medium_ops = tls_cache_hits + tls_misses_medium;
    size_t large_ops = tls_cache_hits + tls_misses_large;

    if (small_ops > 0) {
        small_hit_ratio = (double)tls_cache_hits / (double)small_ops;
    }

    if (medium_ops > 0) {
        medium_hit_ratio = (double)tls_cache_hits / (double)medium_ops;
    }

    if (large_ops > 0) {
        large_hit_ratio = (double)tls_cache_hits / (double)large_ops;
    }

    // Adjust small cache size
    if (small_hit_ratio > tls_growth_threshold && tls_small_max_size < tls_max_cache_size) {
        tls_small_max_size = tls_small_max_size * 2;
        if (tls_small_max_size > tls_max_cache_size) {
            tls_small_max_size = tls_max_cache_size;
        }
        mcp_log_debug("Small cache size increased to %zu due to high hit ratio (%.2f)",
                      tls_small_max_size, small_hit_ratio);
    } else if (small_hit_ratio < tls_shrink_threshold && tls_small_max_size > tls_min_cache_size) {
        tls_small_max_size = tls_small_max_size / 2;
        if (tls_small_max_size < tls_min_cache_size) {
            tls_small_max_size = tls_min_cache_size;
        }
        mcp_log_debug("Small cache size decreased to %zu due to low hit ratio (%.2f)",
                      tls_small_max_size, small_hit_ratio);
    }

    // Adjust medium cache size
    if (medium_hit_ratio > tls_growth_threshold && tls_medium_max_size < tls_max_cache_size) {
        tls_medium_max_size = tls_medium_max_size * 2;
        if (tls_medium_max_size > tls_max_cache_size) {
            tls_medium_max_size = tls_max_cache_size;
        }
        mcp_log_debug("Medium cache size increased to %zu due to high hit ratio (%.2f)",
                      tls_medium_max_size, medium_hit_ratio);
    } else if (medium_hit_ratio < tls_shrink_threshold && tls_medium_max_size > tls_min_cache_size) {
        tls_medium_max_size = tls_medium_max_size / 2;
        if (tls_medium_max_size < tls_min_cache_size) {
            tls_medium_max_size = tls_min_cache_size;
        }
        mcp_log_debug("Medium cache size decreased to %zu due to low hit ratio (%.2f)",
                      tls_medium_max_size, medium_hit_ratio);
    }

    // Adjust large cache size
    if (large_hit_ratio > tls_growth_threshold && tls_large_max_size < tls_max_cache_size) {
        tls_large_max_size = tls_large_max_size * 2;
        if (tls_large_max_size > tls_max_cache_size) {
            tls_large_max_size = tls_max_cache_size;
        }
        mcp_log_debug("Large cache size increased to %zu due to high hit ratio (%.2f)",
                      tls_large_max_size, large_hit_ratio);
    } else if (large_hit_ratio < tls_shrink_threshold && tls_large_max_size > tls_min_cache_size) {
        tls_large_max_size = tls_large_max_size / 2;
        if (tls_large_max_size < tls_min_cache_size) {
            tls_large_max_size = tls_min_cache_size;
        }
        mcp_log_debug("Large cache size decreased to %zu due to low hit ratio (%.2f)",
                      tls_large_max_size, large_hit_ratio);
    }

    return true;
}

/**
 * @brief Helper function to flush a specific cache array
 *
 * @param cache Array of cached memory blocks
 * @param count Number of blocks in the cache
 * @return Number of blocks flushed
 */
static size_t flush_cache_array(void** cache, size_t count) {
    size_t flushed = 0;

    for (size_t i = 0; i < count; i++) {
        if (cache[i]) {
            // Check if this is a pool-allocated block
            size_t block_size = mcp_pool_get_block_size(cache[i]);
            if (block_size > 0) {
                // It's a pool-allocated block, return it to the pool
                mcp_pool_free(cache[i]);
            } else {
                // It's a malloc-allocated block, use free
                free(cache[i]);
            }
            cache[i] = NULL;
            flushed++;
        }
    }

    return flushed;
}

/**
 * @brief Flushes the thread-local cache, returning all blocks to the global pools
 *
 * This function is optimized to:
 * 1. Check if cache is initialized before attempting flush
 * 2. Use a helper function to reduce code duplication
 * 3. Track statistics for performance monitoring
 */
void mcp_thread_cache_flush(void) {
    // Fast path for uninitialized cache
    if (!tls_cache_initialized) {
        return;
    }

    // Get thread ID for logging (in case it wasn't set)
    if (tls_thread_id == 0) {
        tls_thread_id = get_current_thread_id();
    }

    // Flush all caches using the helper function
    size_t small_flushed = flush_cache_array(tls_small_cache, tls_small_count);
    size_t medium_flushed = flush_cache_array(tls_medium_cache, tls_medium_count);
    size_t large_flushed = flush_cache_array(tls_large_cache, tls_large_count);

    // Reset counters
    tls_small_count = 0;
    tls_medium_count = 0;
    tls_large_count = 0;

    // Update statistics
    tls_cache_flushes++;

    // Log flush operation
    mcp_log_debug("Thread-local cache flushed for thread %lu: %zu small, %zu medium, %zu large blocks",
                 tls_thread_id, small_flushed, medium_flushed, large_flushed);
}
