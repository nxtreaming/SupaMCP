#ifndef MCP_THREAD_LOCAL_H
#define MCP_THREAD_LOCAL_H

#include "mcp_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the thread-local arena instance.
 * 
 * @return Pointer to the thread-local arena, or NULL if initialization failed.
 */
mcp_arena_t* mcp_get_thread_arena(void);

/**
 * @brief Initialize the thread-local arena with the given size.
 * 
 * @param initial_size Initial size for the arena buffer
 * @return 0 on success, non-zero on failure
 */
int mcp_init_thread_arena(size_t initial_size);

/**
 * @brief Clean up the thread-local arena.
 */
void mcp_cleanup_thread_arena(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_THREAD_LOCAL_H */
