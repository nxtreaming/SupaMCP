#include "unity.h"
#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_arena.h"
#include "mcp_types.h"
#include <string.h>
#include <stdlib.h>

// Define a test group runner function
void run_mcp_json_tests(void);

// --- Test Cases ---

// Test NULL creation
void test_json_create_null(void) {
    // Test creation (now always uses thread-local arena)
    mcp_json_t* json_node = mcp_json_null_create();
    TEST_ASSERT_NOT_NULL(json_node);
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(json_node));
    // Cleanup thread-local arena
    mcp_arena_destroy_current_thread();
}

// Test Boolean creation
void test_json_create_boolean(void) {
    bool val_true = true;
    bool val_false = false;
    bool out_val;

    // Test true (uses thread-local arena)
    mcp_json_t* json_true = mcp_json_boolean_create(val_true);
    TEST_ASSERT_NOT_NULL(json_true);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json_true));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json_true, &out_val));
    TEST_ASSERT_TRUE(out_val);
    mcp_arena_destroy_current_thread(); // Clean up arena

    // Test false (uses thread-local arena)
    mcp_json_t* json_false = mcp_json_boolean_create(val_false);
    TEST_ASSERT_NOT_NULL(json_false);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json_false));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json_false, &out_val));
    TEST_ASSERT_FALSE(out_val);
    mcp_arena_destroy_current_thread(); // Clean up arena
}

// Test Number creation
void test_json_create_number(void) {
    double val1 = 123.45;
    double val2 = -987;
    double out_val;

    // Test positive double (uses thread-local arena)
    mcp_json_t* json1 = mcp_json_number_create(val1);
    TEST_ASSERT_NOT_NULL(json1);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json1));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json1, &out_val));
    TEST_ASSERT_EQUAL_DOUBLE(val1, out_val);
    mcp_arena_destroy_current_thread(); // Clean up arena

    // Test negative integer (uses thread-local arena)
    mcp_json_t* json2 = mcp_json_number_create(val2);
    TEST_ASSERT_NOT_NULL(json2);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json2));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json2, &out_val));
    TEST_ASSERT_EQUAL_DOUBLE(val2, out_val);
    mcp_arena_destroy_current_thread(); // Clean up arena
}

// Test String creation
void test_json_create_string(void) {
    const char* val1 = "hello world";
    const char* val2 = "";
    const char* out_val;

    // Test normal string (uses thread-local arena)
    mcp_json_t* json1 = mcp_json_string_create(val1);
    TEST_ASSERT_NOT_NULL(json1);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json1));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json1, &out_val));
    TEST_ASSERT_EQUAL_STRING(val1, out_val);
    mcp_json_destroy(json1); // Free internal strdup
    mcp_arena_destroy_current_thread(); // Clean up arena

    // Test empty string (uses thread-local arena)
    mcp_json_t* json2 = mcp_json_string_create(val2);
    TEST_ASSERT_NOT_NULL(json2);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json2));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json2, &out_val));
    TEST_ASSERT_EQUAL_STRING(val2, out_val);
    mcp_json_destroy(json2); // Free internal strdup
    mcp_arena_destroy_current_thread(); // Clean up arena

     // Test NULL input
    mcp_json_t* json_null = mcp_json_string_create(NULL);
    TEST_ASSERT_NULL(json_null);
    // No arena cleanup needed if creation failed
}

// Test Array creation
void test_json_create_array(void) {
    // Test creation (uses thread-local arena)
    mcp_json_t* json_node = mcp_json_array_create();
    TEST_ASSERT_NOT_NULL(json_node);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json_node));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(json_node));
    mcp_json_destroy(json_node); // Clean up internal (none for empty array)
    mcp_arena_destroy_current_thread(); // Clean up arena
}

// Test Object creation
void test_json_create_object(void) {
    size_t count = 99; // Initialize to non-zero
    // Test creation (uses thread-local arena)
    mcp_json_t* json_node = mcp_json_object_create();
    TEST_ASSERT_NOT_NULL(json_node);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json_node));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(json_node, NULL, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
    mcp_json_destroy(json_node); // Clean up internal hash table
    mcp_arena_destroy_current_thread(); // Clean up arena
}

