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
            return kmcp_error_log((error_code), "NULL pointer detected"); \
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
            return kmcp_error_log((error_code), (message)); \
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
            return kmcp_error_log(KMCP_ERROR_MEMORY_ALLOCATION, "Memory allocation failed"); \
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
            result = kmcp_error_log(KMCP_ERROR_MEMORY_ALLOCATION, "Memory allocation failed"); \
            goto label; \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* KMCP_COMMON_H */
