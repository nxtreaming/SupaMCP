/**
 * @file kmcp_version_test.c
 * @brief Test for KMCP version information
 */

#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Test version information
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_version_info() {
    printf("Testing version information...\n");

    // Test version string
    const char* version = kmcp_get_version();
    if (!version || strlen(version) == 0) {
        printf("FAIL: Version string is NULL or empty\n");
        return 1;
    }
    printf("PASS: Version string: %s\n", version);

    // Test build info string
    const char* build_info = kmcp_get_build_info();
    if (!build_info || strlen(build_info) == 0) {
        printf("FAIL: Build info string is NULL or empty\n");
        return 1;
    }
    printf("PASS: Build info: %s\n", build_info);

    return 0;
}

/**
 * @brief Main function for version tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_version_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    printf("=== KMCP Version Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_version_info();

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", failures);
    }

    return failures;
}

#ifdef STANDALONE_TEST
int main() {
    return kmcp_version_test_main();
}
#endif
