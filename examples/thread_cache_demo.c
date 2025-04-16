/**
 * @file thread_cache_demo.c
 * @brief Demonstrates the thread-local cache features, including adaptive sizing
 */

#include "mcp_thread_cache.h"
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
void print_thread_cache_stats(void);
void run_allocation_test(size_t size, int pattern, int iterations);
void demonstrate_adaptive_sizing(void);
void demonstrate_custom_configuration(void);
void demonstrate_multiple_threads(void);

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Thread Cache Demo starting");

    // Initialize memory pool system with default sizes
    if (!mcp_memory_pool_system_init(64, 32, 16)) {
        mcp_log_error("Failed to initialize memory pool system");
        return 1;
    }

    // Initialize thread-local cache with default settings
    if (!mcp_thread_cache_init()) {
        mcp_log_error("Failed to initialize thread-local cache");
        return 1;
    }

    printf("Thread Cache Demo\n");
    printf("=================\n\n");

    // Print initial statistics
    printf("Initial Thread Cache Statistics:\n");
    print_thread_cache_stats();

    // Demonstrate basic allocation and freeing
    printf("\nRunning basic allocation tests...\n");
    run_allocation_test(SMALL_SIZE, PATTERN_SEQUENTIAL, 1);
    print_thread_cache_stats();

    // Demonstrate different allocation patterns
    printf("\nRunning different allocation patterns...\n");
    run_allocation_test(SMALL_SIZE, PATTERN_ALTERNATE, 1);
    print_thread_cache_stats();

    run_allocation_test(MEDIUM_SIZE, PATTERN_RANDOM, 1);
    print_thread_cache_stats();

    // Demonstrate adaptive sizing
    printf("\nDemonstrating adaptive cache sizing...\n");
    demonstrate_adaptive_sizing();

    // Demonstrate custom configuration
    printf("\nDemonstrating custom cache configuration...\n");
    demonstrate_custom_configuration();

    // Clean up
    mcp_thread_cache_cleanup();
    mcp_memory_pool_system_cleanup();
    mcp_log_info("Thread Cache Demo completed");

    return 0;
}

/**
 * @brief Prints the current thread cache statistics
 */
void print_thread_cache_stats(void) {
    mcp_thread_cache_stats_t stats;
    if (mcp_thread_cache_get_stats(&stats)) {
        printf("Thread Cache Statistics:\n");
        printf("  Small cache count: %zu/%zu\n", stats.small_cache_count, stats.small_max_size);
        printf("  Medium cache count: %zu/%zu\n", stats.medium_cache_count, stats.medium_max_size);
        printf("  Large cache count: %zu/%zu\n", stats.large_cache_count, stats.large_max_size);
        printf("  Cache hits: %zu\n", stats.cache_hits);
        printf("  Cache misses (small): %zu\n", stats.misses_small);
        printf("  Cache misses (medium): %zu\n", stats.misses_medium);
        printf("  Cache misses (large): %zu\n", stats.misses_large);
        printf("  Cache misses (other): %zu\n", stats.misses_other);
        printf("  Cache flushes: %zu\n", stats.cache_flushes);
        printf("  Hit ratio: %.2f%%\n", stats.hit_ratio * 100.0);
        printf("  Adaptive sizing: %s\n", stats.adaptive_sizing ? "enabled" : "disabled");
    } else {
        printf("Failed to get thread cache statistics\n");
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
            blocks[i] = mcp_thread_cache_alloc(size);
            if (blocks[i]) {
                // Write some data to ensure the memory is usable
                memset(blocks[i], i & 0xFF, size);

                // Free immediately if sequential pattern
                if (pattern == PATTERN_SEQUENTIAL) {
                    mcp_thread_cache_free(blocks[i], size);
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
                    mcp_thread_cache_free(blocks[index], size);
                    blocks[index] = NULL;
                }
            }
        }

        // Free remaining blocks for non-sequential patterns
        if (pattern != PATTERN_SEQUENTIAL) {
            for (int i = 0; i < NUM_ALLOCATIONS; i++) {
                if (blocks[i]) {
                    mcp_thread_cache_free(blocks[i], size);
                    blocks[i] = NULL;
                }
            }
        }
    }

    mcp_log_info("Completed allocation test with size %zu bytes (pattern %d)", size, pattern);
}

