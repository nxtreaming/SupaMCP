#include "unity.h"
#include "mcp_thread_local.h"
#include "mcp_object_pool.h"
#include "mcp_types.h"

#define TEST_ARENA_SIZE (1024 * 1024) // 1MB arena for tests

// Declare the global test pool pointer (defined in test_mcp_cache.c)
extern mcp_object_pool_t* test_pool;

// Forward declarations for test suite runners
void run_cache_lru_tests(void);

// setUp and tearDown functions are optional, run before/after each test
void setUp(void) {
    // Reset the thread-local arena before each test
    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena) {
        mcp_arena_reset(arena);
    }
    // Create the global test pool if it doesn't exist
    if (test_pool == NULL) {
        test_pool = mcp_object_pool_create(sizeof(mcp_content_item_t), 32, 0);
        // We don't use TEST_ASSERT here as it's setUp, maybe log?
        if (test_pool == NULL) {
            printf("CRITICAL: Failed to create global test object pool in setUp!\n");
        }
    }
}

void tearDown(void) {
    // Destroy the global test pool if it exists
    if (test_pool != NULL) {
        mcp_object_pool_destroy(test_pool);
        test_pool = NULL;
    }
    // Arena is reset in setUp
}

// Main test runner
int main(void) {
    // Initialize thread-local arena for tests
    if (mcp_arena_init_current_thread(TEST_ARENA_SIZE) != 0) {
        printf("Failed to initialize thread-local arena\n");
        return -1;
    }

    UNITY_BEGIN(); // IMPORTANT: Call this before any tests

    // Run only LRU cache tests
    run_cache_lru_tests();

    int result = UNITY_END(); // IMPORTANT: Call this to finalize tests

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();

    return result;
}
