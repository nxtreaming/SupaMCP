# KMCP Tool SDK Guide

This guide explains how to use the KMCP Tool SDK to develop and integrate custom tools with KMCP.

## Overview

The KMCP Tool SDK allows developers to create custom tools that can be used with KMCP. Tools are the primary way to extend KMCP functionality and provide new capabilities to users.

## Basic Concepts

- **Tool**: A component that provides specific functionality to KMCP clients
- **Tool Metadata**: Information about a tool, including name, version, description, and capabilities
- **Tool Callbacks**: Functions that are called by KMCP to initialize, execute, and clean up a tool
- **Tool Context**: Context information for a tool execution, including parameters and user data
- **Tool Parameters**: Input parameters for a tool execution, provided as a JSON object
- **Tool Result**: Output result of a tool execution, returned as a JSON object

## Creating a Tool

### Tool Structure

A KMCP tool consists of:

1. **Metadata**: Information about the tool
2. **Callbacks**: Functions that implement the tool's functionality
3. **Registration**: Code to register the tool with KMCP

### Tool Metadata

Tool metadata provides information about the tool to KMCP and clients:

```c
#include "kmcp_tool_sdk.h"

// Define tool metadata
kmcp_tool_metadata_t metadata = {
    .name = "example-tool",
    .version = "1.0.0",
    .description = "An example tool for KMCP",
    .author = "Your Name",
    .website = "https://example.com",
    .license = "MIT",
    .tags = (const char*[]){"example", "demo"},
    .tags_count = 2,
    .category = KMCP_TOOL_CATEGORY_UTILITY,
    .capabilities = KMCP_TOOL_CAP_NONE,
    .dependencies = NULL,
    .dependencies_count = 0
};
```

### Tool Callbacks

Tool callbacks implement the tool's functionality:

```c
// Tool initialization callback
kmcp_error_t tool_init(void** user_data) {
    // Allocate and initialize tool-specific data
    *user_data = malloc(sizeof(some_data_t));
    if (!*user_data) {
        return KMCP_ERROR_MEMORY;
    }
    
    // Initialize the data
    some_data_t* data = (some_data_t*)*user_data;
    // ... initialize data ...
    
    return KMCP_SUCCESS;
}

// Tool cleanup callback
void tool_cleanup(void* user_data) {
    // Free tool-specific data
    if (user_data) {
        some_data_t* data = (some_data_t*)user_data;
        // ... cleanup data ...
        free(user_data);
    }
}

// Tool execution callback
kmcp_error_t tool_execute(void* user_data, const mcp_json_t* params, mcp_json_t** result) {
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
    
    int count = kmcp_tool_get_int_param(params, "count", 1);
    
    // Process the parameters
    char* output = malloc(strlen(text) * count + 1);
    if (!output) {
        *result = kmcp_tool_create_error_result("Memory allocation failed", KMCP_ERROR_MEMORY);
        return KMCP_SUCCESS;
    }
    
    output[0] = '\0';
    for (int i = 0; i < count; i++) {
        strcat(output, text);
    }
    
    // Create result
    mcp_json_t* data = mcp_json_object();
    mcp_json_object_set_string(data, "output", output);
    *result = kmcp_tool_create_data_result(data);
    
    // Free temporary data
    free(output);
    mcp_json_free(data);
    
    return KMCP_SUCCESS;
}

// Tool cancellation callback (optional)
void tool_cancel(void* user_data) {
    // Cancel the tool execution
    some_data_t* data = (some_data_t*)user_data;
    data->cancelled = true;
}

// Define tool callbacks
kmcp_tool_callbacks_t callbacks = {
    .init = tool_init,
    .cleanup = tool_cleanup,
    .execute = tool_execute,
    .cancel = tool_cancel  // Optional
};
```

### Tool Registration

Register the tool with KMCP:

```c
// Register the tool
kmcp_error_t result = kmcp_tool_register(&metadata, &callbacks);
if (result != KMCP_SUCCESS) {
    // Handle error
    return 1;
}
```

### Complete Tool Example

Here's a complete example of a simple tool that repeats a text string a specified number of times:

