#ifndef MCP_LOG_H
#define MCP_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Logging levels
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3
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

#ifdef __cplusplus
}
#endif

#endif // MCP_LOG_H
