#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp_template.h>
#include <mcp_template_optimized.h>
#include <mcp_json.h>
#include <mcp_json_utils.h>
#include <mcp_log.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <mcp_thread_local.h>
#include <mcp_memory_pool.h>
#include <mcp_memory_constants.h>

// Test cases
typedef struct {
    const char* name;
    const char* template_uri;
    const char* uri;
} test_case_t;

// Debug function to print template matching results
void debug_template_match(const char* template_uri, const char* uri) {
    printf("Template: %s\n", template_uri);
    printf("URI: %s\n", uri);
    
    // Test original implementation
    int original_result = mcp_template_matches(uri, template_uri);
    printf("Original match result: %s\n", original_result ? "MATCH" : "NO MATCH");
    
    // Test optimized implementation
    int optimized_result = mcp_template_matches_optimized(uri, template_uri);
    printf("Optimized match result: %s\n", optimized_result ? "MATCH" : "NO MATCH");
    
    // Extract parameters using original implementation
    mcp_json_t* original_params = mcp_template_extract_params(uri, template_uri);
    printf("Original parameter extraction: %s\n", original_params ? "SUCCESS" : "FAILURE");
    
    // Print parameters if extraction succeeded
    if (original_params) {
        char* params_str = mcp_json_stringify(original_params);
        printf("Original parameters: %s\n", params_str);
        free(params_str);
        mcp_json_destroy(original_params);
    }
    
    // Extract parameters using optimized implementation
    mcp_json_t* optimized_params = mcp_template_extract_params_optimized(uri, template_uri);
    printf("Optimized parameter extraction: %s\n", optimized_params ? "SUCCESS" : "FAILURE");
    
    // Print parameters if extraction succeeded
    if (optimized_params) {
        char* params_str = mcp_json_stringify(optimized_params);
        printf("Optimized parameters: %s\n", params_str);
        free(params_str);
        mcp_json_destroy(optimized_params);
    }
    
    printf("\n");
}

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    
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
    
    printf("Template Debug Tool\n");
    printf("==================\n\n");
    
    // Define test cases
    test_case_t test_cases[] = {
        {
            "Simple",
            "example://{name}",
            "example://john"
        },
        {
            "Medium",
            "example://{user}/posts/{post_id}",
            "example://john/posts/42"
        },
        {
            "Complex",
            "example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}",
            "example://john/posts/42/comments/123/456"
        },
        {
            "Very Complex",
            "example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}/{sort:pattern:date*}/{filter:pattern:all*}/{page:int=1}/{limit:int=10}",
            "example://john/posts/42/comments/123/456/date-desc/all-active/2/20"
        },
        {
            "Optional Parameter (included)",
            "example://{user}/settings/{theme?}",
            "example://john/settings/dark"
        },
        {
            "Optional Parameter (omitted)",
            "example://{user}/settings/{theme?}",
            "example://john/settings/"
        },
        {
            "Default Value",
            "example://{user}/settings/{theme=light}",
            "example://john/settings/"
        },
        {
            "Pattern Matching",
            "example://{user}/settings/{theme:pattern:dark*}",
            "example://john/settings/dark-mode"
        }
    };
    
    // Run test cases
    size_t test_count = sizeof(test_cases) / sizeof(test_cases[0]);
    for (size_t i = 0; i < test_count; i++) {
        printf("Test Case %zu: %s\n", i + 1, test_cases[i].name);
        printf("--------------------------------------------------\n");
        debug_template_match(test_cases[i].template_uri, test_cases[i].uri);
        
        // Reset the template cache between test cases
        mcp_template_cache_cleanup();
        printf("--------------------------------------------------\n\n");
    }
    
    // Clean up
    mcp_template_cache_cleanup();
    mcp_thread_cache_cleanup();
    mcp_memory_pool_system_cleanup();
    
    return 0;
}
