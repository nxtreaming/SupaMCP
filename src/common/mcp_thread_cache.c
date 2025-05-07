#include "mcp_cache_aligned.h"
#include "mcp_thread_cache.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_log.h"
#include "mcp_list.h"
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

// LRU constants
#define LRU_ENABLED 1                 // Enable LRU replacement strategy by default

// Forward declaration of mcp_list_t for LRU implementation
typedef struct mcp_list mcp_list_t;
typedef struct mcp_list_node mcp_list_node_t;

// Thread-local cache state structure with cache line alignment to prevent false sharing
typedef MCP_CACHE_ALIGNED struct {
    // Thread identification
    unsigned long thread_id;                  // Thread identifier for debugging and analysis

    // Cache arrays
    void* small_cache[MAX_CACHE_SIZE];        // Small block cache
    void* medium_cache[MAX_CACHE_SIZE];       // Medium block cache
    void* large_cache[MAX_CACHE_SIZE];        // Large block cache

    // Block size metadata (to avoid mcp_pool_get_block_size calls)
    size_t small_block_sizes[MAX_CACHE_SIZE]; // Size of each small block
    size_t medium_block_sizes[MAX_CACHE_SIZE];// Size of each medium block
    size_t large_block_sizes[MAX_CACHE_SIZE]; // Size of each large block

    // LRU tracking
    mcp_list_t* small_lru_list;               // LRU list for small blocks
    mcp_list_t* medium_lru_list;              // LRU list for medium blocks
    mcp_list_t* large_lru_list;               // LRU list for large blocks

    // LRU node pointers (for quick access)
    mcp_list_node_t* small_lru_nodes[MAX_CACHE_SIZE]; // LRU nodes for small blocks
    mcp_list_node_t* medium_lru_nodes[MAX_CACHE_SIZE]; // LRU nodes for medium blocks
    mcp_list_node_t* large_lru_nodes[MAX_CACHE_SIZE]; // LRU nodes for large blocks

    // Legacy LRU tracking (for backward compatibility)
    size_t small_lru_counters[MAX_CACHE_SIZE];// Access counter for small blocks
    size_t medium_lru_counters[MAX_CACHE_SIZE];// Access counter for medium blocks
    size_t large_lru_counters[MAX_CACHE_SIZE];// Access counter for large blocks
    size_t lru_clock;                         // Global LRU clock

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

    // Core statistics
    size_t cache_hits;                        // Number of cache hits
    size_t misses_small;                      // Number of small block misses
    size_t misses_medium;                     // Number of medium block misses
    size_t misses_large;                      // Number of large block misses
    size_t misses_other;                      // Number of other size misses
    size_t cache_flushes;                     // Number of cache flushes

    // Configuration flags
    bool initialized;                         // Whether cache is initialized
    bool adaptive_sizing;                     // Whether adaptive sizing is enabled
    bool lru_enabled;                         // Whether LRU replacement is enabled

    // Adaptive sizing thresholds
    double growth_threshold;                  // Hit ratio threshold for growing cache
    double shrink_threshold;                  // Hit ratio threshold for shrinking cache
} thread_cache_state_t;

// Function to get current thread ID in a platform-independent way
static unsigned long get_current_thread_id(void) {
#ifdef _WIN32
    return (unsigned long)GetCurrentThreadId();
#else
    return (unsigned long)pthread_self();
#endif
}

/**
 * @brief Helper function to get a memory block from the LRU list
 *
 * This function gets the least recently used block from the LRU list,
 * removes it from the list, and returns it.
 *
 * @param lru_list The LRU list to get the block from
 * @param cache The cache array containing the blocks
 * @param block_sizes The array of block sizes
 * @param lru_nodes The array of LRU node pointers
 * @param count Pointer to the count of blocks in the cache
 * @return Pointer to the memory block, or NULL if the list is empty
 */
