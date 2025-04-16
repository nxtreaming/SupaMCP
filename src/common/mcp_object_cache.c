#include "mcp_object_cache.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Default cache sizes
#define DEFAULT_CACHE_SIZE 16
#define MIN_CACHE_SIZE 4
#define MAX_CACHE_SIZE 64
#define DEFAULT_GROWTH_THRESHOLD 0.8  // Grow cache if hit ratio > 80%
#define DEFAULT_SHRINK_THRESHOLD 0.3  // Shrink cache if hit ratio < 30%
#define DEFAULT_ADJUSTMENT_INTERVAL 100  // Adjust every 100 operations

// Object cache type names
static const char* object_cache_type_names[MCP_OBJECT_CACHE_TYPE_COUNT] = {
    "Generic",
    "String",
    "JSON",
    "Arena",
    "Buffer",
    "Custom1",
    "Custom2",
    "Custom3",
    "Custom4"
};

// Thread-local cache structures
#ifdef _WIN32
__declspec(thread) static bool tls_system_initialized = false;
__declspec(thread) static void* tls_object_caches[MCP_OBJECT_CACHE_TYPE_COUNT][MAX_CACHE_SIZE] = {{NULL}};
__declspec(thread) static size_t tls_object_counts[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_cache_hits[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_cache_misses[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_cache_flushes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static bool tls_cache_initialized[MCP_OBJECT_CACHE_TYPE_COUNT] = {false};

// Cache configuration
__declspec(thread) static size_t tls_max_sizes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static bool tls_adaptive_sizing[MCP_OBJECT_CACHE_TYPE_COUNT] = {false};
__declspec(thread) static double tls_growth_thresholds[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static double tls_shrink_thresholds[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_min_cache_sizes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_max_cache_sizes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_adjustment_intervals[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static size_t tls_operations_since_adjustment[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__declspec(thread) static void (*tls_constructors[MCP_OBJECT_CACHE_TYPE_COUNT])(void*) = {NULL};
__declspec(thread) static void (*tls_destructors[MCP_OBJECT_CACHE_TYPE_COUNT])(void*) = {NULL};
#else
__thread static bool tls_system_initialized = false;
__thread static void* tls_object_caches[MCP_OBJECT_CACHE_TYPE_COUNT][MAX_CACHE_SIZE] = {{NULL}};
__thread static size_t tls_object_counts[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_cache_hits[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_cache_misses[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_cache_flushes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static bool tls_cache_initialized[MCP_OBJECT_CACHE_TYPE_COUNT] = {false};

// Cache configuration
__thread static size_t tls_max_sizes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static bool tls_adaptive_sizing[MCP_OBJECT_CACHE_TYPE_COUNT] = {false};
__thread static double tls_growth_thresholds[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static double tls_shrink_thresholds[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_min_cache_sizes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_max_cache_sizes[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_adjustment_intervals[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static size_t tls_operations_since_adjustment[MCP_OBJECT_CACHE_TYPE_COUNT] = {0};
__thread static void (*tls_constructors[MCP_OBJECT_CACHE_TYPE_COUNT])(void*) = {NULL};
__thread static void (*tls_destructors[MCP_OBJECT_CACHE_TYPE_COUNT])(void*) = {NULL};
#endif

// Forward declarations
static bool adjust_cache_size(mcp_object_cache_type_t type);
static void apply_default_config(mcp_object_cache_type_t type);

bool mcp_object_cache_system_init(void) {
    if (tls_system_initialized) {
        return true;
    }

    // Check if the memory pool system is initialized
    if (!mcp_memory_pool_system_is_initialized()) {
        mcp_log_warn("Object cache system initialized but memory pool system is not initialized");
        // Continue anyway, we'll fall back to malloc/free when needed
    }

    // Initialize all cache types with default configuration
    for (int i = 0; i < MCP_OBJECT_CACHE_TYPE_COUNT; i++) {
        apply_default_config((mcp_object_cache_type_t)i);
    }

    tls_system_initialized = true;
    mcp_log_debug("Object cache system initialized");

    return true;
}

void mcp_object_cache_system_shutdown(void) {
    if (!tls_system_initialized) {
        return;
    }

    // Flush all caches
    for (int i = 0; i < MCP_OBJECT_CACHE_TYPE_COUNT; i++) {
        mcp_object_cache_flush((mcp_object_cache_type_t)i);
    }

    tls_system_initialized = false;
    mcp_log_debug("Object cache system shutdown");
}

bool mcp_object_cache_system_is_initialized(void) {
    return tls_system_initialized;
}

bool mcp_object_cache_init(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return false;
    }

    if (!tls_system_initialized) {
        if (!mcp_object_cache_system_init()) {
            return false;
        }
    }

    if (tls_cache_initialized[type]) {
        // Already initialized, reconfigure instead
        return mcp_object_cache_configure(type, config);
    }

    if (!config) {
        // Use default configuration
        apply_default_config(type);
    } else {
        // Apply custom configuration
        tls_max_sizes[type] = config->max_size;
        tls_adaptive_sizing[type] = config->adaptive_sizing;
        tls_growth_thresholds[type] = config->growth_threshold;
        tls_shrink_thresholds[type] = config->shrink_threshold;
        tls_min_cache_sizes[type] = config->min_cache_size;
        tls_max_cache_sizes[type] = config->max_cache_size;
        tls_constructors[type] = config->constructor;
        tls_destructors[type] = config->destructor;

        // Validate and clamp configuration values
        if (tls_max_sizes[type] > MAX_CACHE_SIZE) tls_max_sizes[type] = MAX_CACHE_SIZE;
        if (tls_max_sizes[type] < MIN_CACHE_SIZE) tls_max_sizes[type] = MIN_CACHE_SIZE;

        if (tls_growth_thresholds[type] < 0.0) tls_growth_thresholds[type] = 0.0;
        if (tls_growth_thresholds[type] > 1.0) tls_growth_thresholds[type] = 1.0;

        if (tls_shrink_thresholds[type] < 0.0) tls_shrink_thresholds[type] = 0.0;
        if (tls_shrink_thresholds[type] > 1.0) tls_shrink_thresholds[type] = 1.0;

        if (tls_min_cache_sizes[type] < 1) tls_min_cache_sizes[type] = 1;
        if (tls_max_cache_sizes[type] < tls_min_cache_sizes[type]) {
            tls_max_cache_sizes[type] = tls_min_cache_sizes[type];
        }
    }

    // Reset cache counters
    tls_object_counts[type] = 0;
    tls_cache_hits[type] = 0;
    tls_cache_misses[type] = 0;
    tls_cache_flushes[type] = 0;
    tls_operations_since_adjustment[type] = 0;
    tls_adjustment_intervals[type] = DEFAULT_ADJUSTMENT_INTERVAL;

    tls_cache_initialized[type] = true;
    mcp_log_debug("Object cache initialized for type %s", object_cache_type_names[type]);

    return true;
}

void mcp_object_cache_cleanup(mcp_object_cache_type_t type) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return;
    }

    if (!tls_cache_initialized[type]) {
        return;
    }

    // Flush the cache
    mcp_object_cache_flush(type);

    tls_cache_initialized[type] = false;
    mcp_log_debug("Object cache cleaned up for type %s", object_cache_type_names[type]);
}

void* mcp_object_cache_alloc(mcp_object_cache_type_t type, size_t size) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return NULL;
    }

    // If object cache is not initialized, initialize it with default configuration
    if (!tls_cache_initialized[type]) {
        if (!mcp_object_cache_init(type, NULL)) {
            // Fall back to direct allocation
            void* ptr = malloc(size);
            if (ptr && tls_constructors[type]) {
                tls_constructors[type](ptr);
            }
            return ptr;
        }
    }

    // Increment operation counter for adaptive sizing
    tls_operations_since_adjustment[type]++;

    // Check if we have a cached object
    if (tls_object_counts[type] > 0) {
        void* ptr = tls_object_caches[type][--tls_object_counts[type]];
        tls_cache_hits[type]++;

        // Call constructor if registered
        if (tls_constructors[type]) {
            tls_constructors[type](ptr);
        }

        return ptr;
    }

    // Cache miss
    tls_cache_misses[type]++;

    // Check if we need to adjust cache sizes
    if (tls_adaptive_sizing[type] &&
        tls_operations_since_adjustment[type] >= tls_adjustment_intervals[type]) {
        adjust_cache_size(type);
    }

    // Allocate a new object
    void* ptr = NULL;
    if (mcp_memory_pool_system_is_initialized()) {
        ptr = mcp_pool_alloc(size);
    } else {
        // Fall back to malloc if memory pool system is not initialized
        ptr = malloc(size);
    }

    // Call constructor if registered
    if (ptr && tls_constructors[type]) {
        tls_constructors[type](ptr);
    }

    return ptr;
}

void mcp_object_cache_free(mcp_object_cache_type_t type, void* ptr, size_t size) {
    if (!ptr) return;

    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        // Fall back to regular free
        free(ptr);
        return;
    }

    // If object cache is not initialized, initialize it with default configuration
    if (!tls_cache_initialized[type]) {
        if (!mcp_object_cache_init(type, NULL)) {
            // Fall back to direct free
            if (tls_destructors[type]) {
                tls_destructors[type](ptr);
            }
            free(ptr);
            return;
        }
    }

    // Increment operation counter for adaptive sizing
    tls_operations_since_adjustment[type]++;

    // Call destructor if registered
    if (tls_destructors[type]) {
        tls_destructors[type](ptr);
    }

    // Check if we can cache the object
    if (tls_object_counts[type] < tls_max_sizes[type]) {
        tls_object_caches[type][tls_object_counts[type]++] = ptr;
        return;
    }

    // Cache is full, determine if this is a pool block
    if (size == 0) {
        size = mcp_pool_get_block_size(ptr);
    }

    if (size > 0) {
        // It's a pool-allocated block, return it to the pool
        mcp_pool_free(ptr);
    } else {
        // It's a malloc-allocated block, use free
        free(ptr);
    }

    // Check if we need to adjust cache sizes
    if (tls_adaptive_sizing[type] &&
        tls_operations_since_adjustment[type] >= tls_adjustment_intervals[type]) {
        adjust_cache_size(type);
    }
}

bool mcp_object_cache_get_stats(mcp_object_cache_type_t type, mcp_object_cache_stats_t* stats) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT || !stats) {
        return false;
    }

    if (!tls_cache_initialized[type]) {
        return false;
    }

    // Cache occupancy
    stats->cache_count = tls_object_counts[type];
    stats->max_size = tls_max_sizes[type];
    stats->adaptive_sizing = tls_adaptive_sizing[type];
    stats->cache_hits = tls_cache_hits[type];
    stats->cache_misses = tls_cache_misses[type];
    stats->cache_flushes = tls_cache_flushes[type];

    // Calculate hit ratio
    size_t total_operations = stats->cache_hits + stats->cache_misses;
    if (total_operations > 0) {
        stats->hit_ratio = (double)stats->cache_hits / (double)total_operations;
    } else {
        stats->hit_ratio = 0.0;
    }

    return true;
}

bool mcp_object_cache_configure(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT || !config) {
        return false;
    }

    if (!tls_system_initialized) {
        if (!mcp_object_cache_system_init()) {
            return false;
        }
    }

    // Apply configuration
    tls_max_sizes[type] = config->max_size;
    tls_adaptive_sizing[type] = config->adaptive_sizing;
    tls_growth_thresholds[type] = config->growth_threshold;
    tls_shrink_thresholds[type] = config->shrink_threshold;
    tls_min_cache_sizes[type] = config->min_cache_size;
    tls_max_cache_sizes[type] = config->max_cache_size;

    // Only update constructor/destructor if they are provided
    if (config->constructor) {
        tls_constructors[type] = config->constructor;
    }

    if (config->destructor) {
        tls_destructors[type] = config->destructor;
    }

    // Validate and clamp configuration values
    if (tls_max_sizes[type] > MAX_CACHE_SIZE) tls_max_sizes[type] = MAX_CACHE_SIZE;
    if (tls_max_sizes[type] < MIN_CACHE_SIZE) tls_max_sizes[type] = MIN_CACHE_SIZE;

    if (tls_growth_thresholds[type] < 0.0) tls_growth_thresholds[type] = 0.0;
    if (tls_growth_thresholds[type] > 1.0) tls_growth_thresholds[type] = 1.0;

    if (tls_shrink_thresholds[type] < 0.0) tls_shrink_thresholds[type] = 0.0;
    if (tls_shrink_thresholds[type] > 1.0) tls_shrink_thresholds[type] = 1.0;

    if (tls_min_cache_sizes[type] < 1) tls_min_cache_sizes[type] = 1;
    if (tls_max_cache_sizes[type] < tls_min_cache_sizes[type]) {
        tls_max_cache_sizes[type] = tls_min_cache_sizes[type];
    }

    // Mark as initialized if it wasn't already
    if (!tls_cache_initialized[type]) {
        tls_cache_initialized[type] = true;
    }

    mcp_log_debug("Object cache reconfigured for type %s", object_cache_type_names[type]);
    return true;
}

bool mcp_object_cache_enable_adaptive_sizing(mcp_object_cache_type_t type, bool enable) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        return false;
    }

    if (!tls_cache_initialized[type]) {
        if (!mcp_object_cache_init(type, NULL)) {
            return false;
        }
    }

    tls_adaptive_sizing[type] = enable;
    mcp_log_debug("Object cache adaptive sizing %s for type %s",
                 enable ? "enabled" : "disabled",
                 object_cache_type_names[type]);
    return true;
}

void mcp_object_cache_flush(mcp_object_cache_type_t type) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        return;
    }

    if (!tls_cache_initialized[type]) {
        return;
    }

    // Flush the cache
    for (size_t i = 0; i < tls_object_counts[type]; i++) {
        void* ptr = tls_object_caches[type][i];

        // Call destructor if registered
        if (tls_destructors[type]) {
            tls_destructors[type](ptr);
        }

        // Check if this is a pool-allocated block
        size_t block_size = mcp_pool_get_block_size(ptr);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(ptr);
        } else {
            // It's a malloc-allocated block, use free
            free(ptr);
        }
        tls_object_caches[type][i] = NULL;
    }
    tls_object_counts[type] = 0;
    tls_cache_flushes[type]++;

    mcp_log_debug("Object cache flushed for type %s", object_cache_type_names[type]);
}