// Test Array Operations
void test_json_array_operations(void) {
    mcp_json_t* arr = mcp_json_array_create(); // Uses TLS arena
    mcp_json_t* item1 = mcp_json_number_create(1); // Uses TLS arena
    mcp_json_t* item2 = mcp_json_string_create("two"); // Uses TLS arena (node), malloc (string)
    mcp_json_t* item3 = mcp_json_boolean_create(true); // Uses TLS arena

    TEST_ASSERT_NOT_NULL(arr);
    TEST_ASSERT_NOT_NULL(item1);
    TEST_ASSERT_NOT_NULL(item2);
    TEST_ASSERT_NOT_NULL(item3);

    // Test Add Item
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr, item1));
    TEST_ASSERT_EQUAL_INT(1, mcp_json_array_get_size(arr));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr, item2));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(arr));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr, item3));
    TEST_ASSERT_EQUAL_INT(3, mcp_json_array_get_size(arr));

    // Test Get Item
    mcp_json_t* retrieved1 = mcp_json_array_get_item(arr, 0);
    mcp_json_t* retrieved2 = mcp_json_array_get_item(arr, 1);
    mcp_json_t* retrieved3 = mcp_json_array_get_item(arr, 2);
    mcp_json_t* retrieved_invalid = mcp_json_array_get_item(arr, 3); // Out of bounds

    TEST_ASSERT_EQUAL_PTR(item1, retrieved1);
    TEST_ASSERT_EQUAL_PTR(item2, retrieved2);
    TEST_ASSERT_EQUAL_PTR(item3, retrieved3);
    TEST_ASSERT_NULL(retrieved_invalid);

    // Verify content (optional but good)
    double num_val;
    const char* str_val;
    bool bool_val;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(retrieved1, &num_val));
    TEST_ASSERT_EQUAL_DOUBLE(1.0, num_val);
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(retrieved2, &str_val));
    TEST_ASSERT_EQUAL_STRING("two", str_val);
     TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(retrieved3, &bool_val));
    TEST_ASSERT_TRUE(bool_val);

    // Cleanup
    mcp_json_destroy(arr); // Frees internal mallocs (string in item2, array backing store)
    mcp_arena_destroy_current_thread(); // Frees nodes (arr, item1, item2, item3)
}

