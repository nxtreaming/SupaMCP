#include "internal/client_internal.h"
#include <mcp_log.h>
#include <mcp_json_message.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <mcp_cache_aligned.h>
#include <mcp_memory_pool.h>
#include <mcp_string_utils.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Performance optimization constants
#define MAX_URI_LENGTH 2048
#define MAX_PARAMS_SIZE 8192
#define MAX_RESULT_SIZE (10 * 1024 * 1024) // 10MB max response size

/**
 * @brief Initialize thread-local arena for JSON parsing with optimized performance
 *
 * This optimized helper function efficiently ensures that the thread-local arena
 * is initialized for JSON parsing operations. It's used by all API functions that
 * need to parse JSON responses.
 *
 * The function checks if the thread cache and arena are already initialized to
 * avoid redundant initialization, improving performance for repeated calls.
 *
 * @return 0 on success, -1 on failure
 */
static int ensure_thread_local_arena() {
    // Use static variable to track initialization status for this thread
    static int initialized = 0;

    // Fast path for already initialized case
    if (initialized) {
        return 0;
    }

    mcp_log_debug("Initializing thread-local memory for API call");

    // Initialize thread cache if needed
    if (!mcp_thread_cache_is_initialized()) {
        // Configure thread cache with optimized settings for API operations
        mcp_thread_cache_config_t config = {
            .small_cache_size = 32,    // Larger cache for API operations
            .medium_cache_size = 16,   // More medium objects for JSON parsing
            .large_cache_size = 8,     // More large objects for content items
            .adaptive_sizing = true,   // Enable adaptive sizing
            .growth_threshold = 0.8,   // Grow cache when hit ratio is above 80%
            .shrink_threshold = 0.2,   // Shrink cache when hit ratio is below 20%
            .min_cache_size = 8,       // Minimum cache size
            .max_cache_size = 64       // Maximum cache size
        };

        if (!mcp_thread_cache_init_with_config(&config)) {
            mcp_log_error("Failed to initialize thread cache for API call");
            return -1;
        }

        mcp_log_debug("Thread cache initialized successfully");
    }

    // Check if arena is already initialized
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        // Initialize arena with larger size for API operations
        const size_t api_arena_size = MCP_ARENA_DEFAULT_SIZE * 2;

        if (mcp_arena_init_current_thread(api_arena_size) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }

        mcp_log_debug("Thread-local arena initialized with size %zu", api_arena_size);
    }

    // Mark as initialized for this thread
    initialized = 1;
    return 0;
}

/**
 * @brief Reset the thread-local arena after use with optimized performance
 *
 * This optimized helper function efficiently resets the thread-local arena
 * after JSON parsing operations are complete, preserving memory for future use
 * while clearing all allocations.
 */
static void reset_thread_local_arena() {
    // Only log at debug level to avoid performance impact
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        mcp_log_debug("Resetting thread-local arena");
    }

    // Reset the arena to clear all allocations but preserve the memory
    mcp_arena_reset_current_thread();
}

/**
 * @brief List resources from the MCP server with optimized performance
 *
 * This optimized function efficiently retrieves a list of available resources
 * from the MCP server, with improved memory management, error handling, and logging.
 *
 * @param client The MCP client instance
 * @param resources Pointer to store the array of resources
 * @param count Pointer to store the number of resources
 * @return 0 on success, -1 on failure
 */
