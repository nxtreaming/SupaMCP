#include "unity.h"
#include "internal/server_internal.h"
#include "mcp_server.h"
#include "mcp_types.h"
#include "mcp_json.h"
#include "mcp_auth.h"
#include <stdlib.h>
#include <string.h>

// --- Test Globals / Mocks ---
static mcp_server_t* mock_server = NULL;
static mcp_arena_t test_arena; // Arena for request parameters if needed by handlers

// --- Helper Functions ---
// Helper to create a basic mock server for testing
static mcp_server_t* create_mock_server(const char* api_key) {
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Mock server for testing",
        .api_key = api_key // Use provided key
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    // Note: We don't initialize transport, thread pool, cache etc. for handler tests
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    // Add some default resources/tools if needed for specific tests later
    return server;
}

// Helper to create a mock auth context
static mcp_auth_context_t* create_mock_auth_context(bool allow_all) {
     mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
     if (!context) return NULL;
     context->type = MCP_AUTH_API_KEY; // Assume API key for simplicity
     context->identifier = mcp_strdup("test_user");

     if (allow_all) {
         context->allowed_resources_count = 1;
         context->allowed_resources = (char**)malloc(sizeof(char*));
         context->allowed_resources[0] = mcp_strdup("*");

         context->allowed_tools_count = 1;
         context->allowed_tools = (char**)malloc(sizeof(char*));
         context->allowed_tools[0] = mcp_strdup("*");
     } else {
         // Example: Restricted permissions
         context->allowed_resources_count = 1;
         context->allowed_resources = (char**)malloc(sizeof(char*));
         context->allowed_resources[0] = mcp_strdup("example://hello");

         context->allowed_tools_count = 1;
         context->allowed_tools = (char**)malloc(sizeof(char*));
         context->allowed_tools[0] = mcp_strdup("echo");
     }
     // Basic check
     if (!context->identifier || !context->allowed_resources || !context->allowed_resources[0] ||
         !context->allowed_tools || !context->allowed_tools[0]) {
          mcp_auth_context_free(context);
          return NULL;
     }
     return context;
 }

 // --- Test Cases ---
 // Note: setUp and tearDown are defined globally in test_runner.c

// Test case for handle_ping_request
void test_handle_ping_request_success(void) {
    mcp_request_t request = { .id = 1, .method = "ping", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true); // Full permissions
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_ping_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_OBJECT, mcp_json_get_type(result_node));
    mcp_json_t* msg_node = mcp_json_object_get_property(result_node, "message");
    TEST_ASSERT_NOT_NULL(msg_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_STRING, mcp_json_get_type(msg_node));
    const char* msg_val = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(msg_node, &msg_val));
    TEST_ASSERT_EQUAL_STRING("pong", msg_val);

    mcp_json_destroy(resp_json); // Frees TLS arena nodes
    free(response_str); // Free the malloc'd string
    mcp_auth_context_free(auth_context);
}

// Test case for handle_list_resources_request with no resources
void test_handle_list_resources_empty(void) {
    mcp_request_t request = { .id = 2, .method = "list_resources", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_resources_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* resources_node = mcp_json_object_get_property(result_node, "resources");
    TEST_ASSERT_NOT_NULL(resources_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(resources_node));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(resources_node)); // Expect empty array

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_list_resources_request with resources
void test_handle_list_resources_with_data(void) {
    // Add some resources to the mock server
    mcp_resource_t* r1 = mcp_resource_create("res://one", "Resource One", "text/plain", "Desc 1");
    mcp_resource_t* r2 = mcp_resource_create("res://two", "Resource Two", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r1));
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r2));
    mcp_resource_free(r1); // Server makes copies
    mcp_resource_free(r2);

    mcp_request_t request = { .id = 3, .method = "list_resources", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_resources_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* resources_node = mcp_json_object_get_property(result_node, "resources");
    TEST_ASSERT_NOT_NULL(resources_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(resources_node));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(resources_node)); // Expect 2 resources

    // Check first resource
    mcp_json_t* res1_obj = mcp_json_array_get_item(resources_node, 0);
    TEST_ASSERT_NOT_NULL(res1_obj);
    const char* uri1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "uri"), &uri1);
    TEST_ASSERT_EQUAL_STRING("res://one", uri1);
    const char* name1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "name"), &name1);
    TEST_ASSERT_EQUAL_STRING("Resource One", name1);
    const char* mime1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "mimeType"), &mime1);
    TEST_ASSERT_EQUAL_STRING("text/plain", mime1);
    const char* desc1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "description"), &desc1);
    TEST_ASSERT_EQUAL_STRING("Desc 1", desc1);

    // Check second resource (some fields NULL)
    mcp_json_t* res2_obj = mcp_json_array_get_item(resources_node, 1);
    TEST_ASSERT_NOT_NULL(res2_obj);
    const char* uri2 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res2_obj, "uri"), &uri2);
    TEST_ASSERT_EQUAL_STRING("res://two", uri2);
    const char* name2 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res2_obj, "name"), &name2);
    TEST_ASSERT_EQUAL_STRING("Resource Two", name2);
    TEST_ASSERT_NULL(mcp_json_object_get_property(res2_obj, "mimeType"));
    TEST_ASSERT_NULL(mcp_json_object_get_property(res2_obj, "description"));


    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}


// TODO: Add more test cases for other handlers (read_resource, list_tools, call_tool)
// TODO: Test different scenarios (success, errors, permissions)

// --- Test Runner Setup (to be added to test_runner.c) ---
/*
void run_test_mcp_server_handlers(void) {
    RUN_TEST(test_handle_ping_request_success);
    RUN_TEST(test_handle_list_resources_empty);
    RUN_TEST(test_handle_list_resources_with_data);
    // Add other tests here
}
*/