bool mcp_object_cache_register_type(mcp_object_cache_type_t type,
                                   void (*constructor)(void*),
                                   void (*destructor)(void*)) {
    if (type <= MCP_OBJECT_CACHE_GENERIC || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type for registration: %d", type);
        return false;
    }

    if (!tls_system_initialized) {
        if (!mcp_object_cache_system_init()) {
            return false;
        }
    }

    tls_constructors[type] = constructor;
    tls_destructors[type] = destructor;

    mcp_log_debug("Custom object type %s registered with %s constructor and %s destructor",
                 object_cache_type_names[type],
                 constructor ? "a" : "no",
                 destructor ? "a" : "no");
    return true;
}

const char* mcp_object_cache_type_name(mcp_object_cache_type_t type) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        return "Unknown";
    }
    return object_cache_type_names[type];
}

static bool adjust_cache_size(mcp_object_cache_type_t type) {
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        return false;
    }

    if (!tls_cache_initialized[type]) {
        return false;
    }

    // Reset operation counter
    tls_operations_since_adjustment[type] = 0;

    // If adaptive sizing is disabled, do nothing
    if (!tls_adaptive_sizing[type]) {
        return true;
    }

    // Calculate hit ratio
    double hit_ratio = 0.0;
    size_t total_operations = tls_cache_hits[type] + tls_cache_misses[type];
    if (total_operations > 0) {
        hit_ratio = (double)tls_cache_hits[type] / (double)total_operations;
    }

    // Adjust cache size based on hit ratio
    if (hit_ratio > tls_growth_thresholds[type] && tls_max_sizes[type] < tls_max_cache_sizes[type]) {
        // High hit ratio, grow the cache
        size_t new_size = tls_max_sizes[type] * 2;
        if (new_size > tls_max_cache_sizes[type]) {
            new_size = tls_max_cache_sizes[type];
        }
        tls_max_sizes[type] = new_size;
        mcp_log_debug("%s cache size increased to %zu due to high hit ratio (%.2f)",
                     object_cache_type_names[type], new_size, hit_ratio);
    } else if (hit_ratio < tls_shrink_thresholds[type] && tls_max_sizes[type] > tls_min_cache_sizes[type]) {
        // Low hit ratio, shrink the cache
        size_t new_size = tls_max_sizes[type] / 2;
        if (new_size < tls_min_cache_sizes[type]) {
            new_size = tls_min_cache_sizes[type];
        }

        // If current count exceeds new max size, flush excess objects
        if (tls_object_counts[type] > new_size) {
            for (size_t i = new_size; i < tls_object_counts[type]; i++) {
                void* ptr = tls_object_caches[type][i];

                // Call destructor if registered
                if (tls_destructors[type]) {
                    tls_destructors[type](ptr);
                }

                // Check if this is a pool-allocated block
                size_t block_size = mcp_pool_get_block_size(ptr);
                if (block_size > 0) {
                    // It's a pool-allocated block, return it to the pool
                    mcp_pool_free(ptr);
                } else {
                    // It's a malloc-allocated block, use free
                    free(ptr);
                }
                tls_object_caches[type][i] = NULL;
            }
            tls_object_counts[type] = new_size;
        }

        tls_max_sizes[type] = new_size;
        mcp_log_debug("%s cache size decreased to %zu due to low hit ratio (%.2f)",
                     object_cache_type_names[type], new_size, hit_ratio);
    }

    return true;
}

static void apply_default_config(mcp_object_cache_type_t type) {
    tls_max_sizes[type] = DEFAULT_CACHE_SIZE;
    tls_adaptive_sizing[type] = false;
    tls_growth_thresholds[type] = DEFAULT_GROWTH_THRESHOLD;
    tls_shrink_thresholds[type] = DEFAULT_SHRINK_THRESHOLD;
    tls_min_cache_sizes[type] = MIN_CACHE_SIZE;
    tls_max_cache_sizes[type] = MAX_CACHE_SIZE;
    tls_adjustment_intervals[type] = DEFAULT_ADJUSTMENT_INTERVAL;
    tls_constructors[type] = NULL;
    tls_destructors[type] = NULL;

    // Reset counters
    tls_object_counts[type] = 0;
    tls_cache_hits[type] = 0;
    tls_cache_misses[type] = 0;
    tls_cache_flushes[type] = 0;
    tls_operations_since_adjustment[type] = 0;
}
