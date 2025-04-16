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

// External declarations for functions and variables defined in test_mcp_cache.c
extern void platform_sleep(unsigned int seconds);
extern mcp_content_item_t* create_text_item(const char* text);
extern mcp_object_pool_t* test_pool;
extern void check_content(mcp_content_item_t** content, size_t count, const char* expected_text);
extern void release_retrieved_content(mcp_content_item_t** content, size_t count);

// --- Test Cases ---

void test_lru_basic_eviction(void) {
    // Create a cache with capacity 3
    mcp_resource_cache_t* cache = mcp_cache_create(3, 60);
    TEST_ASSERT_NOT_NULL(cache);

    // Create 4 items (one more than capacity)
    mcp_content_item_t* items[4];
    for (int i = 0; i < 4; i++) {
        char value[20];
        sprintf(value, "value%d", i);
        items[i] = create_text_item(value);
        TEST_ASSERT_NOT_NULL(items[i]);
    }

    // Add first 3 items to fill the cache
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Verify all 3 items are in the cache
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, key, test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        char expected[20];
        sprintf(expected, "value%d", i);
        check_content(content, count, expected);
        release_retrieved_content(content, count);
    }

    // Access key1 and key2 to make key0 the least recently used
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    mcp_cache_get(cache, "key1", test_pool, &content, &count);
    release_retrieved_content(content, count);
    mcp_cache_get(cache, "key2", test_pool, &content, &count);
    release_retrieved_content(content, count);

    // Add the 4th item, which should evict key0 (LRU)
    int result = mcp_cache_put(cache, "key3", test_pool, &items[3], 1, 0);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify key0 is no longer in the cache
    content = NULL;
    count = 0;
    result = mcp_cache_get(cache, "key0", test_pool, &content, &count);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_NULL(content);

    // Verify key1, key2, and key3 are in the cache
    for (int i = 1; i < 4; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        content = NULL;
        count = 0;
        result = mcp_cache_get(cache, key, test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        char expected[20];
        sprintf(expected, "value%d", i);
        check_content(content, count, expected);
        release_retrieved_content(content, count);
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        mcp_content_item_free(items[i]);
    }
    mcp_cache_destroy(cache);
}

