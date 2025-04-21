/**
 * @file kmcp_error.h
 * @brief Enhanced error handling system for KMCP module
 *
 * This file defines the error handling system for the KMCP module, including
 * error codes, error categories, error context, and error reporting functions.
 */

#ifndef KMCP_ERROR_H
#define KMCP_ERROR_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief KMCP error categories
 *
 * Error categories group related error codes for better organization and handling.
 */
typedef enum {
    KMCP_ERROR_CATEGORY_NONE = 0,       /**< No error category */
    KMCP_ERROR_CATEGORY_SYSTEM = 1,      /**< System-related errors (memory, IO, etc.) */
    KMCP_ERROR_CATEGORY_NETWORK = 2,     /**< Network-related errors */
    KMCP_ERROR_CATEGORY_PROTOCOL = 3,    /**< Protocol-related errors */
    KMCP_ERROR_CATEGORY_RESOURCE = 4,    /**< Resource-related errors */
    KMCP_ERROR_CATEGORY_CONFIGURATION = 5, /**< Configuration-related errors */
    KMCP_ERROR_CATEGORY_SECURITY = 6,    /**< Security-related errors */
    KMCP_ERROR_CATEGORY_TOOL = 7,        /**< Tool-related errors */
    KMCP_ERROR_CATEGORY_SERVER = 8,      /**< Server-related errors */
    KMCP_ERROR_CATEGORY_CLIENT = 9,      /**< Client-related errors */
    KMCP_ERROR_CATEGORY_INTERNAL = 10    /**< Internal errors */
} kmcp_error_category_t;

/**
 * @brief KMCP error severity levels
 */
typedef enum {
    KMCP_ERROR_SEVERITY_NONE = 0,       /**< No error severity */
    KMCP_ERROR_SEVERITY_INFO = 1,        /**< Informational message */
    KMCP_ERROR_SEVERITY_WARNING = 2,     /**< Warning message */
    KMCP_ERROR_SEVERITY_ERROR = 3,       /**< Error message */
    KMCP_ERROR_SEVERITY_FATAL = 4        /**< Fatal error message */
} kmcp_error_severity_t;

/**
 * @brief KMCP error codes
 *
 * Error codes are organized by category for better organization and handling.
 */
typedef enum {
    /* Success code */
    KMCP_SUCCESS = 0,                  /**< Operation successful */

    /* System category errors (-1 to -99) */
    KMCP_ERROR_INVALID_PARAMETER = -1, /**< Invalid parameter */
    KMCP_ERROR_MEMORY_ALLOCATION = -2, /**< Memory allocation failed */
    KMCP_ERROR_FILE_NOT_FOUND = -3,    /**< File not found */
    KMCP_ERROR_PARSE_FAILED = -4,      /**< Parsing failed */
    KMCP_ERROR_TIMEOUT = -6,           /**< Operation timed out */
    KMCP_ERROR_NOT_IMPLEMENTED = -7,   /**< Feature not implemented */
    KMCP_ERROR_PERMISSION_DENIED = -8, /**< Permission denied */
    KMCP_ERROR_PROCESS_FAILED = -9,    /**< Process operation failed */
    KMCP_ERROR_THREAD_CREATION = -14,  /**< Thread creation failed */
    KMCP_ERROR_IO = -23,               /**< Input/output error */
    KMCP_ERROR_NOT_FOUND = -24,        /**< Item not found */
    KMCP_ERROR_ALREADY_EXISTS = -25,   /**< Item already exists */
    KMCP_ERROR_INVALID_OPERATION = -26, /**< Invalid operation */

    /* Network category errors (-100 to -199) */
    KMCP_ERROR_CONNECTION_FAILED = -100, /**< Connection failed */
    KMCP_ERROR_NETWORK_ERROR = -101,    /**< Network error */
    KMCP_ERROR_SSL_CERTIFICATE = -102,  /**< SSL certificate error */
    KMCP_ERROR_SSL_HANDSHAKE = -103,    /**< SSL handshake failed */

    /* Protocol category errors (-200 to -299) */
    KMCP_ERROR_PROTOCOL_ERROR = -200,   /**< Protocol error */

    /* Resource category errors (-300 to -399) */
    KMCP_ERROR_RESOURCE_NOT_FOUND = -300, /**< Resource not found */
    KMCP_ERROR_RESOURCE_BUSY = -301,    /**< Resource is busy */

    /* Configuration category errors (-400 to -499) */
    KMCP_ERROR_CONFIG_INVALID = -400,   /**< Invalid configuration */

    /* Security category errors (-500 to -599) */
    /* (Reserved for future use) */

    /* Tool category errors (-600 to -699) */
    KMCP_ERROR_TOOL_NOT_FOUND = -600,   /**< Tool not found */
    KMCP_ERROR_TOOL_EXECUTION = -601,   /**< Tool execution failed */

    /* Server category errors (-700 to -799) */
    KMCP_ERROR_SERVER_NOT_FOUND = -700, /**< Server not found */
    KMCP_ERROR_SERVER_ERROR = -701,     /**< Server returned an error */

    /* Client category errors (-800 to -899) */
    KMCP_ERROR_OPERATION_CANCELED = -800, /**< Operation was canceled */

    /* Internal category errors (-900 to -999) */
    KMCP_ERROR_INTERNAL = -900          /**< Internal error */
} kmcp_error_t;

/**
 * @brief Maximum length of error context message
 */
#define KMCP_ERROR_CONTEXT_MAX_LENGTH 256

