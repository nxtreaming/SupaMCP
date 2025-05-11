#include "mcp_object_pool.h"
#include "mcp_log.h"
#include "mcp_cache_aligned.h"
#include "mcp_atom.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Platform-specific thread-local storage
#ifdef _WIN32
#include <windows.h>
#define MCP_THREAD_LOCAL __declspec(thread)
#else
#include <pthread.h>
#define MCP_THREAD_LOCAL __thread
#endif


// Memory alignment for better performance
#define MCP_OBJECT_ALIGN_SIZE 8
#define MCP_OBJECT_ALIGN_UP(value) (((value) + (MCP_OBJECT_ALIGN_SIZE - 1)) & ~(MCP_OBJECT_ALIGN_SIZE - 1))

// Thread-local cache configuration
#define TLS_CACHE_SIZE 8  // Number of objects to cache per thread
#define TLS_CACHE_ENABLED 1  // Set to 0 to disable thread-local caching

// Internal structure for a node in the free list - cache aligned
typedef MCP_CACHE_ALIGNED struct mcp_pool_node_s {
    struct mcp_pool_node_s* next;
    char padding[MCP_CACHE_LINE_SIZE - sizeof(struct mcp_pool_node_s*)];
} mcp_pool_node_t;

// Thread-local cache for object pools
typedef struct {
    mcp_object_pool_t* pool;     // The pool this cache belongs to
    void* objects[TLS_CACHE_SIZE]; // Cached objects
    size_t count;                // Number of objects in the cache
} mcp_object_pool_tls_cache_t;

// Maximum number of pools to cache per thread
#define MAX_CACHED_POOLS 8

// Thread-local cache for multiple pools
static MCP_THREAD_LOCAL mcp_object_pool_tls_cache_t tls_caches[MAX_CACHED_POOLS] = {0};

// Internal structure for the object pool - cache aligned for better performance
typedef struct mcp_object_pool_s {
    // Frequently accessed fields in first cache line
    size_t object_size;                // Size of each object
    size_t aligned_size;               // Aligned size of each object
    size_t total_objects;              // Total objects allocated (free + acquired)
    size_t free_objects;               // Number of objects currently in the free list
    size_t max_capacity;               // Maximum number of objects allowed
    mcp_pool_node_t* free_list_head;   // Head of the linked list of free objects
    mcp_mutex_t* lock;                 // Mutex for thread safety
    void* memory_block;                // Pointer to the initially allocated contiguous block (if any)

    // Statistics - updated atomically when possible
    size_t peak_usage;                 // Peak number of objects in use
    size_t current_usage;              // Current number of objects in use

    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE -
                ((2 * sizeof(size_t) + 6 * sizeof(size_t) +
                  sizeof(mcp_pool_node_t*) + sizeof(mcp_mutex_t*) +
                  sizeof(void*)) % MCP_CACHE_LINE_SIZE)];
} MCP_CACHE_ALIGNED mcp_object_pool_t;

// Forward declarations for thread-local cache functions
static mcp_object_pool_tls_cache_t* find_tls_cache(mcp_object_pool_t* pool);
static mcp_object_pool_tls_cache_t* create_tls_cache(mcp_object_pool_t* pool);
static void* aligned_malloc(size_t size);
static void aligned_free(void* ptr);

// Helper function for aligned memory allocation
static void* aligned_malloc(size_t size) {
#ifdef _WIN32
    return _aligned_malloc(size, MCP_OBJECT_ALIGN_SIZE);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, MCP_OBJECT_ALIGN_SIZE, size) != 0) {
        return NULL;
    }
    return ptr;
#endif
}

// Helper function for aligned memory deallocation
static void aligned_free(void* ptr) {
    // Add NULL check
    if (ptr == NULL) {
        return;
    }

#ifdef _WIN32
    // On Windows, _aligned_free handles NULL pointers safely
    _aligned_free(ptr);
#else
    // On POSIX systems, free handles NULL pointers safely
    free(ptr);
#endif
}

