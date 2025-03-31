#include "mcp_arena.h"
#include <stdlib.h>
#include <string.h>

// Internal function to allocate a new block
static mcp_arena_block_t* mcp_arena_new_block(size_t size) {
    // Allocate memory for the block header and the data area
    mcp_arena_block_t* block = (mcp_arena_block_t*)malloc(sizeof(mcp_arena_block_t) + size);
    if (block == NULL) {
        return NULL;
    }
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

// Initialize an arena
void mcp_arena_init(mcp_arena_t* arena, size_t default_block_size) {
    if (arena == NULL) {
        return;
    }
    arena->current_block = NULL; // Start with no blocks
    arena->default_block_size = (default_block_size > 0) ? default_block_size : MCP_ARENA_DEFAULT_BLOCK_SIZE;
}

// Destroy an arena and free all its blocks
void mcp_arena_destroy(mcp_arena_t* arena) {
    if (arena == NULL) {
        return;
    }
    mcp_arena_block_t* current = arena->current_block;
    while (current != NULL) {
        mcp_arena_block_t* next = current->next;
        free(current);
        current = next;
    }
    arena->current_block = NULL; // Reset the arena state
}

// Reset an arena for reuse without freeing memory
void mcp_arena_reset(mcp_arena_t* arena) {
    if (arena == NULL) {
        return;
    }
    // Just reset the 'used' counter in each block
    mcp_arena_block_t* current = arena->current_block;
    while (current != NULL) {
        current->used = 0;
        current = current->next;
    }
    // Optionally, could keep only the first block and free the rest,
    // but for simplicity, we keep all blocks and just reset usage.
}

// Allocate memory from the arena
void* mcp_arena_alloc(mcp_arena_t* arena, size_t size) {
    if (arena == NULL || size == 0) {
        return NULL;
    }

    // Align the requested size for proper memory alignment
    size_t aligned_size = MCP_ARENA_ALIGN_UP(size);

    // Try allocating from the current block
    if (arena->current_block != NULL &&
        (arena->current_block->size - arena->current_block->used) >= aligned_size)
    {
        void* ptr = arena->current_block->data + arena->current_block->used;
        arena->current_block->used += aligned_size;
        return ptr;
    }

    // Current block is full or doesn't exist, need a new block
    // Determine the size of the new block (at least the requested size, or default)
    size_t block_size = (aligned_size > arena->default_block_size) ? aligned_size : arena->default_block_size;

    mcp_arena_block_t* new_block = mcp_arena_new_block(block_size);
    if (new_block == NULL) {
        return NULL; // Allocation failed
    }

    // Allocate the memory from the new block
    void* ptr = new_block->data;
    new_block->used = aligned_size;

    // Add the new block to the beginning of the list
    new_block->next = arena->current_block;
    arena->current_block = new_block;

    return ptr;
}
