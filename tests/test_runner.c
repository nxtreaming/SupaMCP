#include "unity.h"

// Forward declarations for test suite runners
void run_mcp_arena_tests(void);
void run_mcp_tcp_transport_tests(void);
void run_mcp_json_tests(void);
// Add declarations for other test suite runners here later

// setUp and tearDown functions are optional, run before/after each test
void setUp(void) {
    // e.g., initialize resources needed for tests
}

void tearDown(void) {
    // e.g., clean up resources allocated in setUp
}

// Main test runner
int main(void) {
    UNITY_BEGIN(); // IMPORTANT: Call this before any tests

    // Run test suites
    run_mcp_arena_tests();
    run_mcp_tcp_transport_tests();
    run_mcp_json_tests();
    // Add calls to other test suite runners here later

    return UNITY_END(); // IMPORTANT: Call this to finalize tests
}