// Test Object Operations (Hash Table)
void test_json_object_operations(void) {
    mcp_json_t* obj = mcp_json_object_create(); // Uses TLS arena

    // Create some values (all use TLS arena for nodes)
    mcp_json_t* val_num = mcp_json_number_create(100);
    mcp_json_t* val_str = mcp_json_string_create("value"); // Internal string malloc'd
    mcp_json_t* val_bool = mcp_json_boolean_create(false);
    mcp_json_t* val_null = mcp_json_null_create();
    mcp_json_t* val_arr = mcp_json_array_create(); // Empty array

    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_NOT_NULL(val_num);
    TEST_ASSERT_NOT_NULL(val_str);
    TEST_ASSERT_NOT_NULL(val_bool);
    TEST_ASSERT_NOT_NULL(val_null);
    TEST_ASSERT_NOT_NULL(val_arr);

    // Test Set/Has/Get
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj, "key1", val_num));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj, "key1"));
    TEST_ASSERT_FALSE(mcp_json_object_has_property(obj, "key_missing"));
    TEST_ASSERT_EQUAL_PTR(val_num, mcp_json_object_get_property(obj, "key1"));
    TEST_ASSERT_NULL(mcp_json_object_get_property(obj, "key_missing"));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj, "key2", val_str));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj, "key2"));
    TEST_ASSERT_EQUAL_PTR(val_str, mcp_json_object_get_property(obj, "key2"));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj, "boolKey", val_bool));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj, "boolKey"));
    TEST_ASSERT_EQUAL_PTR(val_bool, mcp_json_object_get_property(obj, "boolKey"));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj, "nullKey", val_null));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj, "nullKey"));
    TEST_ASSERT_EQUAL_PTR(val_null, mcp_json_object_get_property(obj, "nullKey"));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj, "arrKey", val_arr));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj, "arrKey"));
    TEST_ASSERT_EQUAL_PTR(val_arr, mcp_json_object_get_property(obj, "arrKey"));

    // Test Update
    mcp_json_t* val_num_updated = mcp_json_number_create(200); // Uses TLS arena
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj, "key1", val_num_updated)); // val_num should be destroyed internally
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj, "key1"));
    TEST_ASSERT_EQUAL_PTR(val_num_updated, mcp_json_object_get_property(obj, "key1"));

    // Test Get Names
    char** names = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(obj, &names, &count));
    TEST_ASSERT_EQUAL_UINT(5, count); // key1, key2, boolKey, nullKey, arrKey
    TEST_ASSERT_NOT_NULL(names);
    // Note: Order is not guaranteed with hash table
    int found_mask = 0;
    if (names) {
        for(size_t i=0; i<count; ++i) {
            if (names[i] != NULL) {
                if (strcmp(names[i], "key1") == 0) found_mask |= 1;
                if (strcmp(names[i], "key2") == 0) found_mask |= 2;
                if (strcmp(names[i], "boolKey") == 0) found_mask |= 4;
                if (strcmp(names[i], "nullKey") == 0) found_mask |= 8;
                if (strcmp(names[i], "arrKey") == 0) found_mask |= 16;
                free(names[i]); // Free names allocated by get_property_names
            }
        }
        free(names);
    }
    TEST_ASSERT_EQUAL_INT(0x1F, found_mask); // Check all keys were found

    // Test Delete
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_delete_property(obj, "key1")); // val_num_updated destroyed internally
    TEST_ASSERT_FALSE(mcp_json_object_has_property(obj, "key1"));
    TEST_ASSERT_EQUAL_INT(-1, mcp_json_object_delete_property(obj, "key_missing")); // Delete non-existent

    // Verify remaining properties
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(obj, &names, &count));
    TEST_ASSERT_EQUAL_UINT(4, count);
    // Free names array
    if (names) {
        for(size_t i=0; i<count; ++i) free(names[i]);
        free(names);
    }

    // Cleanup
    mcp_json_destroy(obj); // Frees internal mallocs (string in val_str, hash table stuff)
    mcp_arena_destroy_current_thread(); // Frees all nodes (obj, val_*, etc.)
}

// Test JSON Parsing
void test_json_parse_basic_types(void) {
    mcp_json_t* json;
    double num_val;
    bool bool_val;
    const char* str_val;

    // Test Null
    json = mcp_json_parse("  null  "); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(json));
    mcp_arena_destroy_current_thread();

    // Test True
    json = mcp_json_parse("true"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json, &bool_val));
    TEST_ASSERT_TRUE(bool_val);
    mcp_arena_destroy_current_thread();

    // Test False
    json = mcp_json_parse("false"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json, &bool_val));
    TEST_ASSERT_FALSE(bool_val);
    mcp_arena_destroy_current_thread();

    // Test Integer
    json = mcp_json_parse("123"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json, &num_val));
    TEST_ASSERT_EQUAL_DOUBLE(123.0, num_val);
    mcp_arena_destroy_current_thread();

    // Test Float
    json = mcp_json_parse("-45.67"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json, &num_val));
    TEST_ASSERT_EQUAL_DOUBLE(-45.67, num_val);
    mcp_arena_destroy_current_thread();

     // Test String
    json = mcp_json_parse("\"hello\\nworld\""); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json, &str_val));
    TEST_ASSERT_EQUAL_STRING("hello\\nworld", str_val); // Note: parser doesn't unescape yet
    mcp_json_destroy(json); // Free internal string
    mcp_arena_destroy_current_thread(); // Free node

    // Test Empty String
    json = mcp_json_parse("\"\""); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json, &str_val));
    TEST_ASSERT_EQUAL_STRING("", str_val);
    mcp_json_destroy(json); // Free internal string
    mcp_arena_destroy_current_thread(); // Free node
}

void test_json_parse_structures(void) {
    mcp_json_t* json;
    const char* json_str;

    // Test Empty Array
    json = mcp_json_parse("[]"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(json));
    mcp_arena_destroy_current_thread();

    // Test Simple Array
    json = mcp_json_parse("[1, \"two\", true]"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(3, mcp_json_array_get_size(json));
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(mcp_json_array_get_item(json, 0)));
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(mcp_json_array_get_item(json, 1)));
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(mcp_json_array_get_item(json, 2)));
    mcp_json_destroy(json); // Free internal string
    mcp_arena_destroy_current_thread(); // Free nodes

    // Test Empty Object
    json = mcp_json_parse("{}"); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json));
    TEST_ASSERT_FALSE(mcp_json_object_has_property(json, "any"));
    mcp_arena_destroy_current_thread();

    // Test Simple Object
    json_str = "{\"a\": 1, \"b\": \"bee\", \"c\": null}";
    json = mcp_json_parse(json_str); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(json, "a"));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(json, "b"));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(json, "c"));
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(mcp_json_object_get_property(json, "a")));
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(mcp_json_object_get_property(json, "b")));
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(mcp_json_object_get_property(json, "c")));
    mcp_json_destroy(json); // Free internal strings, hash table entries/names
    mcp_arena_destroy_current_thread(); // Free nodes

    // Test Nested Structure
    json_str = "[{\"id\": 1, \"ok\": true}, {\"id\": 2, \"ok\": false}]";
    json = mcp_json_parse(json_str); // Use TLS arena
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(json));
    mcp_json_t* obj1 = mcp_json_array_get_item(json, 0);
    mcp_json_t* obj2 = mcp_json_array_get_item(json, 1);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(obj1));
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(obj2));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj1, "id"));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj2, "ok"));
    mcp_json_destroy(json); // Free internal hash table stuff
    mcp_arena_destroy_current_thread(); // Free nodes
}

void test_json_parse_invalid(void) {
    TEST_ASSERT_NULL(mcp_json_parse("")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("[1, 2")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("{\"a\": 1")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("{\"a\": }")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("{a: 1}")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("[1, ]")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("{\"a\":1,}")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("123a")); mcp_arena_destroy_current_thread();
    TEST_ASSERT_NULL(mcp_json_parse("\"hello")); mcp_arena_destroy_current_thread();
}

// Test JSON Stringification
void test_json_stringify(void) {
    mcp_json_t* json;
    char* str = NULL;

    // Test simple types (use thread-local arena)
    json = mcp_json_null_create();
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("null", str);
    free(str); mcp_arena_destroy_current_thread();

    json = mcp_json_boolean_create(true);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("true", str);
    free(str); mcp_arena_destroy_current_thread();

    json = mcp_json_number_create(-12.34);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("-12.34", str); // Precision might vary slightly
    free(str); mcp_arena_destroy_current_thread();

    json = mcp_json_string_create("ab\"c\\d\n");
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("\"ab\\\"c\\\\d\\n\"", str); // Check escaping
    free(str); mcp_json_destroy(json); mcp_arena_destroy_current_thread();

    // Test empty array/object
    json = mcp_json_array_create();
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("[]", str);
    free(str); mcp_json_destroy(json); mcp_arena_destroy_current_thread();

    json = mcp_json_object_create();
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("{}", str);
    free(str); mcp_json_destroy(json); mcp_arena_destroy_current_thread();

    // Test simple array
    json = mcp_json_array_create();
    mcp_json_array_add_item(json, mcp_json_number_create(1));
    mcp_json_array_add_item(json, mcp_json_string_create("two"));
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("[1,\"two\"]", str);
    free(str); mcp_json_destroy(json); mcp_arena_destroy_current_thread();

    // Test simple object
    json = mcp_json_object_create();
    mcp_json_object_set_property(json, "a", mcp_json_number_create(1));
    mcp_json_object_set_property(json, "b", mcp_json_string_create("bee"));
    str = mcp_json_stringify(json);
    // Order not guaranteed by hash table, check both possibilities
    TEST_ASSERT_TRUE(strcmp(str, "{\"a\":1,\"b\":\"bee\"}") == 0 || strcmp(str, "{\"b\":\"bee\",\"a\":1}") == 0);
    free(str); mcp_json_destroy(json); free(json);
}

