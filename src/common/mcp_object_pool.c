#include "mcp_object_pool.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

// Internal structure for a node in the free list
typedef struct mcp_pool_node_s {
    struct mcp_pool_node_s* next;
} mcp_pool_node_t;

// Internal structure for the object pool
struct mcp_object_pool_s {
    size_t object_size;
    size_t total_objects;    // Total objects allocated (free + acquired)
    size_t free_objects;     // Number of objects currently in the free list
    size_t max_capacity;     // Maximum number of objects allowed
    mcp_pool_node_t* free_list_head; // Head of the linked list of free objects
    mcp_mutex_t* lock;       // Mutex for thread safety
    void* memory_block;      // Pointer to the initially allocated contiguous block (if any)
};

// Helper function to allocate a new object (not from the pool initially)
static void* allocate_new_object(mcp_object_pool_t* pool) {
    if (pool->max_capacity > 0 && pool->total_objects >= pool->max_capacity) {
        mcp_log_warn("Object pool reached max capacity (%zu)", pool->max_capacity);
        return NULL; // Pool is full
    }

    // Allocate memory for the object itself
    void* obj = malloc(pool->object_size);
    if (!obj) {
        mcp_log_error("Failed to allocate memory for new pool object");
        return NULL;
    }
    pool->total_objects++;
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

    mcp_object_pool_t* pool = (mcp_object_pool_t*)malloc(sizeof(mcp_object_pool_t));
    if (!pool) {
        mcp_log_error("Failed to allocate memory for object pool structure");
        return NULL;
    }

    pool->object_size = object_size;
    pool->total_objects = 0;
    pool->free_objects = 0;
    pool->max_capacity = max_capacity;
    pool->free_list_head = NULL;
    pool->memory_block = NULL;
    pool->lock = mcp_mutex_create();

    if (!pool->lock) {
        mcp_log_error("Failed to create mutex for object pool");
        free(pool);
        return NULL;
    }

    // Pre-allocate initial objects if requested
    if (initial_capacity > 0) {
        // Try allocating as a single block for better locality
        pool->memory_block = malloc(initial_capacity * object_size);
        if (pool->memory_block) {
            pool->total_objects = initial_capacity;
            pool->free_objects = initial_capacity;
            char* current_obj_ptr = (char*)pool->memory_block;
            mcp_pool_node_t* current_node = NULL;
            for (size_t i = 0; i < initial_capacity; ++i) {
                current_node = (mcp_pool_node_t*)current_obj_ptr;
                current_node->next = pool->free_list_head;
                pool->free_list_head = current_node;
                current_obj_ptr += object_size;
            }
        } else {
            // Allocate individually if block allocation fails
            mcp_log_warn("Failed to allocate initial objects as a single block, allocating individually.");
            for (size_t i = 0; i < initial_capacity; ++i) {
                void* obj = allocate_new_object(pool); // total_objects incremented here
                if (obj) {
                    mcp_pool_node_t* node = (mcp_pool_node_t*)obj;
                    node->next = pool->free_list_head;
                    pool->free_list_head = node;
                    pool->free_objects++;
                } else {
                    mcp_log_error("Failed to pre-allocate object %zu/%zu", i + 1, initial_capacity);
                    // Clean up already allocated objects if pre-allocation fails midway
                    mcp_object_pool_destroy(pool);
                    return NULL;
                }
            }
        }
    }

    mcp_log_info("Object pool created: obj_size=%zu, initial=%zu, max=%zu",
            object_size, pool->total_objects, max_capacity);
    return pool;
}

void mcp_object_pool_destroy(mcp_object_pool_t* pool) {
    if (!pool) {
        return;
    }

    mcp_mutex_lock(pool->lock);

    // If objects were allocated in a single block, just free the block
    if (pool->memory_block) {
        free(pool->memory_block);
    } else {
        // Otherwise, need to free individually (only those currently in the free list)
        // Note: This assumes acquired objects are managed/freed elsewhere or released before destroy.
        // A more robust implementation might track all allocated objects.
        mcp_pool_node_t* current = pool->free_list_head;
        mcp_pool_node_t* next;
        size_t freed_count = 0;
        while (current) {
            next = current->next;
            free(current);
            freed_count++;
            current = next;
        }
        if (freed_count != pool->free_objects) {
            mcp_log_warn("Mismatch freeing objects: freed %zu, expected %zu (acquired objects not freed)", freed_count, pool->free_objects);
        }
        // Ideally, we should also free objects that were acquired but not released.
        // This simple version doesn't track acquired objects separately from the initial block.
    }

    pool->free_list_head = NULL;
    pool->total_objects = 0;
    pool->free_objects = 0;

    // Unlock before destroying the mutex itself
    mcp_mutex_unlock(pool->lock);
    mcp_mutex_destroy(pool->lock);
    pool->lock = NULL; // Avoid double destroy

    // Free the pool structure itself
    free(pool);
    mcp_log_info("Object pool destroyed");
}

void* mcp_object_pool_acquire(mcp_object_pool_t* pool) {
    if (!pool) {
        return NULL;
    }

    mcp_mutex_lock(pool->lock);

    // Try to get from free list first
    if (pool->free_list_head) {
        mcp_pool_node_t* node = pool->free_list_head;
        pool->free_list_head = node->next;
        pool->free_objects--;
        mcp_mutex_unlock(pool->lock);
        // Optional: Clear memory before returning? Depends on usage.
        // memset(node, 0, pool->object_size);
        return (void*)node;
    }

    // Free list is empty, try to allocate a new one if allowed
    void* new_obj = allocate_new_object(pool); // total_objects incremented here if successful
    mcp_mutex_unlock(pool->lock);

    return new_obj;
}

bool mcp_object_pool_release(mcp_object_pool_t* pool, void* obj) {
    if (!pool || !obj) {
        return false;
    }

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
    // Reading size_t might be atomic on some platforms, but lock for safety
    mcp_mutex_lock(pool->lock);
    size_t count = pool->free_objects;
    mcp_mutex_unlock(pool->lock);
    return count;
}

size_t mcp_object_pool_get_total_count(mcp_object_pool_t* pool) {
    if (!pool) return 0;
    mcp_mutex_lock(pool->lock);
    size_t count = pool->total_objects;
    mcp_mutex_unlock(pool->lock);
    return count;
}
