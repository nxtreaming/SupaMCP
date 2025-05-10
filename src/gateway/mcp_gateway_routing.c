#include "mcp_gateway_routing.h"
#include "mcp_gateway_pool.h"
#include "mcp_client.h"
#include "mcp_json.h"
#include "mcp_log.h"
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
 * @brief Forwards a request to a specified backend server using the gateway pool manager.
 *
 * Optimized implementation with better error handling and memory management.
 */
char* gateway_forward_request(
    gateway_pool_manager_t* pool_manager,
    const mcp_backend_info_t* target_backend,
    const mcp_request_t* request,
    int* error_code_out)
{
    // Validate input parameters
    if (!pool_manager || !target_backend || !request || !error_code_out) {
        if (error_code_out) *error_code_out = MCP_ERROR_INVALID_PARAMS;
        return NULL;
    }

    // Initialize output variables
    *error_code_out = MCP_ERROR_NONE;
    char* backend_response_json = NULL;
    char* backend_error_message = NULL;
    mcp_error_code_t backend_error_code = MCP_ERROR_NONE;
    mcp_client_t* client_handle = NULL;

    // Log the request
    mcp_log_info("Forwarding request for method '%s' (ID: %llu) to backend '%s'...",
                 request->method, (unsigned long long)request->id, target_backend->name);

    // Get a client connection handle
    // Note: Currently gateway_pool_get_connection doesn't support timeout parameter

    client_handle = (mcp_client_t*)gateway_pool_get_connection(pool_manager, target_backend);
    if (!client_handle) {
        mcp_log_error("Failed to get connection from gateway pool for backend '%s'.", target_backend->name);
        *error_code_out = MCP_ERROR_INTERNAL_ERROR;
        return mcp_json_create_error_response(
            request->id,
            *error_code_out,
            "Gateway failed to get backend connection."
        );
    }

    // Prepare parameters string (use empty object if NULL)
    const char* params_str = (request->params != NULL) ? (const char*)request->params : "{}";

    // Send the request to the backend
    int send_status = mcp_client_send_raw_request(
        client_handle,
        request->method,
        params_str,
        request->id,
        &backend_response_json,
        &backend_error_code,
        &backend_error_message
    );

    // Always release the connection back to the pool
    gateway_pool_release_connection(pool_manager, target_backend, client_handle);
    client_handle = NULL; // Avoid using after release

    // Process the result based on status
    if (send_status == 0) {
        if (backend_error_code != MCP_ERROR_NONE) {
            // Backend returned an error
            mcp_log_warn("Backend '%s' returned error for request ID %llu: %d (%s)",
                         target_backend->name, (unsigned long long)request->id,
                         backend_error_code, backend_error_message ? backend_error_message : "N/A");

            // Create error response
            *error_code_out = backend_error_code;
            char* error_response = mcp_json_create_error_response(
                request->id,
                backend_error_code,
                backend_error_message
            );

            // Clean up
            free(backend_response_json);
            free(backend_error_message);

            return error_response;
        } else {
            // Backend returned success
            mcp_log_debug("Successfully received response from backend '%s' for request ID %llu.",
                          target_backend->name, (unsigned long long)request->id);

            // Clean up and return success
            free(backend_error_message);
            return backend_response_json;
        }
    } else {
        // Communication failed
        mcp_log_error("Failed to forward request to backend '%s' (status: %d).",
                     target_backend->name, send_status);

        // Create error response
        *error_code_out = MCP_ERROR_TRANSPORT_ERROR;
        char* error_response = mcp_json_create_error_response(
            request->id,
            *error_code_out,
            "Gateway transport error communicating with backend."
        );

        // Clean up
        free(backend_response_json);
        free(backend_error_message);

        return error_response;
    }
}
