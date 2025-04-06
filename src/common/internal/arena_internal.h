#ifndef MCP_ARENA_INTERNAL_H
#define MCP_ARENA_INTERNAL_H

#include "mcp_arena.h"

// Use the public size constant
#define MCP_ARENA_ALIGN_UP(n) (((n) + 7) & ~7)  // Align to 8 bytes

// Block structure for arena memory management
typedef struct mcp_arena_block {
    void* data;                    // Pointer to allocated memory
    size_t size;                   // Total size of this block
    size_t used;                   // Amount of memory used in this block
    struct mcp_arena_block* next;  // Next block in chain
} mcp_arena_block_t;

// Block structure is internal implementation detail

#endif // MCP_ARENA_INTERNAL_H
