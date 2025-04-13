#ifndef MCP_MEMORY_POOL_H
#define MCP_MEMORY_POOL_H

#include <stddef.h>
#include <stdbool.h>
#include "mcp_memory_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Size classes for the memory pools
 */
typedef enum {
    MCP_POOL_SIZE_SMALL,   /**< Small objects (256 bytes) */
    MCP_POOL_SIZE_MEDIUM,  /**< Medium objects (1024 bytes) */
    MCP_POOL_SIZE_LARGE,   /**< Large objects (4096 bytes) */
    MCP_POOL_SIZE_COUNT    /**< Number of size classes */
} mcp_pool_size_class_t;

/**
 * @brief Memory pool statistics
 */
typedef struct {
    size_t total_blocks;      /**< Total number of blocks in the pool */
    size_t free_blocks;       /**< Number of free blocks in the pool */
    size_t allocated_blocks;  /**< Number of allocated blocks */
    size_t block_size;        /**< Size of each block in bytes */
    size_t total_memory;      /**< Total memory managed by the pool (bytes) */
    size_t peak_usage;        /**< Peak memory usage (bytes) */
} mcp_memory_pool_stats_t;

/**
 * @brief Opaque handle to a memory pool
 */
typedef struct mcp_memory_pool mcp_memory_pool_t;

/**
 * @brief Creates a new memory pool for a specific size class
 *
 * @param block_size Size of each block in the pool (in bytes)
 * @param initial_blocks Number of blocks to pre-allocate
 * @param max_blocks Maximum number of blocks the pool can grow to (0 for unlimited)
 * @return Pointer to the created memory pool, or NULL on failure
 */
mcp_memory_pool_t* mcp_memory_pool_create(size_t block_size, size_t initial_blocks, size_t max_blocks);

/**
 * @brief Destroys a memory pool and frees all associated resources
 *
 * @param pool The memory pool to destroy
 */
void mcp_memory_pool_destroy(mcp_memory_pool_t* pool);

/**
 * @brief Allocates a block from the memory pool
 *
 * @param pool The memory pool to allocate from
 * @return Pointer to the allocated block, or NULL if allocation failed
 */
void* mcp_memory_pool_alloc(mcp_memory_pool_t* pool);

/**
 * @brief Returns a block to the memory pool
 *
 * @param pool The memory pool to return the block to
 * @param block Pointer to the block to return
 * @return true if the block was successfully returned, false otherwise
 */
bool mcp_memory_pool_free(mcp_memory_pool_t* pool, void* block);

/**
 * @brief Gets statistics about the memory pool
 *
 * @param pool The memory pool to get statistics for
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_memory_pool_get_stats(mcp_memory_pool_t* pool, mcp_memory_pool_stats_t* stats);

/**
 * @brief Initializes the global memory pool system
 *
 * This function initializes the global memory pools for small, medium, and large objects.
 * It should be called once at program startup before using any of the global pool functions.
 *
 * @param small_initial Initial number of small blocks (256 bytes)
 * @param medium_initial Initial number of medium blocks (1024 bytes)
 * @param large_initial Initial number of large blocks (4096 bytes)
 * @return true if initialization was successful, false otherwise
 */
bool mcp_memory_pool_system_init(size_t small_initial, size_t medium_initial, size_t large_initial);

/**
 * @brief Cleans up the global memory pool system
 *
 * This function destroys the global memory pools and frees all associated resources.
 * It should be called once at program shutdown.
 */
void mcp_memory_pool_system_cleanup(void);

/**
 * @brief Checks if the memory pool system is initialized
 *
 * @return true if the memory pool system is initialized, false otherwise
 */
bool mcp_memory_pool_system_is_initialized(void);

/**
 * @brief Allocates memory from the appropriate global pool based on size
 *
 * @param size Size of memory to allocate (in bytes)
 * @return Pointer to the allocated memory, or NULL if allocation failed
 * @note Falls back to malloc for sizes larger than the largest pool size
 */
void* mcp_pool_alloc(size_t size);

/**
 * @brief Frees memory allocated from a global pool
 *
 * @param ptr Pointer to the memory to free
 * @note Safe to call with NULL
 * @note Safe to call with pointers allocated by malloc (will use free)
 */
void mcp_pool_free(void* ptr);

/**
 * @brief Gets statistics for a specific size class pool
 *
 * @param size_class The size class to get statistics for
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_pool_get_stats(mcp_pool_size_class_t size_class, mcp_memory_pool_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif /* MCP_MEMORY_POOL_H */
