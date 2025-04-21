#include "kmcp_memory.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Test basic memory allocation and freeing
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_basic_allocation() {
    printf("Testing basic memory allocation and freeing...\n");

    // Allocate memory
    void* ptr = KMCP_MEMORY_ALLOC(100);
    if (!ptr) {
        printf("FAIL: Failed to allocate memory\n");
        return 1;
    }

    // Write to memory to ensure it's usable
    memset(ptr, 0xAA, 100);

    // Free memory
    kmcp_memory_free(ptr);

    // Get memory statistics
    kmcp_memory_stats_t stats;
    kmcp_error_t result = kmcp_memory_get_stats(&stats);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to get memory statistics\n");
        return 1;
    }

    // Verify statistics
    if (stats.allocation_count != 1) {
        printf("FAIL: Unexpected allocation count: %zu\n", stats.allocation_count);
        return 1;
    }

    if (stats.free_count != 1) {
        printf("FAIL: Unexpected free count: %zu\n", stats.free_count);
        return 1;
    }

    if (stats.active_allocations != 0) {
        printf("FAIL: Unexpected active allocations: %zu\n", stats.active_allocations);
        return 1;
    }

    printf("PASS: Basic memory allocation and freeing tests passed\n");
    return 0;
}

/**
 * @brief Test memory allocation with tags
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_tagged_allocation() {
    printf("Testing memory allocation with tags...\n");

    // Allocate memory with a tag
    void* ptr = KMCP_MEMORY_ALLOC_TAG(100, "test_tag");
    if (!ptr) {
        printf("FAIL: Failed to allocate memory with tag\n");
        return 1;
    }

    // Free memory
    kmcp_memory_free(ptr);

    printf("PASS: Memory allocation with tags tests passed\n");
    return 0;
}

/**
 * @brief Test calloc functionality
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_calloc() {
    printf("Testing calloc functionality...\n");

    // Allocate and zero memory
    int* ptr = (int*)KMCP_MEMORY_CALLOC(10, sizeof(int));
    if (!ptr) {
        printf("FAIL: Failed to allocate memory with calloc\n");
        return 1;
    }

    // Verify memory is zeroed
    for (int i = 0; i < 10; i++) {
        if (ptr[i] != 0) {
            printf("FAIL: Memory not zeroed at index %d\n", i);
            kmcp_memory_free(ptr);
            return 1;
        }
    }

    // Free memory
    kmcp_memory_free(ptr);

    printf("PASS: Calloc functionality tests passed\n");
    return 0;
}

/**
 * @brief Test realloc functionality
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_realloc() {
    printf("Testing realloc functionality...\n");

    // Allocate memory
    int* ptr = (int*)KMCP_MEMORY_ALLOC(10 * sizeof(int));
    if (!ptr) {
        printf("FAIL: Failed to allocate memory\n");
        return 1;
    }

    // Initialize memory
    for (int i = 0; i < 10; i++) {
        ptr[i] = i;
    }

    // Reallocate memory
    ptr = (int*)KMCP_MEMORY_REALLOC(ptr, 20 * sizeof(int));
    if (!ptr) {
        printf("FAIL: Failed to reallocate memory\n");
        return 1;
    }

    // Verify original data is preserved
    for (int i = 0; i < 10; i++) {
        if (ptr[i] != i) {
            printf("FAIL: Original data not preserved at index %d\n", i);
            kmcp_memory_free(ptr);
            return 1;
        }
    }

    // Initialize new memory
    for (int i = 10; i < 20; i++) {
        ptr[i] = i;
    }

    // Free memory
    kmcp_memory_free(ptr);

    printf("PASS: Realloc functionality tests passed\n");
    return 0;
}

/**
 * @brief Test string duplication
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_strdup() {
    printf("Testing string duplication...\n");

    // Duplicate a string
    const char* original = "Hello, World!";
    char* dup = KMCP_MEMORY_STRDUP(original);
    if (!dup) {
        printf("FAIL: Failed to duplicate string\n");
        return 1;
    }

    // Verify the string was duplicated correctly
    if (strcmp(dup, original) != 0) {
        printf("FAIL: Duplicated string does not match original\n");
        kmcp_memory_free(dup);
        return 1;
    }

    // Free memory
    kmcp_memory_free(dup);

    printf("PASS: String duplication tests passed\n");
    return 0;
}

/**
 * @brief Test memory context functionality
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_memory_context() {
    printf("Testing memory context functionality...\n");

    // Create a memory context
    kmcp_memory_context_t* context = kmcp_memory_context_create("test_context");
    if (!context) {
        printf("FAIL: Failed to create memory context\n");
        return 1;
    }

    // Allocate memory in the context
    void* ptr1 = KMCP_MEMORY_CONTEXT_ALLOC(context, 100);
    if (!ptr1) {
        printf("FAIL: Failed to allocate memory in context\n");
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Allocate and zero memory in the context
    void* ptr2 = KMCP_MEMORY_CONTEXT_CALLOC(context, 10, sizeof(int));
    if (!ptr2) {
        printf("FAIL: Failed to allocate and zero memory in context\n");
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Duplicate a string in the context
    const char* original = "Hello, Context!";
    char* ptr3 = KMCP_MEMORY_CONTEXT_STRDUP(context, original);
    if (!ptr3) {
        printf("FAIL: Failed to duplicate string in context\n");
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_free(context, ptr2);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Verify the string was duplicated correctly
    if (strcmp(ptr3, original) != 0) {
        printf("FAIL: Duplicated string in context does not match original\n");
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_free(context, ptr2);
        kmcp_memory_context_free(context, ptr3);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Get context statistics
    kmcp_memory_stats_t stats;
    kmcp_error_t result = kmcp_memory_context_get_stats(context, &stats);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to get context statistics\n");
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_free(context, ptr2);
        kmcp_memory_context_free(context, ptr3);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Verify statistics
    if (stats.allocation_count != 3) {
        printf("FAIL: Unexpected context allocation count: %zu\n", stats.allocation_count);
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_free(context, ptr2);
        kmcp_memory_context_free(context, ptr3);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    if (stats.active_allocations != 3) {
        printf("FAIL: Unexpected context active allocations: %zu\n", stats.active_allocations);
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_free(context, ptr2);
        kmcp_memory_context_free(context, ptr3);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Print context statistics
    result = kmcp_memory_context_print_stats(context);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to print context statistics\n");
        kmcp_memory_context_free(context, ptr1);
        kmcp_memory_context_free(context, ptr2);
        kmcp_memory_context_free(context, ptr3);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Free memory in the context
    kmcp_memory_context_free(context, ptr1);
    kmcp_memory_context_free(context, ptr2);
    kmcp_memory_context_free(context, ptr3);

    // Get updated context statistics
    result = kmcp_memory_context_get_stats(context, &stats);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to get updated context statistics\n");
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Verify updated statistics
    if (stats.free_count != 3) {
        printf("FAIL: Unexpected context free count: %zu\n", stats.free_count);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    if (stats.active_allocations != 0) {
        printf("FAIL: Unexpected context active allocations after free: %zu\n", stats.active_allocations);
        kmcp_memory_context_destroy(context);
        return 1;
    }

    // Destroy the context
    kmcp_memory_context_destroy(context);

    printf("PASS: Memory context functionality tests passed\n");
    return 0;
}

/**
 * @brief Test memory leak detection
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_leak_detection() {
    printf("Testing memory leak detection...\n");

    // Reset memory statistics
    kmcp_error_t result = kmcp_memory_reset_stats();
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to reset memory statistics\n");
        return 1;
    }

    // Allocate memory without freeing it
    void* ptr = KMCP_MEMORY_ALLOC_TAG(100, "deliberate_leak");
    if (!ptr) {
        printf("FAIL: Failed to allocate memory\n");
        return 1;
    }

    // Get memory statistics
    kmcp_memory_stats_t stats;
    result = kmcp_memory_get_stats(&stats);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to get memory statistics\n");
        kmcp_memory_free(ptr); // Clean up to avoid actual leak
        return 1;
    }

    // Verify statistics
    if (stats.active_allocations != 1) {
        printf("FAIL: Unexpected active allocations: %zu\n", stats.active_allocations);
        kmcp_memory_free(ptr); // Clean up to avoid actual leak
        return 1;
    }

    // Print memory leaks
    result = kmcp_memory_print_leaks();
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to print memory leaks\n");
        kmcp_memory_free(ptr); // Clean up to avoid actual leak
        return 1;
    }

    // Clean up to avoid actual leak
    kmcp_memory_free(ptr);

    printf("PASS: Memory leak detection tests passed\n");
    return 0;
}

/**
 * @brief Main function for memory tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_memory_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }
    
    printf("=== KMCP Memory Tests ===\n");

    int failures = 0;

    // Initialize memory system with full tracking
    kmcp_error_t result = kmcp_memory_init(KMCP_MEMORY_TRACKING_FULL);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to initialize memory system\n");
        return 1;
    }

    // Run tests
    failures += test_basic_allocation();
    failures += test_tagged_allocation();
    failures += test_calloc();
    failures += test_realloc();
    failures += test_strdup();
    failures += test_memory_context();
    failures += test_leak_detection();

    // Print memory statistics
    kmcp_memory_print_stats();

    // Shut down memory system
    result = kmcp_memory_shutdown(true);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to shut down memory system\n");
        failures++;
    }

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", failures);
    }

    return failures;
}

#ifdef STANDALONE_TEST
/**
 * @brief Main function for standalone test
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    return kmcp_memory_test_main();
}
#endif
