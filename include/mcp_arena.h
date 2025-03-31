#ifndef MCP_ARENA_H
#define MCP_ARENA_H

#include <stddef.h> // For size_t

// Default size for newly allocated arena blocks
#define MCP_ARENA_DEFAULT_BLOCK_SIZE (1024 * 4) // 4KB

// Structure for a single memory block in the arena
typedef struct mcp_arena_block {
    struct mcp_arena_block* next; // Pointer to the next block
    size_t size;                  // Total size of this block (excluding this header)
    size_t used;                  // Bytes used in this block
    // Use char array of size 1 for standard compliance (flexible array member alternative)
    char data[1];
} mcp_arena_block_t;

// Arena allocator structure
typedef struct mcp_arena {
    mcp_arena_block_t* current_block; // The block currently being allocated from
    size_t default_block_size;        // Default size for new blocks
} mcp_arena_t;

// Function declarations
void mcp_arena_init(mcp_arena_t* arena, size_t default_block_size);
void mcp_arena_destroy(mcp_arena_t* arena);
void* mcp_arena_alloc(mcp_arena_t* arena, size_t size);
void mcp_arena_reset(mcp_arena_t* arena); // Resets arena without freeing blocks (faster reuse)

// Helper macro for alignment (aligns to pointer size)
#define MCP_ARENA_ALIGN_UP(size) (((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))

#endif // MCP_ARENA_H
