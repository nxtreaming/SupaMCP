/**
 * @file kmcp_test_utils.h
 * @brief Test utilities for KMCP module
 *
 * This file defines test utilities for the KMCP module, including
 * test assertions, fixtures, and other testing helpers.
 */

#ifndef KMCP_TEST_UTILS_H
#define KMCP_TEST_UTILS_H

#include "kmcp_error.h"
#include "kmcp_memory.h"
#include "kmcp_event.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test result structure
 */
typedef struct {
    int total;          /**< Total number of assertions */
    int passed;         /**< Number of passed assertions */
    int failed;         /**< Number of failed assertions */
    char* test_name;    /**< Name of the test */
} kmcp_test_result_t;

/**
 * @brief Test fixture structure
 */
typedef struct {
    void* data;                     /**< Test fixture data */
    kmcp_memory_context_t* context; /**< Memory context for the test */
    kmcp_test_result_t result;      /**< Test result */
} kmcp_test_fixture_t;

/**
 * @brief Test setup function type
 */
typedef void (*kmcp_test_setup_fn)(kmcp_test_fixture_t* fixture);

/**
 * @brief Test teardown function type
 */
typedef void (*kmcp_test_teardown_fn)(kmcp_test_fixture_t* fixture);

/**
 * @brief Test function type
 */
typedef void (*kmcp_test_fn)(kmcp_test_fixture_t* fixture);

/**
 * @brief Initialize the test framework
 *
 * This function initializes the test framework, including the memory
 * management system and event system.
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_test_init(void);

/**
 * @brief Shut down the test framework
 *
 * This function shuts down the test framework, including the memory
 * management system and event system.
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_test_shutdown(void);

/**
 * @brief Create a test fixture
 *
 * This function creates a test fixture with the specified name.
 *
 * @param test_name Name of the test
 * @return kmcp_test_fixture_t* Returns a pointer to the fixture, or NULL on failure
 */
kmcp_test_fixture_t* kmcp_test_fixture_create(const char* test_name);

/**
 * @brief Destroy a test fixture
 *
 * This function destroys a test fixture and frees all associated resources.
 *
 * @param fixture Fixture to destroy
 */
void kmcp_test_fixture_destroy(kmcp_test_fixture_t* fixture);

/**
 * @brief Run a test with setup and teardown
 *
 * This function runs a test with the specified setup and teardown functions.
 *
 * @param test_name Name of the test
 * @param test Test function
 * @param setup Setup function (can be NULL)
 * @param teardown Teardown function (can be NULL)
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_test_run(const char* test_name, 
                 kmcp_test_fn test, 
                 kmcp_test_setup_fn setup, 
                 kmcp_test_teardown_fn teardown);

/**
 * @brief Assert that a condition is true
 *
 * This function asserts that a condition is true and updates the test result.
 *
 * @param fixture Test fixture
 * @param condition Condition to check
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert(kmcp_test_fixture_t* fixture, 
                     bool condition, 
                     const char* message, 
                     const char* file, 
                     int line);

/**
 * @brief Assert that two integers are equal
 *
 * This function asserts that two integers are equal and updates the test result.
 *
 * @param fixture Test fixture
 * @param expected Expected value
 * @param actual Actual value
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_int_eq(kmcp_test_fixture_t* fixture, 
                            int expected, 
                            int actual, 
                            const char* message, 
                            const char* file, 
                            int line);

/**
 * @brief Assert that two strings are equal
 *
 * This function asserts that two strings are equal and updates the test result.
 *
 * @param fixture Test fixture
 * @param expected Expected value
 * @param actual Actual value
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_str_eq(kmcp_test_fixture_t* fixture, 
                            const char* expected, 
                            const char* actual, 
                            const char* message, 
                            const char* file, 
                            int line);

/**
 * @brief Assert that a pointer is not NULL
 *
 * This function asserts that a pointer is not NULL and updates the test result.
 *
 * @param fixture Test fixture
 * @param ptr Pointer to check
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_not_null(kmcp_test_fixture_t* fixture, 
                              const void* ptr, 
                              const char* message, 
                              const char* file, 
                              int line);

/**
 * @brief Assert that a pointer is NULL
 *
 * This function asserts that a pointer is NULL and updates the test result.
 *
 * @param fixture Test fixture
 * @param ptr Pointer to check
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_null(kmcp_test_fixture_t* fixture, 
                          const void* ptr, 
                          const char* message, 
                          const char* file, 
                          int line);

/**
 * @brief Assert that an error code is success
 *
 * This function asserts that an error code is KMCP_SUCCESS and updates the test result.
 *
 * @param fixture Test fixture
 * @param error_code Error code to check
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_success(kmcp_test_fixture_t* fixture, 
                             kmcp_error_t error_code, 
                             const char* message, 
                             const char* file, 
                             int line);

/**
 * @brief Assert that an error code is not success
 *
 * This function asserts that an error code is not KMCP_SUCCESS and updates the test result.
 *
 * @param fixture Test fixture
 * @param error_code Error code to check
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_error(kmcp_test_fixture_t* fixture, 
                           kmcp_error_t error_code, 
                           const char* message, 
                           const char* file, 
                           int line);

/**
 * @brief Assert that an error code matches an expected error code
 *
 * This function asserts that an error code matches an expected error code and updates the test result.
 *
 * @param fixture Test fixture
 * @param expected Expected error code
 * @param actual Actual error code
 * @param message Message to display if the assertion fails
 * @param file Source file where the assertion occurred
 * @param line Line number where the assertion occurred
 * @return bool Returns true if the assertion passed, false otherwise
 */
bool kmcp_test_assert_error_eq(kmcp_test_fixture_t* fixture, 
                              kmcp_error_t expected, 
                              kmcp_error_t actual, 
                              const char* message, 
                              const char* file, 
                              int line);

/**
 * @brief Convenience macro for assertions
 */
#define KMCP_TEST_ASSERT(fixture, condition, message) \
    kmcp_test_assert((fixture), (condition), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for integer equality assertions
 */
#define KMCP_TEST_ASSERT_INT_EQ(fixture, expected, actual, message) \
    kmcp_test_assert_int_eq((fixture), (expected), (actual), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for string equality assertions
 */
#define KMCP_TEST_ASSERT_STR_EQ(fixture, expected, actual, message) \
    kmcp_test_assert_str_eq((fixture), (expected), (actual), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for not NULL assertions
 */
#define KMCP_TEST_ASSERT_NOT_NULL(fixture, ptr, message) \
    kmcp_test_assert_not_null((fixture), (ptr), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for NULL assertions
 */
#define KMCP_TEST_ASSERT_NULL(fixture, ptr, message) \
    kmcp_test_assert_null((fixture), (ptr), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for success assertions
 */
#define KMCP_TEST_ASSERT_SUCCESS(fixture, error_code, message) \
    kmcp_test_assert_success((fixture), (error_code), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for error assertions
 */
#define KMCP_TEST_ASSERT_ERROR(fixture, error_code, message) \
    kmcp_test_assert_error((fixture), (error_code), (message), __FILE__, __LINE__)

/**
 * @brief Convenience macro for error equality assertions
 */
#define KMCP_TEST_ASSERT_ERROR_EQ(fixture, expected, actual, message) \
    kmcp_test_assert_error_eq((fixture), (expected), (actual), (message), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* KMCP_TEST_UTILS_H */