static void* get_lru_block(mcp_list_t* lru_list, void** cache, size_t* block_sizes,
                          mcp_list_node_t** lru_nodes, size_t* count) {
    if (!lru_list || !cache || !block_sizes || !lru_nodes || !count || *count == 0) {
        return NULL;
    }

    // Get the least recently used node (from the back of the list)
    mcp_list_node_t* lru_node = lru_list->tail;
    if (!lru_node) {
        return NULL;
    }

    // Get the index of the block in the cache
    size_t index = (size_t)lru_node->data;
    if (index >= *count) {
        return NULL;
    }

    // Get the block
    void* ptr = cache[index];

    // Remove the node from the LRU list
    mcp_list_remove(lru_list, lru_node, NULL);
    lru_nodes[index] = NULL;

    // Move the last block to this position if it's not the last one
    if (index < *count - 1) {
        cache[index] = cache[*count - 1];
        block_sizes[index] = block_sizes[*count - 1];
        lru_nodes[index] = lru_nodes[*count - 1];

        // Update the data pointer in the node if it exists
        if (lru_nodes[index]) {
            lru_nodes[index]->data = (void*)index;
        }
    }

    // Decrement the count
    (*count)--;

    return ptr;
}

/**
 * @brief Helper function to add a memory block to the LRU list
 *
 * This function adds a memory block to the cache and the LRU list.
 *
 * @param lru_list The LRU list to add the block to
 * @param cache The cache array to add the block to
 * @param block_sizes The array of block sizes
 * @param lru_nodes The array of LRU node pointers
 * @param count Pointer to the count of blocks in the cache
 * @param max_size Maximum size of the cache
 * @param ptr Pointer to the memory block to add
 * @param size Size of the memory block
 * @return true if the block was added, false otherwise
 */
static bool add_to_lru_cache(mcp_list_t* lru_list, void** cache, size_t* block_sizes,
                           mcp_list_node_t** lru_nodes, size_t* count, size_t max_size,
                           void* ptr, size_t size) {
    if (!lru_list || !cache || !block_sizes || !lru_nodes || !count || !ptr) {
        return false;
    }

    // Check if the cache is full
    if (*count >= max_size) {
        return false;
    }

    // Add the block to the cache
    size_t index = *count;
    cache[index] = ptr;
    block_sizes[index] = size;

    // Add to the front of the LRU list (most recently used)
    lru_nodes[index] = mcp_list_push_front(lru_list, (void*)index);
    if (!lru_nodes[index]) {
        // Failed to add to LRU list
        return false;
    }

    // Increment the count
    (*count)++;

    return true;
}

/**
 * @brief Helper function to update the position of a memory block in the LRU list
 *
 * This function moves a memory block to the front of the LRU list (most recently used).
 *
 * @param lru_list The LRU list to update
 * @param lru_nodes The array of LRU node pointers
 * @param index Index of the block in the cache
 * @return true if the position was updated, false otherwise
 */
static bool update_lru_position(mcp_list_t* lru_list, mcp_list_node_t** lru_nodes, size_t index) {
    if (!lru_list || !lru_nodes || !lru_nodes[index]) {
        return false;
    }

    // Move the node to the front of the LRU list (most recently used)
    mcp_list_move_to_front(lru_list, lru_nodes[index]);
    return true;
}

// Thread-local cache state
#ifdef _WIN32
__declspec(thread) static thread_cache_state_t tls_cache_state = {0};
#else
__thread static thread_cache_state_t tls_cache_state = {0};
#endif

// Convenience macros to access the thread-local state
// Cache arrays
#define tls_small_cache          tls_cache_state.small_cache
#define tls_medium_cache         tls_cache_state.medium_cache
#define tls_large_cache          tls_cache_state.large_cache

// Block size metadata
#define tls_small_block_sizes    tls_cache_state.small_block_sizes
#define tls_medium_block_sizes   tls_cache_state.medium_block_sizes
#define tls_large_block_sizes    tls_cache_state.large_block_sizes

// LRU tracking
#define tls_small_lru_list       tls_cache_state.small_lru_list
#define tls_medium_lru_list      tls_cache_state.medium_lru_list
#define tls_large_lru_list       tls_cache_state.large_lru_list

// LRU node pointers
#define tls_small_lru_nodes      tls_cache_state.small_lru_nodes
#define tls_medium_lru_nodes     tls_cache_state.medium_lru_nodes
#define tls_large_lru_nodes      tls_cache_state.large_lru_nodes

// Legacy LRU tracking
#define tls_small_lru_counters   tls_cache_state.small_lru_counters
#define tls_medium_lru_counters  tls_cache_state.medium_lru_counters
#define tls_large_lru_counters   tls_cache_state.large_lru_counters
#define tls_lru_clock            tls_cache_state.lru_clock

