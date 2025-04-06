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

// Test hash collisions
// Custom hash function that always returns the same value
static unsigned long collision_hash(const void* key) { // Match mcp_hash_func_t return type
    (void)key; // Suppress unused
    return 1UL; // Force collision, use UL suffix for unsigned long
}

void test_hashtable_collisions(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        4, // Small capacity to increase collision chance
        0.75f,
        collision_hash, // Use collision hash function
        mcp_hashtable_string_compare, // Still need correct compare
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        free
    );

    char* value1 = strdup("Value 1");
    char* value2 = strdup("Value 2");
    char* value3 = strdup("Value 3");

    // Insert keys that will collide
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key1", value1));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key_collides_1", value2));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key_collides_2", value3));

    TEST_ASSERT_EQUAL(3, mcp_hashtable_size(table));

    // Verify retrieval despite collisions
    void* result;
    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key1", &result));
    TEST_ASSERT_EQUAL_STRING("Value 1", (char*)result);

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key_collides_1", &result));
    TEST_ASSERT_EQUAL_STRING("Value 2", (char*)result);

    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key_collides_2", &result));
    TEST_ASSERT_EQUAL_STRING("Value 3", (char*)result);

    // Test removal with collisions
    TEST_ASSERT_EQUAL(0, mcp_hashtable_remove(table, "key_collides_1"));
    TEST_ASSERT_EQUAL(2, mcp_hashtable_size(table));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "key_collides_1"));
    TEST_ASSERT_TRUE(mcp_hashtable_contains(table, "key1")); // Ensure others remain
    TEST_ASSERT_TRUE(mcp_hashtable_contains(table, "key_collides_2"));

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
    // const char* values[] = { ... }; // Remove unused array

    for (int i = 0; i < 8; i++) {
        // Cast int to uintptr_t first to avoid C4312 warning on 64-bit
        // This cast is generally safe for storing integer values as pointers in tests.
        TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, keys[i], (void*)(uintptr_t)i));
    }

    TEST_ASSERT_EQUAL(8, mcp_hashtable_size(table));

    // Verify all entries are still accessible
    void* result;
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, keys[i], &result));
        // Cast result back to uintptr_t then to int for comparison
        TEST_ASSERT_EQUAL_INT(i, (int)(uintptr_t)result);
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

// Test NULL value operations
void test_hashtable_null_value(void) {
    mcp_hashtable_t* table = mcp_hashtable_create(
        16,
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        NULL // No value free function needed for NULL
    );

    // Put NULL value
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key_null", NULL));
    TEST_ASSERT_EQUAL(1, mcp_hashtable_size(table));
    TEST_ASSERT_TRUE(mcp_hashtable_contains(table, "key_null"));

    // Get NULL value
    void* result = (void*)0xDEADBEEF; // Sentinel value
    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key_null", &result));
    TEST_ASSERT_NULL(result);

    // Update with non-NULL value
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key_null", "not_null"));
    TEST_ASSERT_EQUAL(1, mcp_hashtable_size(table));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key_null", &result));
    TEST_ASSERT_EQUAL_STRING("not_null", (char*)result);

    // Update back to NULL value
    TEST_ASSERT_EQUAL(0, mcp_hashtable_put(table, "key_null", NULL));
    TEST_ASSERT_EQUAL(1, mcp_hashtable_size(table));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_get(table, "key_null", &result));
    TEST_ASSERT_NULL(result);

    // Remove key with NULL value
    TEST_ASSERT_EQUAL(0, mcp_hashtable_remove(table, "key_null"));
    TEST_ASSERT_EQUAL(0, mcp_hashtable_size(table));
    TEST_ASSERT_FALSE(mcp_hashtable_contains(table, "key_null"));

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
    RUN_TEST(test_hashtable_collisions); // Add collision test
    RUN_TEST(test_hashtable_int_operations);
    RUN_TEST(test_hashtable_resize);
    RUN_TEST(test_hashtable_foreach);
    RUN_TEST(test_hashtable_null_value); // Add NULL value test
    RUN_TEST(test_hashtable_edge_cases);
}
