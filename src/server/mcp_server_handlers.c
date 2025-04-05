#include "internal/server_internal.h"
#include "mcp_auth.h" // Include auth header for context and checks
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// --- Internal Request Handler Implementations ---

/**
 * @internal
 * @brief Handles the 'list_resources' request.
 */
char* handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    PROFILE_START("handle_list_resources");
    (void)arena; // Arena not used for this handler

    if (server == NULL || request == NULL || auth_context == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_list_resources");
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

    if (!server->capabilities.resources_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Resources not supported");
        PROFILE_END("handle_list_resources");
        return response;
    }

    // Permission Check (Optional for list, but good practice)
    // For listing, we might check a general permission like "list_resources"
    // if (!mcp_auth_check_general_permission(auth_context, "list_resources")) {
    //     *error_code = MCP_ERROR_FORBIDDEN;
    //     char* response = create_error_response(request->id, *error_code, "Permission denied to list resources");
    //     PROFILE_END("handle_list_resources");
    //     return response;
    // }

    mcp_json_t* resources_json = mcp_json_array_create();
    if (!resources_json) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create resources array");
        PROFILE_END("handle_list_resources");
        return response;
    }

    bool build_error = false;
    for (size_t i = 0; i < server->resource_count; i++) {
        mcp_resource_t* resource = server->resources[i];
        mcp_json_t* res_obj = mcp_json_object_create();
        if (!res_obj ||
            (resource->uri && mcp_json_object_set_property(res_obj, "uri", mcp_json_string_create(resource->uri)) != 0) ||
            (resource->name && mcp_json_object_set_property(res_obj, "name", mcp_json_string_create(resource->name)) != 0) ||
            (resource->mime_type && mcp_json_object_set_property(res_obj, "mimeType", mcp_json_string_create(resource->mime_type)) != 0) ||
            (resource->description && mcp_json_object_set_property(res_obj, "description", mcp_json_string_create(resource->description)) != 0) ||
            mcp_json_array_add_item(resources_json, res_obj) != 0)
        {
            mcp_json_destroy(res_obj);
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

    mcp_json_t* result_obj = mcp_json_object_create();
    if (!result_obj || mcp_json_object_set_property(result_obj, "resources", resources_json) != 0) {
        mcp_json_destroy(resources_json);
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_list_resources");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_list_resources");
        return response;
    }

    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_list_resources");
    return response;
}

/**
 * @internal
 * @brief Handles the 'list_resource_templates' request.
 */
char* handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    PROFILE_START("handle_list_resource_templates");
    (void)arena;

    if (server == NULL || request == NULL || auth_context == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_list_resource_templates");
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

     if (!server->capabilities.resources_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Resources not supported");
        PROFILE_END("handle_list_resource_templates");
         return response;
    }

    // Permission Check (Optional for list)
    // if (!mcp_auth_check_general_permission(auth_context, "list_resource_templates")) { ... }

    mcp_json_t* templates_json = mcp_json_array_create();
     if (!templates_json) {
         *error_code = MCP_ERROR_INTERNAL_ERROR;
         char* response = create_error_response(request->id, *error_code, "Failed to create templates array");
         PROFILE_END("handle_list_resource_templates");
         return response;
     }

    bool build_error = false;
    for (size_t i = 0; i < server->resource_template_count; i++) {
        mcp_resource_template_t* tmpl = server->resource_templates[i];
        mcp_json_t* tmpl_obj = mcp_json_object_create();
        if (!tmpl_obj ||
            (tmpl->uri_template && mcp_json_object_set_property(tmpl_obj, "uriTemplate", mcp_json_string_create(tmpl->uri_template)) != 0) ||
            (tmpl->name && mcp_json_object_set_property(tmpl_obj, "name", mcp_json_string_create(tmpl->name)) != 0) ||
            (tmpl->mime_type && mcp_json_object_set_property(tmpl_obj, "mimeType", mcp_json_string_create(tmpl->mime_type)) != 0) ||
            (tmpl->description && mcp_json_object_set_property(tmpl_obj, "description", mcp_json_string_create(tmpl->description)) != 0) ||
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

    mcp_json_t* result_obj = mcp_json_object_create();
    if (!result_obj || mcp_json_object_set_property(result_obj, "resourceTemplates", templates_json) != 0) {
        mcp_json_destroy(templates_json);
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_list_resource_templates");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
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
 */
char* handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    PROFILE_START("handle_read_resource");
    if (server == NULL || request == NULL || arena == NULL || auth_context == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_read_resource");
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

    mcp_json_t* params_json = mcp_json_parse(request->params);
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

    // --- Permission Check ---
    if (!mcp_auth_check_resource_access(auth_context, uri)) {
        *error_code = MCP_ERROR_FORBIDDEN; // Use a specific permission error
        char* response = create_error_response(request->id, *error_code, "Access denied to resource");
        PROFILE_END("handle_read_resource");
        return response;
    }
    // --- End Permission Check ---


    mcp_content_item_t** content_items = NULL;
    size_t content_count = 0;
    bool fetched_from_handler = false;
 
    // 1. Check cache first
    if (server->resource_cache != NULL) {
        if (mcp_cache_get(server->resource_cache, uri, &content_items, &content_count) == 0) {
            fprintf(stdout, "Cache hit for URI: %s\n", uri);
        } else {
            fprintf(stdout, "Cache miss for URI: %s\n", uri);
            content_items = NULL;
            content_count = 0;
        }
    }

    // 2. If not found in cache, call the resource handler
    if (content_items == NULL) {
        if (server->resource_handler != NULL) {
            char* handler_error_message = NULL;
            mcp_error_code_t handler_status = MCP_ERROR_NONE;

            PROFILE_START("resource_handler_callback");
            handler_status = server->resource_handler(
                server,
                uri,
                server->resource_handler_user_data,
                &content_items,
                &content_count,
                &handler_error_message
            );
            PROFILE_END("resource_handler_callback");

            if (handler_status != MCP_ERROR_NONE) {
                if (content_items) { // Cleanup potentially partial results from handler
                    for(size_t i = 0; i < content_count; ++i) {
                        if (content_items[i]) mcp_content_item_free(content_items[i]);
                    }
                    free(content_items);
                    content_items = NULL;
                }
                *error_code = handler_status;
                const char* msg = handler_error_message ? handler_error_message : "Resource handler failed or resource not found";
                char* response = create_error_response(request->id, *error_code, msg);
                free(handler_error_message);
                PROFILE_END("handle_read_resource");
                return response;
            }

            if (content_items == NULL || content_count == 0) {
                 free(handler_error_message);
                 *error_code = MCP_ERROR_INTERNAL_ERROR;
                 char* response = create_error_response(request->id, *error_code, "Resource handler returned success but no content");
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
        if (mcp_cache_put(server->resource_cache, uri, content_items, content_count, 0) != 0) {
            fprintf(stderr, "Warning: Failed to put resource %s into cache.\n", uri);
        } else {
            fprintf(stdout, "Stored resource %s in cache.\n", uri);
        }
    }

    // 4. Create response JSON structure
    mcp_json_t* contents_json = mcp_json_array_create();
    if (!contents_json) {
        if (content_items) { // Cleanup if JSON creation fails
            for (size_t i = 0; i < content_count; i++) mcp_content_item_free(content_items[i]);
            free(content_items);
        }
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create contents array");
        PROFILE_END("handle_read_resource");
        return response;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < content_count; i++) {
        mcp_content_item_t* item = content_items[i];
        mcp_json_t* item_obj = mcp_json_object_create();
        if (!item_obj ||
            mcp_json_object_set_property(item_obj, "uri", mcp_json_string_create(uri)) != 0 ||
            (item->mime_type && mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(item->mime_type)) != 0) ||
            (item->type == MCP_CONTENT_TYPE_TEXT && item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create((const char*)item->data)) != 0) ||
            mcp_json_array_add_item(contents_json, item_obj) != 0)
        {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }
    }

    // Free original content items after copying to JSON
    if (content_items) {
        for (size_t i = 0; i < content_count; i++) mcp_content_item_free(content_items[i]);
        free(content_items);
    }

    if (json_build_error) {
        mcp_json_destroy(contents_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build content item JSON");
        PROFILE_END("handle_read_resource");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create();
    if (!result_obj || mcp_json_object_set_property(result_obj, "contents", contents_json) != 0) {
        mcp_json_destroy(contents_json);
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_read_resource");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_read_resource");
        return response;
    }

    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_read_resource");
    return response;
}

/**
 * @internal
 * @brief Handles the 'list_tools' request.
 */
char* handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    PROFILE_START("handle_list_tools");
    (void)arena;

    if (server == NULL || request == NULL || auth_context == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_list_tools");
         return NULL;
    }
    *error_code = MCP_ERROR_NONE;

    if (!server->capabilities.tools_supported) {
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        char* response = create_error_response(request->id, *error_code, "Tools not supported");
        PROFILE_END("handle_list_tools");
         return response;
    }

    // Permission Check (Optional for list)
    // if (!mcp_auth_check_general_permission(auth_context, "list_tools")) { ... }

    mcp_json_t* tools_json = mcp_json_array_create();
    if (!tools_json) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create tools array");
        PROFILE_END("handle_list_tools");
        return response;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < server->tool_count; i++) {
        mcp_tool_t* tool = server->tools[i];
        mcp_json_t* tool_obj = mcp_json_object_create();
        mcp_json_t* schema_obj = NULL;
        mcp_json_t* props_obj = NULL;
        mcp_json_t* req_arr = NULL;

        if (!tool_obj || mcp_json_object_set_property(tool_obj, "name", mcp_json_string_create(tool->name)) != 0 ||
            (tool->description && mcp_json_object_set_property(tool_obj, "description", mcp_json_string_create(tool->description)) != 0))
        {
            json_build_error = true; goto tool_loop_cleanup;
        }

        if (tool->input_schema_count > 0) {
            schema_obj = mcp_json_object_create();
            props_obj = mcp_json_object_create();
            req_arr = mcp_json_array_create();
            if (!schema_obj || !props_obj || !req_arr ||
                mcp_json_object_set_property(schema_obj, "type", mcp_json_string_create("object")) != 0 ||
                mcp_json_object_set_property(schema_obj, "properties", props_obj) != 0)
            {
                 json_build_error = true; goto tool_loop_cleanup;
            }

            for (size_t j = 0; j < tool->input_schema_count; j++) {
                mcp_tool_param_schema_t* param = &tool->input_schema[j];
                mcp_json_t* param_obj = mcp_json_object_create();
                 if (!param_obj ||
                    mcp_json_object_set_property(param_obj, "type", mcp_json_string_create(param->type)) != 0 ||
                    (param->description && mcp_json_object_set_property(param_obj, "description", mcp_json_string_create(param->description)) != 0) ||
                    mcp_json_object_set_property(props_obj, param->name, param_obj) != 0)
                 {
                     mcp_json_destroy(param_obj);
                     json_build_error = true; goto tool_loop_cleanup;
                 }

                 if (param->required) {
                     mcp_json_t* name_str = mcp_json_string_create(param->name);
                     if (!name_str || mcp_json_array_add_item(req_arr, name_str) != 0) {
                         mcp_json_destroy(name_str);
                         json_build_error = true; goto tool_loop_cleanup;
                     }
                 }
            }

            if (mcp_json_array_get_size(req_arr) > 0) {
                if (mcp_json_object_set_property(schema_obj, "required", req_arr) != 0) {
                     json_build_error = true; goto tool_loop_cleanup;
                }
            } else {
                mcp_json_destroy(req_arr);
                req_arr = NULL;
            }

             if (mcp_json_object_set_property(tool_obj, "inputSchema", schema_obj) != 0) {
                 json_build_error = true; goto tool_loop_cleanup;
             }
        }

        if (mcp_json_array_add_item(tools_json, tool_obj) != 0) {
             json_build_error = true; goto tool_loop_cleanup;
        }
        continue;

    tool_loop_cleanup:
        mcp_json_destroy(req_arr);
        mcp_json_destroy(schema_obj);
        mcp_json_destroy(tool_obj);
        if (json_build_error) break;
    }

    if (json_build_error) {
        mcp_json_destroy(tools_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build tool JSON");
        PROFILE_END("handle_list_tools");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create();
    if (!result_obj || mcp_json_object_set_property(result_obj, "tools", tools_json) != 0) {
        mcp_json_destroy(tools_json);
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_list_tools");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
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
 */
char* handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    PROFILE_START("handle_call_tool");
    if (server == NULL || request == NULL || arena == NULL || auth_context == NULL || error_code == NULL) {
         if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
         PROFILE_END("handle_call_tool");
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

    mcp_json_t* params_json = mcp_json_parse(request->params);
    if (params_json == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Invalid parameters JSON");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* name_json = mcp_json_object_get_property(params_json, "name");
    const char* name = NULL;
    if (name_json == NULL || mcp_json_get_type(name_json) != MCP_JSON_STRING || mcp_json_get_string(name_json, &name) != 0 || name == NULL) {
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Missing or invalid 'name' parameter");
        PROFILE_END("handle_call_tool");
        return response;
    }

    // --- Permission Check ---
    if (!mcp_auth_check_tool_access(auth_context, name)) {
        *error_code = MCP_ERROR_FORBIDDEN; // Use a specific permission error
        char* response = create_error_response(request->id, *error_code, "Access denied to tool");
        PROFILE_END("handle_call_tool");
        return response;
    }
    // --- End Permission Check ---


    mcp_json_t* args_json = mcp_json_object_get_property(params_json, "arguments");
    // Arguments can be any JSON type. Pass the parsed mcp_json_t* directly.
    // args_json will be NULL if "arguments" is not present or not an object/array/etc.
    // The handler should check for NULL if arguments are expected.

    // Call the tool handler
    mcp_content_item_t** content_items = NULL;
    size_t content_count = 0;
    bool is_error = false;
    char* handler_error_message = NULL;
    mcp_error_code_t handler_status = MCP_ERROR_NONE;

    if (server->tool_handler != NULL) {
        PROFILE_START("tool_handler_callback");
        handler_status = server->tool_handler(
            server,
            name,
            args_json,
            server->tool_handler_user_data,
            &content_items,
            &content_count,
            &is_error,
            &handler_error_message
        );
        PROFILE_END("tool_handler_callback");
    } else {
        handler_status = MCP_ERROR_INTERNAL_ERROR;
        handler_error_message = mcp_strdup("Tool handler not configured");
    }

    if (handler_status != MCP_ERROR_NONE) {
        if (content_items) { // Cleanup potentially partial results from handler
            for(size_t i = 0; i < content_count; ++i) {
                if (content_items[i]) mcp_content_item_free(content_items[i]);
            }
            free(content_items);
            content_items = NULL;
        }
        *error_code = handler_status;
        const char* msg = handler_error_message ? handler_error_message : "Tool handler failed or tool not found";
        char* response = create_error_response(request->id, *error_code, msg);
        free(handler_error_message);
        PROFILE_END("handle_call_tool");
        return response;
    }

    // Handler returned success, build response
    mcp_json_t* content_json = mcp_json_array_create();
    if (!content_json) {
         if (content_items) { // Cleanup if JSON creation fails
             for (size_t i = 0; i < content_count; i++) mcp_content_item_free(content_items[i]);
             free(content_items);
         }
         free(handler_error_message);
         *error_code = MCP_ERROR_INTERNAL_ERROR;
         char* response = create_error_response(request->id, *error_code, "Failed to create content array");
         PROFILE_END("handle_call_tool");
         return response;
    }

    bool json_build_error = false;
    if (content_items) {
        for (size_t i = 0; i < content_count; i++) {
            mcp_content_item_t* item = content_items[i];
            mcp_json_t* item_obj = mcp_json_object_create();
            const char* type_str;
            switch(item->type) {
            case MCP_CONTENT_TYPE_TEXT: type_str = "text"; break;
            case MCP_CONTENT_TYPE_JSON: type_str = "json"; break;
                case MCP_CONTENT_TYPE_BINARY: type_str = "binary"; break;
                default: type_str = "unknown"; break;
            }

            if (!item_obj ||
                mcp_json_object_set_property(item_obj, "type", mcp_json_string_create(type_str)) != 0 ||
                (item->mime_type && mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(item->mime_type)) != 0) ||
                (item->type == MCP_CONTENT_TYPE_TEXT && item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create((const char*)item->data)) != 0) ||
                mcp_json_array_add_item(content_json, item_obj) != 0)
            {
                mcp_json_destroy(item_obj);
                json_build_error = true;
                break;
            }
        }
    }

    // Free handler-allocated content items after copying to JSON
    if (content_items) {
        for (size_t i = 0; i < content_count; i++) mcp_content_item_free(content_items[i]);
        free(content_items);
    }
    free(handler_error_message);

     if (json_build_error) {
        mcp_json_destroy(content_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to build content item JSON");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* result_obj = mcp_json_object_create();
    if (!result_obj ||
        mcp_json_object_set_property(result_obj, "content", content_json) != 0 ||
        mcp_json_object_set_property(result_obj, "isError", mcp_json_boolean_create(is_error)) != 0)
    {
        mcp_json_destroy(content_json);
        mcp_json_destroy(result_obj);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_call_tool");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_call_tool");
        return response;
    }

    char* response = create_success_response(request->id, result_str);
    PROFILE_END("handle_call_tool");
    return response;
}

