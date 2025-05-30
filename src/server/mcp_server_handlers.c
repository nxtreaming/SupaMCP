#include "internal/server_internal.h"
#include "mcp_auth.h"
#include "mcp_template.h"
#include "mcp_hashtable.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"
#include "mcp_types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Macros for common operations
#define HANDLER_BEGIN(name) \
    do { \
        PROFILE_START(name); \
        if (server == NULL || request == NULL || auth_context == NULL || error_code == NULL) { \
            if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS; \
            PROFILE_END(name); \
            return NULL; \
        } \
        *error_code = MCP_ERROR_NONE; \
    } while(0)

#define CHECK_CAPABILITY(name, capability, error_msg) \
    do { \
        if (!server->capabilities.capability) { \
            *error_code = MCP_ERROR_METHOD_NOT_FOUND; \
            char* response = create_error_response(request->id, *error_code, error_msg); \
            PROFILE_END(name); \
            return response; \
        } \
    } while(0)

#define CHECK_PARAMS(name) \
    if (request->params == NULL) { \
        *error_code = MCP_ERROR_INVALID_PARAMS; \
        char* response = create_error_response(request->id, *error_code, "Missing parameters"); \
        PROFILE_END(name); \
        return response; \
    } \
    mcp_json_t* params_json = mcp_json_parse(request->params); \
    if (params_json == NULL) { \
        *error_code = MCP_ERROR_INVALID_PARAMS; \
        char* response = create_error_response(request->id, *error_code, "Invalid parameters JSON"); \
        PROFILE_END(name); \
        return response; \
    }

#define RETURN_ERROR(name, code, msg) \
    do { \
        *error_code = code; \
        char* response = create_error_response(request->id, *error_code, msg); \
        PROFILE_END(name); \
        return response; \
    } while(0)

#define CLEANUP_JSON_AND_RETURN_ERROR(name, json_obj, code, msg) \
    do { \
        mcp_json_destroy(json_obj); \
        *error_code = code; \
        char* response = create_error_response(request->id, *error_code, msg); \
        PROFILE_END(name); \
        return response; \
    } while(0)

// Helper functions for common operations
static char* create_and_return_success_response(const char* profile_name, uint64_t request_id, char* result_str) {
    (void)profile_name;
    char* response = create_success_response(request_id, result_str);
    PROFILE_END(profile_name);
    return response;
}

static char* handle_json_stringify_error(const char* profile_name, uint64_t request_id, int* error_code, mcp_json_t* params_json) {
    (void)profile_name;
    mcp_json_destroy(params_json);
    *error_code = MCP_ERROR_INTERNAL_ERROR;
    char* response = create_error_response(request_id, *error_code, "Failed to stringify result");
    PROFILE_END(profile_name);
    return response;
}

static bool extract_string_param(mcp_json_t* params_json, const char* param_name, const char** value) {
    mcp_json_t* json_value = mcp_json_object_get_property(params_json, param_name);
    return !(json_value == NULL ||
             mcp_json_get_type(json_value) != MCP_JSON_STRING ||
             mcp_json_get_string(json_value, value) != 0 ||
             *value == NULL);
}

// Helper function to create a JSON result object and stringify it
static char* create_json_result(const char* profile_name, uint64_t request_id, int* error_code,
                               mcp_json_t* content_json, const char* property_name, mcp_json_t* params_json) {
    (void)profile_name;
    mcp_json_t* result_obj = mcp_json_object_create();
    if (!result_obj || mcp_json_object_set_property(result_obj, property_name, content_json) != 0) {
        mcp_json_destroy(content_json);
        mcp_json_destroy(result_obj);
        if (params_json) mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request_id, *error_code, "Failed to create result object");
        PROFILE_END(profile_name);
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);

    if (!result_str) {
        if (params_json) mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request_id, *error_code, "Failed to stringify result");
        PROFILE_END(profile_name);
        return response;
    }

    if (params_json) mcp_json_destroy(params_json);
    return create_success_response(request_id, result_str);
}

