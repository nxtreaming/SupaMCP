#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mcp_server.h"
#include "mcp_json.h"
#include "mcp_arena.h" // Include arena allocator

// Server structure
struct mcp_server {
    mcp_server_config_t config;
    mcp_server_capabilities_t capabilities;
    mcp_transport_t* transport;
    bool running;

    // Resources
    mcp_resource_t** resources;
    size_t resource_count;
    size_t resource_capacity;

    // Resource templates
    mcp_resource_template_t** resource_templates;
    size_t resource_template_count;
    size_t resource_template_capacity;

    // Tools
    mcp_tool_t** tools;
    size_t tool_count;
    size_t tool_capacity;

    // Handlers
    mcp_server_resource_handler_t resource_handler;
    void* resource_handler_user_data;
    mcp_server_tool_handler_t tool_handler;
    void* tool_handler_user_data;
};

// Message handling
static int handle_message(mcp_server_t* server, const void* data, size_t size);
static int handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request); // Pass arena
static int send_response(mcp_server_t* server, const mcp_response_t* response);

// Request handlers (pass arena for parsing params)
static int handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request);
static int handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request);
static int handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request);
static int handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request);
static int handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request);

// Transport callback
static int transport_message_callback(void* user_data, const void* data, size_t size) {
    mcp_server_t* server = (mcp_server_t*)user_data;
    return handle_message(server, data, size);
}

// Server creation and management
mcp_server_t* mcp_server_create(
    const mcp_server_config_t* config,
    const mcp_server_capabilities_t* capabilities
) {
    if (config == NULL || capabilities == NULL) {
        return NULL;
    }

    mcp_server_t* server = (mcp_server_t*)malloc(sizeof(mcp_server_t));
    if (server == NULL) {
        return NULL;
    }

    // Copy configuration (uses malloc/strdup)
    server->config.name = config->name ? strdup(config->name) : NULL;
    server->config.version = config->version ? strdup(config->version) : NULL;
    server->config.description = config->description ? strdup(config->description) : NULL;

    // Copy capabilities
    server->capabilities = *capabilities;

    // Initialize other fields
    server->transport = NULL;
    server->running = false;

    server->resources = NULL;
    server->resource_count = 0;
    server->resource_capacity = 0;

    server->resource_templates = NULL;
    server->resource_template_count = 0;
    server->resource_template_capacity = 0;

    server->tools = NULL;
    server->tool_count = 0;
    server->tool_capacity = 0;

    server->resource_handler = NULL;
    server->resource_handler_user_data = NULL;
    server->tool_handler = NULL;
    server->tool_handler_user_data = NULL;

    // Check for allocation failures during config copy
    if ((config->name && !server->config.name) ||
        (config->version && !server->config.version) ||
        (config->description && !server->config.description))
    {
        free((void*)server->config.name);
        free((void*)server->config.version);
        free((void*)server->config.description);
        free(server);
        return NULL;
    }

    return server;
}

int mcp_server_start(
    mcp_server_t* server,
    mcp_transport_t* transport
) {
    if (server == NULL || transport == NULL) {
        return -1;
    }

    server->transport = transport;
    server->running = true;

    return mcp_transport_start(transport, transport_message_callback, server);
}

int mcp_server_stop(mcp_server_t* server) {
    if (server == NULL) {
        return -1;
    }

    server->running = false;

    if (server->transport != NULL) {
        return mcp_transport_stop(server->transport);
    }

    return 0;
}

void mcp_server_destroy(mcp_server_t* server) {
    if (server == NULL) {
        return;
    }

    mcp_server_stop(server);

    // Free configuration
    free((void*)server->config.name);
    free((void*)server->config.version);
    free((void*)server->config.description);

    // Free resources (copies use malloc)
    for (size_t i = 0; i < server->resource_count; i++) {
        mcp_resource_free(server->resources[i]);
    }
    free(server->resources);

    // Free resource templates (copies use malloc)
    for (size_t i = 0; i < server->resource_template_count; i++) {
        mcp_resource_template_free(server->resource_templates[i]);
    }
    free(server->resource_templates);

    // Free tools (copies use malloc)
    for (size_t i = 0; i < server->tool_count; i++) {
        mcp_tool_free(server->tools[i]);
    }
    free(server->tools);

    // Free the server
    free(server);
}

