#include "unity.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"
#include "mcp_memory_tracker.h"
#include "mcp_memory_constants.h"
#include <stdlib.h>
#include <string.h>

// Test fixture setup and teardown for memory pool tests
void memory_pool_test_setup(void) {
    // Initialize the memory pool system
    mcp_memory_pool_system_init(64, 32, 16);

    // Initialize the thread cache
    mcp_thread_cache_init();

    // Initialize the memory tracker
    mcp_memory_tracker_init(true, false);
}

void memory_pool_test_teardown(void) {
    // Clean up the memory tracker
    mcp_memory_tracker_cleanup();

    // Clean up the thread cache
    mcp_thread_cache_cleanup();

    // Clean up the memory pool system
    mcp_memory_pool_system_cleanup();
}

// Test creating and destroying a memory pool
void test_memory_pool_create_destroy(void) {
    memory_pool_test_setup();
    // Create a pool
    mcp_memory_pool_t* pool = mcp_memory_pool_create(256, 10, 0);
    TEST_ASSERT_NOT_NULL(pool);

    // Get stats
    mcp_memory_pool_stats_t stats;
    TEST_ASSERT_TRUE(mcp_memory_pool_get_stats(pool, &stats));

    // Verify stats
    TEST_ASSERT_EQUAL_UINT(10, stats.total_blocks);
    TEST_ASSERT_EQUAL_UINT(10, stats.free_blocks);
    TEST_ASSERT_EQUAL_UINT(0, stats.allocated_blocks);
    TEST_ASSERT_EQUAL_UINT(256, stats.block_size);

    // Destroy the pool
    mcp_memory_pool_destroy(pool);

    memory_pool_test_teardown();
}

// Test allocating and freeing memory from a pool
void test_memory_pool_alloc_free(void) {
    memory_pool_test_setup();
    // Create a pool
    mcp_memory_pool_t* pool = mcp_memory_pool_create(256, 10, 0);
    TEST_ASSERT_NOT_NULL(pool);

    // Allocate some blocks
    void* blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = mcp_memory_pool_alloc(pool);
        TEST_ASSERT_NOT_NULL(blocks[i]);

        // Write some data to the block to ensure it's usable
        memset(blocks[i], i + 1, 256);
    }

    // Get stats
    mcp_memory_pool_stats_t stats;
    TEST_ASSERT_TRUE(mcp_memory_pool_get_stats(pool, &stats));

    // Verify stats
    TEST_ASSERT_EQUAL_UINT(10, stats.total_blocks);
    TEST_ASSERT_EQUAL_UINT(5, stats.free_blocks);
    TEST_ASSERT_EQUAL_UINT(5, stats.allocated_blocks);

    // Free the blocks
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(mcp_memory_pool_free(pool, blocks[i]));
    }

    // Get stats again
    TEST_ASSERT_TRUE(mcp_memory_pool_get_stats(pool, &stats));

    // Verify stats
    TEST_ASSERT_EQUAL_UINT(10, stats.total_blocks);
    TEST_ASSERT_EQUAL_UINT(10, stats.free_blocks);
    TEST_ASSERT_EQUAL_UINT(0, stats.allocated_blocks);

    // Destroy the pool
    mcp_memory_pool_destroy(pool);

    memory_pool_test_teardown();
}

