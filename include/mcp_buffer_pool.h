#ifndef MCP_BUFFER_POOL_H
#define MCP_BUFFER_POOL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque handle for a buffer pool instance. */
typedef struct mcp_buffer_pool mcp_buffer_pool_t;

/**
 * @brief Creates a buffer pool.
 *
 * Creates a pool containing a specified number of fixed-size buffers.
 *
 * @param buffer_size The fixed size of each buffer in the pool.
 * @param num_buffers The initial number of buffers to create in the pool.
 * @return Pointer to the created buffer pool, or NULL on allocation failure.
 * @note The caller is responsible for destroying the pool using mcp_buffer_pool_destroy().
 */
mcp_buffer_pool_t* mcp_buffer_pool_create(size_t buffer_size, size_t num_buffers);

/**
 * @brief Destroys a buffer pool and frees all associated memory.
 * @param pool Pointer to the buffer pool to destroy. If NULL, the function does nothing.
 */
void mcp_buffer_pool_destroy(mcp_buffer_pool_t* pool);

/**
 * @brief Acquires a buffer from the pool.
 *
 * If a free buffer of the pool's fixed size is available, it is returned.
 * If no buffer is available, this function returns NULL.
 *
 * @param pool Pointer to the buffer pool.
 * @return Pointer to an acquired buffer, or NULL if none are available.
 * @note The returned buffer should be released back to the pool using mcp_buffer_pool_release().
 */
void* mcp_buffer_pool_acquire(mcp_buffer_pool_t* pool);

/**
 * @brief Releases a previously acquired buffer back to the pool.
 * @param pool Pointer to the buffer pool.
 * @param buffer Pointer to the buffer being released. Must have been acquired from this pool.
 */
void mcp_buffer_pool_release(mcp_buffer_pool_t* pool, void* buffer);

/**
 * @brief Gets the fixed buffer size of the pool.
 * @param pool Pointer to the buffer pool.
 * @return The size of buffers managed by this pool, or 0 if pool is NULL.
 */
size_t mcp_buffer_pool_get_buffer_size(const mcp_buffer_pool_t* pool);


#ifdef __cplusplus
}
#endif

#endif // MCP_BUFFER_POOL_H
