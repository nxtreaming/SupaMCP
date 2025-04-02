#ifndef MCP_ARENA_H
#define MCP_ARENA_H

#include <stddef.h>

/**
 * @brief Default size in bytes for newly allocated memory blocks within an arena.
 */
#define MCP_ARENA_DEFAULT_BLOCK_SIZE (1024 * 4) // 4KB

/**
 * @internal
 * @brief Represents a single contiguous block of memory within an arena.
 * Arenas are composed of a linked list of these blocks.
 */
typedef struct mcp_arena_block {
    struct mcp_arena_block* next; /**< Pointer to the next block in the arena's linked list, or NULL if this is the last block. */
    size_t size;                  /**< Total allocatable size of this block's data area in bytes. */
    size_t used;                  /**< Number of bytes currently used within this block's data area. */
    /** @brief Start of the allocatable memory within this block. Uses C99 flexible array member concept. */
    char data[1];
} mcp_arena_block_t;

/**
 * @brief Represents an arena allocator.
 *
 * An arena allocator provides fast memory allocation for objects with similar
 * lifetimes. Allocations are typically very fast (pointer bumps). All memory
 * allocated from an arena is freed simultaneously by destroying or resetting
 * the arena, avoiding the need for individual free calls.
 */
typedef struct mcp_arena {
    mcp_arena_block_t* current_block; /**< The block currently being used for allocations. May be NULL initially or after reset/destroy. */
    size_t default_block_size;        /**< The default size used when allocating new blocks. */
} mcp_arena_t;

/**
 * @brief Initializes an arena allocator structure.
 *
 * Sets up the arena for use. No memory blocks are allocated until the first call
 * to mcp_arena_alloc.
 *
 * @param arena Pointer to the mcp_arena_t structure to initialize. Must not be NULL.
 * @param default_block_size The default size for new memory blocks allocated by the arena.
 *                           If 0, MCP_ARENA_DEFAULT_BLOCK_SIZE is used.
 * @note This function is primarily for manual arena management. For thread-local usage,
 *       arenas are typically initialized automatically on first use via mcp_arena_alloc
 *       or mcp_arena_get_current.
 */
void mcp_arena_init(mcp_arena_t* arena, size_t default_block_size);

/**
 * @brief Destroys an arena allocator and frees all associated memory blocks.
 *
 * Releases all memory blocks allocated by the arena. The arena structure itself
 * is not freed (as it might be stack-allocated or managed elsewhere).
 *
 * @param arena Pointer to the mcp_arena_t structure to destroy. Must not be NULL.
 * @note For thread-local arenas managed automatically, use mcp_arena_destroy_current_thread() instead.
 */
void mcp_arena_destroy(mcp_arena_t* arena);

/**
 * @brief Allocates a block of memory from the arena.
 *
 * Attempts to allocate `size` bytes from the current block. If the current block
 * does not have enough space, a new block is allocated (either of the default size
 * or `size`, whichever is larger) and added to the arena's list.
 * Allocations are aligned to the size of a pointer.
 * This function uses the arena associated with the calling thread. If no arena
 * exists for the thread, one will be created automatically with a default block size.
 *
 * @param size The number of bytes to allocate.
 * @return Pointer to the allocated memory block, or NULL if allocation fails (e.g., out of memory).
 * @note Memory allocated with this function should *not* be freed individually using free().
 *       It will be freed when the thread's arena is destroyed (using mcp_arena_destroy_current_thread)
 *       or reset (using mcp_arena_reset_current_thread).
 * @warning Threads using this function *must* call mcp_arena_destroy_current_thread() before exiting
 *          to prevent memory leaks.
 */
void* mcp_arena_alloc(size_t size);

/**
 * @brief Resets an arena allocator, marking all allocated memory as reusable
 *        without freeing the underlying memory blocks.
 *
 * This is faster than mcp_arena_destroy followed by mcp_arena_init if the arena
 * is likely to be reused soon with similar allocation patterns, as it avoids
 * freeing and reallocating the memory blocks. Subsequent allocations will reuse
 * the existing blocks starting from the beginning.
 *
 * @param arena Pointer to the mcp_arena_t structure to reset. Must not be NULL.
 * @note For thread-local arenas managed automatically, use mcp_arena_reset_current_thread() instead.
 */
void mcp_arena_reset(mcp_arena_t* arena);

/**
 * @brief Retrieves the arena associated with the current thread.
 *
 * If no arena exists for the current thread, one is created and initialized
 * with a default block size. This function is useful if you need direct access
 * to the arena structure itself, but typically mcp_arena_alloc is sufficient.
 *
 * @return Pointer to the current thread's arena, or NULL on failure (e.g., out of memory
 *         during initial creation).
 * @warning Threads using this function *must* call mcp_arena_destroy_current_thread() before exiting
 *          to prevent memory leaks.
 */
mcp_arena_t* mcp_arena_get_current(void);

/**
 * @brief Resets the arena associated with the current thread.
 *
 * Marks all memory allocated in the current thread's arena as reusable
 * without freeing the underlying memory blocks.
 * If no arena exists for the current thread, this function does nothing.
 */
void mcp_arena_reset_current_thread(void);

/**
 * @brief Destroys the arena associated with the current thread and frees its memory blocks.
 *
 * This function *must* be called by any thread that used mcp_arena_alloc() or
 * mcp_arena_get_current() before the thread exits, to prevent memory leaks.
 * If no arena exists for the current thread, this function does nothing.
 */
void mcp_arena_destroy_current_thread(void);

/**
 * @internal
 * @brief Helper macro to align a size up to the nearest multiple of the pointer size.
 * Ensures that allocations returned by mcp_arena_alloc are suitably aligned.
 */
#define MCP_ARENA_ALIGN_UP(size) (((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))

#endif // MCP_ARENA_H