int mcp_client_list_resources(
    mcp_client_t* client,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (client == NULL || resources == NULL || count == NULL) {
        mcp_log_error("Invalid parameters for list_resources");
        return -1;
    }

    // Initialize output parameters
    *resources = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized for efficient memory management
    if (ensure_thread_local_arena() != 0) {
        mcp_log_error("Failed to initialize thread-local memory for list_resources");
        return -1;
    }

    // Log the API call at info level
    mcp_log_info("Listing resources from MCP server");

    // Prepare for request
    char* result = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    // Send request with empty parameters
    int send_result = mcp_client_send_request(
        client,
        "list_resources",
        NULL,  // No parameters needed
        &result,
        &error_code,
        &error_message
    );

    // Handle send failure
    if (send_result != 0) {
        mcp_log_error("Failed to send list_resources request: %d", send_result);
        free(error_message);
        return -1;
    }

    // Log response at debug level only (avoid expensive string formatting)
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        if (result) {
            // Truncate long responses in logs
            if (strlen(result) > 100) {
                mcp_log_debug("Received list_resources response: %.100s...", result);
            } else {
                mcp_log_debug("Received list_resources response: %s", result);
            }
        } else {
            mcp_log_debug("Received empty list_resources response");
        }
    }

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resources: %d (%s)",
                     error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Validate result before parsing
    if (result == NULL || strlen(result) == 0) {
        mcp_log_error("Empty result from list_resources");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse the resources from the response
    int parse_result = mcp_json_parse_resources(result, resources, count);

    // Reset the arena after parsing to free temporary memory
    reset_thread_local_arena();

    // Handle parse failure
    if (parse_result != 0) {
        mcp_log_error("Failed to parse list_resources response: %d", parse_result);
        free(error_message);
        free(result);
        return -1;
    }

    // Log success
    mcp_log_info("Successfully retrieved %zu resources", *count);

    // Free the allocated strings before returning
    free(result);
    free(error_message);

    return 0;
}

/**
 * @brief List resource templates from the MCP server with optimized performance
 *
 * This optimized function efficiently retrieves a list of available resource templates
 * from the MCP server, with improved memory management, error handling, and logging.
 *
 * @param client The MCP client instance
 * @param templates Pointer to store the array of templates
 * @param count Pointer to store the number of templates
 * @return 0 on success, -1 on failure
 */
int mcp_client_list_resource_templates(
    mcp_client_t* client,
    mcp_resource_template_t*** templates,
    size_t* count
) {
    if (client == NULL || templates == NULL || count == NULL) {
        mcp_log_error("Invalid parameters for list_resource_templates");
        return -1;
    }

    // Initialize output parameters
    *templates = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized for efficient memory management
    if (ensure_thread_local_arena() != 0) {
        mcp_log_error("Failed to initialize thread-local memory for list_resource_templates");
        return -1;
    }

    // Log the API call at info level
    mcp_log_info("Listing resource templates from MCP server");

    // Prepare for request
    char* result = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    // Send request with empty parameters
    int send_result = mcp_client_send_request(
        client,
        "list_resource_templates",
        NULL,  // No parameters needed
        &result,
        &error_code,
        &error_message
    );

    // Handle send failure
    if (send_result != 0) {
        mcp_log_error("Failed to send list_resource_templates request: %d", send_result);
        free(error_message);
        return -1;
    }

    // Log response at debug level only (avoid expensive string formatting)
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        if (result) {
            size_t result_len = strlen(result);
            mcp_log_debug("Received list_resource_templates response: %zu bytes", result_len);
        } else {
            mcp_log_debug("Received empty list_resource_templates response");
        }
    }

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resource_templates: %d (%s)",
                     error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Validate result before parsing
    if (result == NULL || strlen(result) == 0) {
        mcp_log_error("Empty result from list_resource_templates");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse the templates from the response
    int parse_result = mcp_json_parse_resource_templates(result, templates, count);

    // Reset the arena after parsing to free temporary memory
    reset_thread_local_arena();

    // Handle parse failure
    if (parse_result != 0) {
        mcp_log_error("Failed to parse list_resource_templates response");
        free(error_message);
        free(result);
        return -1;
    }

    // Log success
    mcp_log_info("Successfully retrieved %zu resource templates", *count);

    // Free the allocated strings before returning
    free(result);
    free(error_message);

    return 0;
}

/**
 * @brief Read a resource from the MCP server with optimized performance
 *
 * This optimized function efficiently retrieves a resource from the MCP server,
 * with improved memory management, error handling, and logging.
 *
 * @param client The MCP client instance
 * @param uri The URI of the resource to read
 * @param content Pointer to store the array of content items
 * @param count Pointer to store the number of content items
 * @return 0 on success, -1 on failure
 */
