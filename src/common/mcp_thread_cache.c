#include "mcp_thread_cache.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_log.h"
#include <stdlib.h>

// Define the cache sizes
#define SMALL_CACHE_SIZE 16
#define MEDIUM_CACHE_SIZE 8
#define LARGE_CACHE_SIZE 4

// Thread-local cache structures
#ifdef _WIN32
__declspec(thread) static void* tls_small_cache[SMALL_CACHE_SIZE] = {NULL};
__declspec(thread) static void* tls_medium_cache[MEDIUM_CACHE_SIZE] = {NULL};
__declspec(thread) static void* tls_large_cache[LARGE_CACHE_SIZE] = {NULL};
__declspec(thread) static size_t tls_small_count = 0;
__declspec(thread) static size_t tls_medium_count = 0;
__declspec(thread) static size_t tls_large_count = 0;
__declspec(thread) static size_t tls_cache_hits = 0;
__declspec(thread) static size_t tls_cache_misses = 0;
__declspec(thread) static size_t tls_cache_flushes = 0;
__declspec(thread) static bool tls_cache_initialized = false;
#else
__thread static void* tls_small_cache[SMALL_CACHE_SIZE] = {NULL};
__thread static void* tls_medium_cache[MEDIUM_CACHE_SIZE] = {NULL};
__thread static void* tls_large_cache[LARGE_CACHE_SIZE] = {NULL};
__thread static size_t tls_small_count = 0;
__thread static size_t tls_medium_count = 0;
__thread static size_t tls_large_count = 0;
__thread static size_t tls_cache_hits = 0;
__thread static size_t tls_cache_misses = 0;
__thread static size_t tls_cache_flushes = 0;
__thread static bool tls_cache_initialized = false;
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
    tls_cache_misses = 0;
    tls_cache_flushes = 0;

    tls_cache_initialized = true;
    mcp_log_debug("Thread-local cache initialized");

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

    // Determine which cache to use based on size
    if (size <= SMALL_BLOCK_SIZE) {
        if (tls_small_count > 0) {
            tls_cache_hits++;
            return tls_small_cache[--tls_small_count];
        }
    } else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count > 0) {
            tls_cache_hits++;
            return tls_medium_cache[--tls_medium_count];
        }
    } else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count > 0) {
            tls_cache_hits++;
            return tls_large_cache[--tls_large_count];
        }
    }

    // Cache miss, allocate from the pool if initialized, otherwise use malloc
    tls_cache_misses++;
    if (mcp_memory_pool_system_is_initialized()) {
        return mcp_pool_alloc(size);
    } else {
        // Fall back to malloc if memory pool system is not initialized
        return malloc(size);
    }
}

void mcp_thread_cache_free(void* ptr, size_t size) {
    if (!ptr) return;

    // If thread cache is not initialized, fall back to direct free to avoid circular dependency
    if (!tls_cache_initialized) {
        free(ptr);
        return;
    }

    // Determine which cache to use based on size
    if (size <= SMALL_BLOCK_SIZE) {
        if (tls_small_count < SMALL_CACHE_SIZE) {
            tls_small_cache[tls_small_count++] = ptr;
            return;
        }
    } else if (size <= MEDIUM_BLOCK_SIZE) {
        if (tls_medium_count < MEDIUM_CACHE_SIZE) {
            tls_medium_cache[tls_medium_count++] = ptr;
            return;
        }
    } else if (size <= LARGE_BLOCK_SIZE) {
        if (tls_large_count < LARGE_CACHE_SIZE) {
            tls_large_cache[tls_large_count++] = ptr;
            return;
        }
    }

    // Cache is full or size is too large, return to the pool if initialized, otherwise use free
    if (mcp_memory_pool_system_is_initialized()) {
        mcp_pool_free(ptr);
    } else {
        // Fall back to free if memory pool system is not initialized
        free(ptr);
    }
}

bool mcp_thread_cache_get_stats(mcp_thread_cache_stats_t* stats) {
    if (!stats || !tls_cache_initialized) {
        return false;
    }

    stats->small_cache_count = tls_small_count;
    stats->medium_cache_count = tls_medium_count;
    stats->large_cache_count = tls_large_count;
    stats->cache_hits = tls_cache_hits;
    stats->cache_misses = tls_cache_misses;
    stats->cache_flushes = tls_cache_flushes;

    return true;
}

void mcp_thread_cache_flush(void) {
    if (!tls_cache_initialized) {
        return;
    }

    // Flush small cache
    for (size_t i = 0; i < tls_small_count; i++) {
        mcp_pool_free(tls_small_cache[i]);
        tls_small_cache[i] = NULL;
    }
    tls_small_count = 0;

    // Flush medium cache
    for (size_t i = 0; i < tls_medium_count; i++) {
        mcp_pool_free(tls_medium_cache[i]);
        tls_medium_cache[i] = NULL;
    }
    tls_medium_count = 0;

    // Flush large cache
    for (size_t i = 0; i < tls_large_count; i++) {
        mcp_pool_free(tls_large_cache[i]);
        tls_large_cache[i] = NULL;
    }
    tls_large_count = 0;

    tls_cache_flushes++;
    mcp_log_debug("Thread-local cache flushed");
}
