/**
 * @file kmcp_client_test.c
 * @brief Test file for KMCP client functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp_client.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"
#include "mcp_string_utils.h"

/**
 * @brief Test client creation and destruction
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_client_create_destroy() {
    printf("Testing client creation and destruction...\n");

    // Create client configuration
    kmcp_client_config_t config;
    config.name = mcp_strdup("test-client");
    config.version = mcp_strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create client
    kmcp_client_t* client = kmcp_client_create(&config);
    if (!client) {
        printf("FAIL: Failed to create client\n");
        free(config.name);
        free(config.version);
        return 1;
    }

    // Free configuration strings (client has made copies)
    free(config.name);
    free(config.version);

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        printf("FAIL: Failed to get server manager\n");
        kmcp_client_destroy(client);
        return 1;
    }

    // Destroy client
    kmcp_client_destroy(client);

    printf("PASS: Client creation and destruction tests passed\n");
    return 0;
}

/**
 * @brief Test client creation from file
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_client_create_from_file() {
    printf("Testing client creation from file...\n");

    // Create a temporary config file
    const char* config_file = "test_config.json";
    FILE* fp = fopen(config_file, "w");
    if (!fp) {
        printf("FAIL: Failed to create test config file\n");
        return 1;
    }

    // Write a simple config
    fprintf(fp, "{\n");
    fprintf(fp, "  \"client\": {\n");
    fprintf(fp, "    \"name\": \"test-client\",\n");
    fprintf(fp, "    \"version\": \"1.0.0\",\n");
    fprintf(fp, "    \"use_manager\": true,\n");
    fprintf(fp, "    \"timeout_ms\": 30000\n");
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"servers\": [\n");
    fprintf(fp, "    {\n");
    fprintf(fp, "      \"name\": \"local-server\",\n");
    fprintf(fp, "      \"url\": \"http://localhost:8080\",\n");
    fprintf(fp, "      \"api_key\": \"test-key\"\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    fclose(fp);

    // Create client from file
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        printf("FAIL: Failed to create client from file\n");
        remove(config_file);
        return 1;
    }

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        printf("FAIL: Failed to get server manager\n");
        kmcp_client_destroy(client);
        remove(config_file);
        return 1;
    }

    // Destroy client
    kmcp_client_destroy(client);

    // Clean up
    remove(config_file);

    printf("PASS: Client creation from file tests passed\n");
    return 0;
}

/**
 * @brief Main function for client tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_client_test_main(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Client Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_client_create_destroy();
    failures += test_client_create_from_file();

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", failures);
    }

    // Clean up
    mcp_arena_destroy_current_thread();
    mcp_log_close();

    return failures;
}

#ifdef STANDALONE_TEST
/**
 * @brief Main entry point for standalone client tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main(void) {
    return kmcp_client_test_main();
}
#endif
