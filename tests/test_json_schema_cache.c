#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "unity/unity.h"
#include "mcp_json.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>

// Test schema and JSON strings
static const char* test_schema_str = "{"
    "\"type\": \"object\","
    "\"properties\": {"
        "\"name\": {\"type\": \"string\"},"
        "\"age\": {\"type\": \"number\"},"
        "\"email\": {\"type\": \"string\", \"format\": \"email\"}"
    "},"
    "\"required\": [\"name\", \"age\"]"
"}";

static const char* valid_json_str = "{"
    "\"name\": \"John Doe\","
    "\"age\": 30,"
    "\"email\": \"john.doe@example.com\""
"}";

static const char* invalid_json_str = "{"
    "\"name\": \"John Doe\""
"}";

// Global cache for tests
static mcp_json_schema_cache_t* test_cache = NULL;

// Setup and teardown for schema cache tests
void schema_cache_setup(void) {
    // Create a test cache before each test
    test_cache = mcp_json_schema_cache_create(10);
    TEST_ASSERT_NOT_NULL(test_cache);
}

void schema_cache_teardown(void) {
    // Destroy the test cache after each test
    if (test_cache) {
        mcp_json_schema_cache_destroy(test_cache);
        test_cache = NULL;
    }
}

// Test creating and destroying a schema cache
void test_schema_cache_create_destroy(void) {
    mcp_json_schema_cache_t* cache = mcp_json_schema_cache_create(5);
    TEST_ASSERT_NOT_NULL(cache);

    mcp_json_schema_cache_destroy(cache);
    // No assertion needed, just make sure it doesn't crash
}

// Test adding a schema to the cache
void test_schema_cache_add(void) {
    int result = mcp_json_schema_validate_cached(test_cache, valid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Check cache statistics
    size_t size, capacity, hits, misses;
    result = mcp_json_schema_cache_get_stats(test_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(1, size);
    TEST_ASSERT_EQUAL_size_t(10, capacity);
    TEST_ASSERT_EQUAL_size_t(0, hits);
    TEST_ASSERT_EQUAL_size_t(1, misses);
}

// Test cache hit
void test_schema_cache_hit(void) {
    // First validation (cache miss)
    int result = mcp_json_schema_validate_cached(test_cache, valid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Second validation with same schema (cache hit)
    result = mcp_json_schema_validate_cached(test_cache, valid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Check cache statistics
    size_t size, capacity, hits, misses;
    result = mcp_json_schema_cache_get_stats(test_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(1, size);
    TEST_ASSERT_EQUAL_size_t(10, capacity);
    TEST_ASSERT_EQUAL_size_t(1, hits);
    TEST_ASSERT_EQUAL_size_t(1, misses);
}

// Test cache eviction
void test_schema_cache_eviction(void) {
    // Create a cache with capacity 2
    mcp_json_schema_cache_t* small_cache = mcp_json_schema_cache_create(2);
    TEST_ASSERT_NOT_NULL(small_cache);

    // Add 3 different schemas to trigger eviction
    char schema1[256], schema2[256], schema3[256];
    sprintf(schema1, "{\"id\":\"schema1\",\"type\":\"object\",\"properties\":{\"prop1\":{\"type\":\"string\"}}}");
    sprintf(schema2, "{\"id\":\"schema2\",\"type\":\"object\",\"properties\":{\"prop2\":{\"type\":\"number\"}}}");
    sprintf(schema3, "{\"id\":\"schema3\",\"type\":\"object\",\"properties\":{\"prop3\":{\"type\":\"boolean\"}}}");

    // Add schemas to cache
    int result = mcp_json_schema_validate_cached(small_cache, valid_json_str, schema1);
    TEST_ASSERT_EQUAL_INT(0, result);

    result = mcp_json_schema_validate_cached(small_cache, valid_json_str, schema2);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Check cache size before eviction
    size_t size, capacity, hits, misses;
    result = mcp_json_schema_cache_get_stats(small_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(2, size);

    // Add third schema to trigger eviction
    result = mcp_json_schema_validate_cached(small_cache, valid_json_str, schema3);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Check cache size after eviction
    result = mcp_json_schema_cache_get_stats(small_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(2, size);

    // Try to use the first schema again (should be a miss if evicted)
    result = mcp_json_schema_validate_cached(small_cache, valid_json_str, schema1);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Check cache statistics
    result = mcp_json_schema_cache_get_stats(small_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(2, size);
    TEST_ASSERT_EQUAL_size_t(0, hits);
    TEST_ASSERT_EQUAL_size_t(4, misses);

    mcp_json_schema_cache_destroy(small_cache);
}

// Test clearing the cache
void test_schema_cache_clear(void) {
    // Add a schema to the cache
    int result = mcp_json_schema_validate_cached(test_cache, valid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Check cache size before clearing
    size_t size, capacity, hits, misses;
    result = mcp_json_schema_cache_get_stats(test_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(1, size);

    // Clear the cache
    mcp_json_schema_cache_clear(test_cache);

    // Check cache size after clearing
    result = mcp_json_schema_cache_get_stats(test_cache, &size, &capacity, &hits, &misses);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(0, size);
}

// Test validation with invalid JSON
void test_schema_validation_invalid_json(void) {
    // Validate invalid JSON
    int result = mcp_json_schema_validate_cached(test_cache, invalid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Test the global validation function
void test_global_validation_function(void) {
    // Use the global validation function
    int result = mcp_json_validate_schema(valid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Validate invalid JSON
    result = mcp_json_validate_schema(invalid_json_str, test_schema_str);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Run all tests
void run_json_schema_cache_tests(void) {
    // Test that doesn't need setup/teardown
    RUN_TEST(test_schema_cache_create_destroy);

    // Tests that need setup/teardown
    schema_cache_setup();
    RUN_TEST(test_schema_cache_add);
    schema_cache_teardown();

    schema_cache_setup();
    RUN_TEST(test_schema_cache_hit);
    schema_cache_teardown();

    // This test creates its own cache
    RUN_TEST(test_schema_cache_eviction);

    schema_cache_setup();
    RUN_TEST(test_schema_cache_clear);
    schema_cache_teardown();

    schema_cache_setup();
    RUN_TEST(test_schema_validation_invalid_json);
    schema_cache_teardown();

    schema_cache_setup();
    RUN_TEST(test_global_validation_function);
    schema_cache_teardown();
}