/**
 * @brief Demonstrates adaptive cache sizing
 */
void demonstrate_adaptive_sizing(void) {
    // Enable adaptive sizing
    mcp_log_info("Enabling adaptive sizing");
    mcp_thread_cache_enable_adaptive_sizing(true);

    // Configure with custom thresholds
    mcp_thread_cache_config_t config = {
        .small_cache_size = 8,
        .medium_cache_size = 4,
        .large_cache_size = 2,
        .adaptive_sizing = true,
        .growth_threshold = 0.7,  // Grow cache if hit ratio > 70%
        .shrink_threshold = 0.3,  // Shrink cache if hit ratio < 30%
        .min_cache_size = 2,
        .max_cache_size = 32
    };
    mcp_thread_cache_configure(&config);

    printf("Initial configuration:\n");
    print_thread_cache_stats();

    // Run tests with different patterns to trigger cache size adjustments
    printf("\nRunning tests to demonstrate adaptive sizing...\n");

    // First test: high hit ratio for small blocks
    printf("\nTest 1: Creating high hit ratio for small blocks...\n");
    for (int i = 0; i < 5; i++) {
        run_allocation_test(SMALL_SIZE, PATTERN_ALTERNATE, 1);
    }
    mcp_thread_cache_adjust_size();  // Force adjustment
    printf("After high hit ratio for small blocks:\n");
    print_thread_cache_stats();

    // Second test: low hit ratio for medium blocks
    printf("\nTest 2: Creating low hit ratio for medium blocks...\n");
    mcp_thread_cache_flush();  // Flush cache to start fresh
    for (int i = 0; i < 5; i++) {
        run_allocation_test(MEDIUM_SIZE, PATTERN_SEQUENTIAL, 1);  // Sequential creates low hit ratio
    }
    mcp_thread_cache_adjust_size();  // Force adjustment
    printf("After low hit ratio for medium blocks:\n");
    print_thread_cache_stats();

    // Third test: mixed workload
    printf("\nTest 3: Mixed workload with random pattern...\n");
    mcp_thread_cache_flush();  // Flush cache to start fresh
    for (int i = 0; i < 5; i++) {
        run_allocation_test(LARGE_SIZE, PATTERN_RANDOM, 1);
    }
    mcp_thread_cache_adjust_size();  // Force adjustment
    printf("After mixed workload:\n");
    print_thread_cache_stats();

    // Disable adaptive sizing
    mcp_log_info("Disabling adaptive sizing");
    mcp_thread_cache_enable_adaptive_sizing(false);
}

/**
 * @brief Demonstrates custom cache configuration
 */
void demonstrate_custom_configuration(void) {
    // Flush cache and reset statistics
    mcp_thread_cache_flush();

    // Configure with custom settings
    mcp_thread_cache_config_t config = {
        .small_cache_size = 32,    // Large small cache
        .medium_cache_size = 16,   // Medium medium cache
        .large_cache_size = 8,     // Small large cache
        .adaptive_sizing = false,  // Disable adaptive sizing
        .growth_threshold = 0.8,
        .shrink_threshold = 0.2,
        .min_cache_size = 4,
        .max_cache_size = 64
    };
    mcp_thread_cache_configure(&config);

    printf("Custom configuration applied:\n");
    print_thread_cache_stats();

    // Run a test to see how the custom configuration performs
    printf("\nRunning test with custom configuration...\n");
    run_allocation_test(SMALL_SIZE, PATTERN_ALTERNATE, 1);
    printf("After test with custom configuration:\n");
    print_thread_cache_stats();
}
