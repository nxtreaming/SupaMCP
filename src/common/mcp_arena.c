#include "internal/arena_internal.h"
#include <stdlib.h>


static mcp_arena_block_t* create_block(size_t size) {
    mcp_arena_block_t* block = (mcp_arena_block_t*)malloc(sizeof(mcp_arena_block_t));
    if (!block) return NULL;

    block->data = malloc(size);
    if (!block->data) {
        free(block);
        return NULL;
    }

    block->size = size;
    block->used = 0;
    block->next = NULL;
    return block;
}

static void destroy_block_chain(mcp_arena_block_t* block) {
    while (block) {
        mcp_arena_block_t* next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
}

void mcp_arena_init(mcp_arena_t* arena, size_t initial_size) {
    if (!arena) return;
    
    // Use default size if 0 is passed
    if (initial_size == 0) {
        initial_size = MCP_ARENA_DEFAULT_SIZE;
    }
    
    arena->current_block = NULL;
    arena->default_block_size = initial_size;
}

void mcp_arena_cleanup(mcp_arena_t* arena) {
    if (!arena) return;
    destroy_block_chain(arena->current_block);
    arena->current_block = NULL;
}

void* mcp_arena_alloc(mcp_arena_t* arena, size_t size) {
    if (!arena) {
        // Try to get thread-local arena
        arena = mcp_arena_get_current();
        if (!arena) {
            // Initialize thread-local arena if not exists
    if (mcp_init_thread_arena(MCP_ARENA_DEFAULT_SIZE) != 0) {
                return NULL;
            }
            arena = mcp_arena_get_current();
            if (!arena) return NULL;
        }
    }
    
    // Align size to 8 bytes
    size = MCP_ARENA_ALIGN_UP(size);
    
    // If no current block or not enough space, create a new one
    if (!arena->current_block || arena->current_block->used + size > arena->current_block->size) {
        size_t block_size = size > arena->default_block_size ? size : arena->default_block_size;
        mcp_arena_block_t* new_block = create_block(block_size);
        if (!new_block) return NULL;
        
        // Link new block at head of chain
        new_block->next = arena->current_block;
        arena->current_block = new_block;
    }
    
    void* ptr = (char*)arena->current_block->data + arena->current_block->used;
    arena->current_block->used += size;
    return ptr;
}

void mcp_arena_reset(mcp_arena_t* arena) {
    if (!arena) return;
    mcp_arena_block_t* block = arena->current_block;
    while (block) {
        block->used = 0;
        block = block->next;
    }
}

void mcp_arena_destroy(mcp_arena_t* arena) {
    mcp_arena_cleanup(arena);
}
