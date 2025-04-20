/**
 * @file kmcp_error_test.c
 * @brief Test file for KMCP error handling functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp_error.h"
#include "mcp_thread_local.h"
#include "mcp_log.h"

/**
 * @brief Test error code to message conversion
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_error_messages() {
    printf("Testing error code to message conversion...\n");

    // Test valid error codes
    const char* success_msg = kmcp_error_message(KMCP_SUCCESS);
    if (!success_msg || strcmp(success_msg, "Success") != 0) {
        printf("FAIL: Unexpected message for KMCP_SUCCESS: %s\n", success_msg ? success_msg : "NULL");
        return 1;
    }

    const char* invalid_param_msg = kmcp_error_message(KMCP_ERROR_INVALID_PARAMETER);
    if (!invalid_param_msg || strcmp(invalid_param_msg, "Invalid parameter") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_INVALID_PARAMETER: %s\n",
               invalid_param_msg ? invalid_param_msg : "NULL");
        return 1;
    }

    const char* memory_alloc_msg = kmcp_error_message(KMCP_ERROR_MEMORY_ALLOCATION);
    if (!memory_alloc_msg || strcmp(memory_alloc_msg, "Memory allocation failed") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_MEMORY_ALLOCATION: %s\n",
               memory_alloc_msg ? memory_alloc_msg : "NULL");
        return 1;
    }

    const char* file_not_found_msg = kmcp_error_message(KMCP_ERROR_FILE_NOT_FOUND);
    if (!file_not_found_msg || strcmp(file_not_found_msg, "File not found") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_FILE_NOT_FOUND: %s\n",
               file_not_found_msg ? file_not_found_msg : "NULL");
        return 1;
    }

    // Test new error codes
    const char* ssl_cert_msg = kmcp_error_message(KMCP_ERROR_SSL_CERTIFICATE);
    if (!ssl_cert_msg || strcmp(ssl_cert_msg, "SSL certificate error") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_SSL_CERTIFICATE: %s\n",
               ssl_cert_msg ? ssl_cert_msg : "NULL");
        return 1;
    }

    const char* ssl_handshake_msg = kmcp_error_message(KMCP_ERROR_SSL_HANDSHAKE);
    if (!ssl_handshake_msg || strcmp(ssl_handshake_msg, "SSL handshake failed") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_SSL_HANDSHAKE: %s\n",
               ssl_handshake_msg ? ssl_handshake_msg : "NULL");
        return 1;
    }

    const char* parse_failed_msg = kmcp_error_message(KMCP_ERROR_PARSE_FAILED);
    if (!parse_failed_msg || strcmp(parse_failed_msg, "Parse failed") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_PARSE_FAILED: %s\n",
               parse_failed_msg ? parse_failed_msg : "NULL");
        return 1;
    }

    const char* connection_failed_msg = kmcp_error_message(KMCP_ERROR_CONNECTION_FAILED);
    if (!connection_failed_msg || strcmp(connection_failed_msg, "Connection failed") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_CONNECTION_FAILED: %s\n",
               connection_failed_msg ? connection_failed_msg : "NULL");
        return 1;
    }

    const char* resource_not_found_msg = kmcp_error_message(KMCP_ERROR_RESOURCE_NOT_FOUND);
    if (!resource_not_found_msg || strcmp(resource_not_found_msg, "Resource not found") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_RESOURCE_NOT_FOUND: %s\n",
               resource_not_found_msg ? resource_not_found_msg : "NULL");
        return 1;
    }

    const char* server_not_found_msg = kmcp_error_message(KMCP_ERROR_SERVER_NOT_FOUND);
    if (!server_not_found_msg || strcmp(server_not_found_msg, "Server not found") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_SERVER_NOT_FOUND: %s\n",
               server_not_found_msg ? server_not_found_msg : "NULL");
        return 1;
    }

    const char* internal_msg = kmcp_error_message(KMCP_ERROR_INTERNAL);
    if (!internal_msg || strcmp(internal_msg, "Internal error") != 0) {
        printf("FAIL: Unexpected message for KMCP_ERROR_INTERNAL: %s\n",
               internal_msg ? internal_msg : "NULL");
        return 1;
    }

    // Test invalid error code
    const char* unknown_msg = kmcp_error_message(999);
    if (!unknown_msg || strcmp(unknown_msg, "Unknown error") != 0) {
        printf("FAIL: Unexpected message for unknown error code: %s\n",
               unknown_msg ? unknown_msg : "NULL");
        return 1;
    }

    printf("PASS: Error code to message conversion tests passed\n");
    return 0;
}

/**
 * @brief Test MCP error code conversion
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_error_conversion() {
    printf("Testing MCP error code conversion...\n");

    // Test MCP error code conversion
    if (kmcp_error_from_mcp(0) != KMCP_SUCCESS) {
        printf("FAIL: Unexpected conversion for MCP_ERROR_NONE\n");
        return 1;
    }

    if (kmcp_error_from_mcp(1) != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Unexpected conversion for MCP_ERROR_INVALID_PARAMETER\n");
        return 1;
    }

    if (kmcp_error_from_mcp(2) != KMCP_ERROR_MEMORY_ALLOCATION) {
        printf("FAIL: Unexpected conversion for MCP_ERROR_MEMORY_ALLOCATION\n");
        return 1;
    }

    if (kmcp_error_from_mcp(999) != KMCP_ERROR_INTERNAL) {
        printf("FAIL: Unexpected conversion for unknown MCP error code\n");
        return 1;
    }

    printf("PASS: MCP error code conversion tests passed\n");
    return 0;
}

/**
 * @brief Test error logging
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_error_logging() {
    printf("Testing error logging...\n");

    // Test error logging
    kmcp_error_t result = kmcp_error_log(KMCP_ERROR_INVALID_PARAMETER, "Test error message");
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: kmcp_error_log did not return the same error code\n");
        return 1;
    }

    // Test with success code (should not log anything)
    result = kmcp_error_log(KMCP_SUCCESS, "This should not be logged");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: kmcp_error_log did not return KMCP_SUCCESS\n");
        return 1;
    }

    printf("PASS: Error logging tests passed\n");
    return 0;
}

/**
 * @brief Main function for error tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_error_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }
    printf("=== KMCP Error Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_error_messages();
    failures += test_error_conversion();
    failures += test_error_logging();

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
/**
 * @brief Main function for standalone test
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    return kmcp_error_test_main();
}
#endif