/**
 * @brief Error context structure
 *
 * This structure contains detailed information about an error, including
 * the error code, category, severity, message, file, line, and function.
 */
typedef struct kmcp_error_context {
    kmcp_error_t error_code;                      /**< Error code */
    kmcp_error_category_t category;               /**< Error category */
    kmcp_error_severity_t severity;               /**< Error severity */
    char message[KMCP_ERROR_CONTEXT_MAX_LENGTH];  /**< Error message */
    const char* file;                             /**< Source file where the error occurred */
    int line;                                     /**< Line number where the error occurred */
    const char* function;                         /**< Function where the error occurred */
    struct kmcp_error_context* next;              /**< Next error in the chain (for nested errors) */
} kmcp_error_context_t;

/**
 * @brief Create a new error context
 *
 * This function creates a new error context with the given error code, message,
 * file, line, and function. The caller is responsible for freeing the error
 * context using kmcp_error_context_free().
 *
 * @param error_code Error code
 * @param file Source file where the error occurred
 * @param line Line number where the error occurred
 * @param function Function where the error occurred
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_context_t* Error context, or NULL if memory allocation failed
 */
kmcp_error_context_t* kmcp_error_context_create(kmcp_error_t error_code,
                                              const char* file,
                                              int line,
                                              const char* function,
                                              const char* format, ...);

/**
 * @brief Create a new error context with a va_list
 *
 * This function is similar to kmcp_error_context_create(), but takes a va_list
 * instead of variable arguments.
 *
 * @param error_code Error code
 * @param file Source file where the error occurred
 * @param line Line number where the error occurred
 * @param function Function where the error occurred
 * @param format Format string for the error message
 * @param args Variable arguments list
 * @return kmcp_error_context_t* Error context, or NULL if memory allocation failed
 */
kmcp_error_context_t* kmcp_error_context_create_va(kmcp_error_t error_code,
                                                 const char* file,
                                                 int line,
                                                 const char* function,
                                                 const char* format,
                                                 va_list args);

/**
 * @brief Free an error context
 *
 * This function frees an error context and all nested error contexts.
 *
 * @param context Error context to free
 */
void kmcp_error_context_free(kmcp_error_context_t* context);

/**
 * @brief Add a nested error to an error context
 *
 * This function adds a nested error to an error context, creating a chain of errors.
 *
 * @param context Error context to add the nested error to
 * @param nested_context Nested error context to add
 * @return kmcp_error_context_t* The original error context
 */
kmcp_error_context_t* kmcp_error_context_add_nested(kmcp_error_context_t* context,
                                                  kmcp_error_context_t* nested_context);

/**
 * @brief Get the error message for an error code
 *
 * @param error_code Error code
 * @return const char* Error message (never NULL)
 */
const char* kmcp_error_message(kmcp_error_t error_code);

/**
 * @brief Get the error category for an error code
 *
 * @param error_code Error code
 * @return kmcp_error_category_t Error category
 */
kmcp_error_category_t kmcp_error_get_category(kmcp_error_t error_code);

/**
 * @brief Get the error severity for an error code
 *
 * @param error_code Error code
 * @return kmcp_error_severity_t Error severity
 */
kmcp_error_severity_t kmcp_error_get_severity(kmcp_error_t error_code);

/**
 * @brief Convert MCP error code to KMCP error code
 *
 * This function converts an MCP error code to the corresponding KMCP error code.
 *
 * @param mcp_error MCP error code
 * @return kmcp_error_t Corresponding KMCP error code
 */
kmcp_error_t kmcp_error_from_mcp(int mcp_error);

/**
 * @brief Log an error with the given error code and message
 *
 * This function logs an error with the given error code and message,
 * and returns the error code for convenient error propagation.
 *
 * @param error_code Error code
 * @param file Source file where the error occurred
 * @param line Line number where the error occurred
 * @param function Function where the error occurred
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_t The same error code that was passed in
 */
kmcp_error_t kmcp_error_log_ex(kmcp_error_t error_code,
                             const char* file,
                             int line,
                             const char* function,
                             const char* format, ...);

/**
 * @brief Log an error context
 *
 * This function logs an error context, including all nested error contexts.
 *
 * @param context Error context to log
 */
void kmcp_error_context_log(const kmcp_error_context_t* context);

/**
 * @brief Format an error context as a string
 *
 * This function formats an error context as a string, including all nested error contexts.
 *
 * @param context Error context to format
 * @param buffer Buffer to store the formatted string
 * @param buffer_size Size of the buffer
 * @return size_t Number of characters written to the buffer (excluding the null terminator)
 */
size_t kmcp_error_context_format(const kmcp_error_context_t* context,
                               char* buffer,
                               size_t buffer_size);

/**
 * @brief Backward compatibility function for kmcp_error_log
 *
 * This function provides backward compatibility with the old kmcp_error_log function.
 * New code should use kmcp_error_log_ex instead.
 *
 * @param error_code Error code
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_t The same error code that was passed in
 */
kmcp_error_t kmcp_error_log(kmcp_error_t error_code, const char* format, ...);

/**
 * @brief Convenience macro for logging errors with file, line, and function information
 */
#define KMCP_ERROR_LOG(error_code, format, ...) \
    kmcp_error_log_ex((error_code), __FILE__, __LINE__, __func__, (format), ##__VA_ARGS__)

/**
 * @brief Convenience macro for creating error contexts with file, line, and function information
 */
#define KMCP_ERROR_CONTEXT_CREATE(error_code, format, ...) \
    kmcp_error_context_create((error_code), __FILE__, __LINE__, __func__, (format), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* KMCP_ERROR_H */
