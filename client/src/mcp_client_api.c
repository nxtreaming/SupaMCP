#include "mcp_client_internal.h"
#include <mcp_log.h>
#include <mcp_json_message.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize thread-local arena for JSON parsing
 *
 * This helper function ensures that the thread-local arena is initialized
 * for JSON parsing operations. It's used by all API functions that need
 * to parse JSON responses.
 *
 * @return 0 on success, -1 on failure
 */
static int ensure_thread_local_arena() {
    // Initialize thread cache if needed
    if (!mcp_thread_cache_is_initialized()) {
        if (!mcp_thread_cache_init()) {
            mcp_log_error("Failed to initialize thread cache");
            return -1;
        }
    }

    // Check if arena is already initialized
    mcp_arena_t* current_arena = mcp_arena_get_current();
    if (current_arena == NULL) {
        // Initialize arena if not already done
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            return -1;
        }
    }

    return 0;
}

/**
 * @brief Reset the thread-local arena after use
 *
 * This helper function resets the thread-local arena after JSON parsing
 * operations are complete.
 */
static void reset_thread_local_arena() {
    mcp_log_debug("Resetting thread-local arena");
    mcp_arena_reset_current_thread();
}

/**
 * @brief List resources from the MCP server
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
        return -1;
    }

    // Initialize resources and count
    *resources = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized
    if (ensure_thread_local_arena() != 0) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    mcp_log_debug("Sending list_resources request");
    int send_result = mcp_client_send_request(client, "list_resources", NULL, &result, &error_code, &error_message);

    if (send_result != 0) {
        mcp_log_error("Failed to send list_resources request: %d", send_result);
        free(error_message);
        return -1;
    }

    mcp_log_debug("Received list_resources response: %s", result ? result : "NULL");

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resources: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse the resources from the response
    int parse_result = mcp_json_parse_resources(result, resources, count);

    // Reset the arena after parsing
    reset_thread_local_arena();

    if (parse_result != 0) {
        mcp_log_error("Failed to parse list_resources response: %d", parse_result);
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
 * @brief List resource templates from the MCP server
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
        return -1;
    }

    // Initialize templates and count
    *templates = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized
    if (ensure_thread_local_arena() != 0) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    int send_result = mcp_client_send_request(client, "list_resource_templates", NULL, &result, &error_code, &error_message);
    if (send_result != 0) {
        mcp_log_error("Failed to send list_resource_templates request: %d", send_result);
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

    // Parse result
    int parse_result = mcp_json_parse_resource_templates(result, templates, count);

    // Reset the arena after parsing
    reset_thread_local_arena();

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
 * @brief Read a resource from the MCP server
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
        return -1;
    }

    // Initialize content and count
    *content = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized
    if (ensure_thread_local_arena() != 0) {
        return -1;
    }

    // Create params
    char* params = mcp_json_format_read_resource_params(uri);
    if (params == NULL) {
        mcp_log_error("Failed to format read_resource params for URI '%s'", uri);
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    int send_result = mcp_client_send_request(client, "read_resource", params, &result, &error_code, &error_message);
    if (send_result != 0) {
        mcp_log_error("Failed to send read_resource request: %d", send_result);
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

    // Parse result
    int parse_result = mcp_json_parse_content(result, content, count);

    // Reset the arena after parsing
    reset_thread_local_arena();

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
 * @brief List tools from the MCP server
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
        return -1;
    }

    // Initialize tools and count
    *tools = NULL;
    *count = 0;

    // Ensure thread-local arena is initialized
    if (ensure_thread_local_arena() != 0) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    int send_result = mcp_client_send_request(client, "list_tools", NULL, &result, &error_code, &error_message);
    if (send_result != 0) {
        mcp_log_error("Failed to send list_tools request: %d", send_result);
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

    // Parse result
    int parse_result = mcp_json_parse_tools(result, tools, count);

    // Reset the arena after parsing
    reset_thread_local_arena();

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
 * @brief Call a tool on the MCP server
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
        return -1;
    }

    // Initialize content, count, and is_error
    *content = NULL;
    *count = 0;
    *is_error = false;

    // Ensure thread-local arena is initialized
    if (ensure_thread_local_arena() != 0) {
        return -1;
    }

    // Create params
    char* params = mcp_json_format_call_tool_params(name, arguments ? arguments : "{}");
    if (params == NULL) {
        mcp_log_error("Failed to format call_tool params for tool '%s'", name);
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    int send_result = mcp_client_send_request(client, "call_tool", params, &result, &error_code, &error_message);
    if (send_result != 0) {
        mcp_log_error("Failed to send call_tool request for tool '%s': %d", name, send_result);
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

    // Parse result
    int parse_result = mcp_json_parse_tool_result(result, content, count, is_error);

    // Reset the arena after parsing
    reset_thread_local_arena();

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