// Cache counters
#define tls_small_count          tls_cache_state.small_count
#define tls_medium_count         tls_cache_state.medium_count
#define tls_large_count          tls_cache_state.large_count

// Cache configuration
#define tls_small_max_size       tls_cache_state.small_max_size
#define tls_medium_max_size      tls_cache_state.medium_max_size
#define tls_large_max_size       tls_cache_state.large_max_size
#define tls_min_cache_size       tls_cache_state.min_cache_size
#define tls_max_cache_size       tls_cache_state.max_cache_size
#define tls_adjustment_interval  tls_cache_state.adjustment_interval
#define tls_operations_since_adjustment tls_cache_state.operations_since_adjustment

// Statistics
#define tls_cache_hits           tls_cache_state.cache_hits
#define tls_misses_small         tls_cache_state.misses_small
#define tls_misses_medium        tls_cache_state.misses_medium
#define tls_misses_large         tls_cache_state.misses_large
#define tls_misses_other         tls_cache_state.misses_other
#define tls_cache_flushes        tls_cache_state.cache_flushes

// Configuration flags
#define tls_cache_initialized    tls_cache_state.initialized
#define tls_adaptive_sizing      tls_cache_state.adaptive_sizing
#define tls_lru_enabled          tls_cache_state.lru_enabled

// Adaptive sizing thresholds
#define tls_growth_threshold     tls_cache_state.growth_threshold
#define tls_shrink_threshold     tls_cache_state.shrink_threshold

