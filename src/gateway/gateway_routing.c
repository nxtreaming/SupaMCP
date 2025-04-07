#include "gateway_routing.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_connection_pool.h"
#include "gateway_socket_utils.h"
#include "mcp_json_message.h"
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <regex.h>
#endif

// Implementation of find_backend_for_request
const mcp_backend_info_t* find_backend_for_request(
    const mcp_request_t* request,
    const mcp_backend_info_t* backends,
    size_t backend_count)
{
    if (!request || !request->method || !backends || backend_count == 0) {
        return NULL;
    }

    // --- Handle 'read_resource' routing ---
    if (strcmp(request->method, "read_resource") == 0) {
        if (!request->params) return NULL; // Invalid request

        // Params should be a parsed JSON object from handle_request
        const mcp_json_t* params_obj = (const mcp_json_t*)request->params;
        if (mcp_json_get_type(params_obj) != MCP_JSON_OBJECT) return NULL; // Should not happen if parsing worked

        mcp_json_t* uri_node = mcp_json_object_get_property(params_obj, "uri");
        const char* uri_str = NULL;
        if (!uri_node || mcp_json_get_type(uri_node) != MCP_JSON_STRING || mcp_json_get_string(uri_node, &uri_str) != 0 || !uri_str) {
            return NULL; // Invalid params for read_resource
        }

        // Iterate through backends to find a match
        for (size_t i = 0; i < backend_count; i++) {
            const mcp_backend_info_t* backend = &backends[i];

            // 1. Check prefixes first
            for (size_t j = 0; j < backend->routing.resource_prefix_count; j++) {
                const char* prefix = backend->routing.resource_prefixes[j];
                if (prefix && strncmp(uri_str, prefix, strlen(prefix)) == 0) {
                    mcp_log_debug("Routing resource '%s' to backend '%s' via prefix '%s'", uri_str, backend->name, prefix);
                    return backend; // Found prefix match
                }
            }

#ifndef _WIN32
            // 2. Check regexes (POSIX only)
            for (size_t j = 0; j < backend->routing.resource_regex_count; j++) {
                // Use regexec to match the URI against the compiled regex
                // REG_NOSUB was used in regcomp, so nmatch=0 and pmatch=NULL
                if (regexec(&backend->routing.compiled_resource_regexes[j], uri_str, 0, NULL, 0) == 0) {
                    // Match found
                    mcp_log_debug("Routing resource '%s' to backend '%s' via regex '%s'",
                                  uri_str, backend->name, backend->routing.resource_regex_patterns[j]);
                    return backend; // Found regex match
                }
            }
#endif
        }
        mcp_log_debug("No backend route found for resource '%s'", uri_str);
        return NULL; // No match found
    }

    // --- Handle 'call_tool' routing ---
    if (strcmp(request->method, "call_tool") == 0) {
         if (!request->params) return NULL; // Invalid request

        // Params should be a parsed JSON object
        const mcp_json_t* params_obj = (const mcp_json_t*)request->params;
        if (mcp_json_get_type(params_obj) != MCP_JSON_OBJECT) return NULL;

        mcp_json_t* name_node = mcp_json_object_get_property(params_obj, "name");
        const char* name_str = NULL;
        if (!name_node || mcp_json_get_type(name_node) != MCP_JSON_STRING || mcp_json_get_string(name_node, &name_str) != 0 || !name_str) {
            return NULL; // Invalid params for call_tool
        }

        // Iterate through backends to find a matching tool name
        for (size_t i = 0; i < backend_count; i++) {
            const mcp_backend_info_t* backend = &backends[i];
            for (size_t j = 0; j < backend->routing.tool_name_count; j++) {
                const char* tool_name = backend->routing.tool_names[j];
                if (tool_name && strcmp(name_str, tool_name) == 0) {
                     mcp_log_debug("Routing tool '%s' to backend '%s'", name_str, backend->name);
                    return backend; // Found match
                }
            }
            // TODO: Add regex matching for tool names if needed
        }
         mcp_log_debug("No backend route found for tool '%s'", name_str);
        return NULL; // No match found
    }

    // --- No routing for other methods ---
    mcp_log_debug("No routing rules defined for method '%s'", request->method);
    return NULL;
}


/**
 * @internal
 * @brief Forwards a request to a specified backend server.
 * @note This function currently uses the mcp_connection_pool defined in the
 *       mcp_backend_info_t struct, NOT the gateway_pool_manager. This might
 *       need to be reconciled depending on the intended pooling strategy.
 */
