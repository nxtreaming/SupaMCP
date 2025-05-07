#include "internal/arena_internal.h"
#include "mcp_thread_local.h"
#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_thread_cache.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

// Constants for memory management
#define BATCH_SIZE 8           // Number of blocks to process in a batch
#define MAX_BLOCKS_TO_RESET 4  // Maximum number of blocks to reset in arena_reset

// Optimized block creation function
static mcp_arena_block_t* create_block(size_t size) {
    // Allocate block structure from thread cache
    // which is faster than direct malloc calls
    mcp_arena_block_t* block = (mcp_arena_block_t*)mcp_thread_cache_alloc(sizeof(mcp_arena_block_t));
    if (!block) {
        // Fall back to malloc if thread cache allocation fails
        block = (mcp_arena_block_t*)malloc(sizeof(mcp_arena_block_t));
        if (!block) {
            return NULL;
        }
    }

    // Allocate data buffer based on size
    if (size <= SMALL_BLOCK_SIZE) {
        // Small blocks are very common, optimize this path
        block->data = mcp_thread_cache_alloc(SMALL_BLOCK_SIZE);
        if (block->data) {
            block->size = SMALL_BLOCK_SIZE;
        }
    } else if (size <= MEDIUM_BLOCK_SIZE) {
        block->data = mcp_thread_cache_alloc(MEDIUM_BLOCK_SIZE);
        if (block->data) {
            block->size = MEDIUM_BLOCK_SIZE;
        }
    } else if (size <= LARGE_BLOCK_SIZE) {
        block->data = mcp_thread_cache_alloc(LARGE_BLOCK_SIZE);
        if (block->data) {
            block->size = LARGE_BLOCK_SIZE;
        }
    } else {
        // For larger blocks, use direct malloc and the exact requested size
        block->data = malloc(size);
        if (block->data) {
            block->size = size;
        }
    }

    // Check if data allocation failed
    if (!block->data) {
        mcp_thread_cache_free(block, sizeof(mcp_arena_block_t));
        return NULL;
    }

    block->used = 0;
    block->next = NULL;
    return block;
}

static void destroy_block_chain(mcp_arena_block_t* block) {
    // Fast path for empty chain
    if (!block) {
        return;
    }

    // Process blocks in batches to improve cache locality
    mcp_arena_block_t* batch[BATCH_SIZE];
    int batch_count = 0;

    // Collect blocks in batches and free them
    while (block) {
        // Store the block in the current batch
        batch[batch_count++] = block;
        block = block->next;

        // Process the batch when it's full or we've reached the end
        if (batch_count == BATCH_SIZE || !block) {
            // Free all blocks in the batch
            for (int i = 0; i < batch_count; i++) {
                mcp_arena_block_t* current = batch[i];

                // Free the data buffer based on size
                if (current->size <= LARGE_BLOCK_SIZE) {
                    mcp_thread_cache_free(current->data, current->size);
                } else {
                    free(current->data);
                }

                // Free the block structure
                mcp_thread_cache_free(current, sizeof(mcp_arena_block_t));
            }

            // Reset batch counter
            batch_count = 0;
        }
    }
}

void mcp_arena_init(mcp_arena_t* arena, size_t initial_size) {
    if (!arena)
        return;

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
    if (!arena)
        return;
    destroy_block_chain(arena->current_block);
    arena->current_block = NULL;
    // Reset statistics on cleanup
    arena->total_allocated = 0;
    arena->total_block_size = 0;
    arena->block_count = 0;
}

