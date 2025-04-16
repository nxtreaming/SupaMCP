#include "unity.h"
#include "mcp_thread_local.h"
#include "mcp_object_pool.h"
#include "mcp_types.h"

#define TEST_ARENA_SIZE (1024 * 1024) // 1MB arena for tests

// Declare the global test pool pointer (defined in test_mcp_cache.c)
extern mcp_object_pool_t* test_pool;

// Forward declarations for test suite runners
void run_mcp_arena_tests(void);
void run_mcp_tcp_transport_tests(void);
void run_mcp_json_tests(void);
void run_mcp_buffer_pool_tests(void);
void run_mcp_client_async_tests(void);
void run_mcp_hashtable_tests(void);
void run_cache_tests(void);
void run_cache_lru_tests(void);
void run_mcp_transport_factory_tests(void);
void run_test_mcp_server_handlers(void);
void run_mcp_log_tests(void);
void run_mcp_rate_limiter_tests(void);
void run_mcp_types_tests(void);
void run_mcp_json_message_tests(void);
void run_mcp_thread_pool_tests(void);
void run_mcp_plugin_tests(void);
void run_mcp_stdio_transport_tests(void);
void run_mcp_tcp_client_transport_tests(void);
void run_memory_pool_tests(void);
void run_mcp_template_tests(void);
void run_mcp_template_security_tests(void);
void run_json_schema_cache_tests(void);
void run_all_advanced_rate_limiter_tests(void);
// Add declarations for other test suite runners here later

// setUp and tearDown functions are optional, run before/after each test
void setUp(void) {
    // Reset the thread-local arena before each test
    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena) {
        mcp_arena_reset(arena);
    }
    // Create the global test pool if it doesn't exist
    if (test_pool == NULL) {
        test_pool = mcp_object_pool_create(sizeof(mcp_content_item_t), 32, 0);
        // We don't use TEST_ASSERT here as it's setUp, maybe log?
        if (test_pool == NULL) {
            printf("CRITICAL: Failed to create global test object pool in setUp!\n");
        }
    }
}

void tearDown(void) {
    // Destroy the global test pool if it exists
    if (test_pool != NULL) {
        mcp_object_pool_destroy(test_pool);
        test_pool = NULL;
    }
    // Arena is reset in setUp
}

// Main test runner
int main(void) {
    // Initialize thread-local arena for tests
    if (mcp_arena_init_current_thread(TEST_ARENA_SIZE) != 0) {
        printf("Failed to initialize thread-local arena\n");
        return -1;
    }

    UNITY_BEGIN(); // IMPORTANT: Call this before any tests

    // Run test suites
    run_mcp_arena_tests();
    run_mcp_tcp_transport_tests();
    run_mcp_json_tests();
    run_mcp_buffer_pool_tests();
    run_mcp_client_async_tests();
    run_mcp_hashtable_tests();
    run_cache_tests();
    run_cache_lru_tests();
    run_mcp_transport_factory_tests();
    run_test_mcp_server_handlers();
    run_mcp_log_tests();
    run_mcp_rate_limiter_tests();
    run_mcp_types_tests();
    run_mcp_json_message_tests();
    run_mcp_thread_pool_tests();
    run_mcp_plugin_tests();
    run_mcp_stdio_transport_tests();
    run_mcp_tcp_client_transport_tests();
    run_memory_pool_tests();
    run_mcp_template_tests();
    run_mcp_template_security_tests();
    run_json_schema_cache_tests();
    run_all_advanced_rate_limiter_tests();
    // Add calls to other test suite runners here later

    int result = UNITY_END(); // IMPORTANT: Call this to finalize tests

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();

    return result;
}

// --- Server Handlers Test Suite ---
// Forward declarations for server handler tests
extern void test_handle_ping_request_success(void);
extern void test_handle_list_resources_empty(void);
extern void test_handle_list_resources_with_data(void);
extern void test_handle_list_resources_restricted(void);
extern void test_handle_read_resource_success(void);
extern void test_handle_read_resource_invalid_uri(void);
extern void test_handle_read_resource_permission_denied(void);
extern void test_handle_list_tools_empty(void);
extern void test_handle_list_tools_with_data(void);
extern void test_handle_call_tool_success(void);
extern void test_handle_call_tool_permission_denied(void);
extern void test_handle_call_tool_invalid_params(void);
extern void test_handle_call_tool_invalid_json(void);
extern void test_handle_call_tool_not_found(void);
extern void test_handle_invalid_method(void);
extern void test_handle_read_resource_invalid_json(void);
extern void test_handle_read_resource_missing_fields(void);
extern void test_server_init(void);
extern void test_server_init_invalid_config(void);
extern void test_server_capabilities(void);
extern void test_server_config_validation(void);

// Runner function for server handler tests
void run_test_mcp_server_handlers(void) {
    RUN_TEST(test_handle_ping_request_success);
    RUN_TEST(test_handle_list_resources_empty);
    RUN_TEST(test_handle_list_resources_with_data);
    RUN_TEST(test_handle_list_resources_restricted);
    RUN_TEST(test_handle_read_resource_success);
    RUN_TEST(test_handle_read_resource_invalid_uri);
    RUN_TEST(test_handle_read_resource_permission_denied);
    RUN_TEST(test_handle_list_tools_empty);
    RUN_TEST(test_handle_list_tools_with_data);
    RUN_TEST(test_handle_call_tool_success);
    RUN_TEST(test_handle_call_tool_permission_denied);
    RUN_TEST(test_handle_call_tool_invalid_params);
    RUN_TEST(test_handle_call_tool_invalid_json);
    RUN_TEST(test_handle_call_tool_not_found);
    RUN_TEST(test_handle_invalid_method);
    RUN_TEST(test_handle_read_resource_invalid_json);
    RUN_TEST(test_handle_read_resource_missing_fields);
    RUN_TEST(test_server_init);
    RUN_TEST(test_server_init_invalid_config);
    RUN_TEST(test_server_capabilities);
    RUN_TEST(test_server_config_validation);
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
