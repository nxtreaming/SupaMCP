#ifndef MCP_MEMORY_TRACKER_H
#define MCP_MEMORY_TRACKER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory tracking statistics
 */
typedef struct {
    size_t total_allocations;     /**< Total number of allocations */
    size_t total_frees;           /**< Total number of frees */
    size_t current_allocations;   /**< Current number of active allocations */
    size_t peak_allocations;      /**< Peak number of active allocations */
    size_t total_bytes_allocated; /**< Total bytes allocated */
    size_t current_bytes;         /**< Current bytes allocated */
    size_t peak_bytes;            /**< Peak bytes allocated */
} mcp_memory_stats_t;

/**
 * @brief Initializes the memory tracking system
 *
 * @param track_allocations Whether to track individual allocations (more memory overhead)
 * @param track_backtraces Whether to capture backtraces for each allocation (more CPU overhead)
 * @return true if initialization was successful, false otherwise
 */
bool mcp_memory_tracker_init(bool track_allocations, bool track_backtraces);

/**
 * @brief Cleans up the memory tracking system
 */
void mcp_memory_tracker_cleanup(void);

/**
 * @brief Records an allocation in the tracking system
 *
 * @param ptr Pointer to the allocated memory
 * @param size Size of the allocation in bytes
 * @param file Source file where the allocation occurred
 * @param line Line number where the allocation occurred
 */
void mcp_memory_tracker_record_alloc(void* ptr, size_t size, const char* file, int line);

/**
 * @brief Records a free operation in the tracking system
 *
 * @param ptr Pointer to the memory being freed
 */
void mcp_memory_tracker_record_free(void* ptr);

/**
 * @brief Gets memory tracking statistics
 *
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_memory_tracker_get_stats(mcp_memory_stats_t* stats);

/**
 * @brief Dumps a report of all active allocations to a file
 *
 * @param filename Name of the file to write the report to
 * @return true if the report was successfully written, false otherwise
 */
bool mcp_memory_tracker_dump_leaks(const char* filename);

/**
 * @brief Sets a memory usage limit
 *
 * @param max_bytes Maximum number of bytes that can be allocated
 * @return true if the limit was successfully set, false otherwise
 */
bool mcp_memory_tracker_set_limit(size_t max_bytes);

/**
 * @brief Checks if a memory allocation would exceed the limit
 *
 * @param size Size of the allocation in bytes
 * @return true if the allocation would exceed the limit, false otherwise
 */
bool mcp_memory_tracker_would_exceed_limit(size_t size);

/**
 * @brief Macro to track allocations with file and line information
 */
#ifdef MCP_TRACK_MEMORY
#define MCP_TRACK_ALLOC(ptr, size) mcp_memory_tracker_record_alloc(ptr, size, __FILE__, __LINE__)
#define MCP_TRACK_FREE(ptr) mcp_memory_tracker_record_free(ptr)
#else
#define MCP_TRACK_ALLOC(ptr, size) ((void)0)
#define MCP_TRACK_FREE(ptr) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* MCP_MEMORY_TRACKER_H */
