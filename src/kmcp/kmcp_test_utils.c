#include "kmcp_test_utils.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize the test framework
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_test_init(void) {
    // Initialize memory management system with full tracking
    kmcp_error_t result = kmcp_memory_init(KMCP_MEMORY_TRACKING_FULL);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to initialize memory management system");
        return result;
    }

    // Initialize event system
    result = kmcp_event_init();
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to initialize event system");
        kmcp_memory_shutdown(true);
        return result;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Shut down the test framework
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_test_shutdown(void) {
    // Shut down event system
    kmcp_event_shutdown(); // This function returns void

    // Shut down memory management system
    kmcp_error_t memory_result = kmcp_memory_shutdown(true);
    if (memory_result != KMCP_SUCCESS) {
        mcp_log_error("Failed to shut down memory management system");
    }

    return memory_result;
}

/**
 * @brief Create a test fixture
 *
 * @param test_name Name of the test
 * @return kmcp_test_fixture_t* Returns a pointer to the fixture, or NULL on failure
 */
kmcp_test_fixture_t* kmcp_test_fixture_create(const char* test_name) {
    // Allocate memory for the fixture
    kmcp_test_fixture_t* fixture = (kmcp_test_fixture_t*)KMCP_MEMORY_ALLOC_TAG(
        sizeof(kmcp_test_fixture_t), "test_fixture");
    if (!fixture) {
        mcp_log_error("Failed to allocate memory for test fixture");
        return NULL;
    }

    // Initialize the fixture
    memset(fixture, 0, sizeof(kmcp_test_fixture_t));

    // Create a memory context for the test
    fixture->context = kmcp_memory_context_create(test_name ? test_name : "unnamed_test");
    if (!fixture->context) {
        mcp_log_error("Failed to create memory context for test fixture");
        kmcp_memory_free(fixture);
        return NULL;
    }

    // Set the test name
    if (test_name) {
        fixture->result.test_name = KMCP_MEMORY_CONTEXT_STRDUP(fixture->context, test_name);
        if (!fixture->result.test_name) {
            mcp_log_error("Failed to duplicate test name");
            kmcp_memory_context_destroy(fixture->context);
            kmcp_memory_free(fixture);
            return NULL;
        }
    }

    return fixture;
}

/**
 * @brief Destroy a test fixture
 *
 * @param fixture Fixture to destroy
 */
void kmcp_test_fixture_destroy(kmcp_test_fixture_t* fixture) {
    if (!fixture) {
        return;
    }

    // Print memory leaks in the context if any
    if (fixture->context) {
        kmcp_memory_stats_t stats;
        if (kmcp_memory_context_get_stats(fixture->context, &stats) == KMCP_SUCCESS) {
            if (stats.active_allocations > 0) {
                mcp_log_warn("Test fixture '%s' has %zu active allocations that will be freed",
                            fixture->result.test_name ? fixture->result.test_name : "unnamed",
                            stats.active_allocations);
            }
        }

        // Destroy the memory context (which will free all allocations in the context)
        kmcp_memory_context_destroy(fixture->context);
    }

    // Free the fixture itself
    kmcp_memory_free(fixture);
}

/**
 * @brief Run a test with setup and teardown
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
                 kmcp_test_teardown_fn teardown) {
    if (!test) {
        mcp_log_error("Test function cannot be NULL");
        return 1;
    }

    // Print test header
    printf("\n=== Running test: %s ===\n", test_name ? test_name : "unnamed");

    // Create a test fixture
    kmcp_test_fixture_t* fixture = kmcp_test_fixture_create(test_name);
    if (!fixture) {
        mcp_log_error("Failed to create test fixture");
        return 1;
    }

    // Run setup function if provided
    if (setup) {
        setup(fixture);
    }

    // Run the test
    test(fixture);

    // Run teardown function if provided
    if (teardown) {
        teardown(fixture);
    }

    // Print test results
    printf("\n--- Test results: %s ---\n", test_name ? test_name : "unnamed");
    printf("Total assertions: %d\n", fixture->result.total);
    printf("Passed: %d\n", fixture->result.passed);
    printf("Failed: %d\n", fixture->result.failed);
    printf("%s\n", fixture->result.failed == 0 ? "PASS" : "FAIL");

    // Determine test result
    int result = fixture->result.failed;

    // Destroy the test fixture
    kmcp_test_fixture_destroy(fixture);

    return result;
}

/**
 * @brief Assert that a condition is true
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
                     int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (condition) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        return false;
    }
}

/**
 * @brief Assert that two integers are equal
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
                            int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (expected == actual) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: %d\n", expected);
        printf("  Actual: %d\n", actual);
        return false;
    }
}

/**
 * @brief Assert that two strings are equal
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
                            int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    // Handle NULL strings
    if (expected == NULL && actual == NULL) {
        fixture->result.passed++;
        return true;
    } else if (expected == NULL || actual == NULL) {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: %s\n", expected ? expected : "NULL");
        printf("  Actual: %s\n", actual ? actual : "NULL");
        return false;
    }

    // Compare strings
    if (strcmp(expected, actual) == 0) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: \"%s\"\n", expected);
        printf("  Actual: \"%s\"\n", actual);
        return false;
    }
}

/**
 * @brief Assert that a pointer is not NULL
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
                              int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (ptr != NULL) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: not NULL\n");
        printf("  Actual: NULL\n");
        return false;
    }
}

/**
 * @brief Assert that a pointer is NULL
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
                          int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (ptr == NULL) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: NULL\n");
        printf("  Actual: %p\n", ptr);
        return false;
    }
}

/**
 * @brief Assert that an error code is success
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
                             int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (error_code == KMCP_SUCCESS) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: KMCP_SUCCESS\n");
        printf("  Actual: %d (%s)\n", error_code, kmcp_error_message(error_code));
        return false;
    }
}

/**
 * @brief Assert that an error code is not success
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
                           int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (error_code != KMCP_SUCCESS) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: not KMCP_SUCCESS\n");
        printf("  Actual: KMCP_SUCCESS\n");
        return false;
    }
}

/**
 * @brief Assert that an error code matches an expected error code
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
                              int line) {
    if (!fixture) {
        mcp_log_error("Test fixture cannot be NULL");
        return false;
    }

    // Update test result
    fixture->result.total++;

    if (expected == actual) {
        fixture->result.passed++;
        return true;
    } else {
        fixture->result.failed++;
        printf("Assertion failed at %s:%d: %s\n",
               file ? file : "unknown",
               line,
               message ? message : "no message");
        printf("  Expected: %d (%s)\n", expected, kmcp_error_message(expected));
        printf("  Actual: %d (%s)\n", actual, kmcp_error_message(actual));
        return false;
    }
}
