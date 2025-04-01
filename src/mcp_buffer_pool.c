#include "mcp_buffer_pool.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <stdio.h>

// Platform-specific mutex includes
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/**
 * @internal
 * @brief Represents a node in the linked list of free buffers.
 * Each node points to a pre-allocated buffer chunk.
 */
typedef struct mcp_buffer_node {
    struct mcp_buffer_node* next; /**< Pointer to the next free buffer node. */
    void* buffer;                 /**< Pointer to the actual buffer memory. */
} mcp_buffer_node_t;

/**
 * @internal
 * @brief Internal structure for the buffer pool instance.
 */
struct mcp_buffer_pool {
    size_t buffer_size;         /**< The fixed size of each buffer in the pool. */
    mcp_buffer_node_t* free_list; /**< Head of the linked list of free buffer nodes. */
#ifdef _WIN32
    CRITICAL_SECTION mutex;     /**< Mutex for thread-safe access to the free list. */
#else
    pthread_mutex_t mutex;      /**< Mutex for thread-safe access to the free list. */
#endif
};

mcp_buffer_pool_t* mcp_buffer_pool_create(size_t buffer_size, size_t num_buffers) {
    if (buffer_size == 0 || num_buffers == 0) {
        return NULL; // Invalid parameters
    }

    // Allocate the main pool structure
    mcp_buffer_pool_t* pool = (mcp_buffer_pool_t*)malloc(sizeof(mcp_buffer_pool_t));
    if (pool == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer pool structure.");
        return NULL;
    }

    pool->buffer_size = buffer_size;
    pool->free_list = NULL;

    // Initialize mutex
#ifdef _WIN32
    InitializeCriticalSection(&pool->mutex);
#else
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to initialize buffer pool mutex.");
        free(pool);
        return NULL;
    }
#endif

    // Pre-allocate the buffers and nodes
    for (size_t i = 0; i < num_buffers; ++i) {
        // Allocate the actual buffer memory
        void* buffer = malloc(buffer_size);
        if (buffer == NULL) {
            log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer %zu/%zu for pool.", i + 1, num_buffers);
            mcp_buffer_pool_destroy(pool); // Clean up partially created pool
            return NULL;
        }

        // Allocate the node for the free list
        mcp_buffer_node_t* node = (mcp_buffer_node_t*)malloc(sizeof(mcp_buffer_node_t));
        if (node == NULL) {
            log_message(LOG_LEVEL_ERROR, "Failed to allocate buffer node %zu/%zu for pool.", i + 1, num_buffers);
            free(buffer); // Free the buffer we just allocated
            mcp_buffer_pool_destroy(pool); // Clean up partially created pool
            return NULL;
        }

        // Link the node and buffer, add to free list
        node->buffer = buffer;
        node->next = pool->free_list;
        pool->free_list = node;
    }

    log_message(LOG_LEVEL_DEBUG, "Buffer pool created with %zu buffers of size %zu.", num_buffers, buffer_size);
    return pool;
}

void mcp_buffer_pool_destroy(mcp_buffer_pool_t* pool) {
    if (pool == NULL) {
        return;
    }

    // Lock mutex before accessing list
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    // Free all buffers and nodes in the free list
    mcp_buffer_node_t* current = pool->free_list;
    while (current != NULL) {
        mcp_buffer_node_t* next = current->next;
        free(current->buffer); // Free the actual buffer memory
        free(current);         // Free the node structure
        current = next;
    }
    pool->free_list = NULL; // Mark list as empty

    // Unlock mutex
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
    // Destroy mutex
    DeleteCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
    // Destroy mutex
    pthread_mutex_destroy(&pool->mutex);
#endif

    // Free the pool structure itself
    free(pool);
    log_message(LOG_LEVEL_DEBUG, "Buffer pool destroyed.");
}

void* mcp_buffer_pool_acquire(mcp_buffer_pool_t* pool) {
    if (pool == NULL) {
        return NULL;
    }

    void* buffer = NULL;

    // Lock mutex
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    // Check if the free list is empty
    if (pool->free_list != NULL) {
        // Get the first node from the free list
        mcp_buffer_node_t* node = pool->free_list;
        // Update the free list head
        pool->free_list = node->next;
        // Get the buffer pointer from the node
        buffer = node->buffer;
        // Free the node structure (it's no longer needed)
        free(node);
    } else {
        // Pool is empty, log a warning or handle as needed
        log_message(LOG_LEVEL_WARN, "Buffer pool empty, cannot acquire buffer.");
        // Returning NULL indicates failure to acquire
    }

    // Unlock mutex
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif

    return buffer;
}

void mcp_buffer_pool_release(mcp_buffer_pool_t* pool, void* buffer) {
    if (pool == NULL || buffer == NULL) {
        return; // Invalid arguments
    }

    // Allocate a new node to add the buffer back to the free list
    mcp_buffer_node_t* node = (mcp_buffer_node_t*)malloc(sizeof(mcp_buffer_node_t));
    if (node == NULL) {
        // Failed to allocate node - this is problematic.
        // The buffer is now effectively leaked unless the caller handles it.
        log_message(LOG_LEVEL_ERROR, "Failed to allocate node to release buffer back to pool. Buffer leaked!");
        // Optionally: free(buffer); // Free the buffer directly to prevent leak, but deviates from pool concept.
        return;
    }

    node->buffer = buffer;

    // Lock mutex
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif

    // Add the node to the head of the free list
    node->next = pool->free_list;
    pool->free_list = node;

    // Unlock mutex
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif
}

size_t mcp_buffer_pool_get_buffer_size(const mcp_buffer_pool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    // No mutex needed for read-only access to buffer_size after creation
    return pool->buffer_size;
}
