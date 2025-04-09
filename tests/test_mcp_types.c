#include "unity.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>

// --- Test Cases ---

void test_mcp_strdup_valid(void) {
    const char* original = "Test String";
    char* duplicated = mcp_strdup(original);

    TEST_ASSERT_NOT_NULL(duplicated);
    TEST_ASSERT_EQUAL_STRING(original, duplicated);
    TEST_ASSERT_NOT_EQUAL(original, duplicated); // Should be a different pointer

    free(duplicated); // mcp_strdup uses malloc, so free it
}

void test_mcp_strdup_empty(void) {
    const char* original = "";
    char* duplicated = mcp_strdup(original);

    TEST_ASSERT_NOT_NULL(duplicated);
    TEST_ASSERT_EQUAL_STRING(original, duplicated);
    TEST_ASSERT_EQUAL_UINT(0, strlen(duplicated));

    free(duplicated);
}

void test_mcp_strdup_null(void) {
    char* duplicated = mcp_strdup(NULL);
    TEST_ASSERT_NULL(duplicated); // Should return NULL for NULL input
}

// Helper to create a dummy resource
mcp_resource_t* create_dummy_resource(const char* uri, const char* name) {
    mcp_resource_t* res = (mcp_resource_t*)malloc(sizeof(mcp_resource_t));
    if (!res) return NULL;
    res->uri = mcp_strdup(uri);
    res->name = name ? mcp_strdup(name) : NULL;
    res->description = NULL; // Keep it simple
    res->mime_type = NULL;
    // Check for allocation failures
    if (!res->uri || (name && !res->name)) {
        free(res->uri);
        free(res->name);
        free(res);
        return NULL;
    }
    return res;
}

void test_mcp_free_resources_valid(void) {
    size_t count = 2;
    mcp_resource_t** resources = (mcp_resource_t**)malloc(count * sizeof(mcp_resource_t*));
    TEST_ASSERT_NOT_NULL(resources);

    resources[0] = create_dummy_resource("res:/a", "Resource A");
    resources[1] = create_dummy_resource("res:/b", NULL); // Resource with NULL name
    TEST_ASSERT_NOT_NULL(resources[0]);
    TEST_ASSERT_NOT_NULL(resources[1]);

    mcp_free_resources(resources, count); // Should free internal strings and the array itself
    // No crash is the main test here
}

void test_mcp_free_resources_null_array(void) {
    mcp_free_resources(NULL, 5); // Should handle NULL array gracefully
    // No crash
}

void test_mcp_free_resources_zero_count(void) {
    size_t count = 0;
    mcp_resource_t** resources = (mcp_resource_t**)malloc(1 * sizeof(mcp_resource_t*)); // Allocate dummy array
    TEST_ASSERT_NOT_NULL(resources);
    resources[0] = NULL; // Not strictly necessary

    mcp_free_resources(resources, count); // Should handle zero count (and free the array)
    // No crash
}

void test_mcp_free_resources_null_element(void) {
    size_t count = 3;
    mcp_resource_t** resources = (mcp_resource_t**)malloc(count * sizeof(mcp_resource_t*));
    TEST_ASSERT_NOT_NULL(resources);

    resources[0] = create_dummy_resource("res:/a", "A");
    resources[1] = NULL; // NULL element in the middle
    resources[2] = create_dummy_resource("res:/c", "C");
    TEST_ASSERT_NOT_NULL(resources[0]);
    TEST_ASSERT_NOT_NULL(resources[2]);

    mcp_free_resources(resources, count); // Should handle NULL element gracefully
    // No crash
}

// Similar tests can be added for mcp_free_resource_templates, mcp_free_tools, mcp_free_content

// Test mcp_content_item_free
void test_mcp_content_item_free_valid(void) {
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    TEST_ASSERT_NOT_NULL(item);
    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");
    item->data = malloc(10); // Dummy data
    item->data_size = 10;
    TEST_ASSERT_NOT_NULL(item->mime_type);
    TEST_ASSERT_NOT_NULL(item->data);

    mcp_content_item_free(item); // Should free mime_type, data, and item itself
    // No crash
}

void test_mcp_content_item_free_null_fields(void) {
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    TEST_ASSERT_NOT_NULL(item);
    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = NULL; // NULL mime_type
    item->data = NULL;      // NULL data
    item->data_size = 0;

    mcp_content_item_free(item); // Should handle NULL fields gracefully
    // No crash
}

void test_mcp_content_item_free_null_item(void) {
    mcp_content_item_free(NULL); // Should handle NULL item gracefully
    // No crash
}


// --- Test Group Runner ---
void run_mcp_types_tests(void) {
    RUN_TEST(test_mcp_strdup_valid);
    RUN_TEST(test_mcp_strdup_empty);
    RUN_TEST(test_mcp_strdup_null);
    RUN_TEST(test_mcp_free_resources_valid);
    RUN_TEST(test_mcp_free_resources_null_array);
    RUN_TEST(test_mcp_free_resources_zero_count);
    RUN_TEST(test_mcp_free_resources_null_element);
    RUN_TEST(test_mcp_content_item_free_valid);
    RUN_TEST(test_mcp_content_item_free_null_fields);
    RUN_TEST(test_mcp_content_item_free_null_item);
    // Add calls for template/tool/content free tests if implemented
}
