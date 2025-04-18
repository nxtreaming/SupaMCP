/**
 * @file kmcp_server_manager_test.c
 * @brief Test file for KMCP server manager functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp_server_manager.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_thread_local.h"

/**
 * @brief Test server manager creation
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_server_manager_create() {
    printf("Testing server manager creation...\n");

    // Create server manager
    kmcp_server_manager_t* manager = kmcp_server_manager_create();
    if (!manager) {
        printf("FAIL: Failed to create server manager\n");
        return 1;
    }

    // Clean up
    kmcp_server_manager_destroy(manager);

    printf("PASS: Server manager creation tests passed\n");
    return 0;
}

/**
 * @brief Test server configuration
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_server_config() {
    printf("Testing server configuration...\n");

    // Create server configuration
    // Create server configuration
    kmcp_server_config_t config = {0};
    config.name = mcp_strdup("test_server");
    config.command = mcp_strdup("localhost");
    config.is_http = false;
    if (!config.name || !config.command) {
        printf("FAIL: Failed to create server configuration\n");
        free(config.name);
        free(config.command);
        return 1;
    }

    // Verify configuration fields
    if (strcmp(config.name, "test_server") != 0) {
        printf("FAIL: Server name does not match\n");
        free(config.name);
        free(config.command);
        return 1;
    }

    // Clean up
    free(config.name);
    free(config.command);



    printf("PASS: Server configuration tests passed\n");
    return 0;
}

/**
 * @brief Test server manager add server
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_server_manager_add_server() {
    printf("Testing server manager add server...\n");

    // Create server manager
    kmcp_server_manager_t* manager = kmcp_server_manager_create();
    if (!manager) {
        printf("FAIL: Failed to create server manager\n");
        return 1;
    }

    // Create server configuration
    kmcp_server_config_t config = {0};
    config.name = mcp_strdup("test_server");
    config.command = mcp_strdup("localhost");
    config.is_http = false;

    if (!config.name || !config.command) {
        printf("FAIL: Failed to create server configuration\n");
        free(config.name);
        free(config.command);
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Add server
    kmcp_error_t result = kmcp_server_manager_add(manager, &config);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to add server to manager, error: %s\n", kmcp_error_message(result));
        free(config.name);
        free(config.command);
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Free config resources as they've been copied
    free(config.name);
    free(config.command);

    // Test with invalid parameters
    result = kmcp_server_manager_add(NULL, &config);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL manager, got %d\n", result);
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    result = kmcp_server_manager_add(manager, NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL config, got %d\n", result);
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Clean up
    kmcp_server_manager_destroy(manager);

    printf("PASS: Server manager add server tests passed\n");
    return 0;
}

/**
 * @brief Test server manager get server
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_server_manager_get_server() {
    printf("Testing server manager get server...\n");

    // Create server manager
    kmcp_server_manager_t* manager = kmcp_server_manager_create();
    if (!manager) {
        printf("FAIL: Failed to create server manager\n");
        return 1;
    }

    // Create server configuration
    kmcp_server_config_t config = {0};
    config.name = mcp_strdup("test_server");
    config.command = mcp_strdup("localhost");
    config.is_http = false;

    if (!config.name || !config.command) {
        printf("FAIL: Failed to create server configuration\n");
        free(config.name);
        free(config.command);
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Add server
    kmcp_error_t result = kmcp_server_manager_add(manager, &config);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to add server to manager, error: %s\n", kmcp_error_message(result));
        free(config.name);
        free(config.command);
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Free config resources as they've been copied
    free(config.name);
    free(config.command);

    // Get server by index
    int server_index = 0; // First server
    kmcp_server_connection_t* connection = kmcp_server_manager_get_connection(manager, server_index);
    if (!connection) {
        printf("FAIL: Failed to get server from manager\n");
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    if (!connection) {
        printf("FAIL: Server connection is NULL\n");
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Test with invalid parameters
    connection = kmcp_server_manager_get_connection(NULL, 0);
    if (connection) {
        printf("FAIL: Expected NULL for NULL manager\n");
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Test with invalid index
    connection = kmcp_server_manager_get_connection(manager, 999);
    if (connection) {
        printf("FAIL: Expected NULL for invalid index\n");
        kmcp_server_manager_destroy(manager);
        return 1;
    }

    // Clean up
    kmcp_server_manager_destroy(manager);

    printf("PASS: Server manager get server tests passed\n");
    return 0;
}

/**
 * @brief Main function for server manager tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_server_manager_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Server Manager Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_server_manager_create();
    failures += test_server_config();
    failures += test_server_manager_add_server();
    failures += test_server_manager_get_server();

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
    return kmcp_server_manager_test_main();
}
#endif
