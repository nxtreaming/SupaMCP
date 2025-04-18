#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mcp_template.h>
#include <mcp_template_optimized.h>
#include <mcp_json.h>
#include <mcp_json_utils.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <mcp_thread_local.h>
#include <mcp_memory_pool.h>
#include <mcp_memory_constants.h>

// Number of iterations for each benchmark
#define ITERATIONS 100000

// Template complexity levels
typedef enum {
    TEMPLATE_SIMPLE,       // example://{name}
    TEMPLATE_MEDIUM,       // example://{user}/posts/{post_id}
    TEMPLATE_COMPLEX,      // example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}
    TEMPLATE_VERY_COMPLEX  // example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}/{sort:pattern:date*}/{filter:pattern:all*}/{page:int=1}/{limit:int=10}
} template_complexity_t;

// Benchmark functions
double benchmark_template_matching(const char* uri, const char* template_uri, int iterations);
double benchmark_template_matching_optimized(const char* uri, const char* template_uri, int iterations);
double benchmark_template_extract_params(const char* uri, const char* template_uri, int iterations);
double benchmark_template_extract_params_optimized(const char* uri, const char* template_uri, int iterations);
double benchmark_template_expand(const char* template_uri, const mcp_json_t* params, int iterations);

// Helper function to get a template and URI for a given complexity level
void get_template_and_uri(template_complexity_t complexity, char** template_uri, char** uri, mcp_json_t** params);

int main() {
    printf("Advanced Template Benchmark\n");
    printf("==========================\n\n");

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

    // Run benchmarks for different complexity levels
    template_complexity_t complexity_levels[] = {
        TEMPLATE_SIMPLE,
        TEMPLATE_MEDIUM,
        TEMPLATE_COMPLEX,
        TEMPLATE_VERY_COMPLEX
    };

    const char* complexity_names[] = {
        "Simple",
        "Medium",
        "Complex",
        "Very Complex"
    };

    for (int i = 0; i < sizeof(complexity_levels) / sizeof(complexity_levels[0]); i++) {
        template_complexity_t complexity = complexity_levels[i];
        const char* complexity_name = complexity_names[i];

        char* template_uri = NULL;
        char* uri = NULL;
        mcp_json_t* params = NULL;

        get_template_and_uri(complexity, &template_uri, &uri, &params);

        printf("Template Complexity: %s\n", complexity_name);
        printf("Template: %s\n", template_uri);
        printf("URI: %s\n\n", uri);

        // Template Matching Benchmark
        printf("Template Matching Benchmark (%d iterations):\n", ITERATIONS);
        double time_original = benchmark_template_matching(uri, template_uri, ITERATIONS);
        double time_optimized = benchmark_template_matching_optimized(uri, template_uri, ITERATIONS);
        double speedup = time_original / time_optimized;
        printf("  Original: %.6f seconds\n", time_original);
        printf("  Optimized: %.6f seconds\n", time_optimized);
        printf("  Speedup: %.2fx\n\n", speedup);

        // Parameter Extraction Benchmark
        printf("Parameter Extraction Benchmark (%d iterations):\n", ITERATIONS);
        time_original = benchmark_template_extract_params(uri, template_uri, ITERATIONS);
        time_optimized = benchmark_template_extract_params_optimized(uri, template_uri, ITERATIONS);
        speedup = time_original / time_optimized;
        printf("  Original: %.6f seconds\n", time_original);
        printf("  Optimized: %.6f seconds\n", time_optimized);
        printf("  Speedup: %.2fx\n\n", speedup);

        // Template Expansion Benchmark
        printf("Template Expansion Benchmark (%d iterations):\n", ITERATIONS);
        double time_expand = benchmark_template_expand(template_uri, params, ITERATIONS);
        printf("  Time: %.6f seconds\n\n", time_expand);

        // Clean up
        mcp_json_destroy(params);
        free(template_uri);
        free(uri);

        // Reset the template cache between complexity levels
        mcp_template_cache_cleanup();

        printf("--------------------------------------------------\n\n");
    }

    // Clean up
    mcp_template_cache_cleanup();
    mcp_thread_cache_cleanup();
    mcp_memory_pool_system_cleanup();

    return 0;
}

double benchmark_template_matching(const char* uri, const char* template_uri, int iterations) {
    clock_t start = clock();

    for (int i = 0; i < iterations; i++) {
        int result = mcp_template_matches(uri, template_uri);
        if (!result) {
            printf("Error: Template matching failed\n");
            return 0.0;
        }
    }

    clock_t end = clock();
    return (double)(end - start) / CLOCKS_PER_SEC;
}