// Helper function to clean up content items
static void cleanup_content_items(mcp_server_t* server, mcp_content_item_t** content_items, size_t content_count) {
    if (!content_items) return;

    for (size_t i = 0; i < content_count; i++) {
        if (content_items[i]) {
            if (server && server->content_item_pool) {
                mcp_object_pool_release(server->content_item_pool, content_items[i]);
            } else {
                mcp_content_item_free(content_items[i]);
            }
        }
    }
    mcp_safe_free(content_items, content_count * sizeof(mcp_content_item_t*));
}

// Helper function to build content JSON from content items
static bool build_content_json(mcp_json_t* contents_json, mcp_content_item_t** content_items,
                              size_t content_count, const char* uri) {
    bool json_build_error = false;
    mcp_json_t* uri_json_str = mcp_json_string_create(uri);

    if (!uri_json_str) {
        return true; // Error occurred
    }

    for (size_t i = 0; i < content_count && !json_build_error; i++) {
        mcp_content_item_t* item = content_items[i];
        if (!item) continue;

        mcp_json_t* item_obj = mcp_json_object_create_with_capacity(3);
        if (!item_obj) {
            json_build_error = true;
            break;
        }

        if (mcp_json_object_set_property(item_obj, "uri", uri_json_str) != 0) {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }

        // Only increment reference count after first use
        if (i == 0) {
            mcp_json_increment_ref_count(uri_json_str);
        }

        if (item->mime_type &&
            mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(item->mime_type)) != 0) {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }

        if (item->type == MCP_CONTENT_TYPE_TEXT && item->data &&
            mcp_json_object_set_property(item_obj, "text", mcp_json_string_create((const char*)item->data)) != 0) {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }

        if (mcp_json_array_add_item(contents_json, item_obj) != 0) {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }
    }

    mcp_json_destroy(uri_json_str);
    return json_build_error;
}

// Helper function to handle template resource
static int handle_template_resource(mcp_server_t* server, const char* uri, mcp_content_item_t*** content_items,
                                   size_t* content_count, bool* fetched_from_handler) {
    if (!server->template_routes_table) {
        return MCP_ERROR_RESOURCE_NOT_FOUND;
    }

    char* handler_error_message = NULL;
    mcp_error_code_t handler_status = MCP_ERROR_NONE;

    PROFILE_START("template_handler_callback");
    handler_status = mcp_server_handle_template_resource(
        server, uri, content_items, content_count, &handler_error_message);
    PROFILE_END("template_handler_callback");

    if (handler_status != MCP_ERROR_RESOURCE_NOT_FOUND) {
        *fetched_from_handler = true;
        if (handler_status != MCP_ERROR_NONE) {
            free(handler_error_message);
            return handler_status;
        }
        return MCP_ERROR_NONE;
    }

    free(handler_error_message);
    return MCP_ERROR_RESOURCE_NOT_FOUND;
}

