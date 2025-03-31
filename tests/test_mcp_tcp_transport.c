#include "unity.h"
#include "mcp_tcp_transport.h"
#include "mcp_transport.h"
#include <stdio.h>
#include <stdint.h>

// Platform-specific includes for sleep functions
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h> // For usleep
#endif

// Define a test group runner function (called by the main runner)
void run_mcp_tcp_transport_tests(void);

// --- Test Setup/Teardown (Optional) ---
void setUp_tcp(void) {
    // e.g., platform specific setup if needed
}

void tearDown_tcp(void) {
    // e.g., platform specific cleanup if needed
}


// --- Test Cases ---

// Dummy callback for testing start
static int dummy_message_callback(void* user_data, const void* data, size_t size) {
    (void)user_data;
    (void)data;
    (void)size;
    // In a real test, we might set a flag here
    printf("Dummy callback executed.\n");
    return 0;
}

// Test basic create, start, stop, destroy cycle
void test_tcp_transport_lifecycle(void) {
    mcp_transport_t* transport = NULL;
    int result = -1;

    // Use a common test port (might conflict if run in parallel, but simple for now)
    // TODO: Find an ephemeral port instead?
    const char* host = "127.0.0.1";
    uint16_t port = 18888; // Example port

    printf("Testing TCP Transport Lifecycle (Host: %s, Port: %u)...\n", host, port);

    // Test Create
    transport = mcp_transport_tcp_create(host, port);
    TEST_ASSERT_NOT_NULL_MESSAGE(transport, "mcp_transport_tcp_create failed");
    if (!transport) return; // Avoid crashing following tests

    // Test Start
    result = mcp_transport_start(transport, dummy_message_callback, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "mcp_transport_start failed");
    // Add a small delay to allow the accept thread to potentially start
    #ifdef _WIN32
        Sleep(50);
    #else
        usleep(50000); // 50ms
    #endif

    // Test Stop
    result = mcp_transport_stop(transport);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "mcp_transport_stop failed");

    // Test Destroy
    mcp_transport_destroy(transport);
    // We can't easily assert internal state after destroy, just check it runs without crashing.

    printf("TCP Transport Lifecycle Test Passed.\n");
}

// TODO: Add more tests:
// - Test sending data (requires a client connection)
// - Test receiving data (requires a client connection and callback verification)
// - Test handling multiple clients
// - Test error conditions (e.g., port already in use)


// --- Test Group Runner ---

void run_mcp_tcp_transport_tests(void) {
    // Use specific setUp/tearDown for this group if needed
    // UnityDefaultTestRun(test_tcp_transport_lifecycle, "test_tcp_transport_lifecycle", __LINE__);
    RUN_TEST(test_tcp_transport_lifecycle);
    // Add RUN_TEST calls for other TCP tests here
}