double benchmark_template_matching_optimized(const char* uri, const char* template_uri, int iterations) {
    // First call to initialize the cache
    int init_result = mcp_template_matches_optimized(uri, template_uri);
    if (!init_result) {
        printf("Error: Optimized template matching failed in initial test\n");
        return 0.0;
    }

    clock_t start = clock();

    for (int i = 0; i < iterations; i++) {
        int result = mcp_template_matches_optimized(uri, template_uri);
        if (!result) {
            printf("Error: Optimized template matching failed\n");
            return 0.0;
        }
    }

    clock_t end = clock();
    return (double)(end - start) / CLOCKS_PER_SEC;
}

double benchmark_template_extract_params(const char* uri, const char* template_uri, int iterations) {
    clock_t start = clock();

    for (int i = 0; i < iterations; i++) {
        mcp_json_t* params = mcp_template_extract_params(uri, template_uri);
        if (params == NULL) {
            printf("Error: Parameter extraction failed\n");
            return 0.0;
        }
        mcp_json_destroy(params);
    }

    clock_t end = clock();
    return (double)(end - start) / CLOCKS_PER_SEC;
}

double benchmark_template_extract_params_optimized(const char* uri, const char* template_uri, int iterations) {
    // First call to initialize the cache
    mcp_json_t* init_params = mcp_template_extract_params_optimized(uri, template_uri);
    if (init_params == NULL) {
        printf("Error: Optimized parameter extraction failed in initial test\n");
        return 0.0;
    }
    mcp_json_destroy(init_params);

    clock_t start = clock();

    for (int i = 0; i < iterations; i++) {
        mcp_json_t* params = mcp_template_extract_params_optimized(uri, template_uri);
        if (params == NULL) {
            printf("Error: Optimized parameter extraction failed\n");
            return 0.0;
        }
        mcp_json_destroy(params);
    }

    clock_t end = clock();
    return (double)(end - start) / CLOCKS_PER_SEC;
}

double benchmark_template_expand(const char* template_uri, const mcp_json_t* params, int iterations) {
    // First call to check if expansion works
    char* init_expanded = mcp_template_expand(template_uri, params);
    if (init_expanded == NULL) {
        printf("Error: Template expansion failed in initial test\n");
        return 0.0;
    }
    free(init_expanded);

    clock_t start = clock();

    for (int i = 0; i < iterations; i++) {
        char* expanded = mcp_template_expand(template_uri, params);
        if (expanded == NULL) {
            printf("Error: Template expansion failed after initial success\n");
            return 0.0;
        }
        free(expanded);
    }

    clock_t end = clock();
    return (double)(end - start) / CLOCKS_PER_SEC;
}

void get_template_and_uri(template_complexity_t complexity, char** template_uri, char** uri, mcp_json_t** params) {
    *params = mcp_json_object_create();

    switch (complexity) {
        case TEMPLATE_SIMPLE:
            *template_uri = strdup("example://{name}");
            *uri = strdup("example://john");
            mcp_json_object_set_property(*params, "name", mcp_json_string_create("john"));
            break;

        case TEMPLATE_MEDIUM:
            *template_uri = strdup("example://{user}/posts/{post_id}");
            *uri = strdup("example://john/posts/42");
            mcp_json_object_set_property(*params, "user", mcp_json_string_create("john"));
            mcp_json_object_set_property(*params, "post_id", mcp_json_string_create("42"));
            break;

        case TEMPLATE_COMPLEX:
            *template_uri = strdup("example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}");
            *uri = strdup("example://john/posts/42/comments/123/456");
            mcp_json_object_set_property(*params, "user", mcp_json_string_create("john"));
            mcp_json_object_set_property(*params, "post_id", mcp_json_string_create("42"));
            mcp_json_object_set_property(*params, "comment_id", mcp_json_string_create("123"));
            mcp_json_object_set_property(*params, "reply_id", mcp_json_string_create("456"));
            break;

        case TEMPLATE_VERY_COMPLEX:
            *template_uri = strdup("example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}/{sort:pattern:date*}/{filter:pattern:all*}/{page:int=1}/{limit:int=10}");
            *uri = strdup("example://john/posts/42/comments/123/456/date-desc/all-active/2/20");
            mcp_json_object_set_property(*params, "user", mcp_json_string_create("john"));
            mcp_json_object_set_property(*params, "post_id", mcp_json_string_create("42"));
            mcp_json_object_set_property(*params, "comment_id", mcp_json_string_create("123"));
            mcp_json_object_set_property(*params, "reply_id", mcp_json_string_create("456"));
            mcp_json_object_set_property(*params, "sort", mcp_json_string_create("date-desc"));
            mcp_json_object_set_property(*params, "filter", mcp_json_string_create("all-active"));
            mcp_json_object_set_property(*params, "page", mcp_json_string_create("2"));
            mcp_json_object_set_property(*params, "limit", mcp_json_string_create("20"));
            break;
    }
}
