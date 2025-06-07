#ifndef MCP_LOG_CONFIG_H
#define MCP_LOG_CONFIG_H

/**
 * @file mcp_log_config.h
 * @brief Centralized logging configuration for MCP WebSocket transport optimization
 * 
 * This header provides compile-time configuration for logging verbosity levels
 * to optimize performance in production environments while maintaining debugging
 * capabilities in development builds.
 */

#ifdef __cplusplus
extern "C" {
#endif

// --- Logging Configuration Flags ---

/**
 * @brief Enable debug logging in release builds
 * 
 * By default, debug logging is only enabled in debug builds (_DEBUG).
 * Define this macro to enable debug logging in release builds as well.
 * 
 * Usage: -DMCP_ENABLE_DEBUG_LOGS=1
 */
#ifndef MCP_ENABLE_DEBUG_LOGS
    #if defined(_DEBUG) || defined(DEBUG)
        #define MCP_ENABLE_DEBUG_LOGS 1
    #else
        #define MCP_ENABLE_DEBUG_LOGS 0
    #endif
#endif

/**
 * @brief Enable verbose logging for high-frequency operations
 * 
 * This controls logging of high-frequency operations like message sending/receiving,
 * connection state changes, and performance metrics. Disabled by default to
 * reduce performance impact in production.
 * 
 * Usage: -DMCP_ENABLE_VERBOSE_LOGS=1
 */
#ifndef MCP_ENABLE_VERBOSE_LOGS
    #define MCP_ENABLE_VERBOSE_LOGS 0
#endif

/**
 * @brief Enable data content logging (message payloads, hex dumps)
 * 
 * This controls logging of actual message content and hex dumps.
 * Should only be enabled for debugging specific protocol issues.
 * 
 * Usage: -DMCP_ENABLE_DATA_LOGS=1
 */
#ifndef MCP_ENABLE_DATA_LOGS
    #define MCP_ENABLE_DATA_LOGS 0
#endif

/**
 * @brief Enable performance metrics logging
 * 
 * This controls logging of performance statistics like service call rates,
 * client counts, and timing information.
 * 
 * Usage: -DMCP_ENABLE_PERF_LOGS=1
 */
#ifndef MCP_ENABLE_PERF_LOGS
    #define MCP_ENABLE_PERF_LOGS 0
#endif

// --- Backward Compatibility ---

/**
 * @brief Backward compatibility with existing MCP_VERBOSE_DEBUG flag
 * 
 * If MCP_VERBOSE_DEBUG is defined, enable all verbose logging modes
 * for compatibility with existing code.
 */
#ifdef MCP_VERBOSE_DEBUG
    #undef MCP_ENABLE_VERBOSE_LOGS
    #undef MCP_ENABLE_DATA_LOGS
    #undef MCP_ENABLE_PERF_LOGS
    #define MCP_ENABLE_VERBOSE_LOGS 1
    #define MCP_ENABLE_DATA_LOGS 1
    #define MCP_ENABLE_PERF_LOGS 1
#endif

// --- Logging Level Recommendations ---

/**
 * @brief Recommended logging configurations for different environments
 * 
 * Development Build:
 *   -DMCP_ENABLE_DEBUG_LOGS=1
 *   -DMCP_ENABLE_VERBOSE_LOGS=1
 *   -DMCP_ENABLE_DATA_LOGS=1
 *   -DMCP_ENABLE_PERF_LOGS=1
 * 
 * Testing Build:
 *   -DMCP_ENABLE_DEBUG_LOGS=1
 *   -DMCP_ENABLE_VERBOSE_LOGS=0
 *   -DMCP_ENABLE_DATA_LOGS=0
 *   -DMCP_ENABLE_PERF_LOGS=1
 * 
 * Production Build:
 *   -DMCP_ENABLE_DEBUG_LOGS=0
 *   -DMCP_ENABLE_VERBOSE_LOGS=0
 *   -DMCP_ENABLE_DATA_LOGS=0
 *   -DMCP_ENABLE_PERF_LOGS=0
 * 
 * Debug Production Issues:
 *   -DMCP_ENABLE_DEBUG_LOGS=1
 *   -DMCP_ENABLE_VERBOSE_LOGS=1
 *   -DMCP_ENABLE_DATA_LOGS=0
 *   -DMCP_ENABLE_PERF_LOGS=1
 */

// --- Configuration Summary ---

/**
 * @brief Configuration verification macros
 *
 * These can be used to verify the current logging configuration at compile time.
 * Uncomment the pragma messages below if you want to see the configuration during compilation.
 */

/*
// Uncomment these lines to see logging configuration during compilation:

#if MCP_ENABLE_DEBUG_LOGS
    #pragma message("MCP Logging: Debug logs ENABLED")
#else
    #pragma message("MCP Logging: Debug logs DISABLED")
#endif

#if MCP_ENABLE_VERBOSE_LOGS
    #pragma message("MCP Logging: Verbose logs ENABLED")
#else
    #pragma message("MCP Logging: Verbose logs DISABLED")
#endif

#if MCP_ENABLE_DATA_LOGS
    #pragma message("MCP Logging: Data logs ENABLED")
#else
    #pragma message("MCP Logging: Data logs DISABLED")
#endif

#if MCP_ENABLE_PERF_LOGS
    #pragma message("MCP Logging: Performance logs ENABLED")
#else
    #pragma message("MCP Logging: Performance logs DISABLED")
#endif

*/

#ifdef __cplusplus
}
#endif

#endif // MCP_LOG_CONFIG_H
