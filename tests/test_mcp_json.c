#include "unity.h"
#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_arena.h"
#include "mcp_types.h"
#include <string.h>
#include <stdlib.h>

// Define a test group runner function
void run_mcp_json_tests(void);

// --- Test Globals / Setup / Teardown ---

static mcp_arena_t test_arena;

// Setup function: Called before each test in this file
void setUp_json(void) {
    mcp_arena_init(&test_arena, 0); // Initialize arena with default size
}

// Teardown function: Called after each test in this file
void tearDown_json(void) {
    mcp_arena_destroy(&test_arena); // Clean up arena
}

// --- Test Cases ---

// Test NULL creation
void test_json_create_null(void) {
    // Test with malloc
    mcp_json_t* json_malloc = mcp_json_null_create(NULL);
    TEST_ASSERT_NOT_NULL(json_malloc);
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(json_malloc));
    mcp_json_destroy(json_malloc); // Clean up internal (none for null)
    free(json_malloc);             // Clean up node

    // Test with arena
    mcp_json_t* json_arena = mcp_json_null_create(&test_arena);
    TEST_ASSERT_NOT_NULL(json_arena);
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(json_arena));
    // No destroy needed for arena version, teardown handles it
}

// Test Boolean creation
void test_json_create_boolean(void) {
    bool val_true = true;
    bool val_false = false;
    bool out_val;

    // Test true with malloc
    mcp_json_t* json_true_malloc = mcp_json_boolean_create(NULL, val_true);
    TEST_ASSERT_NOT_NULL(json_true_malloc);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json_true_malloc));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json_true_malloc, &out_val));
    TEST_ASSERT_TRUE(out_val);
    mcp_json_destroy(json_true_malloc); free(json_true_malloc);

    // Test false with arena
    mcp_json_t* json_false_arena = mcp_json_boolean_create(&test_arena, val_false);
    TEST_ASSERT_NOT_NULL(json_false_arena);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json_false_arena));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json_false_arena, &out_val));
    TEST_ASSERT_FALSE(out_val);
}

// Test Number creation
void test_json_create_number(void) {
    double val1 = 123.45;
    double val2 = -987;
    double out_val;

    // Test positive double with malloc
    mcp_json_t* json1_malloc = mcp_json_number_create(NULL, val1);
    TEST_ASSERT_NOT_NULL(json1_malloc);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json1_malloc));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json1_malloc, &out_val));
    TEST_ASSERT_EQUAL_DOUBLE(val1, out_val);
    mcp_json_destroy(json1_malloc); free(json1_malloc);

    // Test negative integer with arena
    mcp_json_t* json2_arena = mcp_json_number_create(&test_arena, val2);
    TEST_ASSERT_NOT_NULL(json2_arena);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json2_arena));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json2_arena, &out_val));
    TEST_ASSERT_EQUAL_DOUBLE(val2, out_val);
}

// Test String creation
void test_json_create_string(void) {
    const char* val1 = "hello world";
    const char* val2 = "";
    const char* out_val;

    // Test normal string with malloc
    mcp_json_t* json1_malloc = mcp_json_string_create(NULL, val1);
    TEST_ASSERT_NOT_NULL(json1_malloc);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json1_malloc));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json1_malloc, &out_val));
    TEST_ASSERT_EQUAL_STRING(val1, out_val);
    mcp_json_destroy(json1_malloc); free(json1_malloc); // Frees internal strdup + node

    // Test empty string with arena
    mcp_json_t* json2_arena = mcp_json_string_create(&test_arena, val2);
    TEST_ASSERT_NOT_NULL(json2_arena);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json2_arena));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json2_arena, &out_val));
    TEST_ASSERT_EQUAL_STRING(val2, out_val);
    mcp_json_destroy(json2_arena); // Call destroy to free internal string

     // Test NULL input
    mcp_json_t* json_null = mcp_json_string_create(&test_arena, NULL);
    TEST_ASSERT_NULL(json_null);
}

// Test Array creation
void test_json_create_array(void) {
     // Test with malloc
    mcp_json_t* json_malloc = mcp_json_array_create(NULL);
    TEST_ASSERT_NOT_NULL(json_malloc);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json_malloc));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(json_malloc));
    mcp_json_destroy(json_malloc); free(json_malloc);

    // Test with arena
    mcp_json_t* json_arena = mcp_json_array_create(&test_arena);
    TEST_ASSERT_NOT_NULL(json_arena);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json_arena));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(json_arena));
}

