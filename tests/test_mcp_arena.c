#include "unity.h"
#include "mcp_arena.h"
#include <string.h>

// Define a test group runner function (called by the main runner)
void run_mcp_arena_tests(void);

// --- Test Cases ---

// Test basic initialization and destruction
void test_arena_init_destroy(void) {
    mcp_arena_t arena;
    mcp_arena_init(&arena, 0); // Use default block size
    TEST_ASSERT_NULL(arena.current_block); // Should start empty
    TEST_ASSERT_EQUAL(MCP_ARENA_DEFAULT_BLOCK_SIZE, arena.default_block_size);
    mcp_arena_destroy(&arena);
    TEST_ASSERT_NULL(arena.current_block); // Should be empty after destroy
}

// Test small allocation using the thread-local arena
void test_arena_small_alloc(void) {
    // Arena is created implicitly on first alloc

    void* ptr1 = mcp_arena_alloc(10);
    TEST_ASSERT_NOT_NULL(ptr1);
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    TEST_ASSERT_NOT_NULL(arena_ptr);
    TEST_ASSERT_NOT_NULL(arena_ptr->current_block);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(10), arena_ptr->current_block->used);

    void* ptr2 = mcp_arena_alloc(20);
    TEST_ASSERT_NOT_NULL(ptr2);
    // Re-get arena pointer in case it changed (shouldn't here)
    arena_ptr = mcp_arena_get_current();
    TEST_ASSERT_NOT_NULL(arena_ptr);
    TEST_ASSERT_NOT_NULL(arena_ptr->current_block); // Should still be same block
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(10) + MCP_ARENA_ALIGN_UP(20), arena_ptr->current_block->used);

    // Write some data to check for overlaps
    memset(ptr1, 0xAA, 10);
    memset(ptr2, 0xBB, 20);

    mcp_arena_destroy_current_thread(); // Clean up thread-local arena
}

// Test allocation that forces a new block using the thread-local arena
void test_arena_new_block_alloc(void) {
    // Arena is created implicitly on first alloc
    // Note: We cannot easily force a specific block size for the implicit arena.
    // This test assumes the default block size is large enough for the first alloc,
    // but not large enough for both. This might need adjustment if default changes.

    void* ptr1 = mcp_arena_alloc(MCP_ARENA_DEFAULT_BLOCK_SIZE - 20); // Allocate most of the default block
    TEST_ASSERT_NOT_NULL(ptr1);
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    TEST_ASSERT_NOT_NULL(arena_ptr);
    mcp_arena_block_t* block1 = arena_ptr->current_block;
    TEST_ASSERT_NOT_NULL(block1);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(MCP_ARENA_DEFAULT_BLOCK_SIZE - 20), block1->used);

    // This allocation should force a new block
    void* ptr2 = mcp_arena_alloc(40);
    TEST_ASSERT_NOT_NULL(ptr2);
    arena_ptr = mcp_arena_get_current(); // Re-get arena pointer
    TEST_ASSERT_NOT_NULL(arena_ptr);
    mcp_arena_block_t* block2 = arena_ptr->current_block;
    TEST_ASSERT_NOT_NULL(block2);
    TEST_ASSERT_NOT_EQUAL(block1, block2); // Should be a new block
    TEST_ASSERT_EQUAL_PTR(block1, block2->next); // Check linked list
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(40), block2->used); // Usage in the new block

    mcp_arena_destroy_current_thread(); // Clean up thread-local arena
}

// Test allocation larger than the default block size using the thread-local arena
void test_arena_large_alloc(void) {
    // Arena is created implicitly on first alloc

    size_t large_size = MCP_ARENA_DEFAULT_BLOCK_SIZE * 2;
    void* ptr = mcp_arena_alloc(large_size);
    TEST_ASSERT_NOT_NULL(ptr);
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    TEST_ASSERT_NOT_NULL(arena_ptr);
    TEST_ASSERT_NOT_NULL(arena_ptr->current_block);
    // The block created should be at least large_size
    TEST_ASSERT_GREATER_OR_EQUAL(MCP_ARENA_ALIGN_UP(large_size), arena_ptr->current_block->size);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(large_size), arena_ptr->current_block->used);

    mcp_arena_destroy_current_thread(); // Clean up thread-local arena
}

// Test thread-local arena reset functionality
void test_arena_reset_current(void) {
    // Arena is created implicitly on first alloc
    void* ptr1 = mcp_arena_alloc(50);
    TEST_ASSERT_NOT_NULL(ptr1);
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    TEST_ASSERT_NOT_NULL(arena_ptr);
    mcp_arena_block_t* block1 = arena_ptr->current_block;
    TEST_ASSERT_NOT_NULL(block1);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(50), block1->used);

    mcp_arena_reset_current_thread();
    arena_ptr = mcp_arena_get_current(); // Re-get arena pointer
    TEST_ASSERT_NOT_NULL(arena_ptr);
    TEST_ASSERT_NOT_NULL(arena_ptr->current_block); // Block should still exist
    TEST_ASSERT_EQUAL(0, arena_ptr->current_block->used); // Usage should be reset

    // Allocate again after reset
    void* ptr2 = mcp_arena_alloc(30);
    TEST_ASSERT_NOT_NULL(ptr2);
    arena_ptr = mcp_arena_get_current(); // Re-get arena pointer
    TEST_ASSERT_NOT_NULL(arena_ptr);
    TEST_ASSERT_EQUAL_PTR(block1, arena_ptr->current_block); // Should reuse the same block
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(30), arena_ptr->current_block->used);
    // Check if pointers are same (start of data section)
    TEST_ASSERT_EQUAL_PTR(ptr1, ptr2);

    mcp_arena_destroy_current_thread(); // Clean up thread-local arena
}

// TODO: Add multi-threaded tests to verify isolation between threads.
// This requires platform-specific thread creation (pthreads/windows threads).

// --- Test Group Runner ---

// This function is called by the main test runner (`test_runner.c`)
void run_mcp_arena_tests(void) {
    RUN_TEST(test_arena_init_destroy); // Test manual management
    RUN_TEST(test_arena_small_alloc); // Test implicit TLS alloc
    RUN_TEST(test_arena_new_block_alloc); // Test implicit TLS alloc
    RUN_TEST(test_arena_large_alloc); // Test implicit TLS alloc
    RUN_TEST(test_arena_reset_current); // Test implicit TLS reset
    // Add more tests for mcp_arena here (e.g., multi-threaded tests)
}
