#include "internal/server_internal.h"
#include "mcp_auth.h"
#include "mcp_json_message.h"
#include "mcp_json_rpc.h"
#include "mcp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @internal
 * @brief Handles the 'ping' request.
 * Simple handler that returns a pong response to confirm server is live.
 * This is primarily used as an initial handshake for connection testing.
 */
char* handle_ping_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    // No params needed, arena unused, auth_context may be unused for ping
    (void)arena;
    (void)auth_context;

    // Basic parameter validation
    if (server == NULL || request == NULL || error_code == NULL) {
        if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot proceed without basic parameters
    }

    // For ping requests, we allow a NULL auth_context as ping is often used for initial connection testing
    // This makes the ping handler more lenient than other handlers
    *error_code = MCP_ERROR_NONE;

    mcp_log_debug("Received ping request (ID: %llu, params: %s)",
                 (unsigned long long)request->id,
                 request->params ? request->params : "NULL");

    // Log auth context info
    if (auth_context) {
        mcp_log_debug("Auth context: type=%d, identifier=%s",
                     auth_context->type,
                     auth_context->identifier ? auth_context->identifier : "NULL");
    } else {
        mcp_log_debug("Auth context is NULL");
    }

    // Create a direct response without using the thread-local arena
    mcp_log_debug("Creating direct ping response");

    // Use mcp_json_format_response instead of mcp_json_create_response
    // This doesn't use the thread-local arena
    const char* pong_result = "{\"message\":\"pong\"}";
    char* response = mcp_json_format_response(request->id, pong_result);

    if (!response) {
        mcp_log_error("Failed to create ping response");
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        return create_error_response(request->id, *error_code, "Failed to create ping response");
    }

    mcp_log_debug("Created ping response (ID: %llu): '%s'",
                 (unsigned long long)request->id,
                 response);

    return response;
}
