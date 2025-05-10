#ifndef MCP_LOG_H
#define MCP_LOG_H

#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Defines the severity levels for log messages.
 * Follows common logging level conventions.
 */
typedef enum {
    MCP_LOG_LEVEL_TRACE = 0, /**< Fine-grained debugging information. */
    MCP_LOG_LEVEL_DEBUG = 1, /**< Detailed debugging information. */
    MCP_LOG_LEVEL_INFO  = 2, /**< Informational messages about normal operation. */
    MCP_LOG_LEVEL_WARN  = 3, /**< Warning conditions that might indicate potential problems. */
    MCP_LOG_LEVEL_ERROR = 4, /**< Error conditions that prevent normal operation. */
    MCP_LOG_LEVEL_FATAL = 5  /**< Severe errors causing program termination (or intended termination). */
} mcp_log_level_t;

/**
 * @brief Initializes the logging system.
 *
 * Sets the global log level and optionally opens a log file.
 *
 * @param log_file_path Path to the log file (can be NULL to disable file logging).
 * @param level The minimum log level to output (e.g., MCP_LOG_LEVEL_INFO will show INFO, WARN, ERROR, FATAL).
 * @return 0 on success, non-zero on failure (e.g., cannot open file).
 */
int mcp_log_init(const char* log_file_path, mcp_log_level_t level);

/**
 * @brief Closes the logging system (closes log file if open).
 */
void mcp_log_close(void);

/**
 * @brief Logs a message.
 *
 * Outputs the message to stderr and to the log file (if configured).
 * The message is only logged if the given level is less than or equal to
 * the globally configured log level.
 *
 * @param level The log level of the message.
 * @param file The source file where the log originated (__FILE__).
 * @param line The line number where the log originated (__LINE__).
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
void mcp_log_log(mcp_log_level_t level, const char* file, int line, const char* format, ...);

// --- Logging Macros ---
// Use macros to automatically capture file/line and check level

/** @brief Log a TRACE level message. */
#define mcp_log_trace(...) mcp_log_log(MCP_LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Log a DEBUG level message. */
#define mcp_log_debug(...) mcp_log_log(MCP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Log an INFO level message. */
#define mcp_log_info(...)  mcp_log_log(MCP_LOG_LEVEL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
/** @brief Log a WARN level message. */
#define mcp_log_warn(...)  mcp_log_log(MCP_LOG_LEVEL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
/** @brief Log an ERROR level message. */
#define mcp_log_error(...) mcp_log_log(MCP_LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Log a FATAL level message. */
#define mcp_log_fatal(...) mcp_log_log(MCP_LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Sets the minimum log level to output.
 * @param level The minimum level. Messages below this level will be ignored.
 */
void mcp_log_set_level(mcp_log_level_t level);

/**
 * @brief Gets the current minimum log level.
 * @return The current minimum log level.
 */
mcp_log_level_t mcp_log_get_level(void);

/**
 * @brief Enables or disables logging output.
 * @param quiet If true, disable all logging output. If false, enable logging based on level.
 */
void mcp_log_set_quiet(bool quiet);

/**
 * @brief Enables or disables colored output (if supported by the terminal).
 * @param use_color True to enable color, false to disable.
 */
void mcp_log_set_color(bool use_color);

/**
 * @brief Defines the output format for log messages.
 */
typedef enum {
    MCP_LOG_FORMAT_TEXT, /**< Simple human-readable text format (default). */
    MCP_LOG_FORMAT_JSON, /**< JSON format, suitable for structured logging collectors. */
    // MCP_LOG_FORMAT_CSV // Example: Could add CSV or other formats
} mcp_log_format_t;

/**
 * @brief Sets the desired output format for logs.
 * @param format The log format to use.
 */
void mcp_log_set_format(mcp_log_format_t format);

/**
 * @brief Records a structured log message with additional context.
 *
 * This allows logging key-value pairs or specific event details in a structured way,
 * especially useful when using JSON format.
 *
 * @param level The log level of the message (use existing mcp_log_level_t).
 * @param component The software component generating the log (e.g., "TCPServer", "JSONParser").
 * @param event A specific event name or identifier (e.g., "ConnectionAccepted", "ParseError").
 * @param format The printf-style format string for the main message.
 * @param ... Arguments for the format string.
 * @note The implementation will decide how to incorporate component and event into the chosen format.
 *       For JSON, they would likely become distinct fields. For TEXT, they might be prefixed.
 */
void mcp_log_structured(
    mcp_log_level_t level,
    const char* component,
    const char* event,
    const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // MCP_LOG_H