// Test the global pool allocation functions
void test_global_pool_alloc_free(void) {
    memory_pool_test_setup();
    // Allocate memory of different sizes
    void* small = mcp_pool_alloc(128);
    void* medium = mcp_pool_alloc(512);
    void* large = mcp_pool_alloc(2048);
    void* extra_large = mcp_pool_alloc(8192); // Should use malloc

    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_NOT_NULL(medium);
    TEST_ASSERT_NOT_NULL(large);
    TEST_ASSERT_NOT_NULL(extra_large);

    // Write some data to ensure the memory is usable
    memset(small, 1, 128);
    memset(medium, 2, 512);
    memset(large, 3, 2048);
    memset(extra_large, 4, 8192);

    // Get stats for each pool
    mcp_memory_pool_stats_t small_stats, medium_stats, large_stats;
    TEST_ASSERT_TRUE(mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &small_stats));
    TEST_ASSERT_TRUE(mcp_pool_get_stats(MCP_POOL_SIZE_MEDIUM, &medium_stats));
    TEST_ASSERT_TRUE(mcp_pool_get_stats(MCP_POOL_SIZE_LARGE, &large_stats));

    // Free the memory
    mcp_pool_free(small);
    mcp_pool_free(medium);
    mcp_pool_free(large);
    mcp_pool_free(extra_large);

    memory_pool_test_teardown();
}

// Test the thread-local cache
void test_thread_cache(void) {
    memory_pool_test_setup();
    // Allocate memory using the thread cache
    void* blocks[20];
    for (int i = 0; i < 20; i++) {
        blocks[i] = mcp_thread_cache_alloc(128);
        TEST_ASSERT_NOT_NULL(blocks[i]);

        // Write some data to ensure the memory is usable
        memset(blocks[i], i + 1, 128);
    }

    // Get thread cache stats
    mcp_thread_cache_stats_t stats;
    TEST_ASSERT_TRUE(mcp_thread_cache_get_stats(&stats));

    // Free the memory
    for (int i = 0; i < 20; i++) {
        mcp_thread_cache_free(blocks[i], 128);
    }

    // Flush the cache
    mcp_thread_cache_flush();

    // Get stats again
    TEST_ASSERT_TRUE(mcp_thread_cache_get_stats(&stats));
    TEST_ASSERT_EQUAL_UINT(0, stats.small_cache_count);

    memory_pool_test_teardown();
}

// Test the memory tracker
void test_memory_tracker(void) {
    memory_pool_test_setup();
    // Get initial stats
    mcp_memory_stats_t initial_stats;
    TEST_ASSERT_TRUE(mcp_memory_tracker_get_stats(&initial_stats));

    // Allocate some memory and track it
    void* ptr = malloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);
    mcp_memory_tracker_record_alloc(ptr, 1024, __FILE__, __LINE__);

    // Get updated stats
    mcp_memory_stats_t updated_stats;
    TEST_ASSERT_TRUE(mcp_memory_tracker_get_stats(&updated_stats));

    // Verify stats
    TEST_ASSERT_EQUAL_UINT(initial_stats.total_allocations + 1, updated_stats.total_allocations);
    TEST_ASSERT_EQUAL_UINT(initial_stats.current_allocations + 1, updated_stats.current_allocations);
    TEST_ASSERT_EQUAL_UINT(initial_stats.current_bytes + 1024, updated_stats.current_bytes);

    // Free the memory and track it
    mcp_memory_tracker_record_free(ptr);
    free(ptr);

    // Get final stats
    mcp_memory_stats_t final_stats;
    TEST_ASSERT_TRUE(mcp_memory_tracker_get_stats(&final_stats));

    // Verify stats
    TEST_ASSERT_EQUAL_UINT(updated_stats.total_frees + 1, final_stats.total_frees);
    TEST_ASSERT_EQUAL_UINT(updated_stats.current_allocations - 1, final_stats.current_allocations);
    TEST_ASSERT_EQUAL_UINT(updated_stats.current_bytes - 1024, final_stats.current_bytes);

    // Test memory limits
    TEST_ASSERT_TRUE(mcp_memory_tracker_set_limit(512));
    TEST_ASSERT_TRUE(mcp_memory_tracker_would_exceed_limit(1024));
    TEST_ASSERT_FALSE(mcp_memory_tracker_would_exceed_limit(256));

    // Dump leaks to a file
    TEST_ASSERT_TRUE(mcp_memory_tracker_dump_leaks("memory_leaks.txt"));

    memory_pool_test_teardown();
}

// These tests are run from the test runner