int mcp_client_read_resource(
    mcp_client_t* client,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (client == NULL || uri == NULL || content == NULL || count == NULL) {
        mcp_log_error("Invalid parameters for read_resource");
        return -1;
    }

    // Check URI length for reasonable limits
    size_t uri_len = strlen(uri);
    if (uri_len == 0 || uri_len > MAX_URI_LENGTH) {
        mcp_log_error("Invalid URI length: %zu (max: %d)", uri_len, MAX_URI_LENGTH);
        return -1;
    }

    // Initialize output parameters
    *content = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized for efficient memory management
    if (ensure_thread_local_arena() != 0) {
        mcp_log_error("Failed to initialize thread-local memory for read_resource");
        return -1;
    }

    // Log the API call at info level
    mcp_log_info("Reading resource: %s", uri);

    // Create params with URI validation
    char* params = mcp_json_format_read_resource_params(uri);
    if (params == NULL) {
        mcp_log_error("Failed to format read_resource params for URI '%s'", uri);
        return -1;
    }

    // Check params size for reasonable limits
    size_t params_len = strlen(params);
    if (params_len > MAX_PARAMS_SIZE) {
        mcp_log_error("Params too large: %zu bytes (max: %d)", params_len, MAX_PARAMS_SIZE);
        free(params);
        return -1;
    }

    // Prepare for request
    char* result = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    // Send request with formatted parameters
    int send_result = mcp_client_send_request(
        client,
        "read_resource",
        params,
        &result,
        &error_code,
        &error_message
    );

    // Free params as soon as we're done with them
    free(params);

    // Handle send failure
    if (send_result != 0) {
        mcp_log_error("Failed to send read_resource request: %d", send_result);
        free(error_message);
        return -1;
    }

    // Log response size at debug level
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        if (result) {
            size_t result_len = strlen(result);
            mcp_log_debug("Received read_resource response: %zu bytes", result_len);
        } else {
            mcp_log_debug("Received empty read_resource response");
        }
    }

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for read_resource '%s': %d (%s)",
                     uri, error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Validate result before parsing
    if (result == NULL || strlen(result) == 0) {
        mcp_log_error("Empty result from read_resource '%s'", uri);
        free(error_message);
        free(result);
        return -1;
    }

    // Parse the content from the response
    int parse_result = mcp_json_parse_content(result, content, count);

    // Reset the arena after parsing to free temporary memory
    reset_thread_local_arena();

    // Handle parse failure
    if (parse_result != 0) {
        mcp_log_error("Failed to parse read_resource response for '%s'", uri);
        free(error_message);
        free(result);
        return -1;
    }

    // Log success
    mcp_log_info("Successfully read resource '%s': %zu content items", uri, *count);

    // Free the allocated strings before returning
    free(result);
    free(error_message);

    return 0;
}

/**
 * @brief List tools from the MCP server with optimized performance
 *
 * This optimized function efficiently retrieves a list of available tools
 * from the MCP server, with improved memory management, error handling, and logging.
 *
 * @param client The MCP client instance
 * @param tools Pointer to store the array of tools
 * @param count Pointer to store the number of tools
 * @return 0 on success, -1 on failure
 */
int mcp_client_list_tools(
    mcp_client_t* client,
    mcp_tool_t*** tools,
    size_t* count
) {
    if (client == NULL || tools == NULL || count == NULL) {
        mcp_log_error("Invalid parameters for list_tools");
        return -1;
    }

    // Initialize output parameters
    *tools = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized for efficient memory management
    if (ensure_thread_local_arena() != 0) {
        mcp_log_error("Failed to initialize thread-local memory for list_tools");
        return -1;
    }

    // Log the API call at info level
    mcp_log_info("Listing tools from MCP server");

    // Prepare for request
    char* result = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    // Send request with empty parameters
    int send_result = mcp_client_send_request(
        client,
        "list_tools",
        NULL,  // No parameters needed
        &result,
        &error_code,
        &error_message
    );

    // Handle send failure
    if (send_result != 0) {
        mcp_log_error("Failed to send list_tools request: %d", send_result);
        free(error_message);
        return -1;
    }

    // Log response at debug level only (avoid expensive string formatting)
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        if (result) {
            size_t result_len = strlen(result);
            mcp_log_debug("Received list_tools response: %zu bytes", result_len);
        } else {
            mcp_log_debug("Received empty list_tools response");
        }
    }

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_tools: %d (%s)",
                     error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Validate result before parsing
    if (result == NULL || strlen(result) == 0) {
        mcp_log_error("Empty result from list_tools");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse the tools from the response
    int parse_result = mcp_json_parse_tools(result, tools, count);

    // Reset the arena after parsing to free temporary memory
    reset_thread_local_arena();

    // Handle parse failure
    if (parse_result != 0) {
        mcp_log_error("Failed to parse list_tools response");
        free(error_message);
        free(result);
        return -1;
    }

    // Log success
    mcp_log_info("Successfully retrieved %zu tools", *count);

    // Free the allocated strings before returning
    free(result);
    free(error_message);

    return 0;
}

