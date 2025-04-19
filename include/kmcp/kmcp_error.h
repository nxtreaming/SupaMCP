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
    KMCP_ERROR_INTERNAL = -99          /**< Internal error */
} kmcp_error_t;

/**
 * @brief Get error message for a KMCP error code
 *
 * @param error_code Error code
 * @return const char* Error message
 */
const char* kmcp_error_message(kmcp_error_t error_code);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_ERROR_H */
