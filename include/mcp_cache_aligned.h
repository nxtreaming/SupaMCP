#ifndef MCP_CACHE_ALIGNED_H
#define MCP_CACHE_ALIGNED_H

// Adjust cache line size based on CPU architecture
#if defined(__x86_64__) || defined(_M_X64)
#define MCP_CACHE_LINE_SIZE 64
#else
#define MCP_CACHE_LINE_SIZE 32
#endif

// Define cache line alignment attribute
#ifdef _MSC_VER
#define MCP_CACHE_ALIGNED __declspec(align(MCP_CACHE_LINE_SIZE))
#else
#define MCP_CACHE_ALIGNED __attribute__((aligned(MCP_CACHE_LINE_SIZE)))
#endif

/**
 * @brief Macro to add padding to a structure to align to cache line boundary
 *
 * This macro adds padding bytes to ensure that the next field in a structure
 * starts at a new cache line, preventing false sharing in multi-threaded environments.
 *
 * @param id A unique identifier to create a unique field name
 */
#define MCP_CACHE_PADDING(id) \
    char _mcp_padding_##id[MCP_CACHE_LINE_SIZE]

#endif /* MCP_CACHE_ALIGNED_H */