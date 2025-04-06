#include "internal/server_internal.h"
#include "gateway_routing.h"
#include "mcp_auth.h"
#include "mcp_arena.h"
#include "mcp_json.h"
#include "mcp_json_message.h"
#include "gateway_socket_utils.h"
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
    mcp_arena_init(&arena, MCP_ARENA_DEFAULT_SIZE);

    // Data received from transport_message_callback is guaranteed to be NULL-terminated.
    PROFILE_START("handle_message"); // Profile overall message handling

    const char* json_str = (const char*)data; // Cast directly, it's null-terminated

    // Parse the message using the thread-local arena implicitly
    mcp_message_t message;
    // Pass the null-terminated string directly to the parser
    int parse_result = mcp_json_parse_message(json_str, &message);

    if (parse_result != 0) {
        mcp_log_error("JSON parsing failed within handle_message (parser error code: %d)", parse_result);
        mcp_arena_destroy(&arena); // Clean up arena on parse error
        *error_code = MCP_ERROR_PARSE_ERROR;
        // TODO: Generate and return a JSON-RPC Parse Error response string?
        // For now, just return NULL indicating failure.
        return NULL;
    }

    // --- Authentication Check ---
    mcp_auth_context_t* auth_context = NULL;
    mcp_auth_type_t required_auth_type = (server->config.api_key != NULL && strlen(server->config.api_key) > 0)
                                           ? MCP_AUTH_API_KEY
                                           : MCP_AUTH_NONE;
    const char* credentials = NULL;
    uint64_t request_id_for_auth_error = 0; // Use 0 if ID not available

    if (message.type == MCP_MESSAGE_TYPE_REQUEST) {
        request_id_for_auth_error = message.request.id; // Get ID for potential error response
        if (required_auth_type == MCP_AUTH_API_KEY) {
            // Extract apiKey from params if required
            mcp_json_t* params_json = mcp_json_parse(message.request.params); // Use TLS arena
            if (params_json && mcp_json_get_type(params_json) == MCP_JSON_OBJECT) {
                mcp_json_t* key_node = mcp_json_object_get_property(params_json, "apiKey");
                if (key_node && mcp_json_get_type(key_node) == MCP_JSON_STRING) {
                    mcp_json_get_string(key_node, &credentials); // Get pointer to key string
                }
            }
            // Note: credentials will be NULL if apiKey is missing or not a string
        }
    }
    // For notifications or responses, we might skip auth or handle differently,
    // but for now, we'll verify based on server config even for these.
    // If API key is required, notifications without it would fail here.

    if (mcp_auth_verify(server, required_auth_type, credentials, &auth_context) != 0) {
        mcp_log_warn("Authentication failed for incoming message.");
        *error_code = MCP_ERROR_INVALID_REQUEST; // Use a generic auth failure code for JSON-RPC
        char* error_response = create_error_response(request_id_for_auth_error, *error_code, "Authentication failed");
        // Cleanup before returning error
        mcp_message_release_contents(&message);
        mcp_arena_destroy(&arena);
        PROFILE_END("handle_message");
        return error_response;
    }
    mcp_log_debug("Authentication successful (Identifier: %s)", auth_context ? auth_context->identifier : "N/A");
    // --- End Authentication Check ---


    // Handle the message based on its type
    char* response_str = NULL;
    switch (message.type) {
        case MCP_MESSAGE_TYPE_REQUEST:
            // Pass auth_context to handle_request
            response_str = handle_request(server, &arena, &message.request, auth_context, error_code);
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

    // Free the authentication context if it was created
    mcp_auth_context_free(auth_context);

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
 * @param auth_context The authentication context for the client making the request.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code if the method is not found or access denied.
 * @return A malloc'd JSON string response (success or error response).
 */
char* handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    // Added auth_context check
    if (server == NULL || request == NULL || arena == NULL || auth_context == NULL || error_code == NULL) {
        if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot proceed without auth context
    }
    *error_code = MCP_ERROR_NONE; // Default to success

    // --- Gateway Routing Check ---
    // Only attempt routing if gateway mode is enabled and backends are configured
    if (server->is_gateway_mode && server->backends != NULL && server->backend_count > 0) {
        // Check if this request should be routed to a backend
        const mcp_backend_info_t* target_backend = find_backend_for_request(request, server->backends, server->backend_count);

        if (target_backend) {
            // Found a backend to route to. Call the extracted forwarding function.
            // The gateway_forward_request function now handles pool interaction, send/recv,
            // and returns either the backend's response or an error response, already malloc'd.
            // It also sets the error_code appropriately.
            return gateway_forward_request(target_backend, request, error_code);
        }
        // If target_backend is NULL, fall through to local handling.
    }
    // --- End Gateway Routing Check ---


    // --- Local Handling (Gateway mode disabled OR no backend route found) ---
    mcp_log_debug("Handling request locally (method: %s).", request->method);

    // Handle the request locally based on its method
    // Note: Pass auth_context down to specific handlers
    if (strcmp(request->method, "ping") == 0) {
        // Special handling for ping requests, all servers should support this connection health check
        return handle_ping_request(server, arena, request, auth_context, error_code);
    } else if (strcmp(request->method, "list_resources") == 0) {
        return handle_list_resources_request(server, arena, request, auth_context, error_code);
    } else if (strcmp(request->method, "list_resource_templates") == 0) {
        return handle_list_resource_templates_request(server, arena, request, auth_context, error_code);
    } else if (strcmp(request->method, "read_resource") == 0) {
        return handle_read_resource_request(server, arena, request, auth_context, error_code);
    } else if (strcmp(request->method, "list_tools") == 0) {
        return handle_list_tools_request(server, arena, request, auth_context, error_code);
    } else if (strcmp(request->method, "call_tool") == 0) {
        return handle_call_tool_request(server, arena, request, auth_context, error_code);
    } else {
        // Unknown method - Create and return error response string
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        // Use the helper function from mcp_server_response.c (declared in internal header)
        return create_error_response(request->id, *error_code, "Method not found");
    }
}
