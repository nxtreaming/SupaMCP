#include <stdio.h>
#include <stdlib.h>
#include "mcp_log.h"

// Forward declaration for the test function
extern int run_tests(void);

/**
 * @brief Main function for integration tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    printf("=== KMCP Integration Tests ===\n\n");

    // Run tests
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
