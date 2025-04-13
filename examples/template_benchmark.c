#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mcp_template.h>
#include <mcp_template_optimized.h>
#include <mcp_json.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <mcp_thread_local.h>
#include <mcp_memory_pool.h>
#include <mcp_memory_constants.h>

// Number of iterations for each benchmark
#define ITERATIONS 100000

// Benchmark templates
const char* templates[] = {
    "example://{name}/resource",
    "example://{name}/{version?}",
    "example://{name}/{version=1.0}",
    "example://{name}/{id:int}",
    "example://{name}/{version:float=1.0}/{id:int?}",
    "example://{name}/{type:pattern:i*e}",
    "example://{name}/{version:float?}/{id:int=0}"
};

// Benchmark URIs
const char* uris[] = {
    "example://test/resource",
    "example://test/",
    "example://test/2.0",
    "example://test/123",
    "example://test/2.5/42",
    "example://test/image",
    "example://test/2.5/123"
};

// Number of templates
#define TEMPLATE_COUNT (sizeof(templates) / sizeof(templates[0]))

/**
 * @brief Benchmark the original template matching function
 */
double benchmark_original_matching() {
    clock_t start = clock();

    for (int i = 0; i < ITERATIONS; i++) {
        for (size_t j = 0; j < TEMPLATE_COUNT; j++) {
            mcp_template_matches(uris[j], templates[j]);
        }
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

/**
 * @brief Benchmark the optimized template matching function
 */
double benchmark_optimized_matching() {
    clock_t start = clock();

    for (int i = 0; i < ITERATIONS; i++) {
        for (size_t j = 0; j < TEMPLATE_COUNT; j++) {
            mcp_template_matches_optimized(uris[j], templates[j]);
        }
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

/**
 * @brief Benchmark the original parameter extraction function
 */
double benchmark_original_extraction() {
    clock_t start = clock();

    for (int i = 0; i < ITERATIONS; i++) {
        for (size_t j = 0; j < TEMPLATE_COUNT; j++) {
            mcp_json_t* params = mcp_template_extract_params(uris[j], templates[j]);
            if (params != NULL) {
                mcp_json_destroy(params);
            }
        }
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

/**
 * @brief Benchmark the optimized parameter extraction function
 */
double benchmark_optimized_extraction() {
    clock_t start = clock();

    for (int i = 0; i < ITERATIONS; i++) {
        for (size_t j = 0; j < TEMPLATE_COUNT; j++) {
            mcp_json_t* params = mcp_template_extract_params_optimized(uris[j], templates[j]);
            if (params != NULL) {
                mcp_json_destroy(params);
            }
        }
    }

    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

int main() {
    printf("Template Benchmark\n");
    printf("=================\n\n");

    // Initialize memory pool system
    if (!mcp_memory_pool_system_init(64, 32, 16)) {
        printf("Failed to initialize memory pool system\n");
        return 1;
    }

    // Initialize thread-local cache
    if (!mcp_thread_cache_init()) {
        printf("Failed to initialize thread-local cache\n");
        mcp_memory_pool_system_cleanup();
        return 1;
    }

    // Initialize thread-local arena
    if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
        printf("Failed to initialize thread-local arena\n");
        mcp_thread_cache_cleanup();
        mcp_memory_pool_system_cleanup();
        return 1;
    }

    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena == NULL) {
        printf("Failed to get thread-local arena\n");
        mcp_thread_cache_cleanup();
        mcp_memory_pool_system_cleanup();
        return 1;
    }

    // Warm up the cache
    for (size_t j = 0; j < TEMPLATE_COUNT; j++) {
        mcp_template_matches_optimized(uris[j], templates[j]);
        mcp_json_t* params = mcp_template_extract_params_optimized(uris[j], templates[j]);
        if (params != NULL) {
            mcp_json_destroy(params);
        }
    }

    // Benchmark template matching
    printf("Template Matching Benchmark (%d iterations):\n", ITERATIONS);
    double original_matching_time = benchmark_original_matching();
    double optimized_matching_time = benchmark_optimized_matching();
    printf("  Original: %.6f seconds\n", original_matching_time);
    printf("  Optimized: %.6f seconds\n", optimized_matching_time);
    printf("  Speedup: %.2fx\n\n", original_matching_time / optimized_matching_time);

    // Benchmark parameter extraction
    printf("Parameter Extraction Benchmark (%d iterations):\n", ITERATIONS);
    double original_extraction_time = benchmark_original_extraction();
    double optimized_extraction_time = benchmark_optimized_extraction();
    printf("  Original: %.6f seconds\n", original_extraction_time);
    printf("  Optimized: %.6f seconds\n", optimized_extraction_time);
    printf("  Speedup: %.2fx\n\n", original_extraction_time / optimized_extraction_time);

    // Clean up
    mcp_template_cache_cleanup();
    mcp_thread_cache_cleanup();
    mcp_memory_pool_system_cleanup();

    return 0;
}