/**
 * @brief Call a tool on the MCP server with optimized performance
 *
 * This optimized function efficiently calls a tool on the MCP server,
 * with improved memory management, error handling, and logging.
 *
 * @param client The MCP client instance
 * @param name The name of the tool to call
 * @param arguments The JSON arguments to pass to the tool (can be NULL)
 * @param content Pointer to store the array of content items
 * @param count Pointer to store the number of content items
 * @param is_error Pointer to store whether the tool result is an error
 * @return 0 on success, -1 on failure
 */
int mcp_client_call_tool(
    mcp_client_t* client,
    const char* name,
    const char* arguments,
    mcp_content_item_t*** content,
    size_t* count,
    bool* is_error
) {
    if (client == NULL || name == NULL || content == NULL || count == NULL || is_error == NULL) {
        mcp_log_error("Invalid parameters for call_tool");
        return -1;
    }

    // Check tool name length for reasonable limits
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 100) {
        mcp_log_error("Invalid tool name length: %zu", name_len);
        return -1;
    }

    // Initialize output parameters
    *content = NULL;
    *count = 0;
    *is_error = false;

    // Ensure thread-local arena is initialized for efficient memory management
    if (ensure_thread_local_arena() != 0) {
        mcp_log_error("Failed to initialize thread-local memory for call_tool");
        return -1;
    }

    // Log the API call at info level
    mcp_log_info("Calling tool: %s", name);

    // Use empty object as default arguments if none provided
    const char* args_to_use = arguments ? arguments : "{}";

    // Log arguments at debug level only
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        // Truncate long arguments in logs
        if (strlen(args_to_use) > 100) {
            mcp_log_debug("Tool arguments: %.100s...", args_to_use);
        } else {
            mcp_log_debug("Tool arguments: %s", args_to_use);
        }
    }

    // Create params with validation
    char* params = mcp_json_format_call_tool_params(name, args_to_use);
    if (params == NULL) {
        mcp_log_error("Failed to format call_tool params for tool '%s'", name);
        return -1;
    }

    // Check params size for reasonable limits
    size_t params_len = strlen(params);
    if (params_len > MAX_PARAMS_SIZE) {
        mcp_log_error("Params too large: %zu bytes (max: %d)", params_len, MAX_PARAMS_SIZE);
        free(params);
        return -1;
    }

    // Prepare for request
    char* result = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    // Send request with formatted parameters
    int send_result = mcp_client_send_request(
        client,
        "call_tool",
        params,
        &result,
        &error_code,
        &error_message
    );

    // Free params as soon as we're done with them
    free(params);

    // Handle send failure
    if (send_result != 0) {
        mcp_log_error("Failed to send call_tool request for tool '%s': %d", name, send_result);
        free(error_message);
        return -1;
    }

    // Log response size at debug level
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        if (result) {
            size_t result_len = strlen(result);
            mcp_log_debug("Received call_tool response: %zu bytes", result_len);
        } else {
            mcp_log_debug("Received empty call_tool response");
        }
    }

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for call_tool '%s': %d (%s)",
                     name, error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Validate result before parsing
    if (result == NULL || strlen(result) == 0) {
        mcp_log_error("Empty result from call_tool '%s'", name);
        free(error_message);
        free(result);
        return -1;
    }

    // Parse the tool result from the response
    int parse_result = mcp_json_parse_tool_result(result, content, count, is_error);

    // Reset the arena after parsing to free temporary memory
    reset_thread_local_arena();

    // Handle parse failure
    if (parse_result != 0) {
        mcp_log_error("Failed to parse call_tool response for tool '%s'", name);
        free(error_message);
        free(result);
        return -1;
    }

    // Log success or tool error
    if (*is_error) {
        mcp_log_info("Tool '%s' returned an error response with %zu content items", name, *count);
    } else {
        mcp_log_info("Successfully called tool '%s': %zu content items", name, *count);
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);

    return 0;
}
