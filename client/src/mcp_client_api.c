#include "mcp_client_internal.h"
#include <mcp_log.h>
#include <mcp_json_message.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <stdlib.h>
#include <string.h>

/**
 * List resources from the MCP server
 */
int mcp_client_list_resources(
    mcp_client_t* client,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (client == NULL || resources == NULL || count == NULL) {
        return -1;
    }

    // Initialize resources and count
    *resources = NULL;
    *count = 0;

    // Initialize thread-local arena for JSON parsing
    if (!mcp_thread_cache_is_initialized()) {
        mcp_thread_cache_init();
    }
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    mcp_log_debug("Sending list_resources request");
    int send_result = mcp_client_send_request(client, "list_resources", NULL, &result, &error_code, &error_message);

    if (send_result != 0) {
        mcp_log_error("Failed to send list_resources request: %d", send_result);
        free(error_message); // Free error message if send failed
        return -1;
    }

    mcp_log_debug("Received list_resources response: %s", result ? result : "NULL");

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resources: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result); // Free the result JSON containing the error object
        return -1;
    }

    // Make sure the thread-local arena is initialized for parsing
    if (mcp_arena_get_current() == NULL) {
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            free(error_message);
            free(result);
            return -1;
        }
    }

    //Note: We can use mcp_json_parse_resources_indirect() here if we want to simplify
    //int parse_result = mcp_json_parse_resources_indirect(result, resources, count);
    int parse_result = mcp_json_parse_resources(result, resources, count);

    // Reset the arena after parsing
    mcp_arena_reset_current_thread();

    if (parse_result != 0) {
        mcp_log_error("Failed to parse list_resources response: %d", parse_result);
        free(error_message); // Should be NULL here anyway
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message); // Should be NULL
    return 0;
}

/**
 * List resource templates from the MCP server
 */
int mcp_client_list_resource_templates(
    mcp_client_t* client,
    mcp_resource_template_t*** templates,
    size_t* count
) {
    if (client == NULL || templates == NULL || count == NULL) {
        return -1;
    }

    // Initialize templates and count
    *templates = NULL;
    *count = 0;

    // Initialize thread-local arena for JSON parsing
    if (!mcp_thread_cache_is_initialized()) {
        mcp_thread_cache_init();
    }
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_resource_templates", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resource_templates: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Make sure the thread-local arena is initialized for parsing
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    if (arena_ptr == NULL) {
        mcp_log_debug("Initializing thread-local arena for template parsing");
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            free(error_message);
            free(result);
            return -1;
        }
    }

    // Parse result
    int parse_result = mcp_json_parse_resource_templates(result, templates, count);

    // Reset the arena after parsing
    mcp_log_debug("Resetting thread-local arena after template parsing");
    mcp_arena_reset_current_thread();

    if (parse_result != 0) {
        mcp_log_error("Failed to parse list_resource_templates response.");
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

/**
 * Read a resource from the MCP server
 */
int mcp_client_read_resource(
    mcp_client_t* client,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (client == NULL || uri == NULL || content == NULL || count == NULL) {
        return -1;
    }

    // Initialize content and count
    *content = NULL;
    *count = 0;

    // Initialize thread-local arena for JSON parsing
    if (!mcp_thread_cache_is_initialized()) {
        mcp_thread_cache_init();
    }
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }
    }

    // Create params
    char* params = mcp_json_format_read_resource_params(uri);
    if (params == NULL) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "read_resource", params, &result, &error_code, &error_message) != 0) {
        free(params);
        free(error_message);
        return -1;
    }
    free(params);

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for read_resource: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Make sure the thread-local arena is initialized for parsing
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    if (arena_ptr == NULL) {
        mcp_log_debug("Initializing thread-local arena for content parsing");
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            free(error_message);
            free(result);
            return -1;
        }
    }

    // Parse result
    int parse_result = mcp_json_parse_content(result, content, count);

    // Reset the arena after parsing
    mcp_log_debug("Resetting thread-local arena after content parsing");
    mcp_arena_reset_current_thread();

    if (parse_result != 0) {
        mcp_log_error("Failed to parse read_resource response.");
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

/**
 * List tools from the MCP server
 */
int mcp_client_list_tools(
    mcp_client_t* client,
    mcp_tool_t*** tools,
    size_t* count
) {
    if (client == NULL || tools == NULL || count == NULL) {
        return -1;
    }

    // Initialize tools and count
    *tools = NULL;
    *count = 0;

    // Initialize thread-local arena for JSON parsing
    if (!mcp_thread_cache_is_initialized()) {
        mcp_thread_cache_init();
    }
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_tools", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_tools: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Make sure the thread-local arena is initialized for parsing
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    if (arena_ptr == NULL) {
        mcp_log_debug("Initializing thread-local arena for tools parsing");
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            free(error_message);
            free(result);
            return -1;
        }
    }

    // Parse result
    int parse_result = mcp_json_parse_tools(result, tools, count);

    // Reset the arena after parsing
    mcp_log_debug("Resetting thread-local arena after tools parsing");
    mcp_arena_reset_current_thread();

    if (parse_result != 0) {
         mcp_log_error("Failed to parse list_tools response.");
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

/**
 * Call a tool on the MCP server
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
        return -1;
    }

    // Initialize content, count, and is_error
    *content = NULL;
    *count = 0;
    *is_error = false;

    // Initialize thread-local arena for JSON parsing
    if (!mcp_thread_cache_is_initialized()) {
        mcp_thread_cache_init();
    }
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }
    }

    // Create params
    char* params = mcp_json_format_call_tool_params(name, arguments);
    if (params == NULL) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "call_tool", params, &result, &error_code, &error_message) != 0) {
        free(params);
        free(error_message);
        return -1;
    }
    free(params);

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
         mcp_log_error("Server returned error for call_tool '%s': %d (%s)", name, error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Make sure the thread-local arena is initialized for parsing
    mcp_arena_t* arena_ptr = mcp_arena_get_current();
    if (arena_ptr == NULL) {
        mcp_log_debug("Initializing thread-local arena for tool result parsing");
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            free(error_message);
            free(result);
            return -1;
        }
    }

    // Parse result
    int parse_result = mcp_json_parse_tool_result(result, content, count, is_error);

    // Reset the arena after parsing
    mcp_log_debug("Resetting thread-local arena after tool result parsing");
    mcp_arena_reset_current_thread();

    if (parse_result != 0) {
         mcp_log_error("Failed to parse call_tool response for tool '%s'.", name);
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}