// Helper function to allocate a new object (not from the pool initially)
static void* allocate_new_object(mcp_object_pool_t* pool) {
    if (pool->max_capacity > 0 && pool->total_objects >= pool->max_capacity) {
        mcp_log_warn("Object pool reached max capacity (%zu)", pool->max_capacity);
        return NULL; // Pool is full
    }

    // Allocate aligned memory for the object
    void* obj = aligned_malloc(pool->aligned_size);
    if (!obj) {
        mcp_log_error("Failed to allocate memory for new pool object");
        return NULL;
    }

    // Update statistics
    ATOMIC_INCREMENT(pool->total_objects);
    ATOMIC_INCREMENT(pool->current_usage);
    ATOMIC_EXCHANGE_MAX(pool->peak_usage, pool->current_usage);

    return obj;
}

mcp_object_pool_t* mcp_object_pool_create(size_t object_size, size_t initial_capacity, size_t max_capacity) {
    if (object_size < sizeof(mcp_pool_node_t)) {
        // Ensure objects are large enough to hold the 'next' pointer when free
        object_size = sizeof(mcp_pool_node_t);
        mcp_log_warn("Object size increased to %zu to accommodate pool node", object_size);
    }
    if (max_capacity > 0 && initial_capacity > max_capacity) {
        initial_capacity = max_capacity;
        mcp_log_warn("Initial capacity adjusted to max capacity (%zu)", max_capacity);
    }

    // Allocate cache-aligned pool structure
    mcp_object_pool_t* pool = (mcp_object_pool_t*)aligned_malloc(sizeof(mcp_object_pool_t));
    if (!pool) {
        mcp_log_error("Failed to allocate memory for object pool structure");
        return NULL;
    }

    // Clear the structure to initialize all fields to zero
    memset(pool, 0, sizeof(mcp_object_pool_t));

    // Initialize the pool structure
    pool->object_size = object_size;
    pool->aligned_size = MCP_OBJECT_ALIGN_UP(object_size);
    pool->max_capacity = max_capacity;
    pool->lock = mcp_mutex_create();

    if (!pool->lock) {
        mcp_log_error("Failed to create mutex for object pool");
        aligned_free(pool);
        return NULL;
    }

    // Pre-allocate initial objects if requested
    if (initial_capacity > 0) {
        // Try allocating as a single block for better locality
        // Use aligned allocation for better memory access
        pool->memory_block = aligned_malloc(initial_capacity * pool->aligned_size);
        if (pool->memory_block) {
            pool->total_objects = initial_capacity;
            pool->free_objects = initial_capacity;
            char* current_obj_ptr = (char*)pool->memory_block;
            mcp_pool_node_t* current_node = NULL;
            for (size_t i = 0; i < initial_capacity; ++i) {
                current_node = (mcp_pool_node_t*)current_obj_ptr;
                current_node->next = pool->free_list_head;
                pool->free_list_head = current_node;
                current_obj_ptr += pool->aligned_size; // Use aligned size for proper spacing
            }
        } else {
            // Allocate individually if block allocation fails
            mcp_log_warn("Failed to allocate initial objects as a single block, allocating individually.");
            for (size_t i = 0; i < initial_capacity; ++i) {
                void* obj = aligned_malloc(pool->aligned_size);
                if (obj) {
                    mcp_pool_node_t* node = (mcp_pool_node_t*)obj;
                    node->next = pool->free_list_head;
                    pool->free_list_head = node;
                    pool->free_objects++;
                    pool->total_objects++;
                } else {
                    mcp_log_error("Failed to pre-allocate object %zu/%zu", i + 1, initial_capacity);
                    // Clean up already allocated objects if pre-allocation fails midway
                    mcp_object_pool_destroy(pool);
                    return NULL;
                }
            }
        }
    }

    mcp_log_info("Object pool created: obj_size=%zu, aligned_size=%zu, initial=%zu, max=%zu",
            object_size, pool->aligned_size, pool->total_objects, max_capacity);
    return pool;
}

