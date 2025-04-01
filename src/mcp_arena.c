#include "mcp_arena.h"
#include <stdlib.h>
#include <string.h>

/**
 * @internal
 * @brief Allocates a new memory block for the arena.
 * The allocated size includes space for the block header and the requested data size.
 * @param size The minimum required size for the data area of the block.
 * @return Pointer to the newly allocated block, or NULL on failure.
 */
static mcp_arena_block_t* mcp_arena_new_block(size_t size) {
    // Calculate total size needed: header + data area
    // Note: sizeof(mcp_arena_block_t) already includes the 'char data[1]'
    // so we need size-1 additional bytes for the data if size > 0.
    // However, allocating sizeof(header) + size is simpler and safer.
    size_t total_size = sizeof(mcp_arena_block_t) + size;
    mcp_arena_block_t* block = (mcp_arena_block_t*)malloc(total_size);
    if (block == NULL) {
        return NULL; // Allocation failed
    }
    // Initialize block header
    block->next = NULL;
    block->size = size; // Store the usable data size
    block->used = 0;
    // Note: block->data is implicitly the memory right after the header fields
    return block;
}

// --- Public API Implementation ---

void mcp_arena_init(mcp_arena_t* arena, size_t default_block_size) {
    if (arena == NULL) {
        return; // Invalid argument
    }
    // Initialize arena fields
    arena->current_block = NULL; // No blocks allocated yet
    // Set the default block size, using the defined default if 0 is provided
    arena->default_block_size = (default_block_size > 0) ? default_block_size : MCP_ARENA_DEFAULT_BLOCK_SIZE;
}

void mcp_arena_destroy(mcp_arena_t* arena) {
    if (arena == NULL) {
        return; // Invalid argument
    }
    // Traverse the linked list of blocks
    mcp_arena_block_t* current = arena->current_block;
    while (current != NULL) {
        mcp_arena_block_t* next = current->next; // Store pointer to next block
        free(current);                           // Free the current block
        current = next;                          // Move to the next block
    }
    // Reset arena state to indicate it's empty/destroyed
    arena->current_block = NULL;
    // arena->default_block_size remains unchanged
}

void mcp_arena_reset(mcp_arena_t* arena) {
    if (arena == NULL) {
        return; // Invalid argument
    }
    // Traverse the linked list of blocks
    mcp_arena_block_t* current = arena->current_block;
    while (current != NULL) {
        // Reset the used counter for each block, making its memory available again
        current->used = 0;
        current = current->next;
    }
    // Note: This keeps the allocated blocks in memory for faster reuse.
    // If memory footprint needs to be minimized immediately, mcp_arena_destroy
    // should be used instead.
}

void* mcp_arena_alloc(mcp_arena_t* arena, size_t size) {
    if (arena == NULL || size == 0) {
        return NULL; // Invalid arguments
    }

    // Ensure the requested size is aligned to pointer size for safety
    size_t aligned_size = MCP_ARENA_ALIGN_UP(size);

    // 1. Try allocating from the current block if it exists and has enough space
    if (arena->current_block != NULL &&
        (arena->current_block->size - arena->current_block->used) >= aligned_size)
    {
        // Calculate the pointer to the available space
        // The data starts right after the header fields in the block struct
        void* ptr = arena->current_block->data + arena->current_block->used;
        // Bump the used pointer
        arena->current_block->used += aligned_size;
        // Return the allocated pointer
        return ptr;
    }

    // 2. Need a new block (current block is NULL or full)
    // Determine the size for the new block: must be at least the requested aligned size,
    // but preferably the default block size to avoid many small blocks for small allocations.
    size_t new_block_data_size = (aligned_size > arena->default_block_size) ? aligned_size : arena->default_block_size;

    // Allocate the new block structure and its data area
    mcp_arena_block_t* new_block = mcp_arena_new_block(new_block_data_size);
    if (new_block == NULL) {
        return NULL; // Failed to allocate a new block
    }

    // Allocate the requested memory from the start of the new block's data area
    void* ptr = new_block->data;
    // Mark the used space in the new block
    new_block->used = aligned_size;

    // Prepend the new block to the arena's linked list
    new_block->next = arena->current_block;
    // Update the arena's current block pointer
    arena->current_block = new_block;

    // Return the pointer to the allocated memory within the new block
    return ptr;
}