// Test Object creation
void test_json_create_object(void) {
    size_t count = 99; // Initialize to non-zero
    // Test with malloc
    mcp_json_t* json_malloc = mcp_json_object_create(NULL);
    TEST_ASSERT_NOT_NULL(json_malloc);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json_malloc));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(json_malloc, NULL, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
    mcp_json_destroy(json_malloc); free(json_malloc);

    // Test with arena
    mcp_json_t* json_arena = mcp_json_object_create(&test_arena);
    TEST_ASSERT_NOT_NULL(json_arena);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json_arena));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(json_arena, NULL, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
}

// Test Array Operations
void test_json_array_operations(void) {
    mcp_json_t* arr_malloc = mcp_json_array_create(NULL);
    mcp_json_t* arr_arena = mcp_json_array_create(&test_arena);
    mcp_json_t* item1 = mcp_json_number_create(NULL, 1);
    mcp_json_t* item2 = mcp_json_string_create(NULL, "two");
    mcp_json_t* item3 = mcp_json_boolean_create(&test_arena, true); // Mix allocators

    TEST_ASSERT_NOT_NULL(arr_malloc);
    TEST_ASSERT_NOT_NULL(arr_arena);
    TEST_ASSERT_NOT_NULL(item1);
    TEST_ASSERT_NOT_NULL(item2);
    TEST_ASSERT_NOT_NULL(item3);

    // Test Add Item
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr_malloc, item1));
    TEST_ASSERT_EQUAL_INT(1, mcp_json_array_get_size(arr_malloc));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr_malloc, item2));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(arr_malloc));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr_arena, item3));
    TEST_ASSERT_EQUAL_INT(1, mcp_json_array_get_size(arr_arena));

    // Test Get Item
    mcp_json_t* retrieved1 = mcp_json_array_get_item(arr_malloc, 0);
    mcp_json_t* retrieved2 = mcp_json_array_get_item(arr_malloc, 1);
    mcp_json_t* retrieved3 = mcp_json_array_get_item(arr_arena, 0);
    mcp_json_t* retrieved_invalid = mcp_json_array_get_item(arr_malloc, 2); // Out of bounds

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


    // Cleanup (important: destroy assumes malloc for items added)
    mcp_json_destroy(arr_malloc); free(arr_malloc);
    // For arr_arena, items were malloc'd, so destroy is needed before arena cleanup
    mcp_arena_reset(&test_arena); // Reset arena first
    arr_arena = mcp_json_array_create(&test_arena);
    item1 = mcp_json_number_create(&test_arena, 1);
    item2 = mcp_json_string_create(&test_arena, "two_arena"); // Use arena, but string inside is still malloc
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr_arena, item1));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_add_item(arr_arena, item2));
    mcp_json_destroy(arr_arena); // Call this to free internal mallocs (like the string in item2)
}

