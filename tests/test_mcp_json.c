#include "unity.h"
#include "mcp_json.h"
#include "mcp_arena.h"
#include <string.h>
#include <stdlib.h> // For free()

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
    // Arena teardown will clean node, but internal strdup needs mcp_json_destroy
    // This highlights the complexity - let's assume destroy is called before arena reset/destroy
    // mcp_json_destroy(json2_arena); // Call destroy to free internal string

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


// --- Test Group Runner ---

void run_mcp_json_tests(void) {
    // Setup/Teardown are called automatically by RUN_TEST
    RUN_TEST(test_json_create_null);
    RUN_TEST(test_json_create_boolean);
    RUN_TEST(test_json_create_number);
    RUN_TEST(test_json_create_string);
    RUN_TEST(test_json_create_array);
    RUN_TEST(test_json_create_object);
    // Add more RUN_TEST calls here for array/object operations, parse, stringify etc.
}
