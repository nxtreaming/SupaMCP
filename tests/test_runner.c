#include "unity.h"

// Forward declarations for test suite runners
void run_mcp_arena_tests(void);
void run_mcp_tcp_transport_tests(void);
void run_mcp_json_tests(void);
void run_mcp_buffer_pool_tests(void);
void run_mcp_client_async_tests(void);     // Add declaration
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
    run_mcp_buffer_pool_tests();
    run_mcp_client_async_tests();     // Add call for async tests
    // Add calls to other test suite runners here later

    return UNITY_END(); // IMPORTANT: Call this to finalize tests
}

// --- Buffer Pool Test Suite ---
// Forward declarations for buffer pool tests
extern void test_mcp_buffer_pool_create_valid(void);
extern void test_mcp_buffer_pool_create_invalid(void);
extern void test_mcp_buffer_pool_acquire_all(void);
extern void test_mcp_buffer_pool_acquire_release_cycle(void);
extern void test_mcp_buffer_pool_release_null(void);
extern void test_mcp_buffer_pool_acquire_null_pool(void);
extern void test_mcp_buffer_pool_release_null_pool(void);
extern void test_mcp_buffer_pool_get_size(void);
extern void test_mcp_buffer_pool_get_size_null(void);

// Runner function for buffer pool tests
void run_mcp_buffer_pool_tests(void) {
    RUN_TEST(test_mcp_buffer_pool_create_valid);
    RUN_TEST(test_mcp_buffer_pool_create_invalid);
    RUN_TEST(test_mcp_buffer_pool_acquire_all);
    RUN_TEST(test_mcp_buffer_pool_acquire_release_cycle);
    RUN_TEST(test_mcp_buffer_pool_release_null);
    RUN_TEST(test_mcp_buffer_pool_acquire_null_pool);
    RUN_TEST(test_mcp_buffer_pool_release_null_pool);
    RUN_TEST(test_mcp_buffer_pool_get_size);
    RUN_TEST(test_mcp_buffer_pool_get_size_null);
}