```c
#include "kmcp_tool_sdk.h"
#include <string.h>
#include <stdlib.h>

// Tool initialization callback
kmcp_error_t repeat_tool_init(void** user_data) {
    // No user data needed for this simple tool
    *user_data = NULL;
    return KMCP_SUCCESS;
}

// Tool cleanup callback
void repeat_tool_cleanup(void* user_data) {
    // Nothing to clean up
}

// Tool execution callback
kmcp_error_t repeat_tool_execute(void* user_data, const mcp_json_t* params, mcp_json_t** result) {
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
    
    int count = kmcp_tool_get_int_param(params, "count", 1);
    if (count < 1) {
        *result = kmcp_tool_create_error_result("'count' must be at least 1", KMCP_ERROR_INVALID_PARAMETER);
        return KMCP_SUCCESS;
    }
    
    // Log the request
    kmcp_tool_log(context, 2, "Repeating text '%s' %d times", text, count);
    
    // Check for cancellation
    if (kmcp_tool_is_cancelled(context)) {
        *result = kmcp_tool_create_error_result("Operation cancelled", KMCP_ERROR_CANCELLED);
        return KMCP_SUCCESS;
    }
    
    // Process the parameters
    size_t text_len = strlen(text);
    char* output = malloc(text_len * count + 1);
    if (!output) {
        *result = kmcp_tool_create_error_result("Memory allocation failed", KMCP_ERROR_MEMORY);
        return KMCP_SUCCESS;
    }
    
    output[0] = '\0';
    for (int i = 0; i < count; i++) {
        // Send progress updates
        kmcp_tool_send_progress(context, (float)i / count, "Repeating text...");
        
        // Check for cancellation
        if (kmcp_tool_is_cancelled(context)) {
            free(output);
            *result = kmcp_tool_create_error_result("Operation cancelled", KMCP_ERROR_CANCELLED);
            return KMCP_SUCCESS;
        }
        
        strcat(output, text);
    }
    
    // Create result
    mcp_json_t* data = mcp_json_object();
    mcp_json_object_set_string(data, "output", output);
    mcp_json_object_set_int(data, "length", strlen(output));
    *result = kmcp_tool_create_data_result(data);
    
    // Free temporary data
    free(output);
    mcp_json_free(data);
    
    // Log the completion
    kmcp_tool_log(context, 2, "Completed repeating text");
    
    return KMCP_SUCCESS;
}

// Define tool metadata
kmcp_tool_metadata_t repeat_tool_metadata = {
    .name = "repeat",
    .version = "1.0.0",
    .description = "Repeats a text string a specified number of times",
    .author = "Your Name",
    .website = "https://example.com",
    .license = "MIT",
    .tags = (const char*[]){"text", "utility"},
    .tags_count = 2,
    .category = KMCP_TOOL_CATEGORY_UTILITY,
    .capabilities = KMCP_TOOL_CAP_CANCELLABLE,
    .dependencies = NULL,
    .dependencies_count = 0
};

// Define tool callbacks
kmcp_tool_callbacks_t repeat_tool_callbacks = {
    .init = repeat_tool_init,
    .cleanup = repeat_tool_cleanup,
    .execute = repeat_tool_execute,
    .cancel = NULL  // No custom cancellation needed
};

// Tool registration function
kmcp_error_t register_repeat_tool(void) {
    return kmcp_tool_register(&repeat_tool_metadata, &repeat_tool_callbacks);
}

// Tool unregistration function
kmcp_error_t unregister_repeat_tool(void) {
    return kmcp_tool_unregister("repeat");
}
```

## Tool Parameters and Results

### Getting Tool Parameters

The KMCP Tool SDK provides helper functions to get parameters from the JSON object:

```c
// Get string parameter
const char* text = kmcp_tool_get_string_param(params, "text", "default");

// Get integer parameter
int count = kmcp_tool_get_int_param(params, "count", 1);

// Get boolean parameter
bool flag = kmcp_tool_get_bool_param(params, "flag", false);

// Get number parameter
double value = kmcp_tool_get_number_param(params, "value", 0.0);

// Get object parameter
const mcp_json_t* obj = kmcp_tool_get_object_param(params, "options");

// Get array parameter
const mcp_json_t* arr = kmcp_tool_get_array_param(params, "items");
```

### Creating Tool Results

The KMCP Tool SDK provides helper functions to create results:

```c
// Create a success result
*result = kmcp_tool_create_success_result("Operation completed successfully");

// Create an error result
*result = kmcp_tool_create_error_result("An error occurred", KMCP_ERROR_INVALID_PARAMETER);

// Create a data result
mcp_json_t* data = mcp_json_object();
mcp_json_object_set_string(data, "output", "Hello, world!");
mcp_json_object_set_int(data, "count", 42);
*result = kmcp_tool_create_data_result(data);
mcp_json_free(data);
```

## Tool Context

The tool context provides access to the execution context of a tool:

