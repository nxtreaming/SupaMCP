#include "mcp_thread_local.h"
#include "internal/arena_internal.h"
#include "mcp_log.h"
#include "mcp_memory_pool.h"
#include <stdlib.h>
#include <string.h>

// Performance statistics for thread-local storage
typedef struct {
    size_t arena_allocations;      // Number of arena allocations
    size_t arena_resets;           // Number of arena resets
    size_t arena_destroys;         // Number of arena destroys
    size_t cache_allocations;      // Number of cache allocations
    size_t cache_frees;            // Number of cache frees
    size_t cache_hits;             // Number of cache hits
    size_t cache_misses;           // Number of cache misses
} mcp_thread_local_stats_t;

// Thread-local storage for performance statistics
#ifdef _WIN32
__declspec(thread) static mcp_thread_local_stats_t tls_stats = {0};
#else
__thread static mcp_thread_local_stats_t tls_stats = {0};
#endif

#ifdef _WIN32
#include <windows.h>
static DWORD arena_tls_index = TLS_OUT_OF_INDEXES;
#else
#include <pthread.h>
static pthread_key_t arena_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
#endif

// Flag to track if thread-local storage is initialized
#ifdef _WIN32
__declspec(thread) static bool tls_initialized = false;
#else
__thread static bool tls_initialized = false;
#endif

// Forward declarations
static void cleanup_arena(void* arena);
#ifndef _WIN32
static void make_key(void);
#endif

/**
 * @brief Initialize the thread-local arena with the given initial size.
 *
 * This function is optimized to:
 * 1. Check if arena is already initialized to avoid redundant initialization
 * 2. Use memory pool for arena allocation if available
 * 3. Track performance statistics
 *
 * @param initial_size The initial size in bytes for the arena's buffer
 * @return 0 on success, -1 on failure
 */
int mcp_arena_init_current_thread(size_t initial_size) {
    // Check if already initialized
    if (tls_initialized && mcp_arena_get_current() != NULL) {
        mcp_log_debug("Thread-local arena already initialized");
        return 0;
    }

    // Initialize TLS mechanism if needed
#ifdef _WIN32
    if (arena_tls_index == TLS_OUT_OF_INDEXES) {
        arena_tls_index = TlsAlloc();
        if (arena_tls_index == TLS_OUT_OF_INDEXES) {
            mcp_log_error("Failed to allocate TLS index for arena");
            return -1;
        }
    }
#else
    pthread_once(&key_once, make_key);
#endif

    // Allocate arena structure - try to use memory pool if available
    mcp_arena_t* arena = NULL;
    if (mcp_memory_pool_system_is_initialized()) {
        arena = (mcp_arena_t*)mcp_pool_alloc(sizeof(mcp_arena_t));
    } else {
        arena = (mcp_arena_t*)malloc(sizeof(mcp_arena_t));
    }

    if (!arena) {
        mcp_log_error("Failed to allocate memory for thread-local arena");
        return -1;
    }

    // Initialize the arena
    memset(arena, 0, sizeof(mcp_arena_t)); // Clear all fields
    mcp_arena_init(arena, initial_size);
    if (!arena->default_block_size) {
        mcp_log_error("Failed to initialize thread-local arena");
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(arena);
        } else {
            free(arena);
        }
        return -1;
    }

    // Store arena in thread-local storage
#ifdef _WIN32
    if (!TlsSetValue(arena_tls_index, arena)) {
        mcp_log_error("Failed to set TLS value for arena");
        cleanup_arena(arena);
        return -1;
    }
#else
    if (pthread_setspecific(arena_key, arena) != 0) {
        mcp_log_error("Failed to set thread-specific value for arena");
        cleanup_arena(arena);
        return -1;
    }
#endif

    // Update statistics
    tls_stats.arena_allocations++;
    tls_initialized = true;

    mcp_log_debug("Thread-local arena initialized with size %zu", initial_size);
    return 0;
}

