/**
 * @file memory_pool_stats_demo.c
 * @brief Demonstrates the memory pool statistics and monitoring features
 */

#include "mcp_memory_pool.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Define allocation sizes for testing
#define SMALL_SIZE 128
#define MEDIUM_SIZE 512
#define LARGE_SIZE 2048

// Define allocation patterns
#define PATTERN_SEQUENTIAL 0  // Allocate and free sequentially
#define PATTERN_ALTERNATE 1   // Allocate all, then free all
#define PATTERN_RANDOM 2      // Random allocations and frees

// Number of allocations for each test
#define NUM_ALLOCATIONS 1000

// Function prototypes
void print_pool_stats(void);
void run_allocation_test(size_t size, int pattern, int iterations);
void demonstrate_memory_tracking(void);
void demonstrate_memory_usage_patterns(void);
void demonstrate_memory_leak_detection(void);

// Structure to hold memory block pointers for leak simulation
typedef struct {
    void* blocks[100];
    int count;
} leak_simulation_t;

// Global leak simulation for demonstration
leak_simulation_t g_leak_sim = {0};

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Memory Pool Statistics Demo starting");

    // Initialize memory pool system with default sizes
    if (!mcp_memory_pool_system_init(64, 32, 16)) {
        mcp_log_error("Failed to initialize memory pool system");
        return 1;
    }

    printf("Memory Pool Statistics Demo\n");
    printf("===========================\n\n");

    // Print initial statistics
    printf("Initial Memory Pool Statistics:\n");
    print_pool_stats();

    // Demonstrate basic allocation and freeing
    printf("\nRunning basic allocation tests...\n");
    run_allocation_test(SMALL_SIZE, PATTERN_SEQUENTIAL, 1);
    print_pool_stats();

    // Demonstrate memory tracking
    printf("\nDemonstrating memory usage tracking...\n");
    demonstrate_memory_tracking();

    // Demonstrate different memory usage patterns
    printf("\nDemonstrating different memory usage patterns...\n");
    demonstrate_memory_usage_patterns();

    // Demonstrate memory leak detection
    printf("\nDemonstrating memory leak detection...\n");
    demonstrate_memory_leak_detection();

    // Clean up
    mcp_memory_pool_system_cleanup();
    mcp_log_info("Memory Pool Statistics Demo completed");

    return 0;
}

/**
 * @brief Prints the current memory pool statistics
 */
void print_pool_stats(void) {
    mcp_memory_pool_stats_t stats;

    printf("Small Pool Statistics:\n");
    if (mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &stats)) {
        printf("  Total blocks: %zu\n", stats.total_blocks);
        printf("  Free blocks: %zu\n", stats.free_blocks);
        printf("  Allocated blocks: %zu\n", stats.allocated_blocks);
        printf("  Block size: %zu bytes\n", stats.block_size);
        printf("  Total memory: %zu bytes\n", stats.total_memory);
        printf("  Peak usage: %zu blocks\n", stats.peak_usage);
    } else {
        printf("  Failed to get statistics\n");
    }

    printf("\nMedium Pool Statistics:\n");
    if (mcp_pool_get_stats(MCP_POOL_SIZE_MEDIUM, &stats)) {
        printf("  Total blocks: %zu\n", stats.total_blocks);
        printf("  Free blocks: %zu\n", stats.free_blocks);
        printf("  Allocated blocks: %zu\n", stats.allocated_blocks);
        printf("  Block size: %zu bytes\n", stats.block_size);
        printf("  Total memory: %zu bytes\n", stats.total_memory);
        printf("  Peak usage: %zu blocks\n", stats.peak_usage);
    } else {
        printf("  Failed to get statistics\n");
    }

    printf("\nLarge Pool Statistics:\n");
    if (mcp_pool_get_stats(MCP_POOL_SIZE_LARGE, &stats)) {
        printf("  Total blocks: %zu\n", stats.total_blocks);
        printf("  Free blocks: %zu\n", stats.free_blocks);
        printf("  Allocated blocks: %zu\n", stats.allocated_blocks);
        printf("  Block size: %zu bytes\n", stats.block_size);
        printf("  Total memory: %zu bytes\n", stats.total_memory);
        printf("  Peak usage: %zu blocks\n", stats.peak_usage);
    } else {
        printf("  Failed to get statistics\n");
    }
}

/**
 * @brief Runs an allocation test with the specified size and pattern
 *
 * @param size Size of each allocation
 * @param pattern Allocation pattern (PATTERN_SEQUENTIAL, PATTERN_ALTERNATE, PATTERN_RANDOM)
 * @param iterations Number of times to repeat the test
 */
