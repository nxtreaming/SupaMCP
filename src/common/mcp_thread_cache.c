#include "mcp_thread_cache.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Default cache sizes
#define DEFAULT_SMALL_CACHE_SIZE 16
#define DEFAULT_MEDIUM_CACHE_SIZE 8
#define DEFAULT_LARGE_CACHE_SIZE 4

// Adaptive sizing constants
#define MIN_CACHE_SIZE 4
#define MAX_CACHE_SIZE 64
#define DEFAULT_GROWTH_THRESHOLD 0.8  // Grow cache if hit ratio > 80%
#define DEFAULT_SHRINK_THRESHOLD 0.3  // Shrink cache if hit ratio < 30%

// Thread-local cache structures
#ifdef _WIN32
__declspec(thread) static void* tls_small_cache[MAX_CACHE_SIZE] = {NULL};
__declspec(thread) static void* tls_medium_cache[MAX_CACHE_SIZE] = {NULL};
__declspec(thread) static void* tls_large_cache[MAX_CACHE_SIZE] = {NULL};
__declspec(thread) static size_t tls_small_count = 0;
__declspec(thread) static size_t tls_medium_count = 0;
__declspec(thread) static size_t tls_large_count = 0;
__declspec(thread) static size_t tls_cache_hits = 0;
__declspec(thread) static size_t tls_misses_small = 0;
__declspec(thread) static size_t tls_misses_medium = 0;
__declspec(thread) static size_t tls_misses_large = 0;
__declspec(thread) static size_t tls_misses_other = 0;
__declspec(thread) static size_t tls_cache_flushes = 0;
__declspec(thread) static bool tls_cache_initialized = false;

// Cache configuration
__declspec(thread) static size_t tls_small_max_size = DEFAULT_SMALL_CACHE_SIZE;
__declspec(thread) static size_t tls_medium_max_size = DEFAULT_MEDIUM_CACHE_SIZE;
__declspec(thread) static size_t tls_large_max_size = DEFAULT_LARGE_CACHE_SIZE;
__declspec(thread) static bool tls_adaptive_sizing = false;
__declspec(thread) static double tls_growth_threshold = DEFAULT_GROWTH_THRESHOLD;
__declspec(thread) static double tls_shrink_threshold = DEFAULT_SHRINK_THRESHOLD;
__declspec(thread) static size_t tls_min_cache_size = MIN_CACHE_SIZE;
__declspec(thread) static size_t tls_max_cache_size = MAX_CACHE_SIZE;
__declspec(thread) static size_t tls_adjustment_interval = 100; // Adjust every 100 operations
__declspec(thread) static size_t tls_operations_since_adjustment = 0;
#else
__thread static void* tls_small_cache[MAX_CACHE_SIZE] = {NULL};
__thread static void* tls_medium_cache[MAX_CACHE_SIZE] = {NULL};
__thread static void* tls_large_cache[MAX_CACHE_SIZE] = {NULL};
__thread static size_t tls_small_count = 0;
__thread static size_t tls_medium_count = 0;
__thread static size_t tls_large_count = 0;
__thread static size_t tls_cache_hits = 0;
__thread static size_t tls_misses_small = 0;
__thread static size_t tls_misses_medium = 0;
__thread static size_t tls_misses_large = 0;
__thread static size_t tls_misses_other = 0;
__thread static size_t tls_cache_flushes = 0;
__thread static bool tls_cache_initialized = false;

// Cache configuration
__thread static size_t tls_small_max_size = DEFAULT_SMALL_CACHE_SIZE;
__thread static size_t tls_medium_max_size = DEFAULT_MEDIUM_CACHE_SIZE;
__thread static size_t tls_large_max_size = DEFAULT_LARGE_CACHE_SIZE;
__thread static bool tls_adaptive_sizing = false;
__thread static double tls_growth_threshold = DEFAULT_GROWTH_THRESHOLD;
__thread static double tls_shrink_threshold = DEFAULT_SHRINK_THRESHOLD;
__thread static size_t tls_min_cache_size = MIN_CACHE_SIZE;
__thread static size_t tls_max_cache_size = MAX_CACHE_SIZE;
__thread static size_t tls_adjustment_interval = 100; // Adjust every 100 operations
__thread static size_t tls_operations_since_adjustment = 0;
#endif