int mcp_server_set_resource_handler(
    mcp_server_t* server,
    mcp_server_resource_handler_t handler,
    void* user_data
) {
    if (server == NULL) {
        return -1;
    }
    server->resource_handler = handler;
    server->resource_handler_user_data = user_data;
    return 0;
}

int mcp_server_set_tool_handler(
    mcp_server_t* server,
    mcp_server_tool_handler_t handler,
    void* user_data
) {
    if (server == NULL) {
        return -1;
    }
    server->tool_handler = handler;
    server->tool_handler_user_data = user_data;
    return 0;
}

// Uses malloc for resource copy
int mcp_server_add_resource(
    mcp_server_t* server,
    const mcp_resource_t* resource
) {
    if (server == NULL || resource == NULL || !server->capabilities.resources_supported) {
        return -1;
    }
    if (server->resource_count >= server->resource_capacity) {
        size_t new_capacity = server->resource_capacity == 0 ? 8 : server->resource_capacity * 2;
        mcp_resource_t** new_resources = (mcp_resource_t**)realloc(server->resources, new_capacity * sizeof(mcp_resource_t*));
        if (new_resources == NULL) return -1;
        server->resources = new_resources;
        server->resource_capacity = new_capacity;
    }
    mcp_resource_t* resource_copy = mcp_resource_create(resource->uri, resource->name, resource->mime_type, resource->description);
    if (resource_copy == NULL) return -1;
    server->resources[server->resource_count++] = resource_copy;
    return 0;
}

// Uses malloc for template copy
int mcp_server_add_resource_template(
    mcp_server_t* server,
    const mcp_resource_template_t* tmpl
) {
    if (server == NULL || tmpl == NULL || !server->capabilities.resources_supported) {
        return -1;
    }
     if (server->resource_template_count >= server->resource_template_capacity) {
        size_t new_capacity = server->resource_template_capacity == 0 ? 8 : server->resource_template_capacity * 2;
        mcp_resource_template_t** new_templates = (mcp_resource_template_t**)realloc(server->resource_templates, new_capacity * sizeof(mcp_resource_template_t*));
        if (new_templates == NULL) return -1;
        server->resource_templates = new_templates;
        server->resource_template_capacity = new_capacity;
    }
    mcp_resource_template_t* template_copy = mcp_resource_template_create(tmpl->uri_template, tmpl->name, tmpl->mime_type, tmpl->description);
    if (template_copy == NULL) return -1;
    server->resource_templates[server->resource_template_count++] = template_copy;
    return 0;
}

// Uses malloc for tool copy
int mcp_server_add_tool(
    mcp_server_t* server,
    const mcp_tool_t* tool
) {
    if (server == NULL || tool == NULL || !server->capabilities.tools_supported) {
        return -1;
    }
    if (server->tool_count >= server->tool_capacity) {
        size_t new_capacity = server->tool_capacity == 0 ? 8 : server->tool_capacity * 2;
        mcp_tool_t** new_tools = (mcp_tool_t**)realloc(server->tools, new_capacity * sizeof(mcp_tool_t*));
        if (new_tools == NULL) return -1;
        server->tools = new_tools;
        server->tool_capacity = new_capacity;
    }
    mcp_tool_t* tool_copy = mcp_tool_create(tool->name, tool->description);
    if (tool_copy == NULL) return -1;
    for (size_t i = 0; i < tool->input_schema_count; i++) {
        if (mcp_tool_add_param(tool_copy, tool->input_schema[i].name, tool->input_schema[i].type, tool->input_schema[i].description, tool->input_schema[i].required) != 0) {
            mcp_tool_free(tool_copy);
            return -1;
        }
    }
    server->tools[server->tool_count++] = tool_copy;
    return 0;
}

