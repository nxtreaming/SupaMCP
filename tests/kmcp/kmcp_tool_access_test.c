/**
 * @file kmcp_tool_access_test.c
 * @brief Test file for KMCP tool access control functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp_tool_access.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

/**
 * @brief Test tool access creation
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_tool_access_create() {
    printf("Testing tool access creation...\n");

    // Create tool access
    kmcp_tool_access_t* access = kmcp_tool_access_create(false); // Default deny
    if (!access) {
        printf("FAIL: Failed to create tool access\n");
        return 1;
    }

    // Clean up
    kmcp_tool_access_destroy(access);

    printf("PASS: Tool access creation tests passed\n");
    return 0;
}

/**
 * @brief Test tool access add
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_tool_access_add() {
    printf("Testing tool access add...\n");

    // Create tool access
    kmcp_tool_access_t* access = kmcp_tool_access_create(false); // Default deny
    if (!access) {
        printf("FAIL: Failed to create tool access\n");
        return 1;
    }

    // Add allowed tool
    kmcp_error_t result = kmcp_tool_access_add(access, "test_tool", true);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to add allowed tool, error: %s\n", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Add disallowed tool
    result = kmcp_tool_access_add(access, "disallowed_tool", false);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to add disallowed tool, error: %s\n", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Test with invalid parameters
    result = kmcp_tool_access_add(NULL, "test_tool", true);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL access, got %d\n", result);
        kmcp_tool_access_destroy(access);
        return 1;
    }

    result = kmcp_tool_access_add(access, NULL, true);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL tool name, got %d\n", result);
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Clean up
    kmcp_tool_access_destroy(access);

    printf("PASS: Tool access add tests passed\n");
    return 0;
}

/**
 * @brief Test tool access check
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_tool_access_check() {
    printf("Testing tool access check...\n");

    // Create tool access
    kmcp_tool_access_t* access = kmcp_tool_access_create(false); // Default deny
    if (!access) {
        printf("FAIL: Failed to create tool access\n");
        return 1;
    }

    // Add allowed tool
    kmcp_error_t result = kmcp_tool_access_add(access, "test_tool", true);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to add allowed tool, error: %s\n", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Add disallowed tool
    result = kmcp_tool_access_add(access, "disallowed_tool", false);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to add disallowed tool, error: %s\n", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Check allowed tool
    bool allowed = kmcp_tool_access_check(access, "test_tool");
    // No error code to check since the function returns a boolean

    if (!allowed) {
        printf("FAIL: Expected tool to be allowed, but it was disallowed\n");
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Check disallowed tool
    allowed = kmcp_tool_access_check(access, "disallowed_tool");
    // No error code to check since the function returns a boolean

    if (allowed) {
        printf("FAIL: Expected tool to be disallowed, but it was allowed\n");
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Check unknown tool (should be disallowed by default)
    allowed = kmcp_tool_access_check(access, "unknown_tool");
    // No error code to check since the function returns a boolean

    if (allowed) {
        printf("FAIL: Expected unknown tool to be disallowed by default, but it was allowed\n");
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Test with invalid parameters
    allowed = kmcp_tool_access_check(NULL, "test_tool");
    if (allowed) {
        printf("FAIL: Expected false for NULL access\n");
        kmcp_tool_access_destroy(access);
        return 1;
    }

    allowed = kmcp_tool_access_check(access, NULL);
    if (allowed) {
        printf("FAIL: Expected false for NULL tool name\n");
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Clean up
    kmcp_tool_access_destroy(access);

    printf("PASS: Tool access check tests passed\n");
    return 0;
}

/**
 * @brief Main function for tool access tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_tool_access_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Tool Access Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_tool_access_create();
    failures += test_tool_access_add();
    failures += test_tool_access_check();

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", failures);
    }

    // Clean up logging
    mcp_log_close();

    return failures;
}

#ifdef STANDALONE_TEST
/**
 * @brief Main function for standalone test
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    return kmcp_tool_access_test_main();
}
#endif