```c
// Get the tool context
kmcp_tool_context_t* context = kmcp_tool_get_context();
if (!context) {
    return KMCP_ERROR_INVALID_CONTEXT;
}

// Set user data in the context
void* my_data = malloc(sizeof(my_data_t));
kmcp_tool_set_user_data(context, my_data);

// Get user data from the context
void* data = NULL;
kmcp_tool_get_user_data(context, &data);

// Log a message
kmcp_tool_log(context, 2, "Processing data: %s", some_data);

// Send progress update
kmcp_tool_send_progress(context, 0.5, "50% complete");

// Send partial result
mcp_json_t* partial = mcp_json_object();
mcp_json_object_set_string(partial, "status", "processing");
kmcp_tool_send_partial_result(context, partial);
mcp_json_free(partial);

// Check if the operation has been cancelled
if (kmcp_tool_is_cancelled(context)) {
    // Clean up and return
    return KMCP_ERROR_CANCELLED;
}
```

## Tool Capabilities

Tool capabilities define what features a tool supports:

```c
// Define tool capabilities
unsigned int capabilities = 
    KMCP_TOOL_CAP_STREAMING |     // Tool supports streaming responses
    KMCP_TOOL_CAP_CANCELLABLE |   // Tool operations can be cancelled
    KMCP_TOOL_CAP_RESOURCE_HEAVY; // Tool requires significant resources
```

Available capabilities:

- `KMCP_TOOL_CAP_NONE`: No special capabilities
- `KMCP_TOOL_CAP_STREAMING`: Tool supports streaming responses
- `KMCP_TOOL_CAP_BINARY`: Tool supports binary data
- `KMCP_TOOL_CAP_ASYNC`: Tool supports asynchronous operation
- `KMCP_TOOL_CAP_CANCELLABLE`: Tool operations can be cancelled
- `KMCP_TOOL_CAP_BATCH`: Tool supports batch operations
- `KMCP_TOOL_CAP_STATEFUL`: Tool maintains state between calls
- `KMCP_TOOL_CAP_RESOURCE_HEAVY`: Tool requires significant resources
- `KMCP_TOOL_CAP_PRIVILEGED`: Tool requires elevated privileges

## Tool Categories

Tool categories help organize tools by their purpose:

```c
// Define tool category
kmcp_tool_category_t category = KMCP_TOOL_CATEGORY_AI;
```

Available categories:

- `KMCP_TOOL_CATEGORY_GENERAL`: General purpose tool
- `KMCP_TOOL_CATEGORY_SYSTEM`: System management tool
- `KMCP_TOOL_CATEGORY_NETWORK`: Network-related tool
- `KMCP_TOOL_CATEGORY_SECURITY`: Security-related tool
- `KMCP_TOOL_CATEGORY_DEVELOPMENT`: Development tool
- `KMCP_TOOL_CATEGORY_MEDIA`: Media processing tool
- `KMCP_TOOL_CATEGORY_AI`: AI/ML tool
- `KMCP_TOOL_CATEGORY_DATABASE`: Database tool
- `KMCP_TOOL_CATEGORY_UTILITY`: Utility tool
- `KMCP_TOOL_CATEGORY_CUSTOM`: Custom category

## Building and Packaging Tools

### Building a Tool as a Shared Library

Tools can be built as shared libraries that can be loaded by KMCP at runtime:

```c
// Tool entry point
KMCP_TOOL_EXPORT kmcp_error_t kmcp_tool_init(void) {
    // Register the tool
    return register_repeat_tool();
}

// Tool exit point
KMCP_TOOL_EXPORT void kmcp_tool_cleanup(void) {
    // Unregister the tool
    unregister_repeat_tool();
}
```

### Building a Tool as a Static Library

Tools can also be built as static libraries that are linked with KMCP at build time:

```c
// Register the tool during application initialization
void app_init(void) {
    // Register the tool
    register_repeat_tool();
}

// Unregister the tool during application cleanup
void app_cleanup(void) {
    // Unregister the tool
    unregister_repeat_tool();
}
```

## Best Practices

1. **Validate Parameters**: Always validate parameters before using them.
2. **Handle Errors**: Always handle errors and return appropriate error messages.
3. **Free Resources**: Always free resources when they are no longer needed.
4. **Check for Cancellation**: Regularly check for cancellation during long-running operations.
5. **Send Progress Updates**: Send progress updates during long-running operations.
6. **Use Appropriate Capabilities**: Set appropriate capabilities for your tool.
7. **Use Appropriate Categories**: Set appropriate categories for your tool.
8. **Document Your Tool**: Provide clear documentation for your tool.
9. **Test Your Tool**: Thoroughly test your tool before releasing it.
10. **Version Your Tool**: Use semantic versioning for your tool.
