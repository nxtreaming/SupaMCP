#include "unity.h"
#include "mcp_buffer_pool.h"
#include <stdlib.h>
#include <stdio.h>

// Define pool parameters for tests
#define TEST_BUFFER_SIZE 128
#define TEST_NUM_BUFFERS 4

static mcp_buffer_pool_t* test_pool = NULL;

// Test setup function - runs before each test
void setUp(void) {
    // Create a new pool for each test to ensure isolation
    test_pool = mcp_buffer_pool_create(TEST_BUFFER_SIZE, TEST_NUM_BUFFERS);
    TEST_ASSERT_NOT_NULL_MESSAGE(test_pool, "setUp failed: Pool creation returned NULL");
}

// Test teardown function - runs after each test
void tearDown(void) {
    mcp_buffer_pool_destroy(test_pool);
    test_pool = NULL;
}

// Test case: Verify pool creation
void test_mcp_buffer_pool_create_valid(void) {
    // setUp already creates the pool, just check it's not NULL
    TEST_ASSERT_NOT_NULL(test_pool);
    // Check if the buffer size is stored correctly
    TEST_ASSERT_EQUAL_size_t(TEST_BUFFER_SIZE, mcp_buffer_pool_get_buffer_size(test_pool));
}

// Test case: Verify pool creation with invalid parameters
void test_mcp_buffer_pool_create_invalid(void) {
    mcp_buffer_pool_t* pool_zero_size = mcp_buffer_pool_create(0, TEST_NUM_BUFFERS);
    TEST_ASSERT_NULL_MESSAGE(pool_zero_size, "Pool creation should fail with zero buffer size");

    mcp_buffer_pool_t* pool_zero_num = mcp_buffer_pool_create(TEST_BUFFER_SIZE, 0);
    TEST_ASSERT_NULL_MESSAGE(pool_zero_num, "Pool creation should fail with zero num buffers");
}

// Test case: Acquire all available buffers
void test_mcp_buffer_pool_acquire_all(void) {
    void* buffers[TEST_NUM_BUFFERS];
    for (size_t i = 0; i < TEST_NUM_BUFFERS; ++i) {
        buffers[i] = mcp_buffer_pool_acquire(test_pool);
        TEST_ASSERT_NOT_NULL_MESSAGE(buffers[i], "Failed to acquire buffer from pool");
    }

    // Try to acquire one more - should fail (return NULL)
    void* extra_buffer = mcp_buffer_pool_acquire(test_pool);
    TEST_ASSERT_NULL_MESSAGE(extra_buffer, "Acquiring from empty pool should return NULL");

    // Release buffers back (needed for tearDown)
    for (size_t i = 0; i < TEST_NUM_BUFFERS; ++i) {
        mcp_buffer_pool_release(test_pool, buffers[i]);
    }
}

// Test case: Acquire and release a buffer
void test_mcp_buffer_pool_acquire_release_cycle(void) {
    void* buffer1 = mcp_buffer_pool_acquire(test_pool);
    TEST_ASSERT_NOT_NULL(buffer1);

    // Can acquire more
    void* buffer2 = mcp_buffer_pool_acquire(test_pool);
    TEST_ASSERT_NOT_NULL(buffer2);

    // Release buffer1
    mcp_buffer_pool_release(test_pool, buffer1);
    buffer1 = NULL; // Avoid using released buffer

    // Should be able to acquire again
    void* buffer3 = mcp_buffer_pool_acquire(test_pool);
    TEST_ASSERT_NOT_NULL(buffer3);

    // Release the remaining buffers
    mcp_buffer_pool_release(test_pool, buffer2);
    mcp_buffer_pool_release(test_pool, buffer3);
}

// Test case: Release NULL buffer (should not crash)
void test_mcp_buffer_pool_release_null(void) {
    // This test mainly checks for robustness against NULL input
    mcp_buffer_pool_release(test_pool, NULL);
    // No assertion needed, just checking it doesn't crash
}

// Test case: Acquire from NULL pool (should not crash)
void test_mcp_buffer_pool_acquire_null_pool(void) {
    void* buffer = mcp_buffer_pool_acquire(NULL);
    TEST_ASSERT_NULL(buffer);
}

// Test case: Release to NULL pool (should not crash)
void test_mcp_buffer_pool_release_null_pool(void) {
    // Acquire a valid buffer first to try releasing it to NULL pool
    void* buffer = mcp_buffer_pool_acquire(test_pool);
    TEST_ASSERT_NOT_NULL(buffer);
    mcp_buffer_pool_release(NULL, buffer); // Should not crash
    // Release it properly for teardown
    mcp_buffer_pool_release(test_pool, buffer);
}

// Test case: Get buffer size
void test_mcp_buffer_pool_get_size(void) {
    TEST_ASSERT_EQUAL_size_t(TEST_BUFFER_SIZE, mcp_buffer_pool_get_buffer_size(test_pool));
}

// Test case: Get buffer size from NULL pool
void test_mcp_buffer_pool_get_size_null(void) {
    TEST_ASSERT_EQUAL_size_t(0, mcp_buffer_pool_get_buffer_size(NULL));
}

// Note: Thread safety tests are complex and omitted here.
// They would typically involve creating multiple threads that
// concurrently acquire and release buffers, checking for race conditions
// or deadlocks using thread sanitizers or specific test patterns.
