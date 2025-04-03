#include "mcp_server_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// --- Internal Request Handler Implementations ---

/**
 * @internal
 * @brief Handles the 'list_resources' request.
 * Iterates through the server's registered resources and builds a JSON response.
 * Uses malloc for building the JSON response structure.
 */
char* handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code) {
    PROFILE_START("handle_list_resources");
    // This request has no parameters, arena is not used for parsing here.
    (void)arena;

    if (server == NULL || request == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_list_resources"); // End profile on error
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

    if (!server->capabilities.resources_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Resources not supported");
        PROFILE_END("handle_list_resources");
        return response;
    }

    // Create response JSON structure using thread-local arena for temporary nodes.
    // The final stringified result will use malloc.
    mcp_json_t* resources_json = mcp_json_array_create(); // Use TLS arena
    if (!resources_json) {
        *error_code = MCP_ERROR_INTERNAL_ERROR; // Allocation failure
        char* response = create_error_response(request->id, *error_code, "Failed to create resources array");
        PROFILE_END("handle_list_resources");
        return response;
    }

    bool build_error = false;
    for (size_t i = 0; i < server->resource_count; i++) {
        mcp_resource_t* resource = server->resources[i];
        mcp_json_t* res_obj = mcp_json_object_create(); // Use TLS arena
        if (!res_obj ||
            (resource->uri && mcp_json_object_set_property(res_obj, "uri", mcp_json_string_create(resource->uri)) != 0) || // Use TLS arena
            (resource->name && mcp_json_object_set_property(res_obj, "name", mcp_json_string_create(resource->name)) != 0) || // Use TLS arena
            (resource->mime_type && mcp_json_object_set_property(res_obj, "mimeType", mcp_json_string_create(resource->mime_type)) != 0) || // Use TLS arena
            (resource->description && mcp_json_object_set_property(res_obj, "description", mcp_json_string_create(resource->description)) != 0) || // Use TLS arena
            mcp_json_array_add_item(resources_json, res_obj) != 0)
        {
            mcp_json_destroy(res_obj); // Handles nested nodes
            build_error = true;
            break;
        }
    }

    if (build_error) {
        mcp_json_destroy(resources_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build resource JSON");
        PROFILE_END("handle_list_resources");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create(); // Use TLS arena
    if (!result_obj || mcp_json_object_set_property(result_obj, "resources", resources_json) != 0) {
        mcp_json_destroy(resources_json); // Still need destroy for internal mallocs
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_list_resources");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj); // Malloc'd result content
    mcp_json_destroy(result_obj); // Destroys nested resources_json
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_list_resources");
        return response;
    }

    // Create the final success response message string (takes ownership of result_str)
    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_list_resources");
    return response;
}

/**
 * @internal
 * @brief Handles the 'list_resource_templates' request.
 * Iterates through the server's registered templates and builds a JSON response.
 * Uses thread-local arena for temporary nodes, malloc for final string.
 */
char* handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code) {
    PROFILE_START("handle_list_resource_templates");
    // No params, arena unused here.
    (void)arena;

    if (server == NULL || request == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_list_resource_templates"); // End profile on error
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

     if (!server->capabilities.resources_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Resources not supported");
        PROFILE_END("handle_list_resource_templates");
         return response;
    }

    // Create response JSON structure using thread-local arena.
    mcp_json_t* templates_json = mcp_json_array_create(); // Use TLS arena
     if (!templates_json) {
         *error_code = MCP_ERROR_INTERNAL_ERROR; // Allocation failure
         char* response = create_error_response(request->id, *error_code, "Failed to create templates array");
         PROFILE_END("handle_list_resource_templates");
         return response;
     }

    bool build_error = false;
    for (size_t i = 0; i < server->resource_template_count; i++) {
        mcp_resource_template_t* tmpl = server->resource_templates[i];
        mcp_json_t* tmpl_obj = mcp_json_object_create(); // Use TLS arena
        if (!tmpl_obj ||
            (tmpl->uri_template && mcp_json_object_set_property(tmpl_obj, "uriTemplate", mcp_json_string_create(tmpl->uri_template)) != 0) || // Use TLS arena
            (tmpl->name && mcp_json_object_set_property(tmpl_obj, "name", mcp_json_string_create(tmpl->name)) != 0) || // Use TLS arena
            (tmpl->mime_type && mcp_json_object_set_property(tmpl_obj, "mimeType", mcp_json_string_create(tmpl->mime_type)) != 0) || // Use TLS arena
            (tmpl->description && mcp_json_object_set_property(tmpl_obj, "description", mcp_json_string_create(tmpl->description)) != 0) || // Use TLS arena
            mcp_json_array_add_item(templates_json, tmpl_obj) != 0)
        {
            mcp_json_destroy(tmpl_obj);
            build_error = true;
            break;
        }
    }

    if (build_error) {
        mcp_json_destroy(templates_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build template JSON");
        PROFILE_END("handle_list_resource_templates");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create(); // Use TLS arena
    if (!result_obj || mcp_json_object_set_property(result_obj, "resourceTemplates", templates_json) != 0) {
        mcp_json_destroy(templates_json); // Still need destroy for internal mallocs
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_list_resource_templates");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj); // Malloc'd result content
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_list_resource_templates");
        return response;
    }

    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_list_resource_templates");
    return response;
}

/**
 * @internal
 * @brief Handles the 'read_resource' request.
 * Parses the 'uri' parameter using the thread-local arena, checks the cache,
 * calls the registered resource handler if needed, stores the result in cache,
 * and builds the JSON response (using thread-local arena for temp nodes, malloc for final string).
 */
char* handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code) {
    PROFILE_START("handle_read_resource");
    if (server == NULL || request == NULL || arena == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_read_resource"); // End profile on error
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

     if (!server->capabilities.resources_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Resources not supported");
        PROFILE_END("handle_read_resource");
        return response;
    }

    if (request->params == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Missing parameters");
        PROFILE_END("handle_read_resource");
        return response;
    }

    // Parse params using the thread-local arena
    mcp_json_t* params_json = mcp_json_parse(request->params); // Use TLS arena
    if (params_json == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Invalid parameters JSON");
        PROFILE_END("handle_read_resource");
        return response;
    }

    mcp_json_t* uri_json = mcp_json_object_get_property(params_json, "uri");
    const char* uri = NULL;
    if (uri_json == NULL || mcp_json_get_type(uri_json) != MCP_JSON_STRING || mcp_json_get_string(uri_json, &uri) != 0 || uri == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Missing or invalid 'uri' parameter");
        PROFILE_END("handle_read_resource");
        return response;
    }


    mcp_content_item_t** content_items = NULL; // Array of POINTERS to content items
    size_t content_count = 0;
    bool fetched_from_handler = false;
    mcp_content_item_t* handler_content_items_struct_array = NULL; // Temp storage for handler result (array of structs)

    // 1. Check cache first
    if (server->resource_cache != NULL) {
        if (mcp_cache_get(server->resource_cache, uri, &content_items, &content_count) == 0) {
            // Cache hit! content_items (mcp_content_item_t**) is populated with copies.
            fprintf(stdout, "Cache hit for URI: %s\n", uri);
        } else {
            // Cache miss or expired
            fprintf(stdout, "Cache miss for URI: %s\n", uri);
            content_items = NULL;
            content_count = 0;
        }
    }


    // 2. If not found in cache (or cache disabled), call the resource handler
    if (content_items == NULL) {
        if (server->resource_handler != NULL) {
            // Handler is expected to return an array of structs (mcp_content_item_t*)
            PROFILE_START("resource_handler_callback");
            int handler_status = server->resource_handler(server, uri, server->resource_handler_user_data, &handler_content_items_struct_array, &content_count);
            PROFILE_END("resource_handler_callback");

            if (handler_status != 0 || handler_content_items_struct_array == NULL || content_count == 0) {
                free(handler_content_items_struct_array);
                *error_code = MCP_ERROR_INTERNAL_ERROR;
                char* response = create_error_response(request->id, *error_code, "Resource handler failed or resource not found");
                PROFILE_END("handle_read_resource");
                return response;
            }

            // Allocate our array of pointers (mcp_content_item_t**)
            content_items = (mcp_content_item_t**)malloc(content_count * sizeof(mcp_content_item_t*));
            if (!content_items) {
                 free(handler_content_items_struct_array);
                 *error_code = MCP_ERROR_INTERNAL_ERROR;
                 char* response = create_error_response(request->id, *error_code, "Failed to allocate content pointer array");
                 PROFILE_END("handle_read_resource");
                 return response;
            }

            // Copy data from handler's array of structs into our array of pointers
            bool copy_error = false;
            for(size_t i = 0; i < content_count; ++i) {
                content_items[i] = mcp_content_item_copy(&handler_content_items_struct_array[i]);
                if (!content_items[i]) {
                    copy_error = true;
                    // Free already copied items
                    for(size_t j = 0; j < i; ++j) {
                        mcp_content_item_free(content_items[j]);
                        free(content_items[j]);
                    }
                    free(content_items);
                    content_items = NULL;
                    break;
                }
            }

            // Free the original handler result array (structs) - no need to free internal data as it was copied
            // Note: The handler is responsible for freeing the array it allocated, but not the internal data if copied successfully.
            // We assume the handler allocated handler_content_items_struct_array with malloc.
            free(handler_content_items_struct_array);
            handler_content_items_struct_array = NULL; // Avoid potential double free

            if (copy_error) {
                 *error_code = MCP_ERROR_INTERNAL_ERROR;
                 char* response = create_error_response(request->id, *error_code, "Failed to copy content items from handler");
                 PROFILE_END("handle_read_resource");
                 return response;
            }

            fetched_from_handler = true;
        } else {
             *error_code = MCP_ERROR_INTERNAL_ERROR;
             char* response = create_error_response(request->id, *error_code, "Resource handler not configured");
             PROFILE_END("handle_read_resource");
             return response;
        }
    }


    // 3. If fetched from handler, put it in the cache
    if (fetched_from_handler && server->resource_cache != NULL) {
        // mcp_cache_put now expects mcp_content_item_t** (array of pointers).
        // Pass the content_items array directly. The cache will make its own copies.
        if (mcp_cache_put(server->resource_cache, uri, content_items, content_count, 0) != 0) {
            fprintf(stderr, "Warning: Failed to put resource %s into cache.\n", uri);
            // Note: If cache put fails, we still own content_items and need to free them later.
        } else {
            fprintf(stdout, "Stored resource %s in cache.\n", uri);
            // Note: If cache put succeeds, the cache now owns the copies. We still free our original content_items later.
        }
    }


    // 4. Create response JSON structure using thread-local arena.
    mcp_json_t* contents_json = mcp_json_array_create(); // Use TLS arena
    if (!contents_json) {
        // Free content items if JSON creation fails
        if (content_items) {
            for (size_t i = 0; i < content_count; i++) {
                mcp_content_item_free(content_items[i]);
                free(content_items[i]);
            }
            free(content_items);
        }
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create contents array");
        PROFILE_END("handle_read_resource");
        return response;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < content_count; i++) {
        mcp_content_item_t* item = content_items[i]; // item is mcp_content_item_t*
        mcp_json_t* item_obj = mcp_json_object_create(); // Use TLS arena
        if (!item_obj ||
            mcp_json_object_set_property(item_obj, "uri", mcp_json_string_create(uri)) != 0 || // Use TLS arena
            (item->mime_type && mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(item->mime_type)) != 0) || // Use TLS arena
            (item->type == MCP_CONTENT_TYPE_TEXT && item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create((const char*)item->data)) != 0) || // Use TLS arena
            // TODO: Handle binary data (e.g., base64 encode)?
            mcp_json_array_add_item(contents_json, item_obj) != 0)
        {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }
    }

    // Free content items array and the items it points to AFTER creating JSON copies
    if (content_items) {
        for (size_t i = 0; i < content_count; i++) {
             mcp_content_item_free(content_items[i]); // Free item contents
             free(content_items[i]); // Free the item struct pointer
        }
        free(content_items); // Free the array itself
    }


    if (json_build_error) {
        mcp_json_destroy(contents_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build content item JSON");
        PROFILE_END("handle_read_resource");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create(); // Use TLS arena
    if (!result_obj || mcp_json_object_set_property(result_obj, "contents", contents_json) != 0) {
        mcp_json_destroy(contents_json); // Still need destroy for internal mallocs
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_read_resource");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj); // Malloc'd result content
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_read_resource");
        return response;
    }


    // Arena handles params_json cleanup via handle_message caller.
    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_read_resource");
    return response;
}

/**
 * @internal
 * @brief Handles the 'list_tools' request.
 * Iterates through the server's registered tools and builds a JSON response
 * including the input schema for each tool. Uses thread-local arena for temp nodes, malloc for final string.
 */
char* handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code) {
    PROFILE_START("handle_list_tools");
    // No params, arena unused.
    (void)arena;

    if (server == NULL || request == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_list_tools"); // End profile on error
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

    if (!server->capabilities.tools_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Tools not supported");
        PROFILE_END("handle_list_tools");
         return response;
    }

    // Create response JSON structure using thread-local arena.
    mcp_json_t* tools_json = mcp_json_array_create(); // Use TLS arena
    if (!tools_json) {
        *error_code = MCP_ERROR_INTERNAL_ERROR; // Allocation failure
        char* response = create_error_response(request->id, *error_code, "Failed to create tools array");
        PROFILE_END("handle_list_tools");
        return response;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < server->tool_count; i++) {
        mcp_tool_t* tool = server->tools[i];
        mcp_json_t* tool_obj = mcp_json_object_create(); // Use TLS arena
        mcp_json_t* schema_obj = NULL;
        mcp_json_t* props_obj = NULL;
        mcp_json_t* req_arr = NULL;

        if (!tool_obj || mcp_json_object_set_property(tool_obj, "name", mcp_json_string_create(tool->name)) != 0 || // Use TLS arena
            (tool->description && mcp_json_object_set_property(tool_obj, "description", mcp_json_string_create(tool->description)) != 0)) // Use TLS arena
        {
            json_build_error = true; goto tool_loop_cleanup;
        }

        if (tool->input_schema_count > 0) {
            schema_obj = mcp_json_object_create(); // Use TLS arena
            props_obj = mcp_json_object_create(); // Use TLS arena
            req_arr = mcp_json_array_create(); // Use TLS arena
            if (!schema_obj || !props_obj || !req_arr ||
                mcp_json_object_set_property(schema_obj, "type", mcp_json_string_create("object")) != 0 || // Use TLS arena
                mcp_json_object_set_property(schema_obj, "properties", props_obj) != 0) // Add props obj early
            {
                 json_build_error = true; goto tool_loop_cleanup;
            }

            for (size_t j = 0; j < tool->input_schema_count; j++) {
                mcp_tool_param_schema_t* param = &tool->input_schema[j];
                mcp_json_t* param_obj = mcp_json_object_create(); // Use TLS arena
                 if (!param_obj ||
                    mcp_json_object_set_property(param_obj, "type", mcp_json_string_create(param->type)) != 0 || // Use TLS arena
                    (param->description && mcp_json_object_set_property(param_obj, "description", mcp_json_string_create(param->description)) != 0) || // Use TLS arena
                    mcp_json_object_set_property(props_obj, param->name, param_obj) != 0) // Add param to props
                 {
                     mcp_json_destroy(param_obj);
                     json_build_error = true; goto tool_loop_cleanup;
                 }

                 if (param->required) {
                     mcp_json_t* name_str = mcp_json_string_create(param->name); // Use TLS arena
                     if (!name_str || mcp_json_array_add_item(req_arr, name_str) != 0) {
                         mcp_json_destroy(name_str); // Still need destroy for internal malloc
                         json_build_error = true; goto tool_loop_cleanup;
                     }
                 }
            }

            if (mcp_json_array_get_size(req_arr) > 0) {
                if (mcp_json_object_set_property(schema_obj, "required", req_arr) != 0) {
                     json_build_error = true; goto tool_loop_cleanup;
                }
            } else {
                mcp_json_destroy(req_arr); // Destroy empty required array
                req_arr = NULL;
            }

             if (mcp_json_object_set_property(tool_obj, "inputSchema", schema_obj) != 0) {
                 json_build_error = true; goto tool_loop_cleanup;
             }
        }

        if (mcp_json_array_add_item(tools_json, tool_obj) != 0) {
             json_build_error = true; goto tool_loop_cleanup;
        }
        continue; // Success for this tool

    tool_loop_cleanup:
        mcp_json_destroy(req_arr);
        // props_obj is owned by schema_obj if set successfully
        // mcp_json_destroy(props_obj);
        mcp_json_destroy(schema_obj);
        mcp_json_destroy(tool_obj);
        if (json_build_error) break; // Exit outer loop on error
    }

    if (json_build_error) {
        mcp_json_destroy(tools_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build tool JSON");
        PROFILE_END("handle_list_tools");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create(); // Use TLS arena
    if (!result_obj || mcp_json_object_set_property(result_obj, "tools", tools_json) != 0) {
        mcp_json_destroy(tools_json); // Still need destroy for internal mallocs
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_list_tools");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj); // Malloc'd result content
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_list_tools");
        return response;
    }

    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_list_tools");
    return response;
}

/**
 * @internal
 * @brief Handles the 'call_tool' request.
 * Parses the 'name' and 'arguments' parameters using the thread-local arena,
 * calls the registered tool handler, and builds the JSON response (using TLS arena for temp nodes, malloc for final string).
 */
char* handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code) {
    PROFILE_START("handle_call_tool");
    if (server == NULL || request == NULL || arena == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_call_tool"); // End profile on error
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

    if (!server->capabilities.tools_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Tools not supported");
        PROFILE_END("handle_call_tool");
        return response;
    }

     if (request->params == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Missing parameters");
        PROFILE_END("handle_call_tool");
        return response;
    }

    // Parse params using thread-local arena
    mcp_json_t* params_json = mcp_json_parse(request->params); // Use TLS arena
    if (params_json == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Invalid parameters JSON");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* name_json = mcp_json_object_get_property(params_json, "name");
    const char* name = NULL;
    if (name_json == NULL || mcp_json_get_type(name_json) != MCP_JSON_STRING || mcp_json_get_string(name_json, &name) != 0) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Missing or invalid 'name' parameter");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* args_json = mcp_json_object_get_property(params_json, "arguments");
    // Arguments can be any JSON type, stringify them for the handler
    char* args_str = NULL;
    if (args_json != NULL) {
        args_str = mcp_json_stringify(args_json); // Uses malloc
        if (args_str == NULL) {
            *error_code = MCP_ERROR_INTERNAL_ERROR;
            char* response = create_error_response(request->id, *error_code, "Failed to stringify arguments");
            PROFILE_END("handle_call_tool");
            return response;
        }
    }

    // Call the tool handler
    mcp_content_item_t* content_items = NULL; // Handler allocates this array (malloc)
    size_t content_count = 0;
    bool is_error = false;
    int handler_status = -1;

    if (server->tool_handler != NULL) {
        PROFILE_START("tool_handler_callback");
        handler_status = server->tool_handler(server, name, args_str ? args_str : "{}", server->tool_handler_user_data, &content_items, &content_count, &is_error);
        PROFILE_END("tool_handler_callback");
    }
    free(args_str); // Free stringified arguments

    if (handler_status != 0 || content_items == NULL || content_count == 0) {
        free(content_items);
        *error_code = MCP_ERROR_INTERNAL_ERROR; // Or more specific
        char* response = create_error_response(request->id, *error_code, "Tool handler failed or tool not found");
        PROFILE_END("handle_call_tool");
        return response;
    }

    // Create response JSON structure using thread-local arena.
    mcp_json_t* content_json = mcp_json_array_create(); // Use TLS arena
    if (!content_json) {
         // Free handler-allocated content if JSON creation fails
         for (size_t i = 0; i < content_count; i++) mcp_content_item_free(&content_items[i]);
         free(content_items);
         *error_code = MCP_ERROR_INTERNAL_ERROR; // Allocation failure
         char* response = create_error_response(request->id, *error_code, "Failed to create content array");
         PROFILE_END("handle_call_tool");
         return response;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < content_count; i++) {
        mcp_content_item_t* item = &content_items[i];
        mcp_json_t* item_obj = mcp_json_object_create(); // Use TLS arena
        const char* type_str;
        switch(item->type) {
            case MCP_CONTENT_TYPE_TEXT: type_str = "text"; break;
            case MCP_CONTENT_TYPE_JSON: type_str = "json"; break;
            case MCP_CONTENT_TYPE_BINARY: type_str = "binary"; break;
            default: type_str = "unknown"; break;
        }

        if (!item_obj ||
            mcp_json_object_set_property(item_obj, "type", mcp_json_string_create(type_str)) != 0 || // Use TLS arena
            (item->mime_type && mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(item->mime_type)) != 0) || // Use TLS arena
            (item->type == MCP_CONTENT_TYPE_TEXT && item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create((const char*)item->data)) != 0) || // Use TLS arena
            // TODO: Handle binary data?
            mcp_json_array_add_item(content_json, item_obj) != 0)
        {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }
    }

    // Free handler-allocated content items
    for (size_t i = 0; i < content_count; i++) mcp_content_item_free(&content_items[i]);
    free(content_items);

     if (json_build_error) {
        mcp_json_destroy(content_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build content item JSON");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create(); // Use TLS arena
    if (!result_obj ||
        mcp_json_object_set_property(result_obj, "content", content_json) != 0 ||
        mcp_json_object_set_property(result_obj, "isError", mcp_json_boolean_create(is_error)) != 0) // Use TLS arena
    {
        mcp_json_destroy(content_json); // Still need destroy for internal mallocs
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_call_tool");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj); // Malloc'd result content
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_call_tool");
        return response;
    }

    // Arena handles params_json cleanup via caller
    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_call_tool");
    return response;
}
