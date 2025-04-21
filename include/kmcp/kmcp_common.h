/**
 * @file kmcp_common.h
 * @brief Common definitions and macros for KMCP module
 */

#ifndef KMCP_COMMON_H
#define KMCP_COMMON_H

#include "kmcp_error.h"
#include "mcp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if a pointer is NULL and return an error if it is
 *
 * @param ptr Pointer to check
 * @param error_code Error code to return if the pointer is NULL
 * @return kmcp_error_t Returns KMCP_SUCCESS if the pointer is not NULL, error_code otherwise
 */
#define KMCP_CHECK_NULL(ptr, error_code) \
    do { \
        if ((ptr) == NULL) { \
            return KMCP_ERROR_LOG((error_code), "NULL pointer detected"); \
        } \
    } while (0)

/**
 * @brief Check if a pointer is NULL and return an error if it is
 *
 * @param ptr Pointer to check
 * @return kmcp_error_t Returns KMCP_SUCCESS if the pointer is not NULL, KMCP_ERROR_INVALID_PARAMETER otherwise
 */
#define KMCP_CHECK_PARAM(ptr) \
    KMCP_CHECK_NULL((ptr), KMCP_ERROR_INVALID_PARAMETER)

/**
 * @brief Check if a condition is true and return an error if it is not
 *
 * @param condition Condition to check
 * @param error_code Error code to return if the condition is false
 * @param message Error message to log if the condition is false
 * @return kmcp_error_t Returns KMCP_SUCCESS if the condition is true, error_code otherwise
 */
#define KMCP_CHECK_CONDITION(condition, error_code, message) \
    do { \
        if (!(condition)) { \
            return KMCP_ERROR_LOG((error_code), (message)); \
        } \
    } while (0)

/**
 * @brief Check if a function call returns an error and return that error if it does
 *
 * @param call Function call to check
 * @return kmcp_error_t Returns the result of the function call
 */
#define KMCP_CHECK_RESULT(call) \
    do { \
        kmcp_error_t result = (call); \
        if (result != KMCP_SUCCESS) { \
            return result; \
        } \
    } while (0)

/**
 * @brief Check if a function call returns an error and goto a label if it does
 *
 * @param call Function call to check
 * @param label Label to goto if the function call returns an error
 * @return kmcp_error_t Returns the result of the function call
 */
#define KMCP_CHECK_RESULT_GOTO(call, label) \
    do { \
        result = (call); \
        if (result != KMCP_SUCCESS) { \
            goto label; \
        } \
    } while (0)

/**
 * @brief Check if a memory allocation succeeded and return an error if it did not
 *
 * @param ptr Pointer to check
 * @return kmcp_error_t Returns KMCP_SUCCESS if the pointer is not NULL, KMCP_ERROR_MEMORY_ALLOCATION otherwise
 */
#define KMCP_CHECK_MEMORY(ptr) \
    do { \
        if ((ptr) == NULL) { \
            return KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Memory allocation failed"); \
        } \
    } while (0)

/**
 * @brief Check if a memory allocation succeeded and goto a label if it did not
 *
 * @param ptr Pointer to check
 * @param label Label to goto if the pointer is NULL
 * @return kmcp_error_t Returns KMCP_SUCCESS if the pointer is not NULL, KMCP_ERROR_MEMORY_ALLOCATION otherwise
 */
#define KMCP_CHECK_MEMORY_GOTO(ptr, label) \
    do { \
        if ((ptr) == NULL) { \
            result = KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Memory allocation failed"); \
            goto label; \
        } \
    } while (0)

/**
 * @brief Check if a function call returns an error and create a nested error context if it does
 *
 * @param call Function call to check
 * @param error_code Error code to return if the function call returns an error
 * @param message Error message to log if the function call returns an error
 * @return kmcp_error_t Returns KMCP_SUCCESS if the function call succeeds, error_code otherwise
 */
#define KMCP_CHECK_RESULT_WITH_CONTEXT(call, error_code, message) \
    do { \
        kmcp_error_t result = (call); \
        if (result != KMCP_SUCCESS) { \
            kmcp_error_context_t* context = KMCP_ERROR_CONTEXT_CREATE(result, "Function call failed"); \
            kmcp_error_context_t* nested = KMCP_ERROR_CONTEXT_CREATE(error_code, message); \
            if (context && nested) { \
                kmcp_error_context_add_nested(nested, context); \
                kmcp_error_context_log(nested); \
                kmcp_error_context_free(nested); \
            } else { \
                if (context) kmcp_error_context_free(context); \
                if (nested) kmcp_error_context_free(nested); \
                KMCP_ERROR_LOG(error_code, "%s (original error: %s)", message, kmcp_error_message(result)); \
            } \
            return error_code; \
        } \
    } while (0)

/**
 * @brief Create an error context and return an error code
 *
 * @param error_code Error code to return
 * @param message Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_t Returns the error code
 */
#define KMCP_RETURN_ERROR_WITH_CONTEXT(error_code, message, ...) \
    do { \
        kmcp_error_context_t* context = KMCP_ERROR_CONTEXT_CREATE(error_code, message, ##__VA_ARGS__); \
        if (context) { \
            kmcp_error_context_log(context); \
            kmcp_error_context_free(context); \
        } else { \
            KMCP_ERROR_LOG(error_code, message, ##__VA_ARGS__); \
        } \
        return error_code; \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* KMCP_COMMON_H */
