#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "unity/unity.h"
#include "mcp_cache.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_object_pool.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Helper function for cross-platform sleep
void platform_sleep(unsigned int seconds) {
#ifdef _WIN32
    Sleep(seconds * 1000); // Sleep takes milliseconds
#else
    sleep(seconds);        // sleep takes seconds
#endif
}

// Helper to create a simple text content item
mcp_content_item_t* create_text_item(const char* text) {
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!item) return NULL;
    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");
    item->data_size = strlen(text) + 1; // Include null terminator
    item->data = malloc(item->data_size);
    if (!item->mime_type || !item->data) {
        mcp_content_item_free(item); // mcp_content_item_free should handle freeing item->data if non-NULL
        return NULL;
    }
    memcpy(item->data, text, item->data_size);
    return item;
}

// Global pool for tests
mcp_object_pool_t* test_pool = NULL; // Keep global pool pointer needed by tests


// Helper to check content (assumes single text item)
void check_content(mcp_content_item_t** content, size_t count, const char* expected_text) {
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_NOT_NULL(content[0]);
    TEST_ASSERT_EQUAL(MCP_CONTENT_TYPE_TEXT, content[0]->type);
    TEST_ASSERT_NOT_NULL(content[0]->data);
    TEST_ASSERT_EQUAL_STRING(expected_text, (const char*)content[0]->data);
    // Optionally, check size: TEST_ASSERT_EQUAL_size_t(strlen(expected_text) + 1, content[0]->data_size);
}

// Helper to release retrieved pooled content
void release_retrieved_content(mcp_content_item_t** content, size_t count) {
    if (content && test_pool) { // Check if pool exists
        for (size_t i = 0; i < count; ++i) {
            if (content[i]) {
                // Free internal data/mime_type first (allocated by acquire_pooled)
                free(content[i]->mime_type);
                free(content[i]->data);
                content[i]->mime_type = NULL;
                content[i]->data = NULL;
                content[i]->data_size = 0;
                // Release the item struct back to the pool
                mcp_object_pool_release(test_pool, content[i]);
            }
        }
        free(content); // Free the array of pointers itself
    } else if (content) {
        // Fallback if pool doesn't exist (shouldn't happen with setUp/tearDown)
        for (size_t i = 0; i < count; ++i) {
            mcp_content_item_free(content[i]);
        }
        free(content);
    }
}

// --- Test Cases ---

void test_cache_create_destroy(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    TEST_ASSERT_NOT_NULL(cache);
    mcp_cache_destroy(cache);
}

void test_cache_put_get_simple(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    mcp_content_item_t* item1 = create_text_item("value1");
    mcp_content_item_t** content_to_put = &item1;

    // Pass test_pool to put
    int put_result = mcp_cache_put(cache, "key1", test_pool, content_to_put, 1, 0);
    TEST_ASSERT_EQUAL_INT(0, put_result);

    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;

    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    check_content(retrieved_content, retrieved_count, "value1");

    // Release retrieved content
    release_retrieved_content(retrieved_content, retrieved_count);

    // IMPORTANT: Do NOT free item1 here, as it's now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // mcp_content_item_free(item1); // This causes double-free!

    mcp_cache_destroy(cache);
}

void test_cache_get_miss(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "nonexistent", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result);
    TEST_ASSERT_NULL(retrieved_content);
    TEST_ASSERT_EQUAL_size_t(0, retrieved_count);
    mcp_cache_destroy(cache);
}

void test_cache_overwrite(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    mcp_content_item_t* item1 = create_text_item("value1");
    mcp_content_item_t* item2 = create_text_item("value2");
    mcp_content_item_t** content_to_put1 = &item1;
    mcp_content_item_t** content_to_put2 = &item2;

    // Pass test_pool to put
    mcp_cache_put(cache, "key1", test_pool, content_to_put1, 1, 0);
    mcp_cache_put(cache, "key1", test_pool, content_to_put2, 1, 0); // Overwrite

    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    check_content(retrieved_content, retrieved_count, "value2"); // Check overwritten value

    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // IMPORTANT: Do NOT free items here, as they're now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // mcp_content_item_free(item1); // This causes double-free!
    // mcp_content_item_free(item2); // This causes double-free!

    mcp_cache_destroy(cache);
}