// Deprecated? Or just for direct injection? Keep for now.
int mcp_server_process_message(
    mcp_server_t* server,
    const void* data,
    size_t size
) {
    if (server == NULL || data == NULL || size == 0) {
        return -1;
    }
    return handle_message(server, data, size);
}

// Message handling - Use Arena Allocator
static int handle_message(mcp_server_t* server, const void* data, size_t size) {
    if (server == NULL || data == NULL || size == 0) {
        return -1;
    }

    // Initialize arena for this message processing cycle
    mcp_arena_t arena;
    mcp_arena_init(&arena, 0); // Use default block size

    // Ensure the data is null-terminated (use arena for temp buffer?)
    // For simplicity, still use malloc here, but could optimize later.
    char* json_str = (char*)malloc(size + 1);
    if (json_str == NULL) {
        mcp_arena_destroy(&arena); // Clean up arena on error
        return -1;
    }
    memcpy(json_str, data, size);
    json_str[size] = '\0';

    // Parse the message using the arena
    // NOTE: mcp_json_parse_message internally uses mcp_json_parse(arena, ...)
    //       but still uses malloc for string duplication within the message struct.
    mcp_message_t message;
    // Pass the arena to the message parser
    int parse_result = mcp_json_parse_message(&arena, json_str, &message);

    free(json_str); // Free the temporary buffer

    if (parse_result != 0) {
        mcp_arena_destroy(&arena); // Clean up arena on parse error
        // TODO: Send JSON-RPC Parse Error response?
        return -1;
    }

    // Handle the message based on its type
    int result = -1;
    switch (message.type) {
        case MCP_MESSAGE_TYPE_REQUEST:
            // Pass arena to request handler
            result = handle_request(server, &arena, &message.request);
            break;
        case MCP_MESSAGE_TYPE_RESPONSE:
            // Server typically doesn't process responses it receives
            result = 0; // Ignore for now
            break;
        case MCP_MESSAGE_TYPE_NOTIFICATION:
            // Server could handle notifications if needed
            result = 0; // Ignore for now
            break;
    }

    // Free the message structure contents (which used malloc/strdup)
    mcp_message_free(&message);

    // Reset or destroy the arena for this cycle
    // Reset is faster if we expect similar allocation patterns next time
    mcp_arena_reset(&arena);
    // Or destroy if memory usage needs to be minimized between messages
    // mcp_arena_destroy(&arena);

    return result;
}

