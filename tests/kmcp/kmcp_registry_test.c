/**
 * @file kmcp_registry_test.c
 * @brief Test for server registry integration
 */

#include "kmcp_registry.h"
#include "kmcp_server_manager.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock registry URL for testing
#define TEST_REGISTRY_URL "http://localhost:8080/registry"

/**
 * @brief Test creating and closing a registry
 */
static int test_registry_create_close(void) {
    printf("Testing registry create/close...\n");

    // Create registry
    kmcp_registry_t* registry = kmcp_registry_create(TEST_REGISTRY_URL);
    if (!registry) {
        printf("FAILED: Failed to create registry\n");
        return 1;
    }

    // Close registry
    kmcp_registry_close(registry);

    printf("PASSED: Registry create/close test\n");
    return 0;
}

/**
 * @brief Test creating a registry with custom configuration
 */
static int test_registry_create_with_config(void) {
    printf("Testing registry create with config...\n");

    // Create configuration
    kmcp_registry_config_t config;
    memset(&config, 0, sizeof(config));
    config.registry_url = TEST_REGISTRY_URL;
    config.api_key = "test_api_key";
    config.cache_ttl_seconds = 60;
    config.connect_timeout_ms = 1000;
    config.request_timeout_ms = 5000;
    config.max_retries = 2;

    // Create registry
    kmcp_registry_t* registry = kmcp_registry_create_with_config(&config);
    if (!registry) {
        printf("FAILED: Failed to create registry with config\n");
        return 1;
    }

    // Close registry
    kmcp_registry_close(registry);

    printf("PASSED: Registry create with config test\n");
    return 0;
}

/**
 * @brief Test function
 */
#ifdef STANDALONE_TEST
int main(int argc, char** argv) {
#else
int kmcp_registry_test_main(int argc, char** argv) {
#endif
    (void)argc;
    (void)argv;
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Run tests
    int result = 0;
    result += test_registry_create_close();
    result += test_registry_create_with_config();

    // Clean up
    mcp_log_close();

    // Return result
    return result;
}
