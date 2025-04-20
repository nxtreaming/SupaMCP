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
extern int kmcp_error_test_main();
extern int kmcp_process_test_main();
extern int kmcp_http_client_test_main();
extern int kmcp_server_manager_test_main();
extern int kmcp_tool_access_test_main();
extern int kmcp_config_parser_test_main();
extern int kmcp_version_test_main();

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

    int total_failures = 0;

    // Run error tests
    printf("Running error tests...\n");
    int failures = kmcp_error_test_main();
    total_failures += failures;
    printf("Error tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Run process tests
    printf("Running process tests...\n");
    failures = kmcp_process_test_main();
    total_failures += failures;
    printf("Process tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Run HTTP client tests
    printf("Running HTTP client tests...\n");
    failures = kmcp_http_client_test_main();
    total_failures += failures;
    printf("HTTP client tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Run server manager tests
    printf("Running server manager tests...\n");
    failures = kmcp_server_manager_test_main();
    total_failures += failures;
    printf("Server manager tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Run tool access tests
    printf("Running tool access tests...\n");
    failures = kmcp_tool_access_test_main();
    total_failures += failures;
    printf("Tool access tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Run config parser tests
    printf("Running config parser tests...\n");
    failures = kmcp_config_parser_test_main();
    total_failures += failures;
    printf("Config parser tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Run version tests
    printf("Running version tests...\n");
    failures = kmcp_version_test_main();
    total_failures += failures;
    printf("Version tests: %s (%d failures)\n\n", failures == 0 ? "PASSED" : "FAILED", failures);

    // Print summary
    printf("=== Test Summary ===\n");
    if (total_failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", total_failures);
    }

    // Clean up logging
    mcp_log_close();

    return total_failures;
}