// Pass arena for parsing params
static int handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request) {
    if (server == NULL || request == NULL || arena == NULL) {
        return -1;
    }

    // Handle the request based on its method
    if (strcmp(request->method, "list_resources") == 0) {
        return handle_list_resources_request(server, arena, request);
    } else if (strcmp(request->method, "list_resource_templates") == 0) {
        return handle_list_resource_templates_request(server, arena, request);
    } else if (strcmp(request->method, "read_resource") == 0) {
        return handle_read_resource_request(server, arena, request);
    } else if (strcmp(request->method, "list_tools") == 0) {
        return handle_list_tools_request(server, arena, request);
    } else if (strcmp(request->method, "call_tool") == 0) {
        return handle_call_tool_request(server, arena, request);
    } else {
        // Unknown method - Response uses malloc
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_METHOD_NOT_FOUND;
        response.error_message = strdup("Method not found");
        response.result = NULL;
        int result = -1;
        if (response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }
}

// Response sending uses malloc internally for stringify
static int send_response(mcp_server_t* server, const mcp_response_t* response) {
    if (server == NULL || response == NULL) {
        return -1;
    }
    mcp_message_t message;
    message.type = MCP_MESSAGE_TYPE_RESPONSE;
    // Directly assign pointer, assuming response lifetime is managed by caller
    // or it's stack-allocated within the handler.
    message.response = *response;

    // Stringify uses malloc
    char* json_str = mcp_json_stringify_message(&message);
    if (json_str == NULL) {
        return -1;
    }
    int result = mcp_transport_send(server->transport, json_str, strlen(json_str));
    free(json_str);
    return result;
}

// --- Request Handlers ---
// These now parse params using the provided arena

static int handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request) {
    // This request has no parameters, arena is not used for parsing here
    (void)arena; // Mark arena as unused

    if (server == NULL || request == NULL || !server->capabilities.resources_supported) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_METHOD_NOT_FOUND;
        response.error_message = strdup("Resources not supported");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    // Create response JSON (uses malloc for nodes/strings via NULL arena)
    mcp_json_t* resources_json = mcp_json_array_create(NULL);
    if (!resources_json) return -1;

    for (size_t i = 0; i < server->resource_count; i++) {
        mcp_resource_t* resource = server->resources[i];
        mcp_json_t* res_obj = mcp_json_object_create(NULL);
        if (!res_obj ||
            (resource->uri && mcp_json_object_set_property(res_obj, "uri", mcp_json_string_create(NULL, resource->uri)) != 0) ||
            (resource->name && mcp_json_object_set_property(res_obj, "name", mcp_json_string_create(NULL, resource->name)) != 0) ||
            (resource->mime_type && mcp_json_object_set_property(res_obj, "mimeType", mcp_json_string_create(NULL, resource->mime_type)) != 0) ||
            (resource->description && mcp_json_object_set_property(res_obj, "description", mcp_json_string_create(NULL, resource->description)) != 0) ||
            mcp_json_array_add_item(resources_json, res_obj) != 0)
        {
            mcp_json_destroy(res_obj); // Handles nested nodes
            mcp_json_destroy(resources_json);
            return -1; // Error during JSON construction
        }
    }

    mcp_json_t* result_obj = mcp_json_object_create(NULL);
    if (!result_obj || mcp_json_object_set_property(result_obj, "resources", resources_json) != 0) {
        mcp_json_destroy(resources_json);
        mcp_json_destroy(result_obj);
        return -1;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj); // Destroys nested resources_json
    if (!result_str) return -1;

    mcp_response_t response;
    response.id = request->id;
    response.error_code = MCP_ERROR_NONE;
    response.error_message = NULL;
    response.result = result_str; // result_str uses malloc

    int send_result = send_response(server, &response);
    free(result_str);
    return send_result;
}

static int handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request) {
    // No params, arena unused here
    (void)arena;

     if (server == NULL || request == NULL || !server->capabilities.resources_supported) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_METHOD_NOT_FOUND;
        response.error_message = strdup("Resources not supported");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    // Create response JSON (uses malloc)
    mcp_json_t* templates_json = mcp_json_array_create(NULL);
     if (!templates_json) return -1;

    for (size_t i = 0; i < server->resource_template_count; i++) {
        mcp_resource_template_t* tmpl = server->resource_templates[i];
        mcp_json_t* tmpl_obj = mcp_json_object_create(NULL);
        if (!tmpl_obj ||
            (tmpl->uri_template && mcp_json_object_set_property(tmpl_obj, "uriTemplate", mcp_json_string_create(NULL, tmpl->uri_template)) != 0) ||
            (tmpl->name && mcp_json_object_set_property(tmpl_obj, "name", mcp_json_string_create(NULL, tmpl->name)) != 0) ||
            (tmpl->mime_type && mcp_json_object_set_property(tmpl_obj, "mimeType", mcp_json_string_create(NULL, tmpl->mime_type)) != 0) ||
            (tmpl->description && mcp_json_object_set_property(tmpl_obj, "description", mcp_json_string_create(NULL, tmpl->description)) != 0) ||
            mcp_json_array_add_item(templates_json, tmpl_obj) != 0)
        {
            mcp_json_destroy(tmpl_obj);
            mcp_json_destroy(templates_json);
            return -1;
        }
    }

    mcp_json_t* result_obj = mcp_json_object_create(NULL);
    if (!result_obj || mcp_json_object_set_property(result_obj, "resourceTemplates", templates_json) != 0) {
        mcp_json_destroy(templates_json);
        mcp_json_destroy(result_obj);
        return -1;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) return -1;

    mcp_response_t response;
    response.id = request->id;
    response.error_code = MCP_ERROR_NONE;
    response.error_message = NULL;
    response.result = result_str;

    int send_result = send_response(server, &response);
    free(result_str);
    return send_result;
}

