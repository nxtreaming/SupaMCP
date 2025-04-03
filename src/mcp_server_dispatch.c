#include "mcp_server_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @internal
 * @brief Parses and handles a single incoming message.
 *
 * Uses an arena for temporary allocations during parsing. Determines message type
 * and dispatches to the appropriate handler (handle_request or handles notifications/responses).
 *
 * @param server The server instance.
 * @param data Raw message data (expected to be null-terminated JSON string).
 * @param size Size of the data.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code on failure (e.g., parse error).
 * @return A malloc'd JSON string response if the message was a request, NULL otherwise (or on error).
 */
char* handle_message(mcp_server_t* server, const void* data, size_t size, int* error_code) {
    if (server == NULL || data == NULL || size == 0 || error_code == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL;
    }
    *error_code = MCP_ERROR_NONE; // Default to success

    // Initialize arena for this message processing cycle
    mcp_arena_t arena;
    mcp_arena_init(&arena, 0); // Use default block size

    // Assume 'data' is null-terminated by the caller (tcp_client_handler_thread_func)
    const char* json_str = (const char*)data;
    PROFILE_START("handle_message"); // Profile overall message handling

    // --- API Key Check (before full parsing) ---
    if (server->config.api_key != NULL && strlen(server->config.api_key) > 0) {
        // Temporarily parse just enough to check for apiKey field
        // NOTE: This now uses the thread-local arena implicitly.
        mcp_json_t* temp_json = mcp_json_parse(json_str);
        bool key_valid = false;
        uint64_t request_id_for_error = 0; // Try to get ID for error response

        if (temp_json && mcp_json_get_type(temp_json) == MCP_JSON_OBJECT) { // Use accessor
            mcp_json_t* id_node = mcp_json_object_get_property(temp_json, "id");
            double id_num;
            if (id_node && mcp_json_get_type(id_node) == MCP_JSON_NUMBER && mcp_json_get_number(id_node, &id_num) == 0) { // Use accessors
                request_id_for_error = (uint64_t)id_num;
            }

            mcp_json_t* key_node = mcp_json_object_get_property(temp_json, "apiKey");
            const char* received_key = NULL;
            if (key_node && mcp_json_get_type(key_node) == MCP_JSON_STRING && mcp_json_get_string(key_node, &received_key) == 0) { // Use accessors
                if (received_key && strcmp(received_key, server->config.api_key) == 0) {
                    key_valid = true;
                }
            }
        }
        // No need to destroy temp_arena, as nodes are in thread-local arena.
        // mcp_arena_reset_current_thread(); // Optional reset

        if (!key_valid) {
            fprintf(stderr, "Error: Invalid or missing API key in request.\n");
            *error_code = MCP_ERROR_INVALID_REQUEST; // Or a specific auth error code?
            mcp_arena_destroy(&arena); // Clean up main arena
            // Return error response (requires request ID, which we tried to get)
            return create_error_response(request_id_for_error, *error_code, "Invalid API Key");
        }
    }
    // --- End API Key Check ---


    // Parse the message using the thread-local arena implicitly
    mcp_message_t message;
    int parse_result = mcp_json_parse_message(json_str, &message);

    // No need to free json_str, it points to the buffer managed by the caller

    if (parse_result != 0) {
        mcp_arena_destroy(&arena); // Clean up arena on parse error
        *error_code = MCP_ERROR_PARSE_ERROR;
        // TODO: Generate and return a JSON-RPC Parse Error response string?
        // For now, just return NULL indicating failure.
        return NULL;
    }

    // Handle the message based on its type
    char* response_str = NULL;
    switch (message.type) {
        case MCP_MESSAGE_TYPE_REQUEST:
            // Pass arena to request handler, get response string back
            response_str = handle_request(server, &arena, &message.request, error_code);
            break;
        case MCP_MESSAGE_TYPE_RESPONSE:
            // Server typically doesn't process responses it receives
            *error_code = 0; // No error, just no response to send
            break;
        case MCP_MESSAGE_TYPE_NOTIFICATION:
            // Server could handle notifications if needed
            // For now, just acknowledge success, no response needed.
            *error_code = 0;
            break;
    }

    // Free the message structure contents (which used malloc/strdup)
    mcp_message_release_contents(&message);

    // Clean up the arena used for this message cycle.
    mcp_arena_destroy(&arena);
    PROFILE_END("handle_message");

    return response_str; // Return malloc'd response string (or NULL)
}

/**
 * @internal
 * @brief Handles a parsed request message by dispatching to the correct method handler.
 *
 * @param server The server instance.
 * @param arena Arena used for parsing the request (can be used by handlers for param parsing).
 * @param request Pointer to the parsed request structure.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code if the method is not found.
 * @return A malloc'd JSON string response (success or error response).
 */
char* handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code) {
    if (server == NULL || request == NULL || arena == NULL || error_code == NULL) {
        if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL;
    }
    *error_code = MCP_ERROR_NONE; // Default to success

    // Handle the request based on its method
    if (strcmp(request->method, "list_resources") == 0) {
        return handle_list_resources_request(server, arena, request, error_code);
    } else if (strcmp(request->method, "list_resource_templates") == 0) {
        return handle_list_resource_templates_request(server, arena, request, error_code);
    } else if (strcmp(request->method, "read_resource") == 0) {
        return handle_read_resource_request(server, arena, request, error_code);
    } else if (strcmp(request->method, "list_tools") == 0) {
        return handle_list_tools_request(server, arena, request, error_code);
    } else if (strcmp(request->method, "call_tool") == 0) {
        return handle_call_tool_request(server, arena, request, error_code);
    } else {
        // Unknown method - Create and return error response string
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        // Use the helper function from mcp_server_response.c (declared in internal header)
        return create_error_response(request->id, *error_code, "Method not found");
    }
}
