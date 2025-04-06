#ifndef MCP_ARENA_H
#define MCP_ARENA_H

#include <stddef.h>

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
 */
struct mcp_arena {
    struct mcp_arena_block* current_block;  /**< Current block for allocations */
    size_t default_block_size;              /**< Default size for new blocks */
};
typedef struct mcp_arena mcp_arena_t;

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
 * This doesn't free the underlying buffer, just resets the used counter.
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
 * @brief Initialize the thread-local arena with the given initial size.
 * 
 * @param initial_size The initial size in bytes for the arena's buffer
 * @return 0 on success, -1 on failure
 */
int mcp_init_thread_arena(size_t initial_size);

/**
 * @brief Get the current thread's arena.
 * 
 * @return Pointer to the thread-local arena, or NULL if not initialized
 */
mcp_arena_t* mcp_arena_get_current(void);

/**
 * @brief Reset the current thread's arena.
 */
void mcp_arena_reset_current_thread(void);

/**
 * @brief Destroy the current thread's arena.
 */
void mcp_arena_destroy_current_thread(void);

/**
 * @brief Get the thread-local arena (backward compatibility alias for mcp_arena_get_current)
 * @return Pointer to the thread-local arena, or NULL if not initialized
 */
#define mcp_get_thread_arena mcp_arena_get_current

#ifdef __cplusplus
}
#endif

#endif /* MCP_ARENA_H */
