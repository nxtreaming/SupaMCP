#include "unity.h"
#include "mcp_buffer_pool.h"
#include <stdlib.h>
#include <stdio.h>

// Define pool parameters for tests
#define TEST_BUFFER_SIZE 128
#define TEST_NUM_BUFFERS 4

// No global setUp/tearDown or pool variable needed here

// Test case: Verify pool creation
void test_mcp_buffer_pool_create_valid(void) {
    mcp_buffer_pool_t* pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL_MESSAGE(pool, "Pool creation returned NULL");
    // Check if the buffer size is stored correctly
    TEST_ASSERT_EQUAL_size_t(TEST_BUFFER_SIZE, mcp_buffer_pool_get_buffer_size(pool));
    mcp_buffer_pool_destroy(pool); // Destroy pool used in this test
}

// Test case: Verify pool creation with invalid parameters
void test_mcp_buffer_pool_create_invalid(void) {
    mcp_buffer_pool_t* pool_zero_size = mcp_buffer_pool_create(0, TEST_NUM_BUFFERS);
    TEST_ASSERT_NULL_MESSAGE(pool_zero_size, "Pool creation should fail with zero buffer size");
    mcp_buffer_pool_destroy(pool_zero_size); // Should handle NULL

    mcp_buffer_pool_t* pool_zero_num = mcp_buffer_pool_create(TEST_BUFFER_SIZE, 0);
    TEST_ASSERT_NULL_MESSAGE(pool_zero_num, "Pool creation should fail with zero num buffers");
    mcp_buffer_pool_destroy(pool_zero_num); // Should handle NULL
}

// Test case: Acquire all available buffers
void test_mcp_buffer_pool_acquire_all(void) {
    mcp_buffer_pool_t* pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL(pool);

    void* buffers[TEST_NUM_BUFFERS];
    for (size_t i = 0; i < TEST_NUM_BUFFERS; ++i) {
        buffers[i] = mcp_buffer_pool_acquire(pool);
        TEST_ASSERT_NOT_NULL_MESSAGE(buffers[i], "Failed to acquire buffer from pool");
    }

    // Try to acquire one more - should fail (return NULL)
    void* extra_buffer = mcp_buffer_pool_acquire(pool);
    TEST_ASSERT_NULL_MESSAGE(extra_buffer, "Acquiring from empty pool should return NULL");

    // Release buffers back
    for (size_t i = 0; i < TEST_NUM_BUFFERS; ++i) {
        mcp_buffer_pool_release(pool, buffers[i]);
    }
    mcp_buffer_pool_destroy(pool); // Destroy pool used in this test
}

// Test case: Acquire and release a buffer
void test_mcp_buffer_pool_acquire_release_cycle(void) {
    mcp_buffer_pool_t* pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL(pool);

    void* buffer1 = mcp_buffer_pool_acquire(pool);
    TEST_ASSERT_NOT_NULL(buffer1);

    // Can acquire more
    void* buffer2 = mcp_buffer_pool_acquire(pool);
    TEST_ASSERT_NOT_NULL(buffer2);

    // Release buffer1
    mcp_buffer_pool_release(pool, buffer1);
    buffer1 = NULL; // Avoid using released buffer

    // Should be able to acquire again
    void* buffer3 = mcp_buffer_pool_acquire(pool);
    TEST_ASSERT_NOT_NULL(buffer3);

    // Release the remaining buffers
    mcp_buffer_pool_release(pool, buffer2);
    mcp_buffer_pool_release(pool, buffer3);
    mcp_buffer_pool_destroy(pool); // Destroy pool used in this test
}

// Test case: Release NULL buffer (should not crash)
void test_mcp_buffer_pool_release_null(void) {
    mcp_buffer_pool_t* pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL(pool);
    // This test mainly checks for robustness against NULL input
    mcp_buffer_pool_release(pool, NULL);
    // No assertion needed, just checking it doesn't crash
    mcp_buffer_pool_destroy(pool); // Destroy pool used in this test
}

// Test case: Acquire from NULL pool (should not crash)
void test_mcp_buffer_pool_acquire_null_pool(void) {
    void* buffer = mcp_buffer_pool_acquire(NULL);
    TEST_ASSERT_NULL(buffer);
}

// Test case: Release to NULL pool (should not crash)
void test_mcp_buffer_pool_release_null_pool(void) {
    mcp_buffer_pool_t* pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL(pool);
    // Acquire a valid buffer first to try releasing it to NULL pool
    void* buffer = mcp_buffer_pool_acquire(pool);
    TEST_ASSERT_NOT_NULL(buffer);
    mcp_buffer_pool_release(NULL, buffer); // Should not crash
    // Release it properly
    mcp_buffer_pool_release(pool, buffer);
    mcp_buffer_pool_destroy(pool); // Destroy pool used in this test
}

// Test case: Get buffer size
void test_mcp_buffer_pool_get_size(void) {
    mcp_buffer_pool_t* pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL(pool);
    TEST_ASSERT_EQUAL_size_t(TEST_BUFFER_SIZE, mcp_buffer_pool_get_buffer_size(pool));
    mcp_buffer_pool_destroy(pool); // Destroy pool used in this test
}

// Test case: Get buffer size from NULL pool
void test_mcp_buffer_pool_get_size_null(void) {
    TEST_ASSERT_EQUAL_size_t(0, mcp_buffer_pool_get_buffer_size(NULL));
}

// Note: Thread safety tests are complex and omitted here.
// They would typically involve creating multiple threads that
// concurrently acquire and release buffers, checking for race conditions
// or deadlocks using thread sanitizers or specific test patterns.