void test_cache_invalidate(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    mcp_content_item_t* item1 = create_text_item("value1");
    mcp_content_item_t** content_to_put = &item1;
    // Pass test_pool to put
    mcp_cache_put(cache, "key1", test_pool, content_to_put, 1, 0);

    int invalidate_result = mcp_cache_invalidate(cache, "key1");
    TEST_ASSERT_EQUAL_INT(0, invalidate_result);

    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result); // Should be gone

    invalidate_result = mcp_cache_invalidate(cache, "nonexistent");
    TEST_ASSERT_EQUAL_INT(-1, invalidate_result); // Invalidate non-existent

    // IMPORTANT: Do NOT free item1 here, as it's now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // mcp_content_item_free(item1); // This causes double-free!

    mcp_cache_destroy(cache);
}

void test_cache_expiry(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 1); // 1 second default TTL
    mcp_content_item_t* item1 = create_text_item("value_ttl");
    mcp_content_item_t** content_to_put = &item1;

    // Put with specific TTL of 1 second
    // Pass test_pool to put
    mcp_cache_put(cache, "key_ttl", test_pool, content_to_put, 1, 1);

    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key_ttl", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result); // Should be present initially
    check_content(retrieved_content, retrieved_count, "value_ttl");
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    platform_sleep(2); // Wait for expiry

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key_ttl", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result); // Should be expired now

    // Test prune expired
    mcp_content_item_t* item_perm = create_text_item("permanent");
    mcp_content_item_t** content_perm = &item_perm;
    // Pass test_pool to put
    mcp_cache_put(cache, "key_perm", test_pool, content_perm, 1, -1); // Never expires
    mcp_cache_put(cache, "key_ttl2", test_pool, content_to_put, 1, 1); // Expires

    platform_sleep(2);
    size_t pruned = mcp_cache_prune_expired(cache);
    TEST_ASSERT_EQUAL_size_t(1, pruned); // Only key_ttl2 should be pruned

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key_perm", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result); // Permanent should still be there
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key_ttl2", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result); // Expired one should be gone

    // IMPORTANT: Do NOT free items here, as they're now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // mcp_content_item_free(item1); // This causes double-free!
    // mcp_content_item_free(item_perm); // This causes double-free!

    mcp_cache_destroy(cache);
}

// --- LRU-K Specific Tests (K=2) ---

void test_lruk_evict_less_than_k_accessed(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(3, 60); // Capacity 3
    mcp_content_item_t* items[4];
    mcp_content_item_t** content_ptrs[4];
    char key[10], value[10];

    // Fill cache
    for (int i = 0; i < 3; ++i) {
        sprintf(key, "key%d", i);
        sprintf(value, "val%d", i);
        items[i] = create_text_item(value);
        content_ptrs[i] = &items[i];
        // Pass test_pool to put
        mcp_cache_put(cache, key, test_pool, content_ptrs[i], 1, 0);
        platform_sleep(1); // Ensure distinct access times
    }

    // Access key1 once more (key1 accessed twice, key0/key2 once)
    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // Add key3, should evict key0 (oldest access among those accessed < K times)
    items[3] = create_text_item("val3");
    content_ptrs[3] = &items[3];
    // Pass test_pool to put
    mcp_cache_put(cache, "key3", test_pool, content_ptrs[3], 1, 0);

    // Verify key0 is gone, others remain
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key0", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result);

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key2", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key3", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // IMPORTANT: Do NOT free items here, as they're now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // for (int i = 0; i < 4; ++i) mcp_content_item_free(items[i]); // This causes double-free!

    mcp_cache_destroy(cache);
}

