#include "unity.h"

// Forward declarations for test suite runners
void run_mcp_arena_tests(void);
void run_mcp_tcp_transport_tests(void);
void run_mcp_json_tests(void);
void run_mcp_buffer_pool_tests(void);
void run_mcp_client_async_tests(void);
void run_mcp_hashtable_tests(void);
void run_cache_tests(void);
void run_mcp_transport_factory_tests(void);
void run_test_mcp_server_handlers(void); // Added declaration
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
    run_mcp_client_async_tests();
    run_mcp_hashtable_tests();
    run_cache_tests();
    run_mcp_transport_factory_tests();
    run_test_mcp_server_handlers(); // Call the server handler test suite
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

// --- Server Handlers Test Suite ---
// Forward declarations for server handler tests
extern void test_handle_ping_request_success(void);
extern void test_handle_list_resources_empty(void);
extern void test_handle_list_resources_with_data(void);
// Add declarations for other handler tests here

// Runner function for server handler tests
void run_test_mcp_server_handlers(void) {
    RUN_TEST(test_handle_ping_request_success);
    RUN_TEST(test_handle_list_resources_empty);
    RUN_TEST(test_handle_list_resources_with_data);
    // Add RUN_TEST calls for other handler tests here
}