static int handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request) {
     if (server == NULL || request == NULL || !server->capabilities.resources_supported) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_METHOD_NOT_FOUND;
        response.error_message = strdup("Resources not supported");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    if (request->params == NULL) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INVALID_PARAMS;
        response.error_message = strdup("Missing parameters");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    // Parse params using the arena
    mcp_json_t* params_json = mcp_json_parse(arena, request->params);
    if (params_json == NULL) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INVALID_PARAMS;
        response.error_message = strdup("Invalid parameters JSON");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        // Arena will be reset/destroyed by caller (handle_message)
        return result;
    }

    mcp_json_t* uri_json = mcp_json_object_get_property(params_json, "uri");
    const char* uri = NULL;
    if (uri_json == NULL || mcp_json_get_type(uri_json) != MCP_JSON_STRING || mcp_json_get_string(uri_json, &uri) != 0) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INVALID_PARAMS;
        response.error_message = strdup("Missing or invalid 'uri' parameter");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        // Arena handles params_json cleanup
        return result;
    }

    // Call the resource handler
    mcp_content_item_t* content_items = NULL; // Handler should allocate this array (using malloc)
    size_t content_count = 0;
    int handler_status = -1;
    if (server->resource_handler != NULL) {
        handler_status = server->resource_handler(server, uri, server->resource_handler_user_data, &content_items, &content_count);
    }

    if (handler_status != 0 || content_items == NULL || content_count == 0) {
        // Handler failed or returned no content
        free(content_items); // Free if allocated but handler failed
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INTERNAL_ERROR; // Or a more specific code if handler provided one
        response.error_message = strdup("Resource handler failed or resource not found");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        // Arena handles params_json cleanup
        return result;
    }

    // Create response JSON (uses malloc)
    mcp_json_t* contents_json = mcp_json_array_create(NULL);
    if (!contents_json) {
        for (size_t i = 0; i < content_count; i++) mcp_content_item_free(&content_items[i]);
        free(content_items);
        return -1;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < content_count; i++) {
        mcp_content_item_t* item = &content_items[i];
        mcp_json_t* item_obj = mcp_json_object_create(NULL);
        if (!item_obj ||
            mcp_json_object_set_property(item_obj, "uri", mcp_json_string_create(NULL, uri)) != 0 || // Use original URI
            (item->mime_type && mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(NULL, item->mime_type)) != 0) ||
            (item->type == MCP_CONTENT_TYPE_TEXT && item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create(NULL, (const char*)item->data)) != 0) ||
            // TODO: Handle binary data (e.g., base64 encode)?
            mcp_json_array_add_item(contents_json, item_obj) != 0)
        {
            mcp_json_destroy(item_obj);
            json_build_error = true;
            break;
        }
    }

    // Free handler-allocated content items AFTER creating JSON copies
    for (size_t i = 0; i < content_count; i++) mcp_content_item_free(&content_items[i]);
    free(content_items);

    if (json_build_error) {
        mcp_json_destroy(contents_json);
        return -1;
    }

    mcp_json_t* result_obj = mcp_json_object_create(NULL);
    if (!result_obj || mcp_json_object_set_property(result_obj, "contents", contents_json) != 0) {
        mcp_json_destroy(contents_json);
        mcp_json_destroy(result_obj);
        return -1;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) return -1;

    mcp_response_t response;
    response.id = request->id;
    response.error_code = MCP_ERROR_NONE;
    response.error_message = NULL;
    response.result = result_str;

    int send_result = send_response(server, &response);
    free(result_str);
    // Arena handles params_json cleanup
    return send_result;
}

