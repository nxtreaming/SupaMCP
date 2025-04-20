/**
 * @file kmcp_error.h
 * @brief Error code definitions for KMCP module
 */

#ifndef KMCP_ERROR_H
#define KMCP_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief KMCP error codes
 */
typedef enum {
    KMCP_SUCCESS = 0,                  /**< Operation successful */
    KMCP_ERROR_INVALID_PARAMETER = -1, /**< Invalid parameter */
    KMCP_ERROR_MEMORY_ALLOCATION = -2, /**< Memory allocation failed */
    KMCP_ERROR_FILE_NOT_FOUND = -3,    /**< File not found */
    KMCP_ERROR_PARSE_FAILED = -4,      /**< Parsing failed */
    KMCP_ERROR_CONNECTION_FAILED = -5, /**< Connection failed */
    KMCP_ERROR_TIMEOUT = -6,           /**< Operation timed out */
    KMCP_ERROR_NOT_IMPLEMENTED = -7,   /**< Feature not implemented */
    KMCP_ERROR_PERMISSION_DENIED = -8, /**< Permission denied */
    KMCP_ERROR_PROCESS_FAILED = -9,    /**< Process operation failed */
    KMCP_ERROR_SERVER_NOT_FOUND = -10, /**< Server not found */
    KMCP_ERROR_TOOL_NOT_FOUND = -11,   /**< Tool not found */
    KMCP_ERROR_RESOURCE_NOT_FOUND = -12, /**< Resource not found */
    KMCP_ERROR_TOOL_EXECUTION = -13,   /**< Tool execution failed */
    KMCP_ERROR_THREAD_CREATION = -14,  /**< Thread creation failed */
    KMCP_ERROR_SSL_CERTIFICATE = -15,  /**< SSL certificate error */
    KMCP_ERROR_SSL_HANDSHAKE = -16,    /**< SSL handshake failed */
    KMCP_ERROR_CONFIG_INVALID = -17,   /**< Invalid configuration */
    KMCP_ERROR_SERVER_ERROR = -18,     /**< Server returned an error */
    KMCP_ERROR_NETWORK_ERROR = -19,    /**< Network error */
    KMCP_ERROR_PROTOCOL_ERROR = -20,   /**< Protocol error */
    KMCP_ERROR_RESOURCE_BUSY = -21,    /**< Resource is busy */
    KMCP_ERROR_OPERATION_CANCELED = -22, /**< Operation was canceled */
    KMCP_ERROR_IO = -23,               /**< Input/output error */
    KMCP_ERROR_NOT_FOUND = -24,        /**< Item not found */
    KMCP_ERROR_ALREADY_EXISTS = -25,   /**< Item already exists */
    KMCP_ERROR_INVALID_OPERATION = -26, /**< Invalid operation */
    KMCP_ERROR_INTERNAL = -99          /**< Internal error */
} kmcp_error_t;

/**
 * @brief Get error message for a KMCP error code
 *
 * @param error_code Error code
 * @return const char* Error message (never NULL)
 */
const char* kmcp_error_message(kmcp_error_t error_code);

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
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_t The same error code that was passed in
 */
kmcp_error_t kmcp_error_log(kmcp_error_t error_code, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_ERROR_H */
