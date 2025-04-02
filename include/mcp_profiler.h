#ifndef MCP_PROFILER_H
#define MCP_PROFILER_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compile-time profiling hooks
// Define MCP_ENABLE_PROFILING (e.g., via CMake -DMCP_ENABLE_PROFILING=ON) to enable profiling.
#ifdef MCP_ENABLE_PROFILING
    #define PROFILE_START(name) mcp_profile_start(name)
    #define PROFILE_END(name)   mcp_profile_end(name)
#else
    #define PROFILE_START(name) ((void)0) // Expand to nothing if profiling disabled
    #define PROFILE_END(name)   ((void)0)
#endif

/**
 * @brief Starts a profiling timer for a named code section.
 *
 * This function should be called at the beginning of the code block to be profiled.
 * Calls are cumulative if the same section name is used multiple times.
 * This function does nothing if MCP_ENABLE_PROFILING is not defined.
 *
 * @param section_name A unique, constant string identifying the code section.
 */
void mcp_profile_start(const char* section_name);

/**
 * @brief Stops the profiling timer for a named code section.
 *
 * This function should be called at the end of the code block being profiled.
 * It calculates the duration since the corresponding mcp_profile_start call
 * and adds it to the total time for that section.
 * This function does nothing if MCP_ENABLE_PROFILING is not defined.
 *
 * @param section_name The same string identifier used in mcp_profile_start.
 */
void mcp_profile_end(const char* section_name);

/**
 * @brief Prints a summary report of the profiling data collected.
 *
 * Outputs the total time spent and number of calls for each profiled section.
 * This function does nothing if MCP_ENABLE_PROFILING is not defined.
 *
 * @param output A FILE stream where the report should be written (e.g., stdout, stderr, or a file).
 */
void mcp_profile_report(FILE* output);

/**
 * @brief Resets all collected profiling data.
 *
 * Clears timings and call counts for all sections.
 * This function does nothing if MCP_ENABLE_PROFILING is not defined.
 */
void mcp_profile_reset();


#ifdef __cplusplus
}
#endif

#endif // MCP_PROFILER_H