// Test Object Operations (Hash Table)
void test_json_object_operations(void) {
    mcp_json_t* obj_malloc = mcp_json_object_create(NULL);
    mcp_json_t* obj_arena = mcp_json_object_create(&test_arena);

    // Create some values (mix allocators)
    mcp_json_t* val_num = mcp_json_number_create(NULL, 100);
    mcp_json_t* val_str = mcp_json_string_create(NULL, "value");
    mcp_json_t* val_bool = mcp_json_boolean_create(&test_arena, false);
    mcp_json_t* val_null = mcp_json_null_create(&test_arena);
    mcp_json_t* val_arr = mcp_json_array_create(NULL); // Empty array

    TEST_ASSERT_NOT_NULL(obj_malloc);
    TEST_ASSERT_NOT_NULL(obj_arena);
    TEST_ASSERT_NOT_NULL(val_num);
    TEST_ASSERT_NOT_NULL(val_str);
    TEST_ASSERT_NOT_NULL(val_bool);
    TEST_ASSERT_NOT_NULL(val_null);
    TEST_ASSERT_NOT_NULL(val_arr);

    // Test Set/Has/Get (malloc object)
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj_malloc, "key1", val_num));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj_malloc, "key1"));
    TEST_ASSERT_FALSE(mcp_json_object_has_property(obj_malloc, "key_missing"));
    TEST_ASSERT_EQUAL_PTR(val_num, mcp_json_object_get_property(obj_malloc, "key1"));
    TEST_ASSERT_NULL(mcp_json_object_get_property(obj_malloc, "key_missing"));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj_malloc, "key2", val_str));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj_malloc, "key2"));
    TEST_ASSERT_EQUAL_PTR(val_str, mcp_json_object_get_property(obj_malloc, "key2"));

    // Test Update (malloc object)
    mcp_json_t* val_num_updated = mcp_json_number_create(NULL, 200);
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj_malloc, "key1", val_num_updated)); // val_num should be destroyed internally
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj_malloc, "key1"));
    TEST_ASSERT_EQUAL_PTR(val_num_updated, mcp_json_object_get_property(obj_malloc, "key1"));

    // Test Set/Has/Get (arena object)
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj_arena, "boolKey", val_bool));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj_arena, "boolKey"));
    TEST_ASSERT_EQUAL_PTR(val_bool, mcp_json_object_get_property(obj_arena, "boolKey"));

    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_set_property(obj_arena, "nullKey", val_null));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(obj_arena, "nullKey"));
    TEST_ASSERT_EQUAL_PTR(val_null, mcp_json_object_get_property(obj_arena, "nullKey"));

    // Test Get Names (malloc object)
    char** names = NULL;
    size_t count = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(obj_malloc, &names, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_NOT_NULL(names);
    // Note: Order is not guaranteed with hash table
    bool found_key1 = false, found_key2 = false;
    if (names) { // Add check for static analysis
        for(size_t i=0; i<count; ++i) {
            if (names[i] != NULL) { // Check individual name pointer
                if (strcmp(names[i], "key1") == 0) found_key1 = true;
                if (strcmp(names[i], "key2") == 0) found_key2 = true;
                free(names[i]); // Free names allocated by get_property_names
            }
        }
        free(names);
    }
    TEST_ASSERT_TRUE(found_key1);
    TEST_ASSERT_TRUE(found_key2);

    // Test Delete (malloc object)
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_delete_property(obj_malloc, "key1")); // val_num_updated destroyed internally
    TEST_ASSERT_FALSE(mcp_json_object_has_property(obj_malloc, "key1"));
    TEST_ASSERT_EQUAL_INT(-1, mcp_json_object_delete_property(obj_malloc, "key_missing")); // Delete non-existent

    // Verify remaining property
    TEST_ASSERT_EQUAL_INT(0, mcp_json_object_get_property_names(obj_malloc, &names, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_NOT_NULL(names);
    if (names && names[0]) { // Add check for static analysis
        TEST_ASSERT_EQUAL_STRING("key2", names[0]);
        free(names[0]);
    }
    free(names);

    // Cleanup
    mcp_json_destroy(obj_malloc); free(obj_malloc); // Frees internal val_str + node
    mcp_json_destroy(obj_arena);
    mcp_json_destroy(val_arr); free(val_arr); // Clean up unused array
}

// Test JSON Parsing
void test_json_parse_basic_types(void) {
    mcp_json_t* json;
    double num_val;
    bool bool_val;
    const char* str_val;

    // Test Null
    json = mcp_json_parse(&test_arena, "  null  ");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(json));

    // Test True
    json = mcp_json_parse(&test_arena, "true");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json, &bool_val));
    TEST_ASSERT_TRUE(bool_val);

    // Test False
    json = mcp_json_parse(&test_arena, "false");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_boolean(json, &bool_val));
    TEST_ASSERT_FALSE(bool_val);

    // Test Integer
    json = mcp_json_parse(&test_arena, "123");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json, &num_val));
    TEST_ASSERT_EQUAL_DOUBLE(123.0, num_val);

    // Test Float
    json = mcp_json_parse(&test_arena, "-45.67");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(json, &num_val));
    TEST_ASSERT_EQUAL_DOUBLE(-45.67, num_val);

     // Test String
    json = mcp_json_parse(&test_arena, "\"hello\\nworld\"");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json, &str_val));
    TEST_ASSERT_EQUAL_STRING("hello\\nworld", str_val); // Note: parser doesn't unescape yet
    mcp_json_destroy(json); // Free internal string

    // Test Empty String
    json = mcp_json_parse(&test_arena, "\"\"");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(json, &str_val));
    TEST_ASSERT_EQUAL_STRING("", str_val);
    mcp_json_destroy(json); // Free internal string
}

