/**
 * @file kmcp_test_runner.c
 * @brief Test runner for KMCP module tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_log.h"
#include "mcp_thread_local.h"

// Function declarations for test entry points
extern int kmcp_error_test_main(void);
extern int kmcp_profile_manager_test_main(void);
extern int kmcp_client_test_main(void);

/**
 * @brief Run all tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int run_tests(void) {
    int failures = 0;

    // Run error handling tests
    printf("\nRunning error handling tests...\n");
    failures += kmcp_error_test_main();

    // Run profile manager tests
    printf("\nRunning profile manager tests...\n");
    failures += kmcp_profile_manager_test_main();

    // Run client tests
    printf("\nRunning client tests...\n");
    failures += kmcp_client_test_main();

    return failures;
}

/**
 * @brief Main function
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Module Tests ===\n\n");

    // Run all tests
    int result = run_tests();

    // Print summary
    printf("=== Test Summary ===\n");
    if (result == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("Tests FAILED with code %d\n", result);
    }

    // Clean up logging
    mcp_log_close();

    return result;
}
