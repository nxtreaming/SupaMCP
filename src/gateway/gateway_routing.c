#include "gateway_routing.h"
#include "gateway_pool.h"
#include "mcp_client.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_json_message.h"
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <regex.h> // Include POSIX regex header only on non-Windows
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
 */
char* gateway_forward_request(
    gateway_pool_manager_t* pool_manager, // Use pool manager
    const mcp_backend_info_t* target_backend,
    const mcp_request_t* request,
    int* error_code_out) // Changed name for clarity
{
    if (!pool_manager || !target_backend || !request || !error_code_out) {
        if (error_code_out) *error_code_out = MCP_ERROR_INVALID_PARAMS;
        return NULL;
    }

    mcp_log_info("Forwarding request for method '%s' (ID: %llu) to backend '%s'...",
                 request->method, (unsigned long long)request->id, target_backend->name);

    *error_code_out = MCP_ERROR_NONE; // Initialize error code
    char* backend_response_json = NULL;
    char* backend_error_message = NULL;
    mcp_error_code_t backend_error_code = MCP_ERROR_NONE;
    bool connection_released = false; // Flag to ensure release happens once

    // 1. Get a client connection handle from the gateway pool manager
    // Note: gateway_pool_get_connection currently doesn't support timeout parameter
    mcp_client_t* client_handle = (mcp_client_t*)gateway_pool_get_connection(pool_manager, target_backend);

    if (!client_handle) {
        mcp_log_error("Failed to get connection from gateway pool for backend '%s'.", target_backend->name);
        *error_code_out = MCP_ERROR_INTERNAL_ERROR; // Or a specific gateway error
        return mcp_json_create_error_response(request->id, *error_code_out, "Gateway failed to get backend connection.");
    }

    mcp_log_debug("Obtained client handle for backend '%s'.", target_backend->name);

    // 2. Send the raw request using the client handle
    // request->params is assumed to be the raw JSON string of parameters
    const char* params_str = (request->params != NULL) ? (const char*)request->params : "{}";

    int send_status = mcp_client_send_raw_request(
        client_handle,
        request->method,
        params_str,
        request->id,
        &backend_response_json, // Output: raw response JSON
        &backend_error_code,    // Output: JSON-RPC error code from backend
        &backend_error_message  // Output: JSON-RPC error message from backend
    );

    // 3. Release the connection handle back to the pool
    // Determine validity based on the result of send_raw_request
    gateway_pool_release_connection(pool_manager, target_backend, client_handle);
    connection_released = true; // Mark as released

    // 4. Process the result
    if (send_status == 0) {
        // Communication successful, check backend_error_code
        if (backend_error_code != MCP_ERROR_NONE) {
            // Backend returned a JSON-RPC error
            mcp_log_warn("Backend '%s' returned error for request ID %llu: %d (%s)",
                         target_backend->name, (unsigned long long)request->id,
                         backend_error_code, backend_error_message ? backend_error_message : "N/A");
            *error_code_out = backend_error_code; // Propagate backend error code
            // Create a new error response based on the backend's error
            char* gateway_error_response = mcp_json_create_error_response(request->id, backend_error_code, backend_error_message);
            free(backend_response_json); // Free the original (unused) result part
            free(backend_error_message);
            return gateway_error_response; // Return the formatted error response
        } else {
            // Backend returned success
            mcp_log_debug("Successfully received response from backend '%s' for request ID %llu.",
                          target_backend->name, (unsigned long long)request->id);
            *error_code_out = MCP_ERROR_NONE;
            free(backend_error_message); // Should be NULL
            return backend_response_json; // Return the successful response JSON (ownership transferred)
        }
    } else {
        // Communication failed (transport error, timeout, etc.)
        mcp_log_error("Failed to forward request to backend '%s' (send_raw_request status: %d).", target_backend->name, send_status);
        *error_code_out = MCP_ERROR_TRANSPORT_ERROR; // General transport error
        free(backend_response_json); // Ensure NULL
        free(backend_error_message); // Free if somehow allocated during failure
        return mcp_json_create_error_response(request->id, *error_code_out, "Gateway transport error communicating with backend.");
    }
}
