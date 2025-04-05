#include "internal/server_internal.h"
#include "gateway_routing.h"
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

    // Ensure data is NULL-terminated for safe handling
    PROFILE_START("handle_message"); // Profile overall message handling
    
    // Create a new buffer, ensuring it's always NULL-terminated
    char* safe_json_str = (char*)malloc(size + 1);
    if (!safe_json_str) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for JSON message");
        if (error_code) *error_code = MCP_ERROR_INTERNAL_ERROR;
        return NULL;
    }
    
    // Copy the original data
    memcpy(safe_json_str, data, size);
    safe_json_str[size] = '\0'; // Ensure terminator exists
    
    // Check if a terminator already exists (avoid double terminators)
    size_t actual_size = size;
    if (size > 0 && ((const char*)data)[size-1] == '\0') {
        // If original message already ends with NULL terminator, adjust actual size but don't change buffer
        actual_size--;
        log_message(LOG_LEVEL_DEBUG, "Message already ends with NULL terminator, actual content size is %zu", actual_size);
    }
    
    const char* json_str = safe_json_str;
#if 0
    // Detailed debug logging, showing original received content
    char debug_buffer[128] = {0};
    size_t display_len = actual_size > 100 ? 100 : actual_size; // Limit log length
    snprintf(debug_buffer, 128, "Received data (%zu bytes): '", actual_size);
    
    for (size_t i = 0; i < display_len; i++) {
        if (i < sizeof(debug_buffer) - strlen(debug_buffer) - 10) { // Leave margin to prevent buffer overflow
            char ch = ((const char*)data)[i];
            if (ch >= 32 && ch < 127) { // Printable characters
                snprintf(debug_buffer + strlen(debug_buffer), 
                         sizeof(debug_buffer) - strlen(debug_buffer), 
                         "%c", ch);
            } else { // Non-printable characters shown in hexadecimal
                snprintf(debug_buffer + strlen(debug_buffer), 
                         sizeof(debug_buffer) - strlen(debug_buffer), 
                         "\\x%02X", (unsigned char)ch);
            }
        }
    }
    
    if (size > display_len) {
        strcat(debug_buffer, "...");
    }
    strcat(debug_buffer, "'");
    log_message(LOG_LEVEL_DEBUG, "%s", debug_buffer);
#endif

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

    // Free the temporary buffer allocated for safe handling
    free(safe_json_str); // Free the safe JSON string we allocated earlier
    
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

    // --- Gateway Routing Check ---
    // Check if this request should be routed to a backend
    // Note: request->params is already parsed into an mcp_json_t* by handle_message
    const mcp_backend_info_t* target_backend = find_backend_for_request(request, server->backends, server->backend_count);

    if (target_backend) {
        // Found a backend to route to.
        log_message(LOG_LEVEL_INFO, "Request for method '%s' routed to backend '%s'. Forwarding...", request->method, target_backend->name);

        // TODO: Implement actual forwarding logic here (Step 2.3+)
        // This involves:
        // 1. Getting a client connection to the target_backend (using connection pool).
        // 2. Constructing the request payload for the backend.
        // 3. Sending the request asynchronously.
        // 4. Handling the backend's response and relaying it.

        // For now, return a temporary error indicating forwarding is not implemented.
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        return create_error_response(request->id, *error_code, "Gateway forwarding not yet implemented.");
    }

    // --- Local Handling (No backend route found) ---
    log_message(LOG_LEVEL_DEBUG, "No backend route found for method '%s'. Handling locally.", request->method);

    // Handle the request locally based on its method
    if (strcmp(request->method, "ping") == 0) {
        // Special handling for ping requests, all servers should support this connection health check
        return handle_ping_request(server, arena, request, error_code);
    } else if (strcmp(request->method, "list_resources") == 0) {
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