void* mcp_arena_alloc(mcp_arena_t* arena, size_t size) {
    // Fast path for zero-size allocations
    if (size == 0) {
        size = 1; // Ensure we return a valid pointer
    }

    // Get or create thread-local arena if none provided
    if (!arena) {
        // Try to get thread-local arena
        arena = mcp_arena_get_current();
        if (!arena) {
            // Initialize thread-local arena if not exists
            if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
                return NULL;
            }
            arena = mcp_arena_get_current();
            if (!arena) {
                return NULL;
            }
        }
    }

    // Align size to 8 bytes for better performance
    size_t aligned_size = MCP_ARENA_ALIGN_UP(size);

    // Fast path for small allocations that fit in the current block
    mcp_arena_block_t* current_block = arena->current_block;
    if (current_block && current_block->used + aligned_size <= current_block->size) {
        void* ptr = (char*)current_block->data + current_block->used;
        current_block->used += aligned_size;
        arena->total_allocated += aligned_size;
        return ptr;
    }

    // Slow path: need to allocate a new block

    // Choose an appropriate block size
    // For very large allocations, use exactly the requested size
    // For normal allocations, use the default block size
    // For allocations slightly larger than default, increase by a factor to reduce fragmentation
    size_t block_size;
    if (aligned_size > arena->default_block_size) {
        if (aligned_size > 4 * arena->default_block_size) {
            // For very large allocations, use exactly what's needed
            block_size = aligned_size;
        } else {
            // For moderately large allocations, round up to reduce fragmentation
            block_size = ((aligned_size + arena->default_block_size - 1) / arena->default_block_size)
                        * arena->default_block_size;
        }
    } else {
        // For normal allocations, use the default block size
        block_size = arena->default_block_size;
    }

    // Create a new block
    mcp_arena_block_t* new_block = create_block(block_size);
    if (!new_block) {
        return NULL;
    }

    // Link new block at head of chain
    new_block->next = arena->current_block;
    arena->current_block = new_block;

    // Update statistics for new block
    arena->total_block_size += new_block->size; // Use actual block size
    arena->block_count++;

    // Allocate from the new block
    void* ptr = new_block->data;
    new_block->used = aligned_size;

    // Update total allocated bytes statistic
    arena->total_allocated += aligned_size;

    return ptr;
}

void mcp_arena_reset(mcp_arena_t* arena) {
    if (!arena)
        return;

    // Fast path for empty arena
    if (!arena->current_block) {
        return;
    }

    // Reset only the first few blocks to reduce overhead
    // Most allocations come from the most recent blocks
    int blocks_reset = 0;

    mcp_arena_block_t* block = arena->current_block;
    while (block && blocks_reset < MAX_BLOCKS_TO_RESET) {
        block->used = 0;
        block = block->next;
        blocks_reset++;
    }

    // If there are more blocks beyond what we reset, consider
    // freeing them to reduce memory usage if they're not needed
    if (block && arena->block_count > MAX_BLOCKS_TO_RESET * 2) {
        // Find the last block we reset
        mcp_arena_block_t* last_reset = arena->current_block;
        for (int i = 1; i < MAX_BLOCKS_TO_RESET; i++) {
            if (last_reset->next) {
                last_reset = last_reset->next;
            }
        }

        // Free the remaining blocks
        mcp_arena_block_t* to_free = last_reset->next;
        last_reset->next = NULL;

        // Update block count
        size_t freed_blocks = 0;
        size_t freed_size = 0;

        // Count blocks to be freed
        mcp_arena_block_t* count_block = to_free;
        while (count_block) {
            freed_blocks++;
            freed_size += count_block->size;
            count_block = count_block->next;
        }

        // Update statistics
        arena->block_count -= freed_blocks;
        arena->total_block_size -= freed_size;

        // Free the blocks
        destroy_block_chain(to_free);
    }

    // Reset total allocated, but keep block stats for blocks we're keeping
    arena->total_allocated = 0;
}

void mcp_arena_destroy(mcp_arena_t* arena) {
    mcp_arena_cleanup(arena);
}

/**
 * @brief Checks if a thread-local arena exists without creating one.
 *
 * This function is useful to determine if a thread-local arena has been
 * initialized without triggering the creation of one if it doesn't exist.
 *
 * @return true if a thread-local arena exists, false otherwise
 */
bool mcp_arena_exists_current_thread(void) {
    return mcp_arena_get_current() != NULL;
}

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
void* mcp_arena_alloc_if_exists(size_t size) {
    mcp_arena_t* arena = mcp_arena_get_current();
    if (!arena) {
        return NULL;
    }
    return mcp_arena_alloc(arena, size);
}

int mcp_arena_get_stats(mcp_arena_t* arena, size_t* out_total_allocated, size_t* out_total_block_size, size_t* out_block_count) {
    if (!arena)
        return -1;

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
