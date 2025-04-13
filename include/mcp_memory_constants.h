#ifndef MCP_MEMORY_CONSTANTS_H
#define MCP_MEMORY_CONSTANTS_H

/**
 * @file mcp_memory_constants.h
 * @brief Common constants for memory management
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Size of small memory blocks (256 bytes)
 */
#define SMALL_BLOCK_SIZE 256

/**
 * @brief Size of medium memory blocks (1024 bytes)
 */
#define MEDIUM_BLOCK_SIZE 1024

/**
 * @brief Size of large memory blocks (4096 bytes)
 */
#define LARGE_BLOCK_SIZE 4096

#ifdef __cplusplus
}
#endif

#endif /* MCP_MEMORY_CONSTANTS_H */
