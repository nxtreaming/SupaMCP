#include "mcp_buffer_pool.h"
#include "mcp_sync.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <stdio.h>

// Magic number to identify buffer nodes from this pool
#define MCP_BUFFER_POOL_MAGIC 0xB0FFEE42

/**
 * @internal
 * @brief Represents the header for a buffer block in the pool's free list.
 * The actual buffer memory follows immediately after this header.
 */
typedef struct mcp_buffer_node {
    uint32_t magic;              /**< Magic number to identify valid nodes. */
    struct mcp_buffer_pool* pool; /**< Pointer to the owning pool. */
    struct mcp_buffer_node* next; /**< Pointer to the next free buffer node. */
    // The buffer data starts immediately after this structure in memory.
} mcp_buffer_node_t;

// Calculate the aligned offset for the buffer data area
static inline size_t get_aligned_offset(void) {
    size_t offset = sizeof(mcp_buffer_node_t);
    // Ensure the buffer data area is aligned to 8 bytes
    return (offset + 7) & ~7;
}

/**
 * @internal
 * @brief Internal structure for the buffer pool instance.
 */
struct mcp_buffer_pool {
    size_t buffer_size;         /**< The fixed size of each buffer in the pool. */
    mcp_buffer_node_t* free_list; /**< Head of the linked list of free buffer nodes. */
    mcp_mutex_t* mutex;        /**< Mutex for thread-safe access to the free list. */
    size_t total_blocks;        /**< Total number of blocks allocated for this pool. */
    size_t allocated_blocks;    /**< Number of blocks currently allocated (acquired). */
};

mcp_buffer_pool_t* mcp_buffer_pool_create(size_t buffer_size, size_t num_buffers) {
    if (buffer_size == 0 || num_buffers == 0)
        return NULL;

    // Allocate the main pool structure
    mcp_buffer_pool_t* pool = (mcp_buffer_pool_t*)malloc(sizeof(mcp_buffer_pool_t));
    if (pool == NULL) {
        mcp_log_error("Failed to allocate buffer pool structure.");
        return NULL;
    }

    pool->buffer_size = buffer_size;
    pool->free_list = NULL;
    pool->total_blocks = 0;
    pool->allocated_blocks = 0;

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
        size_t block_size = get_aligned_offset() + buffer_size;
        mcp_buffer_node_t* node = (mcp_buffer_node_t*)malloc(block_size);
        if (node == NULL) {
            mcp_log_error("Failed to allocate buffer block %zu/%zu for pool.", i + 1, num_buffers);
            mcp_buffer_pool_destroy(pool);
            return NULL;
        }

        // Initialize node metadata
        node->magic = MCP_BUFFER_POOL_MAGIC;
        node->pool = pool;

        // Add the new block (node) to the free list
        node->next = pool->free_list;
        pool->free_list = node;

        // Increment total block count
        pool->total_blocks++;
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

    // Check if there are still allocated blocks
    if (pool->allocated_blocks > 0) {
        mcp_log_warn("Buffer pool being destroyed with %zu/%zu blocks still allocated. "
                    "This may indicate a memory leak.",
                    pool->allocated_blocks, pool->total_blocks);
    }

    // Free all combined node+buffer blocks in the free list
    mcp_buffer_node_t* current = pool->free_list;
    size_t freed_blocks = 0;
    while (current != NULL) {
        mcp_buffer_node_t* next = current->next;
        free(current);
        current = next;
        freed_blocks++;
    }

    // Mark list as empty
    pool->free_list = NULL;

    // Log detailed information about the pool state
    mcp_log_debug("Buffer pool destroyed: %zu blocks freed, %zu blocks were still allocated "
                 "(total: %zu blocks).",
                 freed_blocks, pool->allocated_blocks, pool->total_blocks);

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);
    // Destroy mutex
    mcp_mutex_destroy(pool->mutex);

    // Free the pool structure itself
    free(pool);
}

