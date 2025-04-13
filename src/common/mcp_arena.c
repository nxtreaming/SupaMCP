#include "internal/arena_internal.h"
#include "mcp_thread_local.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_thread_cache.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

static mcp_arena_block_t* create_block(size_t size) {
    // Use thread cache for the block structure
    mcp_arena_block_t* block = (mcp_arena_block_t*)mcp_thread_cache_alloc(sizeof(mcp_arena_block_t));
    if (!block) {
        // Fall back to malloc if thread cache allocation fails
        block = (mcp_arena_block_t*)malloc(sizeof(mcp_arena_block_t));
        if (!block) return NULL;
    }

    // Use thread cache for the data buffer if it fits in one of our pools
    if (size <= LARGE_BLOCK_SIZE) {
        block->data = mcp_thread_cache_alloc(size);
    } else {
        // Fall back to malloc for large allocations
        block->data = malloc(size);
    }

    if (!block->data) {
        mcp_thread_cache_free(block, sizeof(mcp_arena_block_t));
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

        // Free the data buffer
        if (block->size <= LARGE_BLOCK_SIZE) {
            mcp_thread_cache_free(block->data, block->size);
        } else {
            free(block->data);
        }

        // Free the block structure
        mcp_thread_cache_free(block, sizeof(mcp_arena_block_t));

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
    // Initialize statistics
    arena->total_allocated = 0;
    arena->total_block_size = 0;
    arena->block_count = 0;
}

void mcp_arena_cleanup(mcp_arena_t* arena) {
    if (!arena) return;
    destroy_block_chain(arena->current_block);
    arena->current_block = NULL;
    // Reset statistics on cleanup
    arena->total_allocated = 0;
    arena->total_block_size = 0;
    arena->block_count = 0;
}

void* mcp_arena_alloc(mcp_arena_t* arena, size_t size) {
    if (!arena) {
        // Try to get thread-local arena
        arena = mcp_arena_get_current();
        if (!arena) {
            // Initialize thread-local arena if not exists
            if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
                return NULL;
            }
            arena = mcp_arena_get_current();
            if (!arena) return NULL;
        }
    }

    // Align size to 8 bytes
    size_t aligned_size = MCP_ARENA_ALIGN_UP(size);

    // If no current block or not enough space, create a new one
    if (!arena->current_block || arena->current_block->used + aligned_size > arena->current_block->size) {
        size_t block_size = aligned_size > arena->default_block_size ? aligned_size : arena->default_block_size;
        mcp_arena_block_t* new_block = create_block(block_size);
        if (!new_block) return NULL;

        // Link new block at head of chain
        new_block->next = arena->current_block;
        arena->current_block = new_block;

        // Update statistics for new block
        arena->total_block_size += block_size;
        arena->block_count++;
    }

    void* ptr = (char*)arena->current_block->data + arena->current_block->used;
    arena->current_block->used += aligned_size;

    // Update total allocated bytes statistic
    arena->total_allocated += aligned_size;

    return ptr;
}

void mcp_arena_reset(mcp_arena_t* arena) {
    if (!arena) return;
    mcp_arena_block_t* block = arena->current_block;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    // Reset total allocated, but keep block stats as blocks are reused
    arena->total_allocated = 0;
}

void mcp_arena_destroy(mcp_arena_t* arena) {
    mcp_arena_cleanup(arena);
}

int mcp_arena_get_stats(mcp_arena_t* arena, size_t* out_total_allocated, size_t* out_total_block_size, size_t* out_block_count) {
    if (!arena) return -1;

    if (out_total_allocated) {
        *out_total_allocated = arena->total_allocated;
    }
    if (out_total_block_size) {
        *out_total_block_size = arena->total_block_size;
    }
    if (out_block_count) {
        *out_block_count = arena->block_count;
    }
    return 0;
}