void test_lruk_evict_k_accessed(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(3, 60); // Capacity 3
    mcp_content_item_t* items[4];
    mcp_content_item_t** content_ptrs[4];
    char key[10], value[10];
    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;

    // Fill cache
    for (int i = 0; i < 3; ++i) {
        sprintf(key, "key%d", i);
        sprintf(value, "val%d", i);
        items[i] = create_text_item(value);
        content_ptrs[i] = &items[i];
        // Pass test_pool to put
        mcp_cache_put(cache, key, test_pool, content_ptrs[i], 1, 0);
        platform_sleep(1); // Stagger initial put times (which count as first access)
    }

    // Access all keys again to ensure access_count >= K (K=2)
    // Access order: key0, key1, key2 (key0 has oldest 2nd access)
    for (int i = 0; i < 3; ++i) {
        sprintf(key, "key%d", i);
        // Pass test_pool to get
        mcp_cache_get(cache, key, test_pool, &retrieved_content, &retrieved_count);
        release_retrieved_content(retrieved_content, retrieved_count); // Use release helper
        platform_sleep(1); // Stagger second access times
    }

    // Add key3, should evict key0 (oldest K-th access time, K=2 -> history[1])
    items[3] = create_text_item("val3");
    content_ptrs[3] = &items[3];
    // Pass test_pool to put
    mcp_cache_put(cache, "key3", test_pool, content_ptrs[3], 1, 0);

    // Verify key0 is gone, others remain
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key0", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result);

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key2", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // Pass test_pool to get
    get_result = mcp_cache_get(cache, "key3", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // IMPORTANT: Do NOT free items here, as they're now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // for (int i = 0; i < 4; ++i) mcp_content_item_free(items[i]); // This causes double-free!

    mcp_cache_destroy(cache);
}

void test_cache_zero_capacity(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(0, 60); // Zero capacity
    TEST_ASSERT_NOT_NULL(cache); // Creation should succeed

    mcp_content_item_t* item1 = create_text_item("value1");
    mcp_content_item_t** content_to_put = &item1;

    // Put should effectively fail or do nothing gracefully
    // Pass test_pool to put
    int put_result = mcp_cache_put(cache, "key1", test_pool, content_to_put, 1, 0);
    // Depending on implementation, put might return 0 but not store, or -1.
    // Let's assume it doesn't store.
    TEST_ASSERT_EQUAL_INT(0, put_result); // Or -1 if it signals failure

    // Get should always miss
    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "key1", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(-1, get_result); // Should miss

    // IMPORTANT: Do NOT free item1 here, as it's now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // mcp_content_item_free(item1); // This causes double-free!

    mcp_cache_destroy(cache);
}

void test_cache_multiple_items(void) {
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    mcp_content_item_t* item1 = create_text_item("value1");
    mcp_content_item_t* item2 = create_text_item("value2");
    mcp_content_item_t* items_to_put[] = {item1, item2};
    size_t count_to_put = sizeof(items_to_put) / sizeof(items_to_put[0]);

    // Pass test_pool to put
    int put_result = mcp_cache_put(cache, "multi_key", test_pool, items_to_put, count_to_put, 0);
    TEST_ASSERT_EQUAL_INT(0, put_result);

    mcp_content_item_t** retrieved_content = NULL;
    size_t retrieved_count = 0;
    // Pass test_pool to get
    int get_result = mcp_cache_get(cache, "multi_key", test_pool, &retrieved_content, &retrieved_count);
    TEST_ASSERT_EQUAL_INT(0, get_result);
    TEST_ASSERT_EQUAL_size_t(count_to_put, retrieved_count);
    TEST_ASSERT_NOT_NULL(retrieved_content);

    // Check content of both items (order might not be guaranteed, but likely preserved)
    if (retrieved_count == 2 && retrieved_content) {
        TEST_ASSERT_NOT_NULL(retrieved_content[0]);
        TEST_ASSERT_NOT_NULL(retrieved_content[1]);
        // Simple check assuming order is preserved
        TEST_ASSERT_EQUAL_STRING("value1", (const char*)retrieved_content[0]->data);
        TEST_ASSERT_EQUAL_STRING("value2", (const char*)retrieved_content[1]->data);
    }

    release_retrieved_content(retrieved_content, retrieved_count); // Use release helper

    // IMPORTANT: Do NOT free items here, as they're now owned by the cache/pool
    // and will be freed when the pool is destroyed in tearDown
    // mcp_content_item_free(item1); // This causes double-free!
    // mcp_content_item_free(item2); // This causes double-free!

    mcp_cache_destroy(cache);
}


// --- Test Runner ---

void run_cache_tests(void) {
    // Remove UNITY_BEGIN() and UNITY_END() calls
    // These should only be called in the main function
    RUN_TEST(test_cache_create_destroy);
    RUN_TEST(test_cache_put_get_simple);
    RUN_TEST(test_cache_get_miss);
    RUN_TEST(test_cache_overwrite);
    RUN_TEST(test_cache_invalidate);
    RUN_TEST(test_cache_expiry);
    RUN_TEST(test_lruk_evict_less_than_k_accessed);
    RUN_TEST(test_lruk_evict_k_accessed);
    RUN_TEST(test_cache_zero_capacity); // Add new test
    RUN_TEST(test_cache_multiple_items); // Add new test
}

// If running this file directly (optional)
// int main(void) {
//     run_cache_tests();
//     return 0;
// }
