#ifndef MCP_ARENA_H
#define MCP_ARENA_H

#include <stddef.h> // For size_t

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
 */
void mcp_arena_init(mcp_arena_t* arena, size_t default_block_size);

/**
 * @brief Destroys an arena allocator and frees all associated memory blocks.
 *
 * Releases all memory blocks allocated by the arena. The arena structure itself
 * is not freed (as it might be stack-allocated). After calling destroy, the arena
 * should not be used further unless re-initialized with mcp_arena_init.
 *
 * @param arena Pointer to the mcp_arena_t structure to destroy. Must not be NULL.
 */
void mcp_arena_destroy(mcp_arena_t* arena);

/**
 * @brief Allocates a block of memory from the arena.
 *
 * Attempts to allocate `size` bytes from the current block. If the current block
 * does not have enough space, a new block is allocated (either of the default size
 * or `size`, whichever is larger) and added to the arena's list.
 * Allocations are aligned to the size of a pointer.
 *
 * @param arena Pointer to the initialized mcp_arena_t structure. Must not be NULL.
 * @param size The number of bytes to allocate.
 * @return Pointer to the allocated memory block, or NULL if allocation fails (e.g., out of memory).
 * @note Memory allocated with this function should *not* be freed individually using free().
 *       It will be freed when the entire arena is destroyed or reset.
 */
void* mcp_arena_alloc(mcp_arena_t* arena, size_t size);

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
 */
void mcp_arena_reset(mcp_arena_t* arena);

/**
 * @internal
 * @brief Helper macro to align a size up to the nearest multiple of the pointer size.
 * Ensures that allocations returned by mcp_arena_alloc are suitably aligned.
 */
#define MCP_ARENA_ALIGN_UP(size) (((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))

#endif // MCP_ARENA_H
