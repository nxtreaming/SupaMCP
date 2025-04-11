#include "mcp_buffer_pool.h"
#include "mcp_sync.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <stdio.h>

/**
 * @internal
 * @brief Represents the header for a buffer block in the pool's free list.
 * The actual buffer memory follows immediately after this header.
 */
typedef struct mcp_buffer_node {
    struct mcp_buffer_node* next; /**< Pointer to the next free buffer node. */
    // The buffer data starts immediately after this structure in memory.
} mcp_buffer_node_t;

/**
 * @internal
 * @brief Internal structure for the buffer pool instance.
 */
struct mcp_buffer_pool {
    size_t buffer_size;         /**< The fixed size of each buffer in the pool. */
    mcp_buffer_node_t* free_list; /**< Head of the linked list of free buffer nodes. */
    mcp_mutex_t* mutex;        /**< Mutex for thread-safe access to the free list. */
};

mcp_buffer_pool_t* mcp_buffer_pool_create(size_t buffer_size, size_t num_buffers) {
    if (buffer_size == 0 || num_buffers == 0) {
        return NULL; // Invalid parameters
    }

    // Allocate the main pool structure
    mcp_buffer_pool_t* pool = (mcp_buffer_pool_t*)malloc(sizeof(mcp_buffer_pool_t));
    if (pool == NULL) {
        mcp_log_error("Failed to allocate buffer pool structure.");
        return NULL;
    }

    pool->buffer_size = buffer_size;
    pool->free_list = NULL;

    // Initialize mutex
    pool->mutex =  mcp_mutex_create();
    if (!pool->mutex) {
        mcp_log_error("Failed to initialize buffer pool mutex.");
        free(pool);
        return NULL;
    }

    // Pre-allocate the combined node+buffer blocks
    for (size_t i = 0; i < num_buffers; ++i) {
        // Allocate memory for the node header AND the buffer contiguously
        size_t block_size = sizeof(mcp_buffer_node_t) + buffer_size;
        mcp_buffer_node_t* node = (mcp_buffer_node_t*)malloc(block_size);
        if (node == NULL) {
            mcp_log_error("Failed to allocate buffer block %zu/%zu for pool.", i + 1, num_buffers);
            mcp_buffer_pool_destroy(pool); // Clean up partially created pool
            return NULL;
        }

        // Add the new block (node) to the free list
        node->next = pool->free_list;
        pool->free_list = node;
    }

    mcp_log_debug("Buffer pool created with %zu buffers of size %zu.", num_buffers, buffer_size);
    return pool;
}

void mcp_buffer_pool_destroy(mcp_buffer_pool_t* pool) {
    if (pool == NULL) {
        return;
    }

    // Lock mutex before accessing list
    mcp_mutex_lock(pool->mutex);

    // Free all combined node+buffer blocks in the free list
    mcp_buffer_node_t* current = pool->free_list;
    while (current != NULL) {
        mcp_buffer_node_t* next = current->next;
        free(current); // Free the entire block (node header + buffer data)
        current = next;
    }
    pool->free_list = NULL; // Mark list as empty

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);
    // Destroy mutex
    mcp_mutex_destroy(pool->mutex);

    // Free the pool structure itself
    free(pool);
    mcp_log_debug("Buffer pool destroyed.");
}

void* mcp_buffer_pool_acquire(mcp_buffer_pool_t* pool) {
    if (pool == NULL) {
        return NULL;
    }

    void* buffer = NULL;

    // Lock mutex
    mcp_mutex_lock(pool->mutex);

    // Check if the free list is empty
    if (pool->free_list != NULL) {
        // Get the first node from the free list
        mcp_buffer_node_t* node = pool->free_list;
        // Update the free list head
        pool->free_list = node->next;
        // Calculate the pointer to the buffer data area (right after the node header)
        buffer = (void*)((char*)node + sizeof(mcp_buffer_node_t));
        // DO NOT free the node structure - it's part of the block being returned implicitly
    } else {
        // Pool is empty, log a warning or handle as needed
        mcp_log_warn("Buffer pool empty, cannot acquire buffer.");
        // Returning NULL indicates failure to acquire
    }

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);

    return buffer;
}

void mcp_buffer_pool_release(mcp_buffer_pool_t* pool, void* buffer) {
    if (pool == NULL || buffer == NULL) {
        return; // Invalid arguments
    }

    // Calculate the address of the node header from the buffer pointer
    mcp_buffer_node_t* node = (mcp_buffer_node_t*)((char*)buffer - sizeof(mcp_buffer_node_t));

    // Lock mutex
    mcp_mutex_lock(pool->mutex);

    // Add the node (the entire block) back to the head of the free list
    node->next = pool->free_list;
    pool->free_list = node;

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);
}

size_t mcp_buffer_pool_get_buffer_size(const mcp_buffer_pool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    // No mutex needed for read-only access to buffer_size after creation
    return pool->buffer_size;
}