void run_allocation_test(size_t size, int pattern, int iterations) {
    printf("\nRunning allocation test with size %zu bytes (pattern %d)...\n", size, pattern);
    mcp_log_info("Starting allocation test with size %zu bytes (pattern %d)", size, pattern);

    srand((unsigned int)time(NULL));

    for (int iter = 0; iter < iterations; iter++) {
        void* blocks[NUM_ALLOCATIONS] = {NULL};

        // Allocate blocks according to pattern
        for (int i = 0; i < NUM_ALLOCATIONS; i++) {
            blocks[i] = mcp_pool_alloc(size);
            if (blocks[i]) {
                // Write some data to ensure the memory is usable
                memset(blocks[i], i & 0xFF, size);

                // Free immediately if sequential pattern
                if (pattern == PATTERN_SEQUENTIAL) {
                    mcp_pool_free(blocks[i]);
                    blocks[i] = NULL;
                }
            } else {
                mcp_log_error("Allocation failed at index %d", i);
                printf("Allocation failed at index %d\n", i);
            }

            // For random pattern, randomly free some blocks
            if (pattern == PATTERN_RANDOM && (rand() % 2 == 0)) {
                int index = rand() % NUM_ALLOCATIONS;
                if (blocks[index]) {
                    mcp_pool_free(blocks[index]);
                    blocks[index] = NULL;
                }
            }
        }

        // Free remaining blocks for non-sequential patterns
        if (pattern != PATTERN_SEQUENTIAL) {
            for (int i = 0; i < NUM_ALLOCATIONS; i++) {
                if (blocks[i]) {
                    mcp_pool_free(blocks[i]);
                    blocks[i] = NULL;
                }
            }
        }
    }

    mcp_log_info("Completed allocation test with size %zu bytes (pattern %d)", size, pattern);
}

/**
 * @brief Demonstrates memory usage tracking features
 */
void demonstrate_memory_tracking(void) {
    // Note: In this simplified version, we're only using the basic memory pool statistics
    // since the enhanced tracking features are not yet implemented

    // Run tests with different patterns to generate tracking data
    printf("\nRunning tests to generate tracking data...\n");
    run_allocation_test(SMALL_SIZE, PATTERN_SEQUENTIAL, 1);
    run_allocation_test(MEDIUM_SIZE, PATTERN_ALTERNATE, 1);
    run_allocation_test(LARGE_SIZE, PATTERN_RANDOM, 1);

    // Get and print basic usage statistics
    printf("\nMemory Pool Usage Statistics:\n");
    mcp_memory_pool_stats_t small_stats, medium_stats, large_stats;

    if (mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &small_stats)) {
        printf("\nSmall Pool Statistics:\n");
        printf("  Total blocks: %zu\n", small_stats.total_blocks);
        printf("  Free blocks: %zu\n", small_stats.free_blocks);
        printf("  Allocated blocks: %zu\n", small_stats.allocated_blocks);
        printf("  Block size: %zu bytes\n", small_stats.block_size);
        printf("  Total memory: %zu bytes\n", small_stats.total_memory);
        printf("  Peak usage: %zu blocks\n", small_stats.peak_usage);
    }

    if (mcp_pool_get_stats(MCP_POOL_SIZE_MEDIUM, &medium_stats)) {
        printf("\nMedium Pool Statistics:\n");
        printf("  Total blocks: %zu\n", medium_stats.total_blocks);
        printf("  Free blocks: %zu\n", medium_stats.free_blocks);
        printf("  Allocated blocks: %zu\n", medium_stats.allocated_blocks);
        printf("  Block size: %zu bytes\n", medium_stats.block_size);
        printf("  Total memory: %zu bytes\n", medium_stats.total_memory);
        printf("  Peak usage: %zu blocks\n", medium_stats.peak_usage);
    }

    if (mcp_pool_get_stats(MCP_POOL_SIZE_LARGE, &large_stats)) {
        printf("\nLarge Pool Statistics:\n");
        printf("  Total blocks: %zu\n", large_stats.total_blocks);
        printf("  Free blocks: %zu\n", large_stats.free_blocks);
        printf("  Allocated blocks: %zu\n", large_stats.allocated_blocks);
        printf("  Block size: %zu bytes\n", large_stats.block_size);
        printf("  Total memory: %zu bytes\n", large_stats.total_memory);
        printf("  Peak usage: %zu blocks\n", large_stats.peak_usage);
    }

    // Note: The following features would be part of the enhanced memory tracking system
    // that we would implement as part of task 1 in t5.md
    /*
    // Enable tracking for all pools
    mcp_pool_enable_tracking(MCP_POOL_SIZE_SMALL, true);

    // Dump statistics to a file
    mcp_pool_dump_stats("memory_pool_stats.txt", true);

    // Get enhanced usage statistics
    mcp_memory_usage_stats_t usage_stats;
    mcp_pool_get_usage_stats(MCP_POOL_SIZE_SMALL, &usage_stats);

    // Disable tracking
    mcp_pool_enable_tracking(MCP_POOL_SIZE_SMALL, false);
    */
}

/**
 * @brief Demonstrates different memory usage patterns and their impact on statistics
 */