// Test mcp_json_parse_response (Success)
void test_mcp_json_parse_response_success(void) {
    const char* json_str = "{\"id\": 123, \"result\": {\"value\": \"ok\"}}";
    uint64_t id = 0;
    mcp_error_code_t err_code = MCP_ERROR_INTERNAL_ERROR; // Init to error
    char* err_msg = (char*)"dummy"; // Init to non-NULL
    char* result = NULL;

    int ret = mcp_json_parse_response(json_str, &id, &err_code, &err_msg, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT64(123, id);
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, err_code);
    TEST_ASSERT_NULL(err_msg); // Should be NULL on success
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("{\"value\":\"ok\"}", result);

    free(result); // Free the allocated result string
}

// Test mcp_json_parse_response (Error)
void test_mcp_json_parse_response_error(void) {
    const char* json_str = "{\"id\": 456, \"error\": {\"code\": -32601, \"message\": \"Method not found\"}}";
    uint64_t id = 0;
    mcp_error_code_t err_code = MCP_ERROR_NONE; // Init to success
    char* err_msg = NULL;
    char* result = (char*)"dummy"; // Init to non-NULL

    int ret = mcp_json_parse_response(json_str, &id, &err_code, &err_msg, &result);

    TEST_ASSERT_EQUAL_INT(0, ret); // Parse itself succeeds
    TEST_ASSERT_EQUAL_UINT64(456, id);
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_METHOD_NOT_FOUND, err_code);
    TEST_ASSERT_NOT_NULL(err_msg);
    TEST_ASSERT_EQUAL_STRING("Method not found", err_msg);
    TEST_ASSERT_NULL(result); // Should be NULL on error

    free(err_msg); // Free the allocated error message string
}

// Test mcp_json_parse_response (Invalid JSON)
void test_mcp_json_parse_response_invalid_json(void) {
    const char* json_str = "{\"id\": 789, error: {}}"; // Invalid JSON (unquoted key)
    uint64_t id = 0;
    mcp_error_code_t err_code = MCP_ERROR_NONE;
    char* err_msg = NULL;
    char* result = NULL;

    int ret = mcp_json_parse_response(json_str, &id, &err_code, &err_msg, &result);

    TEST_ASSERT_EQUAL_INT(-1, ret); // Parse should fail
}

// Test mcp_json_parse_response (Missing fields)
void test_mcp_json_parse_response_missing_fields(void) {
    const char* json_str1 = "{\"id\": 1}"; // Missing result/error
    const char* json_str2 = "{\"result\": 1}"; // Missing id
    uint64_t id = 0;
    mcp_error_code_t err_code = MCP_ERROR_NONE;
    char* err_msg = NULL;
    char* result = NULL;

    int ret1 = mcp_json_parse_response(json_str1, &id, &err_code, &err_msg, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret1); // Should fail: missing result/error

    int ret2 = mcp_json_parse_response(json_str2, &id, &err_code, &err_msg, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret2); // Should fail: missing id
}

// Test mcp_json_parse_resources
void test_mcp_json_parse_resources_valid(void) {
    const char* json_str = "{\"resources\": [{\"uri\": \"res:/a\", \"name\": \"Resource A\"}, {\"uri\": \"res:/b\"}]}";
    mcp_resource_t** resources = NULL;
    size_t count = 0;

    int ret = mcp_json_parse_resources(json_str, &resources, &count);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_NOT_NULL(resources);
    if (resources) { // Add check to satisfy static analysis
        TEST_ASSERT_NOT_NULL(resources[0]);
        TEST_ASSERT_NOT_NULL(resources[1]);
        if (resources[0]) {
            TEST_ASSERT_EQUAL_STRING("res:/a", resources[0]->uri);
            TEST_ASSERT_EQUAL_STRING("Resource A", resources[0]->name);
        }
        if (resources[1]) {
            TEST_ASSERT_EQUAL_STRING("res:/b", resources[1]->uri);
            TEST_ASSERT_NULL(resources[1]->name); // Name is optional
        }
    }

    mcp_free_resources(resources, count); // Use generic free function
}