// Thread identification
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

    // Initialize all arrays and counters to zero
    memset(&tls_cache_state, 0, sizeof(tls_cache_state));

    // Set thread ID (this was zeroed by the memset)
    tls_thread_id = get_current_thread_id();

    // Initialize LRU clock
    tls_lru_clock = 1;  // Start at 1, 0 means unused

    // Create LRU lists (not thread-safe since we're using them in thread-local storage)
    tls_small_lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);
    tls_medium_lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);
    tls_large_lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);

    // Initialize LRU node pointers
    memset(tls_small_lru_nodes, 0, sizeof(tls_small_lru_nodes));
    memset(tls_medium_lru_nodes, 0, sizeof(tls_medium_lru_nodes));
    memset(tls_large_lru_nodes, 0, sizeof(tls_large_lru_nodes));

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

    // Enable LRU by default
    tls_lru_enabled = LRU_ENABLED;

    // Mark as initialized
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

    // Initialize all arrays and counters to zero
    memset(&tls_cache_state, 0, sizeof(tls_cache_state));

    // Set thread ID (this was zeroed by the memset)
    tls_thread_id = get_current_thread_id();

    // Initialize LRU clock
    tls_lru_clock = 1;  // Start at 1, 0 means unused

    // Create LRU lists (not thread-safe since we're using them in thread-local storage)
    tls_small_lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);
    tls_medium_lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);
    tls_large_lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);

    // Initialize LRU node pointers
    memset(tls_small_lru_nodes, 0, sizeof(tls_small_lru_nodes));
    memset(tls_medium_lru_nodes, 0, sizeof(tls_medium_lru_nodes));
    memset(tls_large_lru_nodes, 0, sizeof(tls_large_lru_nodes));

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

    // Enable LRU by default
    tls_lru_enabled = LRU_ENABLED;

    // Mark as initialized
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

    // Destroy LRU lists
    if (tls_small_lru_list) {
        mcp_list_destroy(tls_small_lru_list, NULL);
        tls_small_lru_list = NULL;
    }

    if (tls_medium_lru_list) {
        mcp_list_destroy(tls_medium_lru_list, NULL);
        tls_medium_lru_list = NULL;
    }

    if (tls_large_lru_list) {
        mcp_list_destroy(tls_large_lru_list, NULL);
        tls_large_lru_list = NULL;
    }

    // Clear LRU node pointers
    memset(tls_small_lru_nodes, 0, sizeof(tls_small_lru_nodes));
    memset(tls_medium_lru_nodes, 0, sizeof(tls_medium_lru_nodes));
    memset(tls_large_lru_nodes, 0, sizeof(tls_large_lru_nodes));

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
            // LRU: Use the LRU list if enabled
            if (tls_lru_enabled && tls_small_lru_list) {
                // Get the least recently used block
                void* ptr = get_lru_block(tls_small_lru_list, tls_small_cache,
                                         tls_small_block_sizes, tls_small_lru_nodes,
                                         &tls_small_count);
                if (ptr) {
                    tls_cache_hits++;
                    return ptr;
                }

                // Fall back to legacy LRU if the list is empty
                size_t lru_index = 0;
                size_t min_counter = tls_small_lru_counters[0];

                // Find the block with the lowest counter value (least recently used)
                for (size_t i = 1; i < tls_small_count; i++) {
                    if (tls_small_lru_counters[i] < min_counter) {
                        min_counter = tls_small_lru_counters[i];
                        lru_index = i;
                    }
                }

                // Get the block and update counters
                ptr = tls_small_cache[lru_index];

                // Remove the block from the cache (move last block to this position)
                if (lru_index < tls_small_count - 1) {
                    tls_small_cache[lru_index] = tls_small_cache[tls_small_count - 1];
                    tls_small_block_sizes[lru_index] = tls_small_block_sizes[tls_small_count - 1];
                    tls_small_lru_counters[lru_index] = tls_small_lru_counters[tls_small_count - 1];
                }

                tls_small_count--;
                tls_cache_hits++;
                return ptr;
            } else {
                // Simple LIFO if LRU is disabled
                tls_cache_hits++;
                void* ptr = tls_small_cache[--tls_small_count];
                return ptr;
            }
        }

        // Cache miss
        tls_misses_small++;
    }
    // Medium blocks
    else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count > 0) {
            // LRU: Find the least recently used block if LRU is enabled
            if (tls_lru_enabled) {
                size_t lru_index = 0;
                size_t min_counter = tls_medium_lru_counters[0];

                // Find the block with the lowest counter value (least recently used)
                for (size_t i = 1; i < tls_medium_count; i++) {
                    if (tls_medium_lru_counters[i] < min_counter) {
                        min_counter = tls_medium_lru_counters[i];
                        lru_index = i;
                    }
                }

                // Get the block and update counters
                void* ptr = tls_medium_cache[lru_index];

                // Remove the block from the cache (move last block to this position)
                if (lru_index < tls_medium_count - 1) {
                    tls_medium_cache[lru_index] = tls_medium_cache[tls_medium_count - 1];
                    tls_medium_block_sizes[lru_index] = tls_medium_block_sizes[tls_medium_count - 1];
                    tls_medium_lru_counters[lru_index] = tls_medium_lru_counters[tls_medium_count - 1];
                }

                tls_medium_count--;
                tls_cache_hits++;
                return ptr;
            } else {
                // Simple LIFO if LRU is disabled
                tls_cache_hits++;
                void* ptr = tls_medium_cache[--tls_medium_count];
                return ptr;
            }
        }

        // Cache miss
        tls_misses_medium++;
    }
    // Large blocks
    else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count > 0) {
            // LRU: Find the least recently used block if LRU is enabled
            if (tls_lru_enabled) {
                size_t lru_index = 0;
                size_t min_counter = tls_large_lru_counters[0];

                // Find the block with the lowest counter value (least recently used)
                for (size_t i = 1; i < tls_large_count; i++) {
                    if (tls_large_lru_counters[i] < min_counter) {
                        min_counter = tls_large_lru_counters[i];
                        lru_index = i;
                    }
                }

                // Get the block and update counters
                void* ptr = tls_large_cache[lru_index];

                // Remove the block from the cache (move last block to this position)
                if (lru_index < tls_large_count - 1) {
                    tls_large_cache[lru_index] = tls_large_cache[tls_large_count - 1];
                    tls_large_block_sizes[lru_index] = tls_large_block_sizes[tls_large_count - 1];
                    tls_large_lru_counters[lru_index] = tls_large_lru_counters[tls_large_count - 1];
                }

                tls_large_count--;
                tls_cache_hits++;
                return ptr;
            } else {
                // Simple LIFO if LRU is disabled
                tls_cache_hits++;
                void* ptr = tls_large_cache[--tls_large_count];
                return ptr;
            }
        }

        // Cache miss
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
        // Try to add to the LRU cache
        if (tls_lru_enabled && tls_small_lru_list) {
            if (tls_small_count < tls_small_max_size) {
                // Add to the LRU cache
                if (add_to_lru_cache(tls_small_lru_list, tls_small_cache,
                                    tls_small_block_sizes, tls_small_lru_nodes,
                                    &tls_small_count, tls_small_max_size,
                                    ptr, size)) {
                    return;
                }
            } else if (tls_small_count > 0) {
                // LRU replacement: get the least recently used block
                void* old_ptr = get_lru_block(tls_small_lru_list, tls_small_cache,
                                             tls_small_block_sizes, tls_small_lru_nodes,
                                             &tls_small_count);
                if (old_ptr) {
                    // Free the evicted block
                    size_t old_size = mcp_pool_get_block_size(old_ptr);
                    if (old_size > 0) {
                        mcp_pool_free(old_ptr);
                    } else {
                        free(old_ptr);
                    }

                    // Add the new block to the cache
                    if (add_to_lru_cache(tls_small_lru_list, tls_small_cache,
                                        tls_small_block_sizes, tls_small_lru_nodes,
                                        &tls_small_count, tls_small_max_size,
                                        ptr, size)) {
                        return;
                    }
                }
            }

            // Fall back to legacy LRU if the list operations failed
            if (tls_small_count < tls_small_max_size) {
                // Add to cache with metadata
                tls_small_cache[tls_small_count] = ptr;
                tls_small_block_sizes[tls_small_count] = size;
                tls_small_lru_counters[tls_small_count] = tls_lru_clock++;
                tls_small_count++;
                return;
            } else if (tls_small_count > 0) {
                // LRU replacement: find the least recently used block
                size_t lru_index = 0;
                size_t min_counter = tls_small_lru_counters[0];

                // Find the block with the lowest counter value (least recently used)
                for (size_t i = 1; i < tls_small_count; i++) {
                    if (tls_small_lru_counters[i] < min_counter) {
                        min_counter = tls_small_lru_counters[i];
                        lru_index = i;
                    }
                }

                // Free the least recently used block
                void* old_ptr = tls_small_cache[lru_index];
                size_t old_size = tls_small_block_sizes[lru_index];

                // Replace with the new block
                tls_small_cache[lru_index] = ptr;
                tls_small_block_sizes[lru_index] = size;
                tls_small_lru_counters[lru_index] = tls_lru_clock++;

                // Free the evicted block
                if (old_size > 0) {
                    mcp_pool_free(old_ptr);
                } else {
                    free(old_ptr);
                }

                return;
            }
        } else {
            // Simple LIFO if LRU is disabled
            if (tls_small_count < tls_small_max_size) {
                // Add to cache with metadata
                tls_small_cache[tls_small_count] = ptr;
                tls_small_block_sizes[tls_small_count] = size;
                tls_small_lru_counters[tls_small_count] = tls_lru_clock++;
                tls_small_count++;
                return;
            }
        }
    }
    // Medium blocks
    else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count < tls_medium_max_size) {
            // Add to cache with metadata
            tls_medium_cache[tls_medium_count] = ptr;
            tls_medium_block_sizes[tls_medium_count] = size;
            tls_medium_lru_counters[tls_medium_count] = tls_lru_clock++;
            tls_medium_count++;
            return;
        } else if (tls_lru_enabled && tls_medium_count > 0) {
            // LRU replacement: find the least recently used block
            size_t lru_index = 0;
            size_t min_counter = tls_medium_lru_counters[0];

            // Find the block with the lowest counter value (least recently used)
            for (size_t i = 1; i < tls_medium_count; i++) {
                if (tls_medium_lru_counters[i] < min_counter) {
                    min_counter = tls_medium_lru_counters[i];
                    lru_index = i;
                }
            }

            // Free the least recently used block
            void* old_ptr = tls_medium_cache[lru_index];
            size_t old_size = tls_medium_block_sizes[lru_index];

            // Replace with the new block
            tls_medium_cache[lru_index] = ptr;
            tls_medium_block_sizes[lru_index] = size;
            tls_medium_lru_counters[lru_index] = tls_lru_clock++;

            // Free the evicted block
            if (old_size > 0) {
                mcp_pool_free(old_ptr);
            } else {
                free(old_ptr);
            }

            return;
        }
    }
    // Large blocks
    else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count < tls_large_max_size) {
            // Add to cache with metadata
            tls_large_cache[tls_large_count] = ptr;
            tls_large_block_sizes[tls_large_count] = size;
            tls_large_lru_counters[tls_large_count] = tls_lru_clock++;
            tls_large_count++;
            return;
        } else if (tls_lru_enabled && tls_large_count > 0) {
            // LRU replacement: find the least recently used block
            size_t lru_index = 0;
            size_t min_counter = tls_large_lru_counters[0];

            // Find the block with the lowest counter value (least recently used)
            for (size_t i = 1; i < tls_large_count; i++) {
                if (tls_large_lru_counters[i] < min_counter) {
                    min_counter = tls_large_lru_counters[i];
                    lru_index = i;
                }
            }

            // Free the least recently used block
            void* old_ptr = tls_large_cache[lru_index];
            size_t old_size = tls_large_block_sizes[lru_index];

            // Replace with the new block
            tls_large_cache[lru_index] = ptr;
            tls_large_block_sizes[lru_index] = size;
            tls_large_lru_counters[lru_index] = tls_lru_clock++;

            // Free the evicted block
            if (old_size > 0) {
                mcp_pool_free(old_ptr);
            } else {
                free(old_ptr);
            }

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

    // Add LRU status
    stats->lru_enabled = tls_lru_enabled;

    return true;
}

