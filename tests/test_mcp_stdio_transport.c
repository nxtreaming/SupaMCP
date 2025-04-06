#include "unity.h"
#include "mcp_stdio_transport.h"
#include "mcp_transport.h" // For base transport functions
#include <stdlib.h>

// --- Test Cases ---

void test_stdio_transport_create_destroy(void) {
    mcp_transport_t* transport = mcp_transport_stdio_create();
    TEST_ASSERT_NOT_NULL(transport);

    // We cannot check internal function pointers directly.
    // We assume create sets them up correctly if it returns non-NULL.

    // Call destroy using the generic function
    mcp_transport_destroy(transport);
    // Note: We can't easily verify if stdin/stdout were affected without complex redirection.
}

// Test calling functions with NULL transport handle
void test_stdio_transport_null_handle(void) {
    // Test generic functions with NULL handle
    mcp_transport_t* transport = NULL;
    char* recv_data = NULL;
    size_t recv_size = 0;

    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_start(transport, NULL, NULL, NULL));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_stop(transport));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_send(transport, "test", 4));
    TEST_ASSERT_NOT_EQUAL(0, mcp_transport_receive(transport, &recv_data, &recv_size, 100));
    mcp_transport_destroy(transport); // Should handle NULL gracefully
}

// Test send/receive with NULL data pointers (should fail gracefully)
void test_stdio_transport_null_data(void) {
    mcp_transport_t* transport = mcp_transport_stdio_create();
    TEST_ASSERT_NOT_NULL(transport);
    size_t received;

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
void run_mcp_stdio_transport_tests(void) {
    RUN_TEST(test_stdio_transport_create_destroy);
    RUN_TEST(test_stdio_transport_null_handle);
    RUN_TEST(test_stdio_transport_null_data);
    // Add more tests if interaction with actual stdin/stdout can be mocked/simulated
}