bool mcp_thread_cache_init(void) {
    if (tls_cache_initialized) {
        return true;
    }

    // Check if the memory pool system is initialized
    // We don't initialize it here to avoid circular dependencies
    if (!mcp_memory_pool_system_is_initialized()) {
        mcp_log_warn("Thread cache initialized but memory pool system is not initialized");
        // Continue anyway, we'll fall back to malloc/free when needed
    }

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

    tls_cache_initialized = true;
    mcp_log_debug("Thread-local cache initialized with default configuration");

    return true;
}

bool mcp_thread_cache_init_with_config(const mcp_thread_cache_config_t* config) {
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
    }

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
    mcp_log_debug("Thread-local cache initialized with custom configuration");

    return true;
}

void mcp_thread_cache_cleanup(void) {
    if (!tls_cache_initialized) {
        return;
    }

    // Flush all caches
    mcp_thread_cache_flush();

    tls_cache_initialized = false;
    mcp_log_debug("Thread-local cache cleaned up");
}

bool mcp_thread_cache_is_initialized(void) {
    return tls_cache_initialized;
}

void* mcp_thread_cache_alloc(size_t size) {
    // If thread cache is not initialized, fall back to direct malloc to avoid circular dependency
    if (!tls_cache_initialized) {
        return malloc(size);
    }

    // Increment operation counter for adaptive sizing
    tls_operations_since_adjustment++;

    // Determine which cache to use based on size
    if (size <= SMALL_BLOCK_SIZE) {
        if (tls_small_count > 0) {
            tls_cache_hits++;
            return tls_small_cache[--tls_small_count];
        }
        tls_misses_small++;
    } else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count > 0) {
            tls_cache_hits++;
            return tls_medium_cache[--tls_medium_count];
        }
        tls_misses_medium++;
    } else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count > 0) {
            tls_cache_hits++;
            return tls_large_cache[--tls_large_count];
        }
        tls_misses_large++;
    } else {
        tls_misses_other++;
    }

    // Check if we need to adjust cache sizes
    if (tls_adaptive_sizing && tls_operations_since_adjustment >= tls_adjustment_interval) {
        mcp_thread_cache_adjust_size();
    }

    // Cache miss, allocate from the pool if initialized, otherwise use malloc
    if (mcp_memory_pool_system_is_initialized()) {
        return mcp_pool_alloc(size);
    } else {
        // Fall back to malloc if memory pool system is not initialized
        return malloc(size);
    }
}

void mcp_thread_cache_free(void* ptr, size_t size) {
    if (!ptr) return;

    // If thread cache is not initialized, determine if this is a pool block
    if (!tls_cache_initialized) {
        // Check if this is a pool-allocated block
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

    // Determine which cache to use based on size
    if (size <= SMALL_BLOCK_SIZE) {
        if (tls_small_count < tls_small_max_size) {
            tls_small_cache[tls_small_count++] = ptr;
            return;
        }
    } else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count < tls_medium_max_size) {
            tls_medium_cache[tls_medium_count++] = ptr;
            return;
        }
    } else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count < tls_large_max_size) {
            tls_large_cache[tls_large_count++] = ptr;
            return;
        }
    }

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

bool mcp_thread_cache_get_stats(mcp_thread_cache_stats_t* stats) {
    if (!stats || !tls_cache_initialized) {
        return false;
    }

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

void mcp_thread_cache_flush(void) {
    if (!tls_cache_initialized) {
        return;
    }

    // Flush small cache
    for (size_t i = 0; i < tls_small_count; i++) {
        // Check if this is a pool-allocated block
        size_t block_size = mcp_pool_get_block_size(tls_small_cache[i]);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(tls_small_cache[i]);
        } else {
            // It's a malloc-allocated block, use free
            free(tls_small_cache[i]);
        }
        tls_small_cache[i] = NULL;
    }
    tls_small_count = 0;

    // Flush medium cache
    for (size_t i = 0; i < tls_medium_count; i++) {
        // Check if this is a pool-allocated block
        size_t block_size = mcp_pool_get_block_size(tls_medium_cache[i]);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(tls_medium_cache[i]);
        } else {
            // It's a malloc-allocated block, use free
            free(tls_medium_cache[i]);
        }
        tls_medium_cache[i] = NULL;
    }
    tls_medium_count = 0;

    // Flush large cache
    for (size_t i = 0; i < tls_large_count; i++) {
        // Check if this is a pool-allocated block
        size_t block_size = mcp_pool_get_block_size(tls_large_cache[i]);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(tls_large_cache[i]);
        } else {
            // It's a malloc-allocated block, use free
            free(tls_large_cache[i]);
        }
        tls_large_cache[i] = NULL;
    }
    tls_large_count = 0;

    tls_cache_flushes++;
    mcp_log_debug("Thread-local cache flushed");
}