// Helper function to handle resource handler
static int handle_resource_handler(mcp_server_t* server, const char* uri, mcp_content_item_t*** content_items,
                                  size_t* content_count, bool* fetched_from_handler) {
    if (!server->resource_handler) {
        return MCP_ERROR_RESOURCE_NOT_FOUND;
    }

    char* handler_error_message = NULL;
    mcp_error_code_t handler_status = MCP_ERROR_NONE;

    PROFILE_START("resource_handler_callback");
    handler_status = server->resource_handler(
        server, uri, server->resource_handler_user_data,
        content_items, content_count, &handler_error_message);
    PROFILE_END("resource_handler_callback");

    if (handler_status != MCP_ERROR_NONE) {
        free(handler_error_message);
        return handler_status;
    }

    if (*content_items == NULL || *content_count == 0) {
        free(handler_error_message);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    *fetched_from_handler = true;
    free(handler_error_message);
    return MCP_ERROR_NONE;
}

// Helper function to cache resource content
static void cache_resource_content(mcp_server_t* server, const char* uri, mcp_content_item_t** content_items,
                                  size_t content_count, bool fetched_from_handler) {
    if (!fetched_from_handler || !server->resource_cache) {
        return;
    }

    PROFILE_START("cache_store");
    int ttl_seconds = 0; // Default: no expiration

    if (content_count > 0 && content_items[0]) {
        // For text content, use shorter TTL (5 minutes)
        // For binary content, use longer TTL (1 hour)
        ttl_seconds = (content_items[0]->type == MCP_CONTENT_TYPE_TEXT) ? 300 : 3600;
    }

    if (mcp_cache_put(server->resource_cache, uri, server->content_item_pool, content_items, content_count, ttl_seconds) != 0) {
        mcp_log_warn("Failed to put resource %s into cache", uri);
    } else if (mcp_log_get_level() <= MCP_LOG_LEVEL_DEBUG) {
        mcp_log_debug("Stored resource %s in cache with TTL=%d seconds", uri, ttl_seconds);
    }
    PROFILE_END("cache_store");
}

// Context struct for list callbacks
typedef struct {
    mcp_json_t* json_array;
    bool error_occurred;
} list_context_t;

typedef struct {
    mcp_resource_template_t* tmpl;
} template_holder_t;

// Callback to add resource info to JSON array
static void list_resource_callback(const void* key, void* value, void* user_data) {
    (void)key;
    list_context_t* ctx = (list_context_t*)user_data;
    if (ctx->error_occurred) return;

    mcp_resource_t* resource = (mcp_resource_t*)value;
    mcp_json_t* res_obj = mcp_json_object_create();

    if (!res_obj ||
        (resource->uri && mcp_json_object_set_property(res_obj, "uri", mcp_json_string_create(resource->uri)) != 0) ||
        (resource->name && mcp_json_object_set_property(res_obj, "name", mcp_json_string_create(resource->name)) != 0) ||
        (resource->mime_type && mcp_json_object_set_property(res_obj, "mimeType", mcp_json_string_create(resource->mime_type)) != 0) ||
        (resource->description && mcp_json_object_set_property(res_obj, "description", mcp_json_string_create(resource->description)) != 0) ||
        mcp_json_array_add_item(ctx->json_array, res_obj) != 0)
    {
        mcp_json_destroy(res_obj);
        ctx->error_occurred = true;
    }
}

/**
 * @internal
 * @brief Handles the 'list_resources' request.
 */
char* handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    const char* profile_name = "handle_list_resources";
    HANDLER_BEGIN(profile_name);
    (void)arena;

    CHECK_CAPABILITY(profile_name, resources_supported, "Resources not supported");

    mcp_json_t* resources_json = mcp_json_array_create();
    if (!resources_json) {
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to create resources array");
    }

    list_context_t context = { .json_array = resources_json, .error_occurred = false };
    if (server->resources_table) {
        mcp_hashtable_foreach(server->resources_table, list_resource_callback, &context);
    }

    if (context.error_occurred) {
        mcp_json_destroy(resources_json);
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to build resource JSON");
    }

    char* response = create_json_result(profile_name, request->id, error_code, resources_json, "resources", NULL);
    return response;
}

// Callback to add template info to JSON array
static void list_template_callback(const void* key, void* value, void* user_data) {
    (void)key;
    list_context_t* ctx = (list_context_t*)user_data;
    if (ctx->error_occurred) return;

    mcp_resource_template_t* tmpl = (mcp_resource_template_t*)value;
    mcp_json_t* tmpl_obj = mcp_json_object_create();

    if (!tmpl_obj ||
        (tmpl->uri_template && mcp_json_object_set_property(tmpl_obj, "uriTemplate", mcp_json_string_create(tmpl->uri_template)) != 0) ||
        (tmpl->name && mcp_json_object_set_property(tmpl_obj, "name", mcp_json_string_create(tmpl->name)) != 0) ||
        (tmpl->mime_type && mcp_json_object_set_property(tmpl_obj, "mimeType", mcp_json_string_create(tmpl->mime_type)) != 0) ||
        (tmpl->description && mcp_json_object_set_property(tmpl_obj, "description", mcp_json_string_create(tmpl->description)) != 0) ||
        mcp_json_array_add_item(ctx->json_array, tmpl_obj) != 0)
    {
        mcp_json_destroy(tmpl_obj);
        ctx->error_occurred = true;
    }
}