/**
 * @brief Get the current thread's arena.
 *
 * This function is optimized to:
 * 1. Fast path for common case (arena exists)
 * 2. Proper error handling for edge cases
 *
 * @return Pointer to the thread-local arena, or NULL if not initialized
 */
mcp_arena_t* mcp_arena_get_current(void) {
    // Fast path - if we know TLS isn't initialized, return NULL immediately
    if (!tls_initialized) {
        return NULL;
    }

#ifdef _WIN32
    // Check if TLS index is valid
    if (arena_tls_index == TLS_OUT_OF_INDEXES) {
        return NULL;
    }

    // Get the arena from TLS
    mcp_arena_t* arena = (mcp_arena_t*)TlsGetValue(arena_tls_index);

    // Check for Windows API errors
    if (arena == NULL && GetLastError() != ERROR_SUCCESS) {
        mcp_log_debug("TlsGetValue failed with error code %lu", GetLastError());
    }

    return arena;
#else
    // Get the arena from thread-specific data
    return (mcp_arena_t*)pthread_getspecific(arena_key);
#endif
}

/**
 * @brief Reset the current thread's arena.
 *
 * This function is optimized to:
 * 1. Track performance statistics
 * 2. Provide detailed logging
 *
 * Makes all memory previously allocated from the thread-local arena available
 * again without freeing the underlying blocks.
 */
void mcp_arena_reset_current_thread(void) {
    // Fast path - if we know TLS isn't initialized, return immediately
    if (!tls_initialized) {
        return;
    }

    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena) {
        // Get stats before reset for logging
        size_t total_allocated = 0;
        mcp_arena_get_stats(arena, &total_allocated, NULL, NULL);

        // Reset the arena
        mcp_arena_reset(arena);

        // Update statistics
        tls_stats.arena_resets++;

        mcp_log_debug("Thread-local arena reset: %zu bytes freed", total_allocated);
    }
}

/**
 * @brief Destroy the current thread's arena.
 *
 * This function is optimized to:
 * 1. Track performance statistics
 * 2. Provide detailed logging
 * 3. Properly clean up all resources
 *
 * Frees all memory blocks associated with the thread-local arena.
 */
void mcp_arena_destroy_current_thread(void) {
    // Fast path - if we know TLS isn't initialized, return immediately
    if (!tls_initialized) {
        return;
    }

    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena) {
        // Log cleanup for debugging purposes
        size_t total_allocated = 0, total_block_size = 0, block_count = 0;
        mcp_arena_get_stats(arena, &total_allocated, &total_block_size, &block_count);
        mcp_log_debug("Destroying thread-local arena: %zu bytes allocated, %zu total block size, %zu blocks",
                     total_allocated, total_block_size, block_count);

        // Clean up the arena
        cleanup_arena(arena);

        // Clear the thread-local storage
#ifdef _WIN32
        TlsSetValue(arena_tls_index, NULL);
#else
        pthread_setspecific(arena_key, NULL);
#endif

        // Update statistics
        tls_stats.arena_destroys++;

        // If this is the last TLS resource, mark TLS as uninitialized
        if (tls_stats.arena_destroys >= tls_stats.arena_allocations) {
            tls_initialized = false;
        }
    }
}

/**
 * @brief Clean up an arena and free its memory.
 *
 * This function is optimized to:
 * 1. Use memory pool for freeing if it was used for allocation
 * 2. Provide proper error handling
 *
 * @param arena Pointer to the arena to clean up
 */
static void cleanup_arena(void* arena) {
    if (!arena) {
        return;
    }

    // Clean up the arena's blocks
    mcp_arena_cleanup((mcp_arena_t*)arena);

    // Free the arena structure - try to use memory pool if available
    if (mcp_memory_pool_system_is_initialized()) {
        // Check if this is a pool-allocated block
        size_t block_size = mcp_pool_get_block_size(arena);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(arena);
            return;
        }
    }

    // Fall back to free if not allocated from pool
    free(arena);
}

#ifndef _WIN32
static void make_key(void) {
    pthread_key_create(&arena_key, cleanup_arena);
}
#endif

