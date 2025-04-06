#ifndef MCP_THREAD_LOCAL_H
#define MCP_THREAD_LOCAL_H

#include "mcp_arena.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the thread-local arena with the given initial size.
 *
 * This function should be called once per thread that needs to use the
 * thread-local arena before calling mcp_arena_alloc with a NULL arena pointer.
 *
 * @param initial_size The initial size in bytes for the arena's buffer.
 *                     If 0, MCP_ARENA_DEFAULT_SIZE will be used.
 * @return 0 on success, -1 on failure (e.g., allocation error, already initialized).
 */
int mcp_arena_init_current_thread(size_t initial_size);

/**
 * @brief Get the current thread's arena.
 *
 * Returns the arena previously initialized by mcp_arena_init_current_thread().
 *
 * @return Pointer to the thread-local arena, or NULL if not initialized or
 *         if thread-local storage is not supported/failed.
 */
mcp_arena_t* mcp_arena_get_current(void);

/**
 * @brief Reset the current thread's arena.
 *
 * Makes all memory previously allocated from the thread-local arena available
 * again without freeing the underlying blocks. Useful for reusing the arena
 * within a thread's lifecycle (e.g., per request).
 */
void mcp_arena_reset_current_thread(void);

/**
 * @brief Destroy the current thread's arena.
 *
 * Frees all memory blocks associated with the thread-local arena. This should
 * be called when the thread is exiting or no longer needs the arena to prevent
 * memory leaks.
 */
void mcp_arena_destroy_current_thread(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_THREAD_LOCAL_H */
