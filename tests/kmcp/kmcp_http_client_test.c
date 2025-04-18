/**
 * @file kmcp_http_client_test.c
 * @brief Test file for KMCP HTTP client functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp_http_client.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

/**
 * @brief Test HTTP client creation
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_http_client_create() {
    printf("Testing HTTP client creation...\n");

    // Test with valid parameters
    const char* base_url = "http://localhost:8080";
    const char* api_key = "test_api_key";

    kmcp_http_client_t* client = kmcp_http_client_create(base_url, api_key);
    if (!client) {
        printf("FAIL: Failed to create HTTP client with valid parameters\n");
        return 1;
    }

    // Clean up
    kmcp_http_client_close(client);

    // Test with NULL base_url
    client = kmcp_http_client_create(NULL, api_key);
    if (client) {
        printf("FAIL: Created HTTP client with NULL base_url\n");
        kmcp_http_client_close(client);
        return 1;
    }

    // Test with invalid base_url
    client = kmcp_http_client_create("invalid_url", api_key);
    if (client) {
        printf("FAIL: Created HTTP client with invalid base_url\n");
        kmcp_http_client_close(client);
        return 1;
    }

    // Test with NULL api_key (should be valid)
    client = kmcp_http_client_create(base_url, NULL);
    if (!client) {
        printf("FAIL: Failed to create HTTP client with NULL api_key\n");
        return 1;
    }

    // Clean up
    kmcp_http_client_close(client);

    printf("PASS: HTTP client creation tests passed\n");
    return 0;
}

/**
 * @brief Test HTTP client send
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_http_client_send() {
    printf("Testing HTTP client send...\n");

    // Create a client
    const char* base_url = "http://localhost:8080";
    kmcp_http_client_t* client = kmcp_http_client_create(base_url, NULL);
    if (!client) {
        printf("FAIL: Failed to create HTTP client\n");
        return 1;
    }

    // Test with invalid parameters
    char* response = NULL;
    int status = 0;
    kmcp_error_t result = kmcp_http_client_send(NULL, "GET", "/", NULL, NULL, &response, &status);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL client, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_send(client, NULL, "/", NULL, NULL, &response, &status);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL method, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_send(client, "GET", NULL, NULL, NULL, &response, &status);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL path, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_send(client, "GET", "/", NULL, NULL, NULL, &status);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL response, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_send(client, "GET", "/", NULL, NULL, &response, NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL status, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    // Note: We can't easily test a successful send without a running server
    // In a real test environment, you would have a mock server or a real server running

    // Clean up
    kmcp_http_client_close(client);

    printf("PASS: HTTP client send tests passed\n");
    return 0;
}

/**
 * @brief Test HTTP client tool call
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_http_client_call_tool() {
    printf("Testing HTTP client tool call...\n");

    // Create a client
    const char* base_url = "http://localhost:8080";
    kmcp_http_client_t* client = kmcp_http_client_create(base_url, NULL);
    if (!client) {
        printf("FAIL: Failed to create HTTP client\n");
        return 1;
    }

    // Test with invalid parameters
    char* result_json = NULL;
    kmcp_error_t result = kmcp_http_client_call_tool(NULL, "test_tool", "{}", &result_json);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL client, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_call_tool(client, NULL, "{}", &result_json);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL tool_name, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_call_tool(client, "test_tool", NULL, &result_json);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL params_json, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_call_tool(client, "test_tool", "{}", NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL result_json, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    // Test with invalid tool name
    result = kmcp_http_client_call_tool(client, "", "{}", &result_json);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for empty tool_name, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_call_tool(client, "invalid/tool", "{}", &result_json);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for invalid tool_name, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    // Test with invalid JSON
    result = kmcp_http_client_call_tool(client, "test_tool", "{", &result_json);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for invalid JSON, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    // Note: We can't easily test a successful tool call without a running server

    // Clean up
    kmcp_http_client_close(client);

    printf("PASS: HTTP client tool call tests passed\n");
    return 0;
}

/**
 * @brief Test HTTP client resource retrieval
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_http_client_get_resource() {
    printf("Testing HTTP client resource retrieval...\n");

    // Create a client
    const char* base_url = "http://localhost:8080";
    kmcp_http_client_t* client = kmcp_http_client_create(base_url, NULL);
    if (!client) {
        printf("FAIL: Failed to create HTTP client\n");
        return 1;
    }

    // Test with invalid parameters
    char* content = NULL;
    char* content_type = NULL;
    kmcp_error_t result = kmcp_http_client_get_resource(NULL, "test_resource", &content, &content_type);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL client, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_get_resource(client, NULL, &content, &content_type);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL resource_uri, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_get_resource(client, "test_resource", NULL, &content_type);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL content, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_get_resource(client, "test_resource", &content, NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL content_type, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    // Test with invalid resource URI
    result = kmcp_http_client_get_resource(client, "", &content, &content_type);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for empty resource_uri, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    result = kmcp_http_client_get_resource(client, "../invalid/path", &content, &content_type);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for path traversal, got %d\n", result);
        kmcp_http_client_close(client);
        return 1;
    }

    // Note: We can't easily test a successful resource retrieval without a running server

    // Clean up
    kmcp_http_client_close(client);

    printf("PASS: HTTP client resource retrieval tests passed\n");
    return 0;
}

/**
 * @brief Main function for HTTP client tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_http_client_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP HTTP Client Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_http_client_create();
    failures += test_http_client_send();
    failures += test_http_client_call_tool();
    failures += test_http_client_get_resource();

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    }
    else {
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
    return kmcp_http_client_test_main();
}
#endif
