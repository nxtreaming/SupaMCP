#include "unity.h"
#include "mcp_arena.h" // Include the header for the code under test
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

// Test small allocation within the first block
void test_arena_small_alloc(void) {
    mcp_arena_t arena;
    mcp_arena_init(&arena, 1024); // Smaller block size for testing

    void* ptr1 = mcp_arena_alloc(&arena, 10);
    TEST_ASSERT_NOT_NULL(ptr1);
    TEST_ASSERT_NOT_NULL(arena.current_block);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(10), arena.current_block->used);

    void* ptr2 = mcp_arena_alloc(&arena, 20);
    TEST_ASSERT_NOT_NULL(ptr2);
    TEST_ASSERT_EQUAL_PTR(arena.current_block, arena.current_block); // Should still be same block
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(10) + MCP_ARENA_ALIGN_UP(20), arena.current_block->used);

    // Write some data to check for overlaps
    memset(ptr1, 0xAA, 10);
    memset(ptr2, 0xBB, 20);

    mcp_arena_destroy(&arena);
}

// Test allocation that forces a new block
void test_arena_new_block_alloc(void) {
    mcp_arena_t arena;
    mcp_arena_init(&arena, 64); // Very small block size

    void* ptr1 = mcp_arena_alloc(&arena, 40);
    TEST_ASSERT_NOT_NULL(ptr1);
    mcp_arena_block_t* block1 = arena.current_block;
    TEST_ASSERT_NOT_NULL(block1);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(40), block1->used);

    // This allocation should exceed the first block's remaining space
    void* ptr2 = mcp_arena_alloc(&arena, 40);
    TEST_ASSERT_NOT_NULL(ptr2);
    mcp_arena_block_t* block2 = arena.current_block;
    TEST_ASSERT_NOT_NULL(block2);
    TEST_ASSERT_NOT_EQUAL(block1, block2); // Should be a new block
    TEST_ASSERT_EQUAL_PTR(block1, block2->next); // Check linked list
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(40), block2->used); // Usage in the new block

    mcp_arena_destroy(&arena);
}

// Test allocation larger than the default block size
void test_arena_large_alloc(void) {
    mcp_arena_t arena;
    mcp_arena_init(&arena, 1024);

    size_t large_size = 2048;
    void* ptr = mcp_arena_alloc(&arena, large_size);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_NOT_NULL(arena.current_block);
    // The block created should be at least large_size
    TEST_ASSERT_GREATER_OR_EQUAL(MCP_ARENA_ALIGN_UP(large_size), arena.current_block->size);
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(large_size), arena.current_block->used);

    mcp_arena_destroy(&arena);
}

// Test arena reset functionality
void test_arena_reset(void) {
     mcp_arena_t arena;
    mcp_arena_init(&arena, 128);

    void* ptr1 = mcp_arena_alloc(&arena, 50);
    TEST_ASSERT_NOT_NULL(ptr1);
    mcp_arena_block_t* block1 = arena.current_block;
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(50), block1->used);

    mcp_arena_reset(&arena);
    TEST_ASSERT_NOT_NULL(arena.current_block); // Block should still exist
    TEST_ASSERT_EQUAL(0, arena.current_block->used); // Usage should be reset

    // Allocate again after reset
    void* ptr2 = mcp_arena_alloc(&arena, 30);
    TEST_ASSERT_NOT_NULL(ptr2);
    TEST_ASSERT_EQUAL_PTR(block1, arena.current_block); // Should reuse the same block
    TEST_ASSERT_EQUAL(MCP_ARENA_ALIGN_UP(30), arena.current_block->used);
    // Check if pointers are same (start of data section)
    TEST_ASSERT_EQUAL_PTR(ptr1, ptr2);

    mcp_arena_destroy(&arena);
}


// --- Test Group Runner ---

// This function is called by the main test runner (`test_runner.c`)
void run_mcp_arena_tests(void) {
    RUN_TEST(test_arena_init_destroy);
    RUN_TEST(test_arena_small_alloc);
    RUN_TEST(test_arena_new_block_alloc);
    RUN_TEST(test_arena_large_alloc);
    RUN_TEST(test_arena_reset);
    // Add more tests for mcp_arena here
}