void test_mcp_json_parse_resources_empty(void) {
    const char* json_str = "{\"resources\": []}";
    mcp_resource_t** resources = NULL;
    size_t count = 1; // Init to non-zero

    int ret = mcp_json_parse_resources(json_str, &resources, &count);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(0, count);
    TEST_ASSERT_NULL(resources);
}

void test_mcp_json_parse_resources_invalid(void) {
    const char* json_str1 = "{\"resources\": [{\"uri\": 1}]}"; // Invalid uri type
    const char* json_str2 = "{\"resources\": 1}"; // Invalid resources type
    mcp_resource_t** resources = NULL;
    size_t count = 0;

    int ret1 = mcp_json_parse_resources(json_str1, &resources, &count);
    TEST_ASSERT_EQUAL_INT(-1, ret1);

    int ret2 = mcp_json_parse_resources(json_str2, &resources, &count);
    TEST_ASSERT_EQUAL_INT(-1, ret2);
}

// Test mcp_json_parse_resource_templates (similar structure to resources)
void test_mcp_json_parse_resource_templates_valid(void) {
    const char* json_str = "{\"resourceTemplates\": [{\"uriTemplate\": \"res://{city}\", \"name\": \"City Resource\"}]}";
    mcp_resource_template_t** templates = NULL;
    size_t count = 0;

    int ret = mcp_json_parse_resource_templates(json_str, &templates, &count);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_NOT_NULL(templates);
    if (templates) { // Add check
        TEST_ASSERT_NOT_NULL(templates[0]);
        if (templates[0]) {
            TEST_ASSERT_EQUAL_STRING("res://{city}", templates[0]->uri_template);
            TEST_ASSERT_EQUAL_STRING("City Resource", templates[0]->name);
        }
    }

    mcp_free_resource_templates(templates, count); // Use generic free function
}

// Test mcp_json_parse_content
void test_mcp_json_parse_content_valid(void) {
    const char* json_str = "{\"contents\": [{\"type\": \"text\", \"text\": \"Hello\"}, {\"type\": \"json\", \"mimeType\": \"app/json\", \"text\": \"{\\\"a\\\":1}\"}]}";
    mcp_content_item_t** content = NULL;
    size_t count = 0;

    int ret = mcp_json_parse_content(json_str, &content, &count);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_NOT_NULL(content);
    if (content) { // Add check
        TEST_ASSERT_NOT_NULL(content[0]);
        TEST_ASSERT_NOT_NULL(content[1]);
        if (content[0]) {
            TEST_ASSERT_EQUAL(MCP_CONTENT_TYPE_TEXT, content[0]->type);
            TEST_ASSERT_EQUAL_STRING("Hello", (char*)content[0]->data); // Correct access via cast
        }
        if (content[1]) {
            TEST_ASSERT_EQUAL(MCP_CONTENT_TYPE_JSON, content[1]->type);
            TEST_ASSERT_EQUAL_STRING("app/json", content[1]->mime_type);
            TEST_ASSERT_EQUAL_STRING("{\"a\":1}", (char*)content[1]->data); // Correct access via cast
        }
    }

    mcp_free_content(content, count); // Use generic free function
}

// Test mcp_json_parse_tools
void test_mcp_json_parse_tools_valid(void) {
    const char* json_str = "{\"tools\": [{\"name\": \"tool_a\", \"description\": \"Does A\"}, {\"name\": \"tool_b\"}]}";
    mcp_tool_t** tools = NULL;
    size_t count = 0;

    int ret = mcp_json_parse_tools(json_str, &tools, &count);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_NOT_NULL(tools);
    if (tools) { // Add check
        TEST_ASSERT_NOT_NULL(tools[0]);
        TEST_ASSERT_NOT_NULL(tools[1]);
        if (tools[0]) {
            TEST_ASSERT_EQUAL_STRING("tool_a", tools[0]->name);
            TEST_ASSERT_EQUAL_STRING("Does A", tools[0]->description);
        }
        if (tools[1]) {
            TEST_ASSERT_EQUAL_STRING("tool_b", tools[1]->name);
            TEST_ASSERT_NULL(tools[1]->description);
        }
    }

    mcp_free_tools(tools, count); // Use generic free function
}

