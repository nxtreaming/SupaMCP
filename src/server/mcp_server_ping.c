#include "internal/server_internal.h"
#include "mcp_auth.h" // Include auth header for context type
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @internal
 * @brief Handles the 'ping' request.
 * Simple handler that returns a pong response to confirm server is live.
 * This is primarily used as an initial handshake for connection testing.
 */
char* handle_ping_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    // No params needed, arena unused, auth_context unused for ping
    (void)arena;
    (void)auth_context; // Explicitly mark as unused for ping

    // Added auth_context check for consistency, though ping usually bypasses auth checks
    if (server == NULL || request == NULL || auth_context == NULL || error_code == NULL) {
        if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot proceed without auth context
    }
    *error_code = MCP_ERROR_NONE;

    mcp_log_debug("Received ping request (ID: %llu)", (unsigned long long)request->id);

    // Create simple response with pong message
    mcp_json_t* result_obj = mcp_json_object_create(); // Use TLS arena
    if (!result_obj || 
        mcp_json_object_set_property(result_obj, "message", mcp_json_string_create("pong")) != 0) {
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create ping response");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj); // Malloc'd result content
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify ping response");
        return response;
    }

    char* response = create_success_response(request->id, result_str);
    mcp_log_debug("Sending pong response to ID %llu", (unsigned long long)request->id);
    return response;
}
