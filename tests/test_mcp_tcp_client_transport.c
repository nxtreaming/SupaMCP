#include "unity.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_transport.h" // For base transport functions
#include <stdlib.h>

// --- Test Cases ---

void test_tcp_client_transport_create_destroy(void) {
    // Create requires host and port
    mcp_transport_t* transport = mcp_transport_tcp_client_create("127.0.0.1", 8080);
    TEST_ASSERT_NOT_NULL(transport);

    // We cannot check internal function pointers directly.
    // We assume create sets them up correctly if it returns non-NULL.

    // Call destroy using the generic function
    mcp_transport_destroy(transport);
    // Note: We can't easily verify socket state without mocking/real connection.
}

void test_tcp_client_transport_create_invalid(void) {
    // NULL host
    mcp_transport_t* transport_null_host = mcp_transport_tcp_client_create(NULL, 8080);
    TEST_ASSERT_NULL(transport_null_host);

    // Invalid port (0)
    mcp_transport_t* transport_zero_port = mcp_transport_tcp_client_create("127.0.0.1", 0);
    TEST_ASSERT_NULL(transport_zero_port);

     // Invalid port (negative - although type is uint16_t, check robustness)
    // mcp_transport_t* transport_neg_port = mcp_tcp_client_transport_create("127.0.0.1", -1);
    // TEST_ASSERT_NULL(transport_neg_port); // This depends on how -1 casts to uint16_t

     // Invalid port (too large)
     mcp_transport_t* transport_large_port = mcp_transport_tcp_client_create("127.0.0.1", 70000); // > 65535
     TEST_ASSERT_NULL(transport_large_port);
}


// Test calling functions with NULL transport handle
void test_tcp_client_transport_null_handle(void) {
    // These shouldn't crash when passed NULL
    mcp_transport_t* transport = NULL;
    // char buffer[10]; // Unused
    // size_t received; // Unused

    // Test generic functions with NULL handle
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_start(transport, NULL, NULL, NULL));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_stop(transport));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_send(transport, "test", 4));
    // For receive, need out parameters
    char* recv_data = NULL;
    size_t recv_size = 0;
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, &recv_data, &recv_size, 100));
    mcp_transport_destroy(transport); // Should handle NULL gracefully

    // Other functions like connect, send, receive, disconnect are harder to test
    // with a NULL handle without calling through the (NULL) function pointers.
    // We assume the implementation checks for NULL internally if needed.
}

// Test send/receive with NULL data pointers (should fail gracefully)
void test_tcp_client_transport_null_data(void) {
    mcp_transport_t* transport = mcp_transport_tcp_client_create("127.0.0.1", 8081); // Use different port
    TEST_ASSERT_NOT_NULL(transport);
    // char buffer[10]; // Unused
    size_t received; // Keep this one as it's used below

    // Send NULL data using generic function
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_send(transport, NULL, 10));

    // Receive with NULL output pointers
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, NULL, &received, 100));
    char* recv_data = NULL;
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, &recv_data, NULL, 100));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, NULL, NULL, 100));


    mcp_transport_destroy(transport);
}

// --- Test Group Runner ---
// Renaming create function call to match header
void test_tcp_client_transport_create_destroy_rename(void) {
    mcp_transport_t* transport = mcp_transport_tcp_client_create("127.0.0.1", 8080);
    TEST_ASSERT_NOT_NULL(transport);
    mcp_transport_destroy(transport);
}

void test_tcp_client_transport_create_invalid_rename(void) {
    mcp_transport_t* transport_null_host = mcp_transport_tcp_client_create(NULL, 8080);
    TEST_ASSERT_NULL(transport_null_host);
    mcp_transport_t* transport_zero_port = mcp_transport_tcp_client_create("127.0.0.1", 0);
    TEST_ASSERT_NULL(transport_zero_port);
     mcp_transport_t* transport_large_port = mcp_transport_tcp_client_create("127.0.0.1", 70000);
     TEST_ASSERT_NULL(transport_large_port);
}

void test_tcp_client_transport_null_data_rename(void) {
    mcp_transport_t* transport = mcp_transport_tcp_client_create("127.0.0.1", 8081);
    TEST_ASSERT_NOT_NULL(transport);
    size_t received; // Keep this one
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_send(transport, NULL, 10));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, NULL, &received, 100));
    char* recv_data = NULL;
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, &recv_data, NULL, 100));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, NULL, NULL, 100));
    mcp_transport_destroy(transport);
}


// --- Test Group Runner ---
void run_mcp_tcp_client_transport_tests(void) {
    // Use renamed tests
    RUN_TEST(test_tcp_client_transport_create_destroy_rename);
    RUN_TEST(test_tcp_client_transport_create_invalid_rename);
    RUN_TEST(test_tcp_client_transport_null_handle); // This one was mostly ok
    RUN_TEST(test_tcp_client_transport_null_data_rename);
    // Add more tests for start, stop, send, receive with mocking/echo server
}