// Test mcp_json_parse_tool_result
void test_mcp_json_parse_tool_result_success(void) {
    const char* json_str = "{\"isError\": false, \"content\": [{\"type\": \"text\", \"text\": \"Success!\"}]}";
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    bool is_error = true; // Init to true

    int ret = mcp_json_parse_tool_result(json_str, &content, &count, &is_error);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_FALSE(is_error);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_NOT_NULL(content);
    if (content) { // Add check
        TEST_ASSERT_NOT_NULL(content[0]);
        if (content[0]) {
            TEST_ASSERT_EQUAL(MCP_CONTENT_TYPE_TEXT, content[0]->type);
            TEST_ASSERT_EQUAL_STRING("Success!", (char*)content[0]->data); // Correct access via cast
        }
    }

    mcp_free_content(content, count); // Use generic free function
}

void test_mcp_json_parse_tool_result_error(void) {
    const char* json_str = "{\"isError\": true, \"content\": [{\"type\": \"text\", \"text\": \"Failure!\"}]}";
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    bool is_error = false; // Init to false

    int ret = mcp_json_parse_tool_result(json_str, &content, &count, &is_error);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(is_error);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_NOT_NULL(content);
     if (content) { // Add check
        TEST_ASSERT_NOT_NULL(content[0]);
        if (content[0]) {
            TEST_ASSERT_EQUAL_STRING("Failure!", (char*)content[0]->data); // Correct access via cast
        }
    }

    mcp_free_content(content, count); // Use generic free function
}


// --- Test Group Runner ---

// Add forward declarations for new tests
extern void test_mcp_json_parse_response_success(void);
extern void test_mcp_json_parse_response_error(void);
extern void test_mcp_json_parse_response_invalid_json(void);
extern void test_mcp_json_parse_response_missing_fields(void);
extern void test_mcp_json_parse_resources_valid(void);
extern void test_mcp_json_parse_resources_empty(void);
extern void test_mcp_json_parse_resources_invalid(void);
extern void test_mcp_json_parse_resource_templates_valid(void);
extern void test_mcp_json_parse_content_valid(void);
extern void test_mcp_json_parse_tools_valid(void);
extern void test_mcp_json_parse_tool_result_success(void);
extern void test_mcp_json_parse_tool_result_error(void);


void run_mcp_json_tests(void) {
    // Setup/Teardown are called automatically by RUN_TEST
    RUN_TEST(test_json_create_null);
    RUN_TEST(test_json_create_boolean);
    RUN_TEST(test_json_create_number);
    RUN_TEST(test_json_create_string);
    RUN_TEST(test_json_create_array);
    RUN_TEST(test_json_create_object);
    RUN_TEST(test_json_array_operations);
    RUN_TEST(test_json_object_operations);
    RUN_TEST(test_json_parse_basic_types);
    RUN_TEST(test_json_parse_structures);
    RUN_TEST(test_json_parse_invalid);
    RUN_TEST(test_json_stringify);
    // Add calls for new tests
    RUN_TEST(test_mcp_json_parse_response_success);
    RUN_TEST(test_mcp_json_parse_response_error);
    RUN_TEST(test_mcp_json_parse_response_invalid_json);
    RUN_TEST(test_mcp_json_parse_response_missing_fields);
    RUN_TEST(test_mcp_json_parse_resources_valid);
    RUN_TEST(test_mcp_json_parse_resources_empty);
    RUN_TEST(test_mcp_json_parse_resources_invalid);
    RUN_TEST(test_mcp_json_parse_resource_templates_valid);
    RUN_TEST(test_mcp_json_parse_content_valid);
    RUN_TEST(test_mcp_json_parse_tools_valid);
    RUN_TEST(test_mcp_json_parse_tool_result_success);
    RUN_TEST(test_mcp_json_parse_tool_result_error);
}
