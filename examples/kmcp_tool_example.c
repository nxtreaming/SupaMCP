/**
 * @file kmcp_tool_example.c
 * @brief Example tool implementation using the KMCP Tool SDK
 */

#include "kmcp_tool_sdk.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Example tool user data
 */
typedef struct {
    int counter;
    char* last_input;
} example_tool_data_t;

/**
 * @brief Initialize the example tool
 */
static kmcp_error_t example_tool_init(kmcp_tool_context_t* context) {
    // Log initialization
    kmcp_tool_log(context, 2, "Initializing example tool");

    // Allocate user data
    example_tool_data_t* data = (example_tool_data_t*)malloc(sizeof(example_tool_data_t));
    if (data == NULL) {
        kmcp_tool_log(context, 4, "Failed to allocate user data");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize user data
    data->counter = 0;
    data->last_input = NULL;

    // Set user data
    kmcp_error_t result = kmcp_tool_set_user_data(context, data);
    if (result != KMCP_SUCCESS) {
        kmcp_tool_log(context, 4, "Failed to set user data: %s", kmcp_error_message(result));
        free(data);
        return result;
    }

    kmcp_tool_log(context, 2, "Example tool initialized successfully");
    return KMCP_SUCCESS;
}

/**
 * @brief Clean up the example tool
 */
static void example_tool_cleanup(kmcp_tool_context_t* context) {
    // Log cleanup
    kmcp_tool_log(context, 2, "Cleaning up example tool");

    // Get user data
    void* user_data = NULL;
    kmcp_error_t result = kmcp_tool_get_user_data(context, &user_data);
    if (result != KMCP_SUCCESS || user_data == NULL) {
        kmcp_tool_log(context, 4, "Failed to get user data: %s", kmcp_error_message(result));
        return;
    }

    // Clean up user data
    example_tool_data_t* data = (example_tool_data_t*)user_data;
    if (data->last_input != NULL) {
        free(data->last_input);
    }
    free(data);

    kmcp_tool_log(context, 2, "Example tool cleaned up successfully");
}

/**
 * @brief Execute the example tool
 */
static kmcp_error_t example_tool_execute(
    kmcp_tool_context_t* context,
    const mcp_json_t* params,
    mcp_json_t** result
) {
    // Log execution
    kmcp_tool_log(context, 2, "Executing example tool");

    // Get user data
    void* user_data = NULL;
    kmcp_error_t error = kmcp_tool_get_user_data(context, &user_data);
    if (error != KMCP_SUCCESS || user_data == NULL) {
        kmcp_tool_log(context, 4, "Failed to get user data: %s", kmcp_error_message(error));
        *result = kmcp_tool_create_error_result("Failed to get user data", (int)error);
        return error;
    }

    example_tool_data_t* data = (example_tool_data_t*)user_data;

    // Get parameters
    const char* input = kmcp_tool_get_string_param(params, "input", "");
    int repeat = kmcp_tool_get_int_param(params, "repeat", 1);
    bool uppercase = kmcp_tool_get_bool_param(params, "uppercase", false);

    // Validate parameters
    if (input[0] == '\0') {
        kmcp_tool_log(context, 3, "No input provided");
        *result = kmcp_tool_create_error_result("No input provided", 400);
        return KMCP_SUCCESS;  // Return success with error result
    }

    if (repeat < 1 || repeat > 100) {
        kmcp_tool_log(context, 3, "Invalid repeat value: %d (must be between 1 and 100)", repeat);
        *result = kmcp_tool_create_error_result("Invalid repeat value (must be between 1 and 100)", 400);
        return KMCP_SUCCESS;  // Return success with error result
    }

    // Update user data
    data->counter++;
    if (data->last_input != NULL) {
        free(data->last_input);
    }
    data->last_input = mcp_strdup(input);

    // Process input
    size_t input_len = strlen(input);
    size_t output_len = input_len * repeat;
    char* output = (char*)malloc(output_len + 1);
    if (output == NULL) {
        kmcp_tool_log(context, 4, "Failed to allocate output buffer");
        *result = kmcp_tool_create_error_result("Failed to allocate output buffer", 500);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Fill output buffer
    for (int i = 0; i < repeat; i++) {
        memcpy(output + (i * input_len), input, input_len);
    }
    output[output_len] = '\0';

    // Convert to uppercase if requested
    if (uppercase) {
        for (size_t i = 0; i < output_len; i++) {
            if (output[i] >= 'a' && output[i] <= 'z') {
                output[i] = output[i] - 'a' + 'A';
            }
        }
    }

    // Create result data
    mcp_json_t* data_obj = mcp_json_object_create();
    if (data_obj == NULL) {
        kmcp_tool_log(context, 4, "Failed to create result data object");
        free(output);
        *result = kmcp_tool_create_error_result("Failed to create result data object", 500);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add output to result
    mcp_json_t* output_json = mcp_json_string_create(output);
    if (output_json == NULL) {
        kmcp_tool_log(context, 4, "Failed to create output JSON string");
        mcp_json_destroy(data_obj);
        free(output);
        *result = kmcp_tool_create_error_result("Failed to create output JSON string", 500);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }
    mcp_json_object_set_property(data_obj, "output", output_json);

    // Add counter to result
    mcp_json_t* counter_json = mcp_json_number_create((double)data->counter);
    if (counter_json == NULL) {
        kmcp_tool_log(context, 4, "Failed to create counter JSON number");
        mcp_json_destroy(data_obj);
        free(output);
        *result = kmcp_tool_create_error_result("Failed to create counter JSON number", 500);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }
    mcp_json_object_set_property(data_obj, "counter", counter_json);

    // Create final result
    *result = kmcp_tool_create_data_result(data_obj);
    mcp_json_destroy(data_obj);
    free(output);

    if (*result == NULL) {
        kmcp_tool_log(context, 4, "Failed to create result");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    kmcp_tool_log(context, 2, "Example tool executed successfully (counter: %d)", data->counter);
    return KMCP_SUCCESS;
}

/**
 * @brief Cancel the example tool
 */
static kmcp_error_t example_tool_cancel(kmcp_tool_context_t* context) {
    // Log cancellation
    kmcp_tool_log(context, 2, "Cancelling example tool");

    // Nothing to do for this simple example
    return KMCP_SUCCESS;
}

/**
 * @brief Register the example tool
 */
static kmcp_error_t register_example_tool(void) {
    // Define tool metadata
    kmcp_tool_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.name = "example";
    metadata.version = "1.0.0";
    metadata.description = "Example tool for demonstrating the KMCP Tool SDK";
    metadata.author = "KMCP Team";
    metadata.website = "https://example.com/kmcp-tools";
    metadata.license = "MIT";
    metadata.category = KMCP_TOOL_CATEGORY_UTILITY;
    metadata.capabilities = KMCP_TOOL_CAP_CANCELLABLE;

    // Define tool tags
    const char* tags[] = {"example", "demo", "utility"};
    metadata.tags = tags;
    metadata.tags_count = sizeof(tags) / sizeof(tags[0]);

    // Define tool callbacks
    kmcp_tool_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.init = example_tool_init;
    callbacks.cleanup = example_tool_cleanup;
    callbacks.execute = example_tool_execute;
    callbacks.cancel = example_tool_cancel;

    // Register tool
    return kmcp_tool_register(&metadata, &callbacks);
}

/**
 * @brief Main function
 */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Starting example tool");

    // Register example tool
    kmcp_error_t result = register_example_tool();
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to register example tool: %s", kmcp_error_message(result));
        mcp_log_close();
        return 1;
    }

    mcp_log_info("Example tool registered successfully");

    // In a real application, we would wait for tool requests here
    // For this example, we'll just exit
    mcp_log_info("Example tool exiting");
    mcp_log_close();
    return 0;
}