/* Thread-local object cache implementation */

/**
 * @brief Initialize the thread-local object cache system.
 *
 * This function is optimized to:
 * 1. Track initialization state
 * 2. Provide detailed logging
 *
 * @return true on success, false on failure
 */
bool mcp_thread_cache_init_current_thread(void) {
    // Check if already initialized
    if (tls_initialized && mcp_object_cache_system_is_initialized()) {
        mcp_log_debug("Thread-local object cache system already initialized");
        return true;
    }

    // Initialize the object cache system
    bool result = mcp_object_cache_system_init();

    // Update initialization state
    if (result) {
        tls_initialized = true;
        mcp_log_debug("Thread-local object cache system initialized");
    } else {
        mcp_log_error("Failed to initialize thread-local object cache system");
    }

    return result;
}

/**
 * @brief Initialize a specific object cache type for the current thread.
 *
 * This function is optimized to:
 * 1. Automatically initialize the cache system if needed
 * 2. Provide detailed error handling
 *
 * @param type The type of objects to cache
 * @param config Configuration for the cache, or NULL for default configuration
 * @return true on success, false on failure
 */
bool mcp_thread_cache_init_type(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config) {
    // Validate parameters
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return false;
    }

    // Initialize the cache system if needed
    if (!mcp_object_cache_system_is_initialized()) {
        if (!mcp_thread_cache_init_current_thread()) {
            return false;
        }
    }

    // Initialize the specific cache type
    bool result = mcp_object_cache_init(type, config);

    if (result) {
        mcp_log_debug("Thread-local object cache initialized for type %d", type);
    } else {
        mcp_log_error("Failed to initialize thread-local object cache for type %d", type);
    }

    return result;
}

/**
 * @brief Allocate an object from the thread-local cache.
 *
 * This function is optimized to:
 * 1. Track performance statistics
 * 2. Provide fast path for common case
 * 3. Automatically initialize if needed
 *
 * @param type The type of object to allocate
 * @param size Size of the object to allocate
 * @return Pointer to the allocated object, or NULL if allocation failed
 */
void* mcp_thread_cache_alloc_object(mcp_object_cache_type_t type, size_t size) {
    // Validate parameters
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return NULL;
    }

    // Fast path for common case
    if (tls_initialized && mcp_object_cache_system_is_initialized()) {
        void* ptr = mcp_object_cache_alloc(type, size);
        if (ptr) {
            // Update statistics
            tls_stats.cache_allocations++;
            tls_stats.cache_hits++;
            return ptr;
        }

        // Update statistics for miss
        tls_stats.cache_misses++;
    } else {
        // Try to initialize the cache system
        if (!mcp_thread_cache_init_current_thread()) {
            mcp_log_warn("Thread-local object cache system not initialized");
            return NULL;
        }

        // Try allocation again
        void* ptr = mcp_object_cache_alloc(type, size);
        if (ptr) {
            // Update statistics
            tls_stats.cache_allocations++;
            return ptr;
        }

        // Update statistics for miss
        tls_stats.cache_misses++;
    }

    // Allocation failed
    mcp_log_warn("Failed to allocate object from thread-local cache (type: %d, size: %zu)", type, size);
    return NULL;
}

/**
 * @brief Free an object to the thread-local cache.
 *
 * This function is optimized to:
 * 1. Track performance statistics
 * 2. Provide fast path for common case
 * 3. Handle edge cases safely
 *
 * @param type The type of object to free
 * @param ptr Pointer to the object to free
 * @param size Size of the object (optional, can be 0 if unknown)
 */
void mcp_thread_cache_free_object(mcp_object_cache_type_t type, void* ptr, size_t size) {
    // Validate parameters
    if (!ptr) return;

    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        free(ptr); // Fall back to free to avoid memory leaks
        return;
    }

    // Fast path for common case
    if (tls_initialized && mcp_object_cache_system_is_initialized()) {
        mcp_object_cache_free(type, ptr, size);
        // Update statistics
        tls_stats.cache_frees++;
        return;
    }

    // Handle edge case - cache system not initialized
    mcp_log_warn("Thread-local object cache system not initialized");

    // Determine if this is a pool block
    if (mcp_memory_pool_system_is_initialized()) {
        size_t block_size = mcp_pool_get_block_size(ptr);
        if (block_size > 0) {
            // It's a pool-allocated block, return it to the pool
            mcp_pool_free(ptr);
            return;
        }
    }

    // Fall back to free to avoid memory leaks
    free(ptr);
}

