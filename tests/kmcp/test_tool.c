#include "kmcp_tool_sdk.h"
#include "mcp_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Tool initialization callback
 */
static kmcp_error_t test_tool_init(void** user_data) {
    // No user data needed for this simple tool
    *user_data = NULL;
    return KMCP_SUCCESS;
}

/**
 * Tool cleanup callback
 */
static void test_tool_cleanup(void* user_data) {
    // Nothing to clean up
}

/**
 * Tool execution callback
 */
static kmcp_error_t test_tool_execute(void* user_data, const mcp_json_t* params, mcp_json_t** result) {
    // Get tool context
    kmcp_tool_context_t* context = kmcp_tool_get_context();
    if (!context) {
        return KMCP_ERROR_INVALID_CONTEXT;
    }
    
    // Get parameters
    const char* text = kmcp_tool_get_string_param(params, "text", NULL);
    if (!text) {
        *result = kmcp_tool_create_error_result("Missing 'text' parameter", KMCP_ERROR_INVALID_PARAMETER);
        return KMCP_SUCCESS;
    }
    
    // Create result
    mcp_json_t* data = mcp_json_object();
    mcp_json_object_set_string(data, "output", text);
    *result = kmcp_tool_create_data_result(data);
    
    // Free temporary data
    mcp_json_free(data);
    
    return KMCP_SUCCESS;
}

/**
 * Tool cancellation callback
 */
static kmcp_error_t test_tool_cancel(void* user_data) {
    // Nothing to cancel
    return KMCP_SUCCESS;
}

/**
 * Register the tool
 */
int main(void) {
    // Define tool metadata
    kmcp_tool_metadata_t metadata = {
        .name = "test-tool",
        .version = "1.0.0",
        .description = "Test tool for KMCP",
        .author = "KMCP Team",
        .website = "https://example.com",
        .license = "MIT",
        .tags = (const char*[]){"test", "example"},
        .tags_count = 2,
        .category = KMCP_TOOL_CATEGORY_UTILITY,
        .capabilities = KMCP_TOOL_CAP_NONE,
        .dependencies = NULL,
        .dependencies_count = 0
    };
    
    // Define tool callbacks
    kmcp_tool_callbacks_t callbacks = {
        .init = test_tool_init,
        .cleanup = test_tool_cleanup,
        .execute = test_tool_execute,
        .cancel = test_tool_cancel
    };
    
    // Register the tool
    kmcp_error_t result = kmcp_tool_register(&metadata, &callbacks);
    if (result != KMCP_SUCCESS) {
        fprintf(stderr, "Failed to register tool: %s\n", kmcp_error_message(result));
        return 1;
    }
    
    printf("Tool registered successfully\n");
    
    // Wait for tool to be used
    while (1) {
        // Sleep to avoid busy waiting
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }
    
    return 0;
}