char* gateway_forward_request(
    const mcp_backend_info_t* target_backend,
    const mcp_request_t* request,
    int* error_code)
{
    if (!target_backend || !request || !error_code) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Should not happen if called correctly
    }

    mcp_log_info("Forwarding request for method '%s' to backend '%s'...", request->method, target_backend->name);

    // Check if the pool was initialized successfully
    // *** WARNING: Using the pool from mcp_backend_info_t, not gateway_pool_manager ***
    if (target_backend->pool == NULL) {
        mcp_log_error("Cannot forward request: Connection pool for backend '%s' is not available.", target_backend->name);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        return mcp_json_create_error_response(request->id, *error_code, "Backend connection pool unavailable.");
    }

    // 1. Get a connection from the pool
    int get_timeout_ms = target_backend->timeout_ms > 0 ? (int)target_backend->timeout_ms : 5000;
    // *** WARNING: Using mcp_connection_pool_get, not gateway_pool_get_connection ***
    // Use SOCKET and INVALID_SOCKET defined via mcp_connection_pool.h
    SOCKET backend_socket = mcp_connection_pool_get(target_backend->pool, get_timeout_ms);

    if (backend_socket == INVALID_SOCKET) { // Use correct invalid handle
        mcp_log_error("Failed to get connection from pool for backend '%s'.", target_backend->name);
        *error_code = MCP_ERROR_INTERNAL_ERROR; // Or maybe a specific gateway/timeout error?
        return mcp_json_create_error_response(request->id, *error_code, "Failed to connect to backend service.");
    }

    mcp_log_debug("Obtained connection socket %d for backend '%s'.", (int)backend_socket, target_backend->name);

    char* backend_request_json = NULL;
    char* backend_response_json = NULL;
    size_t backend_response_len = 0;
    int forward_status = 0; // 0 = success, < 0 = error
    bool connection_is_valid = true; // Assume valid initially

    // 2. Construct request payload
    // request->params is assumed to be the raw JSON string here
    const char* params_str = (request->params != NULL) ? (const char*)request->params : "{}";
    // TODO: Review if mcp_json_create_request is appropriate or if raw params should be forwarded
    backend_request_json = mcp_json_create_request(request->method, params_str, request->id);
    if (!backend_request_json) {
        mcp_log_error("Failed to create request JSON for backend '%s'.", target_backend->name);
        forward_status = -1;
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        goto forward_cleanup; // Skip send/recv
    }

    // 3. Send request to backend
    int send_timeout_ms = target_backend->timeout_ms > 0 ? (int)target_backend->timeout_ms : 5000;
    // Assuming gateway_send_message takes SOCKET type
    int send_status = gateway_send_message(backend_socket, backend_request_json, send_timeout_ms);
    free(backend_request_json); // Free the formatted request string
    backend_request_json = NULL;

    if (send_status != 0) {
        mcp_log_error("Failed to send request to backend '%s' (status: %d).", target_backend->name, send_status);
        forward_status = -1;
        *error_code = MCP_ERROR_TRANSPORT_ERROR;
        connection_is_valid = false; // Connection likely broken
        goto forward_cleanup; // Skip recv
    }

    // 4. Receive response from backend
    int recv_timeout_ms = target_backend->timeout_ms > 0 ? (int)target_backend->timeout_ms : 5000;
    // Assuming gateway_receive_message takes SOCKET type
    int recv_status = gateway_receive_message(backend_socket, &backend_response_json, &backend_response_len, MAX_MCP_MESSAGE_SIZE, recv_timeout_ms);

    if (recv_status != 0) {
        mcp_log_error("Failed to receive response from backend '%s' (status: %d).", target_backend->name, recv_status);
        forward_status = -1;
        *error_code = (recv_status == -2) ? MCP_ERROR_TRANSPORT_ERROR /*Timeout*/ : MCP_ERROR_TRANSPORT_ERROR;
        connection_is_valid = (recv_status != -3); // Connection is invalid unless it was just closed cleanly (-3)
        goto forward_cleanup;
    }

    // 5. Success
    forward_status = 0;
    *error_code = MCP_ERROR_NONE;

forward_cleanup:
    // Release the connection back to the pool
    mcp_log_debug("Releasing connection socket %d for backend '%s' (valid: %s).",
                  (int)backend_socket, target_backend->name, connection_is_valid ? "true" : "false");
    // *** WARNING: Using mcp_connection_pool_release, not gateway_pool_release_connection ***
    // Use SOCKET type for release function
    mcp_connection_pool_release(target_backend->pool, backend_socket, connection_is_valid);

    // Return result or error response
    if (forward_status == 0) {
        // Success: Return the raw JSON response received from the backend.
        return backend_response_json; // Ownership transferred
    } else {
        // Error occurred during forwarding
        free(backend_response_json); // Free if allocated before error
        // Create an error response for the original client
        const char* error_msg = (*error_code == MCP_ERROR_TRANSPORT_ERROR) ? "Backend connection or timeout error" : "Gateway internal forwarding error";
        return mcp_json_create_error_response(request->id, *error_code, error_msg);
    }
}