void mcp_object_pool_destroy(mcp_object_pool_t* pool) {
    if (!pool) {
        return;
    }

    // First, clear any thread-local caches for this pool
#if TLS_CACHE_ENABLED
    for (int i = 0; i < MAX_CACHED_POOLS; i++) {
        if (tls_caches[i].pool == pool) {
            // Free cached objects with careful validation
            for (size_t j = 0; j < tls_caches[i].count; j++) {
                void* obj = tls_caches[i].objects[j];

                // Skip NULL pointers
                if (obj == NULL) {
                    continue;
                }

                // Check if the object is part of the memory block (if we have one)
                if (pool->memory_block) {
                    char* block_start = (char*)pool->memory_block;
                    char* block_end = block_start + (pool->total_objects * pool->aligned_size);

                    // Only free if the object is within our memory block
                    if ((char*)obj >= block_start && (char*)obj < block_end) {
                        // Object is part of our memory block, safe to free
                        // But actually we don't need to free individual objects if they're part of the memory block
                        // as we'll free the whole block later
                    } else {
                        // Object is not part of our memory block, might be individually allocated
                        // or might be invalid - log a warning and skip it
                        mcp_log_warn("Object pool destroy: cached object %p is outside memory block range [%p-%p]",
                                    obj, (void*)block_start, (void*)block_end);
                        continue;
                    }
                } else {
                    // We don't have a memory block, so objects were individually allocated
                    // We'll try to free them, but be careful
                    aligned_free(obj);
                }
            }

            // Clear the cache
            memset(&tls_caches[i], 0, sizeof(mcp_object_pool_tls_cache_t));
        }
    }
#endif

    mcp_mutex_lock(pool->lock);

    // If objects were allocated in a single block, just free the block
    if (pool->memory_block) {
        aligned_free(pool->memory_block);
        pool->memory_block = NULL;
    } else {
        // Otherwise, need to free individually (only those currently in the free list)
        // Note: This assumes acquired objects are managed/freed elsewhere or released before destroy.
        // A more robust implementation might track all allocated objects.
        mcp_pool_node_t* current = pool->free_list_head;
        mcp_pool_node_t* next;
        size_t freed_count = 0;

        while (current) {
            next = current->next;
            // Add NULL check before freeing
            if (current != NULL) {
                aligned_free(current);
                freed_count++;
            }
            current = next;
        }

        if (freed_count != pool->free_objects && pool->free_objects > 0) {
            mcp_log_warn("Mismatch freeing objects: freed %zu, expected %zu (acquired objects not freed)",
                        freed_count, pool->free_objects);
        }
        // Ideally, we should also free objects that were acquired but not released.
        // This simple version doesn't track acquired objects separately from the initial block.
    }

    pool->free_list_head = NULL;
    pool->total_objects = 0;
    pool->free_objects = 0;
    pool->current_usage = 0;
    pool->peak_usage = 0;

    // Unlock before destroying the mutex itself
    mcp_mutex_unlock(pool->lock);
    mcp_mutex_destroy(pool->lock);
    pool->lock = NULL; // Avoid double destroy

    // Log statistics before destroying
    mcp_log_info("Object pool destroyed: total_objects=%zu, peak_usage=%zu",
                pool->total_objects, pool->peak_usage);

    // Free the pool structure itself
    if (pool != NULL) {
        aligned_free(pool);
    }
}

void* mcp_object_pool_acquire(mcp_object_pool_t* pool) {
    if (!pool) {
        return NULL;
    }

    void* obj = NULL;

#if TLS_CACHE_ENABLED
    // Try to get an object from the thread-local cache first
    mcp_object_pool_tls_cache_t* tls_cache = find_tls_cache(pool);
    if (tls_cache && tls_cache->count > 0) {
        // Get an object from the thread-local cache
        obj = tls_cache->objects[--tls_cache->count];

        // Update statistics
        ATOMIC_INCREMENT(pool->current_usage);
        ATOMIC_EXCHANGE_MAX(pool->peak_usage, pool->current_usage);

        return obj;
    }
#endif

    mcp_mutex_lock(pool->lock);

    // Try to get from free list first
    if (pool->free_list_head) {
        mcp_pool_node_t* node = pool->free_list_head;
        pool->free_list_head = node->next;
        pool->free_objects--;
        mcp_mutex_unlock(pool->lock);

        // Update statistics
        ATOMIC_INCREMENT(pool->current_usage);
        ATOMIC_EXCHANGE_MAX(pool->peak_usage, pool->current_usage);

        // Optional: Clear memory before returning? Depends on usage.
        // memset(node, 0, pool->aligned_size);
        return (void*)node;
    }

    // Free list is empty, try to allocate a new one if allowed
    mcp_mutex_unlock(pool->lock);

    // Allocate a new object - statistics updated inside allocate_new_object
    return allocate_new_object(pool);
}