/**
 * @brief Get statistics for a thread-local object cache.
 *
 * This function is optimized to:
 * 1. Validate parameters
 * 2. Handle edge cases safely
 *
 * @param type The type of object cache to get statistics for
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_thread_cache_get_object_stats(mcp_object_cache_type_t type, mcp_object_cache_stats_t* stats) {
    // Validate parameters
    if (!stats) {
        mcp_log_error("NULL stats pointer provided");
        return false;
    }

    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return false;
    }

    // Check if cache system is initialized
    if (!tls_initialized || !mcp_object_cache_system_is_initialized()) {
        mcp_log_warn("Thread-local object cache system not initialized");
        return false;
    }

    // Get statistics
    return mcp_object_cache_get_stats(type, stats);
}

/**
 * @brief Flush a thread-local object cache.
 *
 * This function is optimized to:
 * 1. Validate parameters
 * 2. Handle edge cases safely
 *
 * @param type The type of object cache to flush
 */
void mcp_thread_cache_flush_object_cache(mcp_object_cache_type_t type) {
    // Validate parameters
    if (type < 0 || type >= MCP_OBJECT_CACHE_TYPE_COUNT) {
        mcp_log_error("Invalid object cache type: %d", type);
        return;
    }

    // Check if cache system is initialized
    if (!tls_initialized || !mcp_object_cache_system_is_initialized()) {
        mcp_log_warn("Thread-local object cache system not initialized");
        return;
    }

    // Flush the cache
    mcp_object_cache_flush(type);
    mcp_log_debug("Thread-local object cache flushed for type %d", type);
}

/**
 * @brief Clean up all thread-local object caches for the current thread.
 *
 * This function is optimized to:
 * 1. Track initialization state
 * 2. Provide detailed logging
 */
void mcp_thread_cache_cleanup_current_thread(void) {
    // Check if cache system is initialized
    if (!tls_initialized || !mcp_object_cache_system_is_initialized()) {
        return;
    }

    // Shutdown the object cache system
    mcp_object_cache_system_shutdown();
    mcp_log_debug("Thread-local object cache system cleaned up");

    // Update initialization state
    tls_initialized = false;
}

/**
 * @brief Get statistics about thread-local storage usage.
 *
 * This function provides detailed statistics about the usage of thread-local
 * storage, including arena allocations, resets, and cache operations.
 *
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
    size_t* arena_allocations,
    size_t* arena_resets,
    size_t* arena_destroys,
    size_t* cache_allocations,
    size_t* cache_frees,
    size_t* cache_hits,
    size_t* cache_misses
) {
    // Check if TLS is initialized
    if (!tls_initialized) {
        return false;
    }

    // Fill in statistics if pointers are provided
    if (arena_allocations) *arena_allocations = tls_stats.arena_allocations;
    if (arena_resets) *arena_resets = tls_stats.arena_resets;
    if (arena_destroys) *arena_destroys = tls_stats.arena_destroys;
    if (cache_allocations) *cache_allocations = tls_stats.cache_allocations;
    if (cache_frees) *cache_frees = tls_stats.cache_frees;
    if (cache_hits) *cache_hits = tls_stats.cache_hits;
    if (cache_misses) *cache_misses = tls_stats.cache_misses;

    return true;
}

/**
 * @brief Check if thread-local storage is initialized.
 *
 * This function checks if the thread-local storage has been initialized
 * for the current thread.
 *
 * @return true if thread-local storage is initialized, false otherwise
 */
bool mcp_thread_local_is_initialized(void) {
    return tls_initialized;
}
