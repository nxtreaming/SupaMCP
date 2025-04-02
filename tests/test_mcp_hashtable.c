#include "unity.h"
#include "mcp_hashtable.h"
#include <string.h>
#include <stdlib.h>

// Define a test group runner function (called by the main runner)
void run_mcp_hashtable_tests(void);

// --- Test Cases ---

// Test basic creation and destruction
void test_hashtable_create_destroy(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        16,
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        NULL
    );

    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL(0, mcp_hashtable_size(table));

    mcp_hashtable_destroy(table);
}

// Test string key operations
void test_hashtable_string_operations(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        16,
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        free
    );

    // Test put and get
    char* value1 = strdup("Value 1");
    char* value2 = strdup("Value 2");
    char* value3 = strdup("Value 3");

    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key1", value1));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key2", value2));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key3", value3));

    TEST_ASSERT_EQUAL(3, mcp_hashtable_size(table));

    void* result;
    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key1", &result));
    TEST_ASSERT_EQUAL_STRING("Value 1", (char*)result);

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key2", &result));
    TEST_ASSERT_EQUAL_STRING("Value 2", (char*)result);

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key3", &result));
    TEST_ASSERT_EQUAL_STRING("Value 3", (char*)result);

    // Test contains
    TEST_ASSERT_TRUE(mcp_hashtable_contains(table, "key1"));
    TEST_ASSERT_TRUE(mcp_hashtable_contains(table, "key2"));
    TEST_ASSERT_TRUE(mcp_hashtable_contains(table, "key3"));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "nonexistent"));

    // Test update
    char* new_value = strdup("Updated Value");
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key1", new_value));
    TEST_ASSERT_EQUAL(3, mcp_hashtable_size(table)); // Size shouldn't change

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key1", &result));
    TEST_ASSERT_EQUAL_STRING("Updated Value", (char*)result);

    // Test remove
    TEST_ASSERT_EQUAL(0, mcp_hashtable_remove(table, "key2"));
    TEST_ASSERT_EQUAL(2, mcp_hashtable_size(table));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "key2"));

    // Test clear
    mcp_hashtable_clear(table);
    TEST_ASSERT_EQUAL(0, mcp_hashtable_size(table));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "key1"));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "key3"));

    mcp_hashtable_destroy(table);
}

// Test integer key operations
void test_hashtable_int_operations(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        16,
        0.75f,
        mcp_hashtable_int_hash,
        mcp_hashtable_int_compare,
        mcp_hashtable_int_dup,
        mcp_hashtable_int_free,
        NULL
    );

    // Create some integer keys
    int keys[] = {42, 100, 255};

    // Test put and get
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, &keys[0], "Value 42"));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, &keys[1], "Value 100"));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, &keys[2], "Value 255"));

    TEST_ASSERT_EQUAL(3, mcp_hashtable_size(table));

    void* result;
    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, &keys[0], &result));
    TEST_ASSERT_EQUAL_STRING("Value 42", (char*)result);

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, &keys[1], &result));
    TEST_ASSERT_EQUAL_STRING("Value 100", (char*)result);

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, &keys[2], &result));
    TEST_ASSERT_EQUAL_STRING("Value 255", (char*)result);

    mcp_hashtable_destroy(table);
}

// Test resize functionality
void test_hashtable_resize(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        4, // Small initial capacity to force resize
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        NULL
    );

    // Add enough entries to trigger resize
    const char* keys[] = {
        "key1", "key2", "key3", "key4", "key5", "key6", "key7", "key8"
    };
    const char* values[] = {
        "value1", "value2", "value3", "value4", "value5", "value6", "value7", "value8"
    };

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, keys[i], (void*)values[i]));
    }

    TEST_ASSERT_EQUAL(8, mcp_hashtable_size(table));

    // Verify all entries are still accessible
    void* result;
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, keys[i], &result));
        TEST_ASSERT_EQUAL_STRING(values[i], (char*)result);
    }

    mcp_hashtable_destroy(table);
}

// Test foreach functionality
static void count_callback(const void* key, void* value, void* user_data) {
    (void)key;    // Suppress unused parameter warning
    (void)value;  // Suppress unused parameter warning
    int* counter = (int*)user_data;
    (*counter)++;
}

void test_hashtable_foreach(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        16,
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        NULL
    );

    // Add some entries
    mcp_hashtable_put(table, "key1", "value1");
    mcp_hashtable_put(table, "key2", "value2");
    mcp_hashtable_put(table, "key3", "value3");

    // Count entries using foreach
    int counter = 0;
    mcp_hashtable_foreach(table, count_callback, &counter);

    TEST_ASSERT_EQUAL(3, counter);

    mcp_hashtable_destroy(table);
}

// Test edge cases
void test_hashtable_edge_cases(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        16,
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        NULL
    );

    // Test NULL key
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_put(table, NULL, "value"));

    // Test get with nonexistent key
    void* result;
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_get(table, "nonexistent", &result));

    // Test remove with nonexistent key
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_remove(table, "nonexistent"));

    // Test empty table
    TEST_ASSERT_EQUAL(0, mcp_hashtable_size(table));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "any_key"));

    mcp_hashtable_destroy(table);

    // Test NULL table
    TEST_ASSERT_EQUAL(0, mcp_hashtable_size(NULL));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(NULL, "key"));
}

// --- Test Group Runner ---

// This function is called by the main test runner (`test_runner.c`)
void run_mcp_hashtable_tests(void) {
    RUN_TEST(test_hashtable_create_destroy);
    RUN_TEST(test_hashtable_string_operations);
    RUN_TEST(test_hashtable_int_operations);
    RUN_TEST(test_hashtable_resize);
    RUN_TEST(test_hashtable_foreach);
    RUN_TEST(test_hashtable_edge_cases);
}
