#include "gateway_routing.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include <string.h>

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

        // Iterate through backends to find a matching prefix
        for (size_t i = 0; i < backend_count; i++) {
            const mcp_backend_info_t* backend = &backends[i];
            for (size_t j = 0; j < backend->routing.resource_prefix_count; j++) {
                const char* prefix = backend->routing.resource_prefixes[j];
                if (prefix && strncmp(uri_str, prefix, strlen(prefix)) == 0) {
                    mcp_log_debug("Routing resource '%s' to backend '%s' via prefix '%s'", uri_str, backend->name, prefix);
                    return backend; // Found match
                }
            }
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
        }
         mcp_log_debug("No backend route found for tool '%s'", name_str);
        return NULL; // No match found
    }

    // --- No routing for other methods ---
    return NULL;
}
