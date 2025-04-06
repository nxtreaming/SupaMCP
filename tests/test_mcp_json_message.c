#include "unity.h"
#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_json_message.h"
#include "mcp_arena.h"
#include "mcp_thread_local.h"
#include <stdlib.h>
#include <string.h>

// Helper to parse and check basic structure
void check_json_structure(const char* json_str, const char* expected_method, uint64_t expected_id) {
    TEST_ASSERT_NOT_NULL(json_str);
    mcp_json_t* root = mcp_json_parse(json_str);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "Failed to parse generated JSON");
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(root));

    // Check jsonrpc version
    mcp_json_t* version_node = mcp_json_object_get_property(root, "jsonrpc");
    TEST_ASSERT_NOT_NULL(version_node);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(version_node));
    const char* version_str;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(version_node, &version_str));
    TEST_ASSERT_EQUAL_STRING("2.0", version_str);

    // Check id
    mcp_json_t* id_node = mcp_json_object_get_property(root, "id");
    TEST_ASSERT_NOT_NULL(id_node);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(id_node));
    double id_val;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(id_node, &id_val));
    TEST_ASSERT_EQUAL_UINT64(expected_id, (uint64_t)id_val);

    // Check method (if expected)
    if (expected_method) {
        mcp_json_t* method_node = mcp_json_object_get_property(root, "method");
        TEST_ASSERT_NOT_NULL(method_node);
        TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(method_node));
        const char* method_str;
        TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(method_node, &method_str));
        TEST_ASSERT_EQUAL_STRING(expected_method, method_str);
    }

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();
}


// --- Test Cases ---

void test_create_request_no_params(void) {
    uint64_t id = 123;
    const char* method = "testMethod";
    char* request_str = mcp_json_create_request(method, NULL, id);

    check_json_structure(request_str, method, id);

    // Check for absence of params
    mcp_json_t* root = mcp_json_parse(request_str);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NULL(mcp_json_object_get_property(root, "params"));
    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(request_str); // Function returns malloc'd string
}

void test_create_request_with_params(void) {
    uint64_t id = 456;
    const char* method = "anotherMethod";
    const char* params_json = "{\"arg1\": 1, \"arg2\": \"hello\"}";
    char* request_str = mcp_json_create_request(method, params_json, id);

    check_json_structure(request_str, method, id);

    // Check params content
    mcp_json_t* root = mcp_json_parse(request_str);
    TEST_ASSERT_NOT_NULL(root);
    mcp_json_t* params_node = mcp_json_object_get_property(root, "params");
    TEST_ASSERT_NOT_NULL(params_node);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(params_node));

    // Verify params structure (basic check)
    mcp_json_t* arg1_node = mcp_json_object_get_property(params_node, "arg1");
    TEST_ASSERT_NOT_NULL(arg1_node);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(arg1_node));

    mcp_json_t* arg2_node = mcp_json_object_get_property(params_node, "arg2");
    TEST_ASSERT_NOT_NULL(arg2_node);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(arg2_node));

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(request_str);
}

void test_create_request_invalid_params(void) {
    uint64_t id = 789;
    const char* method = "methodWithInvalidParams";
    const char* invalid_params_json = "{\"arg1\": 1, }"; // Invalid JSON
    char* request_str = mcp_json_create_request(method, invalid_params_json, id);

    // The function should ideally handle invalid params JSON gracefully.
    // Option 1: It includes the invalid string as is.
    // Option 2: It omits the params field.
    // Option 3: It returns NULL (less likely for this helper).
    // Let's assume Option 2 (omits params).

    TEST_ASSERT_NOT_NULL(request_str);
    check_json_structure(request_str, method, id);

    mcp_json_t* root = mcp_json_parse(request_str);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NULL_MESSAGE(mcp_json_object_get_property(root, "params"),
                             "Params field should be omitted for invalid JSON");
    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(request_str);
}


void test_create_response_success(void) {
    uint64_t id = 111;
    const char* result_json = "[true, \"data\"]";
    char* response_str = mcp_json_create_response(id, result_json);

    check_json_structure(response_str, NULL, id); // No method in response

    // Check result content
    mcp_json_t* root = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(root);
    mcp_json_t* result_node = mcp_json_object_get_property(root, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(result_node));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(result_node));

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(response_str);
}

void test_create_response_null_result(void) {
    uint64_t id = 222;
    char* response_str = mcp_json_create_response(id, NULL); // NULL result

    check_json_structure(response_str, NULL, id);

    // Result should be JSON null
    mcp_json_t* root = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(root);
    mcp_json_t* result_node = mcp_json_object_get_property(root, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(result_node));

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(response_str);
}

void test_create_response_invalid_result(void) {
    uint64_t id = 333;
    const char* invalid_result_json = "[true, "; // Invalid JSON
    char* response_str = mcp_json_create_response(id, invalid_result_json);

    // Assume it defaults to null if result JSON is invalid
    check_json_structure(response_str, NULL, id);

    mcp_json_t* root = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(root);
    mcp_json_t* result_node = mcp_json_object_get_property(root, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    TEST_ASSERT_EQUAL_MESSAGE(MCP_JSON_NULL, mcp_json_get_type(result_node),
                             "Result should default to null for invalid JSON");

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(response_str);
}

void test_create_error_response(void) {
    uint64_t id = 444;
    int error_code = -32601; // Method not found
    const char* error_message = "Method does not exist";
    char* response_str = mcp_json_create_error_response(id, error_code, error_message);

    check_json_structure(response_str, NULL, id);

    // Check error object
    mcp_json_t* root = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_NULL(mcp_json_object_get_property(root, "result")); // No result in error response

    mcp_json_t* error_node = mcp_json_object_get_property(root, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(error_node));

    // Check error code
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(code_node));
    double code_val;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &code_val));
    TEST_ASSERT_EQUAL_INT(error_code, (int)code_val);

    // Check error message
    mcp_json_t* message_node = mcp_json_object_get_property(error_node, "message");
    TEST_ASSERT_NOT_NULL(message_node);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(message_node));
    const char* message_str;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(message_node, &message_str));
    TEST_ASSERT_EQUAL_STRING(error_message, message_str);

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(response_str);
}

void test_create_error_response_null_message(void) {
    uint64_t id = 555;
    int error_code = -32700; // Parse error
    char* response_str = mcp_json_create_error_response(id, error_code, NULL); // NULL message

    check_json_structure(response_str, NULL, id);

    // Check error object
    mcp_json_t* root = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(root);
    mcp_json_t* error_node = mcp_json_object_get_property(root, "error");
    TEST_ASSERT_NOT_NULL(error_node);

    // Check error message (should be empty string or default)
    mcp_json_t* message_node = mcp_json_object_get_property(error_node, "message");
    TEST_ASSERT_NOT_NULL(message_node);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(message_node));
    const char* message_str;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(message_node, &message_str));
    TEST_ASSERT_EQUAL_STRING("", message_str); // Expect empty string for NULL message

    mcp_json_destroy(root);
    mcp_arena_destroy_current_thread();

    free(response_str);
}


// --- Test Group Runner ---
void run_mcp_json_message_tests(void) {
    RUN_TEST(test_create_request_no_params);
    RUN_TEST(test_create_request_with_params);
    RUN_TEST(test_create_request_invalid_params);
    RUN_TEST(test_create_response_success);
    RUN_TEST(test_create_response_null_result);
    RUN_TEST(test_create_response_invalid_result);
    RUN_TEST(test_create_error_response);
    RUN_TEST(test_create_error_response_null_message);
}