void test_json_parse_structures(void) {
    mcp_json_t* json;
    const char* json_str;

    // Test Empty Array
    json = mcp_json_parse(&test_arena, "[]");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(json));

    // Test Simple Array
    json = mcp_json_parse(&test_arena, "[1, \"two\", true]");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_ARRAY, mcp_json_get_type(json));
    TEST_ASSERT_EQUAL_INT(3, mcp_json_array_get_size(json));
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(mcp_json_array_get_item(json, 0)));
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(mcp_json_array_get_item(json, 1)));
    TEST_ASSERT_EQUAL(MCP_JSON_BOOLEAN, mcp_json_get_type(mcp_json_array_get_item(json, 2)));
    mcp_json_destroy(json); // Free internal string

    // Test Empty Object
    json = mcp_json_parse(&test_arena, "{}");
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json));
    TEST_ASSERT_FALSE(mcp_json_object_has_property(json, "any"));

    // Test Simple Object
    json_str = "{\"a\": 1, \"b\": \"bee\", \"c\": null}";
    json = mcp_json_parse(&test_arena, json_str);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL(MCP_JSON_OBJECT, mcp_json_get_type(json));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(json, "a"));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(json, "b"));
    TEST_ASSERT_TRUE(mcp_json_object_has_property(json, "c"));
    TEST_ASSERT_EQUAL(MCP_JSON_NUMBER, mcp_json_get_type(mcp_json_object_get_property(json, "a")));
    TEST_ASSERT_EQUAL(MCP_JSON_STRING, mcp_json_get_type(mcp_json_object_get_property(json, "b")));
    TEST_ASSERT_EQUAL(MCP_JSON_NULL, mcp_json_get_type(mcp_json_object_get_property(json, "c")));
    mcp_json_destroy(json); // Free internal strings, hash table entries/names

    // Test Nested Structure
    json_str = "[{\"id\": 1, \"ok\": true}, {\"id\": 2, \"ok\": false}]";
    json = mcp_json_parse(&test_arena, json_str);
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
}

void test_json_parse_invalid(void) {
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, ""));
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "[1, 2")); // Unterminated array
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "{\"a\": 1")); // Unterminated object
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "{\"a\": }")); // Missing value
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "{a: 1}")); // Unquoted key
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "[1, ]")); // Trailing comma in array
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "{\"a\":1,}")); // Trailing comma in object
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "123a")); // Trailing chars
    TEST_ASSERT_NULL(mcp_json_parse(&test_arena, "\"hello")); // Unterminated string
}

// Test JSON Stringification
void test_json_stringify(void) {
    mcp_json_t* json;
    char* str = NULL;

    // Test simple types (use malloc for easy cleanup)
    json = mcp_json_null_create(NULL);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("null", str);
    free(str); mcp_json_destroy(json); free(json);

    json = mcp_json_boolean_create(NULL, true);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("true", str);
    free(str); mcp_json_destroy(json); free(json);

    json = mcp_json_number_create(NULL, -12.34);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("-12.34", str); // Precision might vary slightly
    free(str); mcp_json_destroy(json); free(json);

    json = mcp_json_string_create(NULL, "ab\"c\\d\n");
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("\"ab\\\"c\\\\d\\n\"", str); // Check escaping
    free(str); mcp_json_destroy(json); free(json);

    // Test empty array/object
    json = mcp_json_array_create(NULL);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("[]", str);
    free(str); mcp_json_destroy(json); free(json);

    json = mcp_json_object_create(NULL);
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("{}", str);
    free(str); mcp_json_destroy(json); free(json);

    // Test simple array
    json = mcp_json_array_create(NULL);
    mcp_json_array_add_item(json, mcp_json_number_create(NULL, 1));
    mcp_json_array_add_item(json, mcp_json_string_create(NULL, "two"));
    str = mcp_json_stringify(json);
    TEST_ASSERT_EQUAL_STRING("[1,\"two\"]", str);
    free(str); mcp_json_destroy(json); free(json);

    // Test simple object
    json = mcp_json_object_create(NULL);
    mcp_json_object_set_property(json, "a", mcp_json_number_create(NULL, 1));
    mcp_json_object_set_property(json, "b", mcp_json_string_create(NULL, "bee"));
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