static int handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request) {
    // No params, arena unused
    (void)arena;

    if (server == NULL || request == NULL || !server->capabilities.tools_supported) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_METHOD_NOT_FOUND;
        response.error_message = strdup("Tools not supported");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    // Create response JSON (uses malloc)
    mcp_json_t* tools_json = mcp_json_array_create(NULL);
    if (!tools_json) return -1;

    bool json_build_error = false;
    for (size_t i = 0; i < server->tool_count; i++) {
        mcp_tool_t* tool = server->tools[i];
        mcp_json_t* tool_obj = mcp_json_object_create(NULL);
        mcp_json_t* schema_obj = NULL;
        mcp_json_t* props_obj = NULL;
        mcp_json_t* req_arr = NULL;

        if (!tool_obj || mcp_json_object_set_property(tool_obj, "name", mcp_json_string_create(NULL, tool->name)) != 0 ||
            (tool->description && mcp_json_object_set_property(tool_obj, "description", mcp_json_string_create(NULL, tool->description)) != 0))
        {
            json_build_error = true; goto tool_loop_cleanup;
        }

        if (tool->input_schema_count > 0) {
            schema_obj = mcp_json_object_create(NULL);
            props_obj = mcp_json_object_create(NULL);
            req_arr = mcp_json_array_create(NULL);
            if (!schema_obj || !props_obj || !req_arr ||
                mcp_json_object_set_property(schema_obj, "type", mcp_json_string_create(NULL, "object")) != 0 ||
                mcp_json_object_set_property(schema_obj, "properties", props_obj) != 0) // Add props obj early
            {
                 json_build_error = true; goto tool_loop_cleanup;
            }

            for (size_t j = 0; j < tool->input_schema_count; j++) {
                mcp_tool_param_schema_t* param = &tool->input_schema[j];
                mcp_json_t* param_obj = mcp_json_object_create(NULL);
                 if (!param_obj ||
                    mcp_json_object_set_property(param_obj, "type", mcp_json_string_create(NULL, param->type)) != 0 ||
                    (param->description && mcp_json_object_set_property(param_obj, "description", mcp_json_string_create(NULL, param->description)) != 0) ||
                    mcp_json_object_set_property(props_obj, param->name, param_obj) != 0) // Add param to props
                 {
                     mcp_json_destroy(param_obj);
                     json_build_error = true; goto tool_loop_cleanup;
                 }

                 if (param->required) {
                     mcp_json_t* name_str = mcp_json_string_create(NULL, param->name);
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
        return -1;
    }

    mcp_json_t* result_obj = mcp_json_object_create(NULL);
    if (!result_obj || mcp_json_object_set_property(result_obj, "tools", tools_json) != 0) {
        mcp_json_destroy(tools_json);
        mcp_json_destroy(result_obj);
        return -1;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) return -1;

    mcp_response_t response;
    response.id = request->id;
    response.error_code = MCP_ERROR_NONE;
    response.error_message = NULL;
    response.result = result_str;

    int send_result = send_response(server, &response);
    free(result_str);
    return send_result;
}

static int handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request) {
    if (server == NULL || request == NULL || !server->capabilities.tools_supported) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_METHOD_NOT_FOUND;
        response.error_message = strdup("Tools not supported");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

     if (request->params == NULL) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INVALID_PARAMS;
        response.error_message = strdup("Missing parameters");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    // Parse params using arena
    mcp_json_t* params_json = mcp_json_parse(arena, request->params);
    if (params_json == NULL) {
         mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INVALID_PARAMS;
        response.error_message = strdup("Invalid parameters JSON");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    mcp_json_t* name_json = mcp_json_object_get_property(params_json, "name");
    const char* name = NULL;
    if (name_json == NULL || mcp_json_get_type(name_json) != MCP_JSON_STRING || mcp_json_get_string(name_json, &name) != 0) {
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INVALID_PARAMS;
        response.error_message = strdup("Missing or invalid 'name' parameter");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    mcp_json_t* args_json = mcp_json_object_get_property(params_json, "arguments");
    // Arguments can be any JSON type, stringify them for the handler
    char* args_str = NULL;
    if (args_json != NULL) {
        args_str = mcp_json_stringify(args_json); // Uses malloc
        if (args_str == NULL) {
             mcp_response_t response;
            response.id = request->id;
            response.error_code = MCP_ERROR_INTERNAL_ERROR;
            response.error_message = strdup("Failed to stringify arguments");
            response.result = NULL;
            int result = -1;
            if(response.error_message) {
                result = send_response(server, &response);
                free(response.error_message);
            }
            return result;
        }
    }

    // Call the tool handler
    mcp_content_item_t* content_items = NULL; // Handler allocates this array (malloc)
    size_t content_count = 0;
    bool is_error = false;
    int handler_status = -1;

    if (server->tool_handler != NULL) {
        handler_status = server->tool_handler(server, name, args_str ? args_str : "{}", server->tool_handler_user_data, &content_items, &content_count, &is_error);
    }
    free(args_str); // Free stringified arguments

    if (handler_status != 0 || content_items == NULL || content_count == 0) {
        free(content_items);
        mcp_response_t response;
        response.id = request->id;
        response.error_code = MCP_ERROR_INTERNAL_ERROR; // Or more specific
        response.error_message = strdup("Tool handler failed or tool not found");
        response.result = NULL;
        int result = -1;
        if(response.error_message) {
            result = send_response(server, &response);
            free(response.error_message);
        }
        return result;
    }

    // Create response JSON (uses malloc)
    mcp_json_t* content_json = mcp_json_array_create(NULL);
    if (!content_json) {
         for (size_t i = 0; i < content_count; i++) mcp_content_item_free(&content_items[i]);
         free(content_items);
         return -1;
    }

    bool json_build_error = false;
    for (size_t i = 0; i < content_count; i++) {
        mcp_content_item_t* item = &content_items[i];
        mcp_json_t* item_obj = mcp_json_object_create(NULL);
        const char* type_str;
        switch(item->type) {
            case MCP_CONTENT_TYPE_TEXT: type_str = "text"; break;
            case MCP_CONTENT_TYPE_JSON: type_str = "json"; break;
            case MCP_CONTENT_TYPE_BINARY: type_str = "binary"; break;
            default: type_str = "unknown"; break;
        }

        if (!item_obj ||
            mcp_json_object_set_property(item_obj, "type", mcp_json_string_create(NULL, type_str)) != 0 ||
            (item->mime_type && mcp_json_object_set_property(item_obj, "mimeType", mcp_json_string_create(NULL, item->mime_type)) != 0) ||
            (item->type == MCP_CONTENT_TYPE_TEXT && item->data && mcp_json_object_set_property(item_obj, "text", mcp_json_string_create(NULL, (const char*)item->data)) != 0) ||
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
        return -1;
    }

    mcp_json_t* result_obj = mcp_json_object_create(NULL);
    if (!result_obj ||
        mcp_json_object_set_property(result_obj, "content", content_json) != 0 ||
        mcp_json_object_set_property(result_obj, "isError", mcp_json_boolean_create(NULL, is_error)) != 0)
    {
        mcp_json_destroy(content_json);
        mcp_json_destroy(result_obj);
        return -1;
    }

    char* result_str = mcp_json_stringify(result_obj);
    mcp_json_destroy(result_obj);
    if (!result_str) return -1;

    mcp_response_t response;
    response.id = request->id;
    response.error_code = MCP_ERROR_NONE;
    response.error_message = NULL;
    response.result = result_str;

    int send_result = send_response(server, &response);
    free(result_str);
    // Arena handles params_json cleanup
    return send_result;
}