/**
 * @brief Configures the thread-local cache with new settings
 *
 * This function is optimized to:
 * 1. Validate input parameters
 * 2. Apply and validate configuration values
 * 3. Support LRU and batch allocation settings
 *
 * @param config Pointer to a configuration structure
 * @return true if configuration was successful, false otherwise
 */
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

    // Configure LRU if specified
    if (config->lru_enabled >= 0) {
        tls_lru_enabled = config->lru_enabled;
    }

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

    mcp_log_debug("Thread-local cache reconfigured for thread %lu: LRU %s, adaptive sizing %s",
                 tls_thread_id,
                 tls_lru_enabled ? "enabled" : "disabled",
                 tls_adaptive_sizing ? "enabled" : "disabled");
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
 * @param block_sizes Array of block sizes (can be NULL)
 * @param lru_counters Array of LRU counters (can be NULL)
 * @param lru_nodes Array of LRU node pointers (can be NULL)
 * @param lru_list LRU list (can be NULL)
 * @param count Number of blocks in the cache
 * @return Number of blocks flushed
 */
static size_t flush_cache_array(void** cache, size_t* block_sizes, size_t* lru_counters,
                              mcp_list_node_t** lru_nodes, mcp_list_t* lru_list, size_t count) {
    size_t flushed = 0;

    // First, clear the LRU list if it exists
    if (lru_list) {
        mcp_list_clear(lru_list, NULL);
    }

    for (size_t i = 0; i < count; i++) {
        if (cache[i]) {
            // Use cached block size if available
            size_t block_size = (block_sizes != NULL) ? block_sizes[i] : 0;

            // If block size is not cached or is zero, try to determine it
            if (block_size == 0) {
                block_size = mcp_pool_get_block_size(cache[i]);
            }

            // Free the block
            if (block_size > 0) {
                // It's a pool-allocated block, return it to the pool
                mcp_pool_free(cache[i]);
            } else {
                // It's a malloc-allocated block, use free
                free(cache[i]);
            }

            // Clear the cache entry
            cache[i] = NULL;
            if (block_sizes != NULL) {
                block_sizes[i] = 0;
            }
            if (lru_counters != NULL) {
                lru_counters[i] = 0;
            }
            if (lru_nodes != NULL) {
                lru_nodes[i] = NULL;
            }

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
    size_t small_flushed = flush_cache_array(tls_small_cache, tls_small_block_sizes, tls_small_lru_counters,
                                           tls_small_lru_nodes, tls_small_lru_list, tls_small_count);
    size_t medium_flushed = flush_cache_array(tls_medium_cache, tls_medium_block_sizes, tls_medium_lru_counters,
                                            tls_medium_lru_nodes, tls_medium_lru_list, tls_medium_count);
    size_t large_flushed = flush_cache_array(tls_large_cache, tls_large_block_sizes, tls_large_lru_counters,
                                           tls_large_lru_nodes, tls_large_lru_list, tls_large_count);

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