void demonstrate_memory_usage_patterns(void) {
    // Pattern 1: Short-lived allocations (allocate and free immediately)
    printf("\nPattern 1: Short-lived allocations\n");
    for (int i = 0; i < 100; i++) {
        void* ptr = mcp_pool_alloc(SMALL_SIZE);
        if (ptr) {
            memset(ptr, i & 0xFF, SMALL_SIZE);
            mcp_pool_free(ptr);
        }
    }

    // Get and print basic statistics
    mcp_memory_pool_stats_t small_stats;
    if (mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &small_stats)) {
        printf("  Total blocks: %zu\n", small_stats.total_blocks);
        printf("  Allocated blocks: %zu\n", small_stats.allocated_blocks);
        printf("  Peak usage: %zu blocks\n", small_stats.peak_usage);
    }

    // Pattern 2: Medium-lived allocations (keep for a while, then free in batches)
    printf("\nPattern 2: Medium-lived allocations\n");
    void* medium_blocks[50] = {NULL};
    for (int i = 0; i < 50; i++) {
        medium_blocks[i] = mcp_pool_alloc(SMALL_SIZE);
        if (medium_blocks[i]) {
            memset(medium_blocks[i], i & 0xFF, SMALL_SIZE);
        }
    }

    // Simulate some processing time
    for (int i = 0; i < 1000000; i++) {
        // Just waste some CPU cycles
        volatile int dummy = i * i;
        (void)dummy;
    }

    // Free the blocks
    for (int i = 0; i < 50; i++) {
        if (medium_blocks[i]) {
            mcp_pool_free(medium_blocks[i]);
            medium_blocks[i] = NULL;
        }
    }

    // Get and print updated statistics
    if (mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &small_stats)) {
        printf("  Total blocks: %zu\n", small_stats.total_blocks);
        printf("  Allocated blocks: %zu\n", small_stats.allocated_blocks);
        printf("  Peak usage: %zu blocks\n", small_stats.peak_usage);
    }

    // Pattern 3: Mixed allocation sizes
    printf("\nPattern 3: Mixed allocation sizes\n");
    for (int i = 0; i < 100; i++) {
        // Vary the size to hit different pools
        size_t size = (i % 3 == 0) ? SMALL_SIZE :
                      (i % 3 == 1) ? MEDIUM_SIZE : LARGE_SIZE;

        void* ptr = mcp_pool_alloc(size);
        if (ptr) {
            memset(ptr, i & 0xFF, size);
            mcp_pool_free(ptr);
        }
    }

    // Get and print statistics for all pools
    printf("\nFinal Statistics After All Patterns:\n");
    print_pool_stats();
}

/**
 * @brief Simulates a memory leak by not freeing some allocations
 */
void add_to_leak_simulation(void* ptr) {
    if (g_leak_sim.count < 100) {
        g_leak_sim.blocks[g_leak_sim.count++] = ptr;
    }
}

/**
 * @brief Demonstrates memory leak detection
 */
void demonstrate_memory_leak_detection(void) {
    // Initial statistics
    mcp_memory_pool_stats_t initial_stats;
    mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &initial_stats);
    printf("\nInitial allocated blocks: %zu\n", initial_stats.allocated_blocks);

    // Allocate some memory and "forget" to free it (simulating leaks)
    printf("\nAllocating memory without freeing (simulating leaks)...\n");
    for (int i = 0; i < 20; i++) {
        void* ptr = mcp_pool_alloc(SMALL_SIZE);
        if (ptr) {
            memset(ptr, i & 0xFF, SMALL_SIZE);
            // Simulate a leak by not freeing and storing the pointer
            add_to_leak_simulation(ptr);
        }
    }

    // Check statistics after leaks
    mcp_memory_pool_stats_t after_leak_stats;
    mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &after_leak_stats);
    printf("Allocated blocks after leaks: %zu\n", after_leak_stats.allocated_blocks);
    printf("Detected leak: %zu blocks\n",
           after_leak_stats.allocated_blocks - initial_stats.allocated_blocks);

    // Note: In a full implementation, we would dump statistics to a file here
    printf("\nNote: In a full implementation, we would dump detailed leak information to a file\n");

    // Clean up the "leaked" memory to avoid actual leaks in the demo
    printf("\nCleaning up simulated leaks...\n");
    for (int i = 0; i < g_leak_sim.count; i++) {
        if (g_leak_sim.blocks[i]) {
            mcp_pool_free(g_leak_sim.blocks[i]);
            g_leak_sim.blocks[i] = NULL;
        }
    }
    g_leak_sim.count = 0;

    // Final statistics after cleanup
    mcp_memory_pool_stats_t final_stats;
    mcp_pool_get_stats(MCP_POOL_SIZE_SMALL, &final_stats);
    printf("Allocated blocks after cleanup: %zu\n", final_stats.allocated_blocks);
}
