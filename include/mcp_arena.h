#ifndef MCP_ARENA_H
#define MCP_ARENA_H

#include "mcp_cache_aligned.h"
#include <stddef.h>
#include <stdbool.h>

#define MCP_ARENA_DEFAULT_SIZE (32 * 1024) // 32KB default size

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare internal block structure
typedef struct mcp_arena_block mcp_arena_block_t;

/**
 * @brief Linear allocator for temporary allocations during request processing.
 *
 * The arena allocator provides fast allocation by simply incrementing a pointer,
 * and bulk deallocation by resetting that pointer. It's useful for temporary
 * allocations that have the same lifetime, like during request processing.
 *
 * The structure is cache-line aligned to improve performance in multi-threaded environments.
 */
typedef MCP_CACHE_ALIGNED struct mcp_arena {
    struct mcp_arena_block* current_block;  /**< Current block for allocations */
    size_t default_block_size;              /**< Default size for new blocks */
    // Statistics
    size_t total_allocated;                 /**< Total bytes requested via mcp_arena_alloc since last reset. */
    size_t total_block_size;                /**< Total bytes allocated in all blocks. */
    size_t block_count;                     /**< Number of allocated blocks. */
    // Padding to cache line size to reduce false sharing
    char padding[MCP_CACHE_LINE_SIZE - (sizeof(void*) + 4 * sizeof(size_t)) % MCP_CACHE_LINE_SIZE];
} mcp_arena_t;

/**
 * @brief Initialize an arena allocator with the given initial size.
 *
 * @param arena The arena to initialize
 * @param initial_size The initial size in bytes for the arena's buffer
 */
void mcp_arena_init(mcp_arena_t* arena, size_t initial_size);

/**
 * @brief Clean up an arena allocator, freeing its buffer.
 *
 * @param arena The arena to clean up
 */
void mcp_arena_cleanup(mcp_arena_t* arena);

/**
 * @brief Allocate memory from the arena.
 *
 * If arena is NULL, allocates from the thread-local arena.
 * If no thread-local arena exists, one will be created.
 *
 * @param arena The arena to allocate from, or NULL to use thread-local arena
 * @param size The number of bytes to allocate
 * @return void* Pointer to the allocated memory, or NULL if allocation failed
 */
void* mcp_arena_alloc(mcp_arena_t* arena, size_t size);

/**
 * @brief Reset the arena, making all allocated memory available again.
 *
 * This doesn't free the underlying buffer, just resets the used counter
 * in each block and the total_allocated counter.
 *
 * @param arena The arena to reset
 */
void mcp_arena_reset(mcp_arena_t* arena);

/**
 * @brief Destroy an arena allocator, freeing its buffer.
 * This is an alias for mcp_arena_cleanup for backward compatibility.
 *
 * @param arena The arena to destroy
 */
void mcp_arena_destroy(mcp_arena_t* arena);

/**
 * @brief Retrieves statistics about the arena's memory usage.
 *
 * @param arena The arena instance.
 * @param[out] out_total_allocated Pointer to store the total bytes requested via alloc since last reset. Can be NULL.
 * @param[out] out_total_block_size Pointer to store the total bytes allocated across all blocks. Can be NULL.
 * @param[out] out_block_count Pointer to store the number of allocated blocks. Can be NULL.
 * @return 0 on success, -1 if arena is NULL.
 */
int mcp_arena_get_stats(mcp_arena_t* arena, size_t* out_total_allocated, size_t* out_total_block_size, size_t* out_block_count);

/**
 * @brief Checks if a thread-local arena exists without creating one.
 *
 * This function is useful to determine if a thread-local arena has been
 * initialized without triggering the creation of one if it doesn't exist.
 *
 * @return true if a thread-local arena exists, false otherwise
 */
bool mcp_arena_exists_current_thread(void);

/**
 * @brief Allocates memory from the thread-local arena if it exists.
 *
 * This function is similar to mcp_arena_alloc(NULL, size) but will not
 * create a thread-local arena if one doesn't exist. Instead, it will
 * return NULL.
 *
 * @param size Size of memory to allocate
 * @return Pointer to allocated memory, or NULL if no thread-local arena exists
 */
void* mcp_arena_alloc_if_exists(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MCP_ARENA_H */