/**
 * @internal
 * @brief Handles the 'list_resource_templates' request.
 */
char* handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    const char* profile_name = "handle_list_resource_templates";
    HANDLER_BEGIN(profile_name);
    (void)arena;

    CHECK_CAPABILITY(profile_name, resources_supported, "Resources not supported");

    mcp_json_t* templates_json = mcp_json_array_create();
    if (!templates_json) {
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to create templates array");
    }

    list_context_t context = { .json_array = templates_json, .error_occurred = false };
    if (server->resource_templates_table) {
        mcp_hashtable_foreach(server->resource_templates_table, list_template_callback, &context);
    }

    if (context.error_occurred) {
        mcp_json_destroy(templates_json);
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to build template JSON");
    }

    char* response = create_json_result(profile_name, request->id, error_code, templates_json, "resourceTemplates", NULL);
    return response;
}

/**
 * @internal
 * @brief Handles the 'read_resource' request.
 */
char* handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    const char* profile_name = "handle_read_resource";
    HANDLER_BEGIN(profile_name);
    (void)arena;

    CHECK_CAPABILITY(profile_name, resources_supported, "Resources not supported");
    CHECK_PARAMS(profile_name);

    const char* uri = NULL;
    if (!extract_string_param(params_json, "uri", &uri)) {
        mcp_json_destroy(params_json);
        RETURN_ERROR(profile_name, MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'uri' parameter");
    }

    if (!mcp_auth_check_resource_access(auth_context, uri)) {
        mcp_json_destroy(params_json);
        RETURN_ERROR(profile_name, MCP_ERROR_FORBIDDEN, "Access denied to resource");
    }

    mcp_content_item_t** content_items = NULL;
    size_t content_count = 0;
    bool fetched_from_handler = false;
    int handler_status = MCP_ERROR_NONE;

    // 1. Check cache first
    if (server->resource_cache != NULL) {
        PROFILE_START("cache_lookup");
        if (mcp_cache_get(server->resource_cache, uri, server->content_item_pool, &content_items, &content_count) == 0) {
            if (mcp_log_get_level() <= MCP_LOG_LEVEL_DEBUG) {
                mcp_log_debug("Cache hit for URI: %s", uri);
            }
        } else {
            if (mcp_log_get_level() <= MCP_LOG_LEVEL_DEBUG) {
                mcp_log_debug("Cache miss for URI: %s", uri);
            }
            content_items = NULL;
            content_count = 0;
        }
        PROFILE_END("cache_lookup");
    }

    // 2. If not found in cache, try template-based routing first, then fall back to the default resource handler
    if (content_items == NULL) {
        // Try template-based routing first
        handler_status = handle_template_resource(server, uri, &content_items, &content_count, &fetched_from_handler);
        if (handler_status != MCP_ERROR_RESOURCE_NOT_FOUND) {
            if (handler_status != MCP_ERROR_NONE) {
                mcp_json_destroy(params_json);
                RETURN_ERROR(profile_name, handler_status, "Error processing template resource");
            }
        } else if (content_items == NULL) {
            // Try resource handler
            handler_status = handle_resource_handler(server, uri, &content_items, &content_count, &fetched_from_handler);
            if (handler_status != MCP_ERROR_NONE) {
                cleanup_content_items(server, content_items, content_count);
                mcp_json_destroy(params_json);
                RETURN_ERROR(profile_name, handler_status, "Resource handler failed or resource not found");
            }
        }

        // If still no content, check if resource exists in static list
        if (content_items == NULL) {
            void* resource_ptr = NULL;
            if (!server->resources_table || mcp_hashtable_get(server->resources_table, uri, &resource_ptr) != 0) {
                mcp_json_destroy(params_json);
                RETURN_ERROR(profile_name, MCP_ERROR_RESOURCE_NOT_FOUND, "Resource not found and no handler configured");
            }
            // Resource found in static list, but no handler to generate content
            mcp_json_destroy(params_json);
            RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Resource found but no handler configured to read content");
        }
    }

    // 3. If fetched from handler, put it in the cache
    cache_resource_content(server, uri, content_items, content_count, fetched_from_handler);

    // 4. Create response JSON structure
    PROFILE_START("json_build");
    mcp_json_t* contents_json = mcp_json_array_create_with_capacity(content_count);
    if (!contents_json) {
        cleanup_content_items(server, content_items, content_count);
        mcp_json_destroy(params_json);
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to create contents array");
    }

    bool json_build_error = build_content_json(contents_json, content_items, content_count, uri);
    cleanup_content_items(server, content_items, content_count);

    if (json_build_error) {
        mcp_json_destroy(contents_json);
        mcp_json_destroy(params_json);
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to build content item JSON");
    }

    PROFILE_END("json_build");

    char* response = create_json_result(profile_name, request->id, error_code, contents_json, "contents", params_json);
    return response;
}