void* mcp_buffer_pool_acquire(mcp_buffer_pool_t* pool) {
    if (pool == NULL)
        return NULL;

    void* buffer = NULL;

    // Lock mutex
    mcp_mutex_lock(pool->mutex);

    // Check if the free list is empty
    if (pool->free_list != NULL) {
        // Get the first node from the free list
        mcp_buffer_node_t* node = pool->free_list;
        // Update the free list head
        pool->free_list = node->next;
        // Calculate the pointer to the buffer data area (aligned after the node header)
        buffer = (void*)((char*)node + get_aligned_offset());
        // DO NOT free the node structure - it's part of the block being returned implicitly

        // Update allocated blocks count
        pool->allocated_blocks++;
    } else {
        // Pool is empty, try to dynamically allocate a new buffer
        size_t block_size = get_aligned_offset() + pool->buffer_size;
        mcp_buffer_node_t* node = (mcp_buffer_node_t*)malloc(block_size);
        if (node != NULL) {
            // Initialize node metadata
            node->magic = MCP_BUFFER_POOL_MAGIC;
            node->pool = pool;
            node->next = NULL;

            // Calculate the pointer to the buffer data area (aligned after the node header)
            buffer = (void*)((char*)node + get_aligned_offset());

            // Update statistics
            pool->total_blocks++;
            pool->allocated_blocks++;

            mcp_log_debug("Dynamically allocated new buffer for pool %p (total: %zu, allocated: %zu)",
                         pool, pool->total_blocks, pool->allocated_blocks);
        } else {
            // Failed to allocate new buffer
            mcp_log_error("Buffer pool empty and failed to dynamically allocate new buffer");
            // Returning NULL indicates failure to acquire
        }
    }

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);

    return buffer;
}

void mcp_buffer_pool_release(mcp_buffer_pool_t* pool, void* buffer) {
    if (pool == NULL || buffer == NULL)
        return;

    // Calculate the address of the node header from the buffer pointer
    mcp_buffer_node_t* node = (mcp_buffer_node_t*)((char*)buffer - get_aligned_offset());

    // Validate the node before proceeding
    if (node->magic != MCP_BUFFER_POOL_MAGIC) {
        mcp_log_error("Invalid buffer being released to pool: magic number mismatch (expected: 0x%X, got: 0x%X)",
                     MCP_BUFFER_POOL_MAGIC, node->magic);
        return;
    }

    if (node->pool != pool) {
        mcp_log_error("Buffer being released to wrong pool (buffer pool: %p, target pool: %p)",
                     node->pool, pool);
        return;
    }

    // Lock mutex
    mcp_mutex_lock(pool->mutex);

    // Check if this node is already in the free list (double free)
    mcp_buffer_node_t* current = pool->free_list;
    while (current != NULL) {
        if (current == node) {
            mcp_log_error("Double free detected: buffer %p already in free list", buffer);
            mcp_mutex_unlock(pool->mutex);
            return;
        }
        current = current->next;
    }

    // Add the node (the entire block) back to the head of the free list
    node->next = pool->free_list;
    pool->free_list = node;

    // Update allocated blocks count
    if (pool->allocated_blocks > 0) {
        pool->allocated_blocks--;
    }

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);
}

size_t mcp_buffer_pool_get_buffer_size(const mcp_buffer_pool_t* pool) {
    if (pool == NULL)
        return 0;

    // No mutex needed for read-only access to buffer_size after creation
    return pool->buffer_size;
}

void mcp_buffer_pool_get_stats(const mcp_buffer_pool_t* pool,
                              size_t* total_blocks,
                              size_t* allocated_blocks,
                              size_t* free_blocks) {
    if (pool == NULL) {
        if (total_blocks) *total_blocks = 0;
        if (allocated_blocks) *allocated_blocks = 0;
        if (free_blocks) *free_blocks = 0;
        return;
    }

    // Lock mutex for consistent stats
    mcp_mutex_lock(pool->mutex);

    if (total_blocks) *total_blocks = pool->total_blocks;
    if (allocated_blocks) *allocated_blocks = pool->allocated_blocks;
    if (free_blocks) *free_blocks = pool->total_blocks - pool->allocated_blocks;

    // Unlock mutex
    mcp_mutex_unlock(pool->mutex);
}