void test_lru_update_on_get(void) {
    // Create a cache with capacity 3
    mcp_resource_cache_t* cache = mcp_cache_create(3, 60);
    TEST_ASSERT_NOT_NULL(cache);

    // Create 4 items (one more than capacity)
    mcp_content_item_t* items[4];
    for (int i = 0; i < 4; i++) {
        char value[20];
        sprintf(value, "value%d", i);
        items[i] = create_text_item(value);
        TEST_ASSERT_NOT_NULL(items[i]);
    }

    // Add first 3 items to fill the cache
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Access key0 to make it the most recently used
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    mcp_cache_get(cache, "key0", test_pool, &content, &count);
    release_retrieved_content(content, count);

    // Now key1 should be the least recently used

    // Add the 4th item, which should evict key1 (LRU)
    int result = mcp_cache_put(cache, "key3", test_pool, &items[3], 1, 0);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify key1 is no longer in the cache
    content = NULL;
    count = 0;
    result = mcp_cache_get(cache, "key1", test_pool, &content, &count);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_NULL(content);

    // Verify key0, key2, and key3 are in the cache
    const char* keys[] = {"key0", "key2", "key3"};
    const char* values[] = {"value0", "value2", "value3"};
    for (int i = 0; i < 3; i++) {
        content = NULL;
        count = 0;
        result = mcp_cache_get(cache, keys[i], test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        check_content(content, count, values[i]);
        release_retrieved_content(content, count);
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        mcp_content_item_free(items[i]);
    }
    mcp_cache_destroy(cache);
}

void test_lru_update_on_put(void) {
    // Create a cache with capacity 3
    mcp_resource_cache_t* cache = mcp_cache_create(3, 60);
    TEST_ASSERT_NOT_NULL(cache);

    // Create 5 items (two more than capacity)
    mcp_content_item_t* items[5];
    for (int i = 0; i < 5; i++) {
        char value[20];
        sprintf(value, "value%d", i);
        items[i] = create_text_item(value);
        TEST_ASSERT_NOT_NULL(items[i]);
    }

    // Add first 3 items to fill the cache
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Update key0 with a new value, making it the most recently used
    int result = mcp_cache_put(cache, "key0", test_pool, &items[3], 1, 0);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Now key1 should be the least recently used

    // Add a new item, which should evict key1 (LRU)
    result = mcp_cache_put(cache, "key4", test_pool, &items[4], 1, 0);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify key1 is no longer in the cache
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    result = mcp_cache_get(cache, "key1", test_pool, &content, &count);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_NULL(content);

    // Verify key0, key2, and key4 are in the cache
    const char* keys[] = {"key0", "key2", "key4"};
    const char* values[] = {"value3", "value2", "value4"};
    for (int i = 0; i < 3; i++) {
        content = NULL;
        count = 0;
        result = mcp_cache_get(cache, keys[i], test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        check_content(content, count, values[i]);
        release_retrieved_content(content, count);
    }

    // Clean up
    for (int i = 0; i < 5; i++) {
        mcp_content_item_free(items[i]);
    }
    mcp_cache_destroy(cache);
}

void test_lru_invalidate(void) {
    // Create a cache with capacity 3
    mcp_resource_cache_t* cache = mcp_cache_create(3, 60);
    TEST_ASSERT_NOT_NULL(cache);

    // Create 4 items (one more than capacity)
    mcp_content_item_t* items[4];
    for (int i = 0; i < 4; i++) {
        char value[20];
        sprintf(value, "value%d", i);
        items[i] = create_text_item(value);
        TEST_ASSERT_NOT_NULL(items[i]);
    }

    // Add first 3 items to fill the cache
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Invalidate key1
    int result = mcp_cache_invalidate(cache, "key1");
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify key1 is no longer in the cache
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    result = mcp_cache_get(cache, "key1", test_pool, &content, &count);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_NULL(content);

    // Add a new item, which should not evict anything since we have space
    result = mcp_cache_put(cache, "key3", test_pool, &items[3], 1, 0);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify key0, key2, and key3 are in the cache
    const char* keys[] = {"key0", "key2", "key3"};
    const char* values[] = {"value0", "value2", "value3"};
    for (int i = 0; i < 3; i++) {
        content = NULL;
        count = 0;
        result = mcp_cache_get(cache, keys[i], test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        check_content(content, count, values[i]);
        release_retrieved_content(content, count);
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        mcp_content_item_free(items[i]);
    }
    mcp_cache_destroy(cache);
}

void test_lru_expiry(void) {
    // Create a cache with capacity 3 and 1 second TTL
    mcp_resource_cache_t* cache = mcp_cache_create(3, 1);
    TEST_ASSERT_NOT_NULL(cache);

    // Create 4 items (one more than capacity)
    mcp_content_item_t* items[4];
    for (int i = 0; i < 4; i++) {
        char value[20];
        sprintf(value, "value%d", i);
        items[i] = create_text_item(value);
        TEST_ASSERT_NOT_NULL(items[i]);
    }

    // Add first 3 items to fill the cache
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Wait for items to expire
    platform_sleep(2);

    // Verify all items are expired
    for (int i = 0; i < 3; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, key, test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(-1, result);
        TEST_ASSERT_NULL(content);
    }

    // Add a new item, which should not evict anything since all items are expired
    int result = mcp_cache_put(cache, "key3", test_pool, &items[3], 1, -1); // Never expires
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify only key3 is in the cache
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    result = mcp_cache_get(cache, "key3", test_pool, &content, &count);
    TEST_ASSERT_EQUAL_INT(0, result);
    check_content(content, count, "value3");
    release_retrieved_content(content, count);

    // Clean up
    for (int i = 0; i < 4; i++) {
        mcp_content_item_free(items[i]);
    }
    mcp_cache_destroy(cache);
}

void test_lru_stress(void) {
    // Create a cache with capacity 10
    mcp_resource_cache_t* cache = mcp_cache_create(10, 60);
    TEST_ASSERT_NOT_NULL(cache);

    // Create 20 items (twice the capacity)
    mcp_content_item_t* items[20];
    for (int i = 0; i < 20; i++) {
        char value[20];
        sprintf(value, "value%d", i);
        items[i] = create_text_item(value);
        TEST_ASSERT_NOT_NULL(items[i]);
    }

    // Add all 20 items, which should keep only the 10 most recently added
    for (int i = 0; i < 20; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Verify the first 10 items are no longer in the cache
    for (int i = 0; i < 10; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, key, test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(-1, result);
        TEST_ASSERT_NULL(content);
    }

    // Verify the last 10 items are in the cache
    for (int i = 10; i < 20; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, key, test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        char expected[20];
        sprintf(expected, "value%d", i);
        check_content(content, count, expected);
        release_retrieved_content(content, count);
    }

    // Access items in order to change LRU order (key15-key19 will be most recently used)
    for (int i = 15; i <= 19; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, key, test_pool, &content, &count);
        printf("Accessing key%d: result=%d, content=%p\n", i, result, (void*)content);
        release_retrieved_content(content, count);
    }

    // Add 5 new items, which should evict the 5 least recently used (key10-key14)
    for (int i = 0; i < 5; i++) {
        char key[20];
        sprintf(key, "newkey%d", i);
        printf("Adding newkey%d to cache\n", i);
        int result = mcp_cache_put(cache, key, test_pool, &items[i], 1, 0);
        TEST_ASSERT_EQUAL_INT(0, result);
    }

    // Verify key10-key14 are no longer in the cache
    for (int i = 10; i < 15; i++) {
        char key[20];
        sprintf(key, "key%d", i);
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, key, test_pool, &content, &count);
        printf("Checking if key%d is in cache: result=%d, content=%p\n", i, result, (void*)content);
        TEST_ASSERT_EQUAL_INT(-1, result);
        TEST_ASSERT_NULL(content);
    }

    // Verify key15-key19 and newkey0-newkey4 are in the cache
    const char* keys[] = {"key15", "key16", "key17", "key18", "key19",
                          "newkey0", "newkey1", "newkey2", "newkey3", "newkey4"};
    const char* values[] = {"value15", "value16", "value17", "value18", "value19",
                            "value0", "value1", "value2", "value3", "value4"};
    for (int i = 0; i < 10; i++) {
        mcp_content_item_t** content = NULL;
        size_t count = 0;
        int result = mcp_cache_get(cache, keys[i], test_pool, &content, &count);
        TEST_ASSERT_EQUAL_INT(0, result);
        check_content(content, count, values[i]);
        release_retrieved_content(content, count);
    }

    // Clean up
    for (int i = 0; i < 20; i++) {
        mcp_content_item_free(items[i]);
    }
    mcp_cache_destroy(cache);
}

// Note: setUp and tearDown functions are defined in test_runner.c

// --- Test Runner ---

void run_cache_lru_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lru_basic_eviction);
    RUN_TEST(test_lru_update_on_get);
    RUN_TEST(test_lru_update_on_put);
    RUN_TEST(test_lru_invalidate);
    RUN_TEST(test_lru_expiry);
    RUN_TEST(test_lru_stress);
    UNITY_END();
}

// If running this file directly (optional)
// int main(void) {
//     run_cache_lru_tests();
//     return 0;
// }