// Callback to add tool info to JSON array
static void list_tool_callback(const void* key, void* value, void* user_data) {
    (void)key;
    list_context_t* ctx = (list_context_t*)user_data;
    if (ctx->error_occurred) return;

    mcp_tool_t* tool = (mcp_tool_t*)value;
    mcp_json_t* tool_obj = mcp_json_object_create();
    mcp_json_t* schema_obj = NULL;
    mcp_json_t* props_obj = NULL;
    mcp_json_t* req_arr = NULL;
    bool json_build_error = false;

    if (!tool_obj || mcp_json_object_set_property(tool_obj, "name", mcp_json_string_create(tool->name)) != 0 ||
        (tool->description && mcp_json_object_set_property(tool_obj, "description", mcp_json_string_create(tool->description)) != 0))
    {
        json_build_error = true; goto tool_callback_cleanup;
    }

    if (tool->input_schema_count > 0) {
        schema_obj = mcp_json_object_create();
        props_obj = mcp_json_object_create();
        req_arr = mcp_json_array_create();
        if (!schema_obj || !props_obj || !req_arr ||
            mcp_json_object_set_property(schema_obj, "type", mcp_json_string_create("object")) != 0 ||
            mcp_json_object_set_property(schema_obj, "properties", props_obj) != 0)
        {
            json_build_error = true;
            goto tool_callback_cleanup;
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
                json_build_error = true; goto tool_callback_cleanup;
            }

            if (param->required) {
                mcp_json_t* name_str = mcp_json_string_create(param->name);
                if (!name_str || mcp_json_array_add_item(req_arr, name_str) != 0) {
                    mcp_json_destroy(name_str);
                    json_build_error = true; goto tool_callback_cleanup;
                }
            }
        }

        if (mcp_json_array_get_size(req_arr) > 0) {
            if (mcp_json_object_set_property(schema_obj, "required", req_arr) != 0) {
                json_build_error = true; goto tool_callback_cleanup;
            }
        } else {
            mcp_json_destroy(req_arr);
            req_arr = NULL;
        }

         if (mcp_json_object_set_property(tool_obj, "inputSchema", schema_obj) != 0) {
             json_build_error = true; goto tool_callback_cleanup;
         }
    }

    if (mcp_json_array_add_item(ctx->json_array, tool_obj) != 0) {
        json_build_error = true; goto tool_callback_cleanup;
    }
    return;

tool_callback_cleanup:
    mcp_json_destroy(req_arr);
    mcp_json_destroy(schema_obj);
    mcp_json_destroy(tool_obj);
    ctx->error_occurred = true;
}

/**
 * @internal
 * @brief Handles the 'list_tools' request.
 */
char* handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    const char* profile_name = "handle_list_tools";
    HANDLER_BEGIN(profile_name);
    (void)arena;

    CHECK_CAPABILITY(profile_name, tools_supported, "Tools not supported");

    mcp_json_t* tools_json = mcp_json_array_create();
    if (!tools_json) {
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to create tools array");
    }

    list_context_t context = { .json_array = tools_json, .error_occurred = false };
    if (server->tools_table) {
        mcp_hashtable_foreach(server->tools_table, list_tool_callback, &context);
    }

    if (context.error_occurred) {
        mcp_json_destroy(tools_json);
        RETURN_ERROR(profile_name, MCP_ERROR_INTERNAL_ERROR, "Failed to build tool JSON");
    }

    char* response = create_json_result(profile_name, request->id, error_code, tools_json, "tools", NULL);
    return response;
}

// Helper function to build tool content JSON
static bool build_tool_content_json(mcp_json_t* content_json, mcp_content_item_t** content_items, size_t content_count) {
    bool json_build_error = false;

    if (!content_items) {
        return false;
    }

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
            (item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create((const char*)item->data)) != 0) ||
            mcp_json_array_add_item(content_json, item_obj) != 0)
        {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }
    }

    return json_build_error;
}

/**
 * @internal
 * @brief Handles the 'call_tool' request.
 */
char* handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    PROFILE_START("handle_call_tool");
    if (server == NULL || request == NULL || auth_context == NULL || error_code == NULL) {
        if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        PROFILE_END("handle_call_tool");
        return NULL;
    }
    *error_code = MCP_ERROR_NONE;
    (void)arena;

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

    const char* name = NULL;
    if (!extract_string_param(params_json, "name", &name)) {
        mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_INVALID_PARAMS;
        char* response = create_error_response(request->id, *error_code, "Missing or invalid 'name' parameter");
        PROFILE_END("handle_call_tool");
        return response;
    }

    if (!mcp_auth_check_tool_access(auth_context, name)) {
        mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_FORBIDDEN;
        char* response = create_error_response(request->id, *error_code, "Access denied to tool");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* args_json = mcp_json_object_get_property(params_json, "arguments");
    mcp_content_item_t** content_items = NULL;
    size_t content_count = 0;
    bool is_error = false;
    char* handler_error_message = NULL;
    mcp_error_code_t handler_status = MCP_ERROR_NONE;

    if (server->tool_handler != NULL) {
        PROFILE_START("tool_handler_callback");
        handler_status = server->tool_handler(
            server, name, args_json, server->tool_handler_user_data,
            &content_items, &content_count, &is_error, &handler_error_message);
        PROFILE_END("tool_handler_callback");
    } else {
        handler_status = MCP_ERROR_INTERNAL_ERROR;
        handler_error_message = mcp_strdup("Tool handler not configured");
    }

    if (handler_status != MCP_ERROR_NONE) {
        cleanup_content_items(server, content_items, content_count);
        mcp_json_destroy(params_json);
        *error_code = handler_status;
        const char* msg = handler_error_message ? handler_error_message : "Tool handler failed or tool not found";
        char* response = create_error_response(request->id, *error_code, msg);
        free(handler_error_message);
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_t* content_json = mcp_json_array_create();
    if (!content_json) {
        cleanup_content_items(server, content_items, content_count);
        free(handler_error_message);
        mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create content array");
        PROFILE_END("handle_call_tool");
        return response;
    }

    bool json_build_error = build_tool_content_json(content_json, content_items, content_count);
    cleanup_content_items(server, content_items, content_count);
    free(handler_error_message);

    if (json_build_error) {
        mcp_json_destroy(content_json);
        mcp_json_destroy(params_json);
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
        mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to create result object");
        PROFILE_END("handle_call_tool");
        return response;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) {
        mcp_json_destroy(params_json);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        char* response = create_error_response(request->id, *error_code, "Failed to stringify result");
        PROFILE_END("handle_call_tool");
        return response;
    }

    mcp_json_destroy(params_json);
    char* response = create_success_response(request->id, result_str);
    return response;
}
