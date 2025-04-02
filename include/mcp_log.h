#ifndef MCP_LOG_H
#define MCP_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Defines the severity levels for log messages.
 */
typedef enum {
    LOG_LEVEL_ERROR = 0, /**< Error conditions that prevent normal operation. */
    LOG_LEVEL_WARN = 1,  /**< Warning conditions that might indicate potential problems. */
    LOG_LEVEL_INFO = 2,  /**< Informational messages about normal operation. */
    LOG_LEVEL_DEBUG = 3  /**< Detailed debugging information. */
} log_level_t;

/**
 * @brief Initializes the logging system.
 *
 * Sets the global log level and optionally opens a log file.
 *
 * @param log_file_path Path to the log file (can be NULL to disable file logging).
 * @param level The maximum log level to output.
 * @return 0 on success, non-zero on failure (e.g., cannot open file).
 */
int init_logging(const char* log_file_path, log_level_t level);

/**
 * @brief Closes the logging system (closes log file if open).
 */
void close_logging(void);

/**
 * @brief Logs a message.
 *
 * Outputs the message to stderr and to the log file (if configured).
 * The message is only logged if the given level is less than or equal to
 * the globally configured log level.
 *
 * @param level The log level of the message.
 * @param format The printf-style format string.
 * @param ... Arguments for the format string.
 */
void log_message(log_level_t level, const char* format, ...);

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
 * @param level The log level of the message (use existing log_level_t).
 * @param component The software component generating the log (e.g., "TCPServer", "JSONParser").
 * @param event A specific event name or identifier (e.g., "ConnectionAccepted", "ParseError").
 * @param format The printf-style format string for the main message.
 * @param ... Arguments for the format string.
 * @note The implementation will decide how to incorporate component and event into the chosen format.
 *       For JSON, they would likely become distinct fields. For TEXT, they might be prefixed.
 */
void mcp_log_structured(
    log_level_t level,
    const char* component,
    const char* event,
    const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // MCP_LOG_H