bool mcp_object_pool_release(mcp_object_pool_t* pool, void* obj) {
    if (!pool || !obj) {
        return false;
    }

    // Update statistics
    ATOMIC_DECREMENT(pool->current_usage);

#if TLS_CACHE_ENABLED
    // Try to add the object to the thread-local cache first
    mcp_object_pool_tls_cache_t* tls_cache = find_tls_cache(pool);
    if (!tls_cache) {
        // Create a new thread-local cache for this pool
        tls_cache = create_tls_cache(pool);
    }

    if (tls_cache && tls_cache->count < TLS_CACHE_SIZE) {
        // Add the object to the thread-local cache
        tls_cache->objects[tls_cache->count++] = obj;
        return true;
    }
#endif

    mcp_mutex_lock(pool->lock);

    // TODO: Add optional check: is obj actually part of this pool's memory block?
    // This requires more complex tracking if not using a single block.

    mcp_pool_node_t* node = (mcp_pool_node_t*)obj;
    node->next = pool->free_list_head;
    pool->free_list_head = node;
    pool->free_objects++;

    mcp_mutex_unlock(pool->lock);

    return true;
}

size_t mcp_object_pool_get_free_count(mcp_object_pool_t* pool) {
    if (!pool) return 0;

    size_t count = 0;

#if TLS_CACHE_ENABLED
    // Count objects in thread-local caches
    for (int i = 0; i < MAX_CACHED_POOLS; i++) {
        if (tls_caches[i].pool == pool) {
            count += tls_caches[i].count;
        }
    }
#endif

    // Add objects in the global pool
    mcp_mutex_lock(pool->lock);
    count += pool->free_objects;
    mcp_mutex_unlock(pool->lock);

    return count;
}

size_t mcp_object_pool_get_total_count(mcp_object_pool_t* pool) {
    if (!pool) return 0;

    // No need for a lock since we're just reading a value that's updated atomically
    return pool->total_objects;
}

// Get statistics about the object pool
bool mcp_object_pool_get_stats(mcp_object_pool_t* pool,
                              size_t* total_objects,
                              size_t* free_objects,
                              size_t* current_usage,
                              size_t* peak_usage) {
    if (!pool) {
        return false;
    }

    // Get free count (includes thread-local caches)
    size_t free_count = mcp_object_pool_get_free_count(pool);

    // Set output parameters
    if (total_objects) *total_objects = pool->total_objects;
    if (free_objects) *free_objects = free_count;
    if (current_usage) *current_usage = pool->current_usage;
    if (peak_usage) *peak_usage = pool->peak_usage;

    return true;
}

// Helper function to find a thread-local cache for a pool
static mcp_object_pool_tls_cache_t* find_tls_cache(mcp_object_pool_t* pool) {
    for (int i = 0; i < MAX_CACHED_POOLS; i++) {
        if (tls_caches[i].pool == pool) {
            return &tls_caches[i];
        }
    }
    return NULL;
}

// Helper function to create a thread-local cache for a pool
static mcp_object_pool_tls_cache_t* create_tls_cache(mcp_object_pool_t* pool) {
    // First, look for an empty slot
    for (int i = 0; i < MAX_CACHED_POOLS; i++) {
        if (tls_caches[i].pool == NULL) {
            tls_caches[i].pool = pool;
            tls_caches[i].count = 0;
            return &tls_caches[i];
        }
    }

    // No empty slots, replace the first one (simple LRU strategy)
    // In a more sophisticated implementation, we could track usage and replace the least recently used
    if (tls_caches[0].count > 0) {
        // Return cached objects to the pool
        mcp_mutex_lock(tls_caches[0].pool->lock);
        for (size_t i = 0; i < tls_caches[0].count; i++) {
            mcp_pool_node_t* node = (mcp_pool_node_t*)tls_caches[0].objects[i];
            node->next = tls_caches[0].pool->free_list_head;
            tls_caches[0].pool->free_list_head = node;
            tls_caches[0].pool->free_objects++;
        }
        mcp_mutex_unlock(tls_caches[0].pool->lock);
    }

    // Reset the cache for the new pool
    tls_caches[0].pool = pool;
    tls_caches[0].count = 0;
    return &tls_caches[0];
}
