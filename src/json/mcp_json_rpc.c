#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_profiler.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"

// --- Formatting Functions ---

char* mcp_json_format_request(uint64_t id, const char* method, const char* params) {
    if (method == NULL) { // Params can be NULL for request without params
        return NULL;
    }

    mcp_json_t* request = mcp_json_object_create();
    if (request == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    mcp_json_t* jsonrpc = mcp_json_string_create("2.0");
    if (jsonrpc == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }
    if (mcp_json_object_set_property(request, "jsonrpc", jsonrpc) != 0) {
        mcp_json_destroy(request);
        return NULL;
    }

    // Add id
    mcp_json_t* id_json = mcp_json_number_create((double)id);
    if (id_json == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }
    if (mcp_json_object_set_property(request, "id", id_json) != 0) {
        mcp_json_destroy(request);
        return NULL;
    }

    // Add method
    mcp_json_t* method_json = mcp_json_string_create(method);
    if (method_json == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }
    if (mcp_json_object_set_property(request, "method", method_json) != 0) {
        mcp_json_destroy(request);
        return NULL;
    }

    // Add params if provided
    if (params != NULL) {
        mcp_json_t* params_json = mcp_json_parse(params);
        if (params_json == NULL) {
             mcp_log_warn("Invalid JSON provided for request params, omitting params field: %s", params);
             // Don't add params if parsing failed
        } else {
            if (mcp_json_object_set_property(request, "params", params_json) != 0) {
                mcp_json_destroy(request);
                return NULL;
            }
        }
    }

    // Stringify the request
    char* json = mcp_json_stringify(request);
    mcp_json_destroy(request); // Destroys all nodes created with arena
    return json; // Return malloc'd string
}

char* mcp_json_format_response(uint64_t id, const char* result) {
    // Result can be NULL (represents JSON null)

    mcp_json_t* response = mcp_json_object_create();
    if (response == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    mcp_json_t* jsonrpc = mcp_json_string_create("2.0");
    if (jsonrpc == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }
    if (mcp_json_object_set_property(response, "jsonrpc", jsonrpc) != 0) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Add id
    mcp_json_t* id_json = mcp_json_number_create((double)id);
    if (id_json == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }
    if (mcp_json_object_set_property(response, "id", id_json) != 0) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Add result
    mcp_json_t* result_json = NULL;
    if (result != NULL) {
        result_json = mcp_json_parse(result);
        if (result_json == NULL) {
            mcp_log_warn("Invalid JSON provided for response result, defaulting to null: %s", result);
            result_json = mcp_json_null_create(); // Fallback to null
        }
    } else {
        result_json = mcp_json_null_create(); // Create JSON null
    }

    if (result_json == NULL) { // Check if fallback/creation failed
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(response, "result", result_json) != 0) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Stringify the response
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* mcp_json_format_error_response(uint64_t id, mcp_error_code_t error_code, const char* error_message) {
    mcp_json_t* response = mcp_json_object_create();
    if (response == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    mcp_json_t* jsonrpc = mcp_json_string_create("2.0");
    if (jsonrpc == NULL) { mcp_json_destroy(response); return NULL; }
    if (mcp_json_object_set_property(response, "jsonrpc", jsonrpc) != 0) { mcp_json_destroy(response); return NULL; }

    // Add id
    mcp_json_t* id_json = mcp_json_number_create((double)id);
    if (id_json == NULL) { mcp_json_destroy(response); return NULL; }
    if (mcp_json_object_set_property(response, "id", id_json) != 0) { mcp_json_destroy(response); return NULL; }

    // Add error object
    mcp_json_t* error_obj = mcp_json_object_create();
    if (error_obj == NULL) { mcp_json_destroy(response); return NULL; }

    // Add error code
    mcp_json_t* code_node = mcp_json_number_create((double)error_code);
    if (code_node == NULL) { mcp_json_destroy(error_obj); mcp_json_destroy(response); return NULL; }
    if (mcp_json_object_set_property(error_obj, "code", code_node) != 0) { mcp_json_destroy(error_obj); mcp_json_destroy(response); return NULL; }

    // Add error message (use empty string if NULL)
    mcp_json_t* msg_node = mcp_json_string_create(error_message ? error_message : "");
    if (msg_node == NULL) { mcp_json_destroy(error_obj); mcp_json_destroy(response); return NULL; }
    if (mcp_json_object_set_property(error_obj, "message", msg_node) != 0) { mcp_json_destroy(error_obj); mcp_json_destroy(response); return NULL; }

    // Add error object to response
    if (mcp_json_object_set_property(response, "error", error_obj) != 0) {
        mcp_json_destroy(response); // This will destroy error_obj and its children too
        return NULL;
    }

    // Stringify the response
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

// --- Parsing Functions ---

int mcp_json_parse_response(
    const char* json_str,
    uint64_t* id,
    mcp_error_code_t* error_code,
    char** error_message,
    char** result
) {
    if (json_str == NULL || id == NULL || error_code == NULL || error_message == NULL || result == NULL) {
        return -1;
    }

    // Initialize output parameters
    *id = 0;
    *error_code = MCP_ERROR_NONE;
    *error_message = NULL;
    *result = NULL;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1; // Parse error
    }

    if (mcp_json_get_type(json) != MCP_JSON_OBJECT) {
        mcp_json_destroy(json);
        return -1; // Not a JSON object
    }

    // Get id
    mcp_json_t* id_json = mcp_json_object_get_property(json, "id");
    if (id_json == NULL || mcp_json_get_type(id_json) != MCP_JSON_NUMBER) {
        mcp_json_destroy(json);
        return -1; // Missing or invalid ID
    }
    double id_value;
    if (mcp_json_get_number(id_json, &id_value) != 0 || id_value < 0 || id_value != floor(id_value) || id_value > (double)UINT64_MAX) {
        mcp_json_destroy(json);
        return -1; // Invalid ID value
    }
    *id = (uint64_t)id_value;

    // Check for error member first
    mcp_json_t* error_obj = mcp_json_object_get_property(json, "error");
    if (error_obj != NULL && mcp_json_get_type(error_obj) == MCP_JSON_OBJECT) {
        // Error response
        mcp_json_t* code_node = mcp_json_object_get_property(error_obj, "code");
        mcp_json_t* msg_node = mcp_json_object_get_property(error_obj, "message");

        if (code_node == NULL || mcp_json_get_type(code_node) != MCP_JSON_NUMBER) {
            mcp_json_destroy(json); return -1; // Invalid error code
        }
        double code_value;
        if (mcp_json_get_number(code_node, &code_value) != 0 || code_value != floor(code_value) || code_value < INT_MIN || code_value > INT_MAX) {
            mcp_json_destroy(json); return -1; // Invalid error code value
        }
        *error_code = (mcp_error_code_t)(int)code_value;

        if (msg_node != NULL && mcp_json_get_type(msg_node) == MCP_JSON_STRING) {
            const char* msg_str;
            if (mcp_json_get_string(msg_node, &msg_str) == 0) {
                *error_message = mcp_strdup(msg_str); // Caller must free
                if (*error_message == NULL) { // Allocation failed
                    mcp_json_destroy(json); return -1;
                }
            }
        }
        // Result should not be present in error response
        if (mcp_json_object_has_property(json, "result")) {
             mcp_log_warn("JSON-RPC response contains both 'error' and 'result'.");
             // Technically invalid, but proceed with error info
        }

    } else if (mcp_json_object_has_property(json, "result")) {
        // Success response
        mcp_json_t* result_node = mcp_json_object_get_property(json, "result");
        // Result can be any valid JSON type, including null
        *result = mcp_json_stringify(result_node); // Caller must free
        if (*result == NULL) { // Stringify failed
            mcp_json_destroy(json); return -1;
        }
        *error_code = MCP_ERROR_NONE;
    } else {
        // Invalid response: must contain either 'result' or 'error'
        mcp_json_destroy(json);
        return -1;
    }

    mcp_json_destroy(json); // Free parsed structure
    return 0; // Success
}

char* mcp_json_format_read_resource_params(const char* uri) {
    if (uri == NULL) {
        return NULL;
    }

    mcp_json_t* params = mcp_json_object_create();
    if (params == NULL) {
        return NULL;
    }

    // Add uri
    mcp_json_t* uri_json = mcp_json_string_create(uri);
    if (uri_json == NULL) {
        mcp_json_destroy(params);
        return NULL;
    }
    if (mcp_json_object_set_property(params, "uri", uri_json) != 0) {
        mcp_json_destroy(params); // Destroys uri_json too
        return NULL;
    }

    // Stringify the params
    char* json = mcp_json_stringify(params);
    mcp_json_destroy(params);
    return json;
}

char* mcp_json_format_call_tool_params(const char* name, const char* arguments) {
    if (name == NULL) {
        return NULL;
    }

    mcp_json_t* params = mcp_json_object_create();
    if (params == NULL) {
        return NULL;
    }

    // Add name
    mcp_json_t* name_json = mcp_json_string_create(name);
    if (name_json == NULL) {
        mcp_json_destroy(params);
        return NULL;
    }
    if (mcp_json_object_set_property(params, "name", name_json) != 0) {
        mcp_json_destroy(params);
        return NULL;
    }

    // Add arguments if provided
    if (arguments != NULL) {
        mcp_json_t* arguments_json = mcp_json_parse(arguments);
        if (arguments_json == NULL) {
             mcp_log_warn("Invalid JSON provided for tool arguments: %s", arguments);
             // Proceed without arguments if parsing fails? Or return error?
             // Let's return error for now.
             mcp_json_destroy(params);
             return NULL;
        }
        if (mcp_json_object_set_property(params, "arguments", arguments_json) != 0) {
            mcp_json_destroy(params); // Destroys arguments_json too
            return NULL;
        }
    }

    // Stringify the params
    char* json = mcp_json_stringify(params);
    mcp_json_destroy(params);
    return json;
}

int mcp_json_parse_resources(
    const char* json_str,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (json_str == NULL || resources == NULL || count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *resources = NULL;
    *count = 0;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1;
    }

    // Get resources array
    mcp_json_t* resources_json = mcp_json_object_get_property(json, "resources");
    if (resources_json == NULL || mcp_json_get_type(resources_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get resources count
    int array_size = mcp_json_array_get_size(resources_json);
    if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0; // Empty array is valid
    }
    *count = (size_t)array_size;

    // Allocate resources array
    *resources = (mcp_resource_t**)calloc(*count, sizeof(mcp_resource_t*)); // Use calloc
    if (*resources == NULL) {
        mcp_json_destroy(json);
        return -1; // Allocation failure
    }

    // Parse resources
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* resource_json = mcp_json_array_get_item(resources_json, (int)i);
        if (resource_json == NULL || mcp_json_get_type(resource_json) != MCP_JSON_OBJECT) {
            goto parse_error; // Invalid item in array
        }

        // Parse properties
        const char* uri = NULL;
        const char* name = NULL;
        const char* mime_type = NULL;
        const char* description = NULL;

        mcp_json_t* node = mcp_json_object_get_property(resource_json, "uri");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &uri);
        node = mcp_json_object_get_property(resource_json, "name");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &name);
        node = mcp_json_object_get_property(resource_json, "mimeType");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &mime_type);
        node = mcp_json_object_get_property(resource_json, "description");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &description);

        if (!uri) goto parse_error; // URI is mandatory

        // Create Resource Struct
        (*resources)[i] = mcp_resource_create(uri, name, mime_type, description);
        if ((*resources)[i] == NULL) {
            goto parse_error; // Allocation failure
        }
    }

    mcp_json_destroy(json);
    return 0; // Success

parse_error:
    // Cleanup partially created resources
    if (*resources) {
        for (size_t j = 0; j < *count; j++) { // Iterate up to *count as some might be NULL
            mcp_resource_free((*resources)[j]);
        }
        free(*resources);
        *resources = NULL;
    }
    *count = 0;
    mcp_json_destroy(json);
    return -1; // Indicate error
}

int mcp_json_parse_resource_templates(
    const char* json_str,
    mcp_resource_template_t*** templates,
    size_t* count
) {
     if (json_str == NULL || templates == NULL || count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *templates = NULL;
    *count = 0;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1;
    }

    // Get resource templates array
    mcp_json_t* templates_json = mcp_json_object_get_property(json, "resourceTemplates");
    if (templates_json == NULL || mcp_json_get_type(templates_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get count
    int array_size = mcp_json_array_get_size(templates_json);
     if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0; // Empty array is valid
    }
    *count = (size_t)array_size;

    // Allocate array
    *templates = (mcp_resource_template_t**)calloc(*count, sizeof(mcp_resource_template_t*));
    if (*templates == NULL) {
        mcp_json_destroy(json);
        return -1; // Allocation failure
    }

    // Parse templates
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* template_json = mcp_json_array_get_item(templates_json, (int)i);
        if (template_json == NULL || mcp_json_get_type(template_json) != MCP_JSON_OBJECT) {
            goto parse_error_template; // Invalid item
        }

        // Parse properties
        const char* uri_template = NULL;
        const char* name = NULL;
        const char* mime_type = NULL;
        const char* description = NULL;

        mcp_json_t* node = mcp_json_object_get_property(template_json, "uriTemplate");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &uri_template);
        node = mcp_json_object_get_property(template_json, "name");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &name);
        node = mcp_json_object_get_property(template_json, "mimeType");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &mime_type);
        node = mcp_json_object_get_property(template_json, "description");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &description);

        if (!uri_template) goto parse_error_template; // URI template is mandatory

        // Create Template Struct
        (*templates)[i] = mcp_resource_template_create(uri_template, name, mime_type, description);
        if ((*templates)[i] == NULL) {
            goto parse_error_template; // Allocation failure
        }
    }

    mcp_json_destroy(json);
    return 0; // Success

parse_error_template:
    if (*templates) {
        for (size_t j = 0; j < *count; j++) {
            mcp_resource_template_free((*templates)[j]);
        }
        free(*templates);
        *templates = NULL;
    }
    *count = 0;
    mcp_json_destroy(json);
    return -1;
}

int mcp_json_parse_content(
    const char* json_str,
    mcp_content_item_t*** content,
    size_t* count
) {
     if (json_str == NULL || content == NULL || count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *content = NULL;
    *count = 0;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1;
    }

    // Get contents array
    mcp_json_t* contents_json = mcp_json_object_get_property(json, "contents");
    if (contents_json == NULL || mcp_json_get_type(contents_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get count
    int array_size = mcp_json_array_get_size(contents_json);
     if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0; // Empty array is valid
    }
    *count = (size_t)array_size;

    // Allocate array
    *content = (mcp_content_item_t**)calloc(*count, sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        mcp_json_destroy(json);
        return -1; // Allocation failure
    }

    // Parse items
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* item_json = mcp_json_array_get_item(contents_json, (int)i);
        if (item_json == NULL || mcp_json_get_type(item_json) != MCP_JSON_OBJECT) {
            goto parse_error_content; // Invalid item
        }

        // Parse properties
        mcp_content_type_t type = MCP_CONTENT_TYPE_TEXT; // Default
        const char* mime_type = NULL;
        const char* text = NULL;
        size_t text_size = 0;

        mcp_json_t* node = mcp_json_object_get_property(item_json, "type");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) {
            const char* type_str;
            if (mcp_json_get_string(node, &type_str) == 0) {
                if (strcmp(type_str, "json") == 0) type = MCP_CONTENT_TYPE_JSON;
                else if (strcmp(type_str, "binary") == 0) type = MCP_CONTENT_TYPE_BINARY;
                // else keep default TEXT
            }
        }
        node = mcp_json_object_get_property(item_json, "mimeType");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &mime_type);
        node = mcp_json_object_get_property(item_json, "text");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) {
            if (mcp_json_get_string(node, &text) == 0 && text != NULL) {
                text_size = strlen(text);
            }
        }

        // Create Content Item Struct
        (*content)[i] = mcp_content_item_create(type, mime_type, text, text_size);
        if ((*content)[i] == NULL) {
            goto parse_error_content; // Allocation failure
        }
    }

    mcp_json_destroy(json);
    return 0; // Success

parse_error_content:
    if (*content) {
        for (size_t j = 0; j < *count; j++) {
            mcp_content_item_free((*content)[j]);
        }
        free(*content);
        *content = NULL;
    }
    *count = 0;
    mcp_json_destroy(json);
    return -1;
}

int mcp_json_parse_tools(
    const char* json_str,
    mcp_tool_t*** tools,
    size_t* count
) {
     if (json_str == NULL || tools == NULL || count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *tools = NULL;
    *count = 0;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1;
    }

    // Get tools array
    mcp_json_t* tools_json = mcp_json_object_get_property(json, "tools");
    if (tools_json == NULL || mcp_json_get_type(tools_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get count
    int array_size = mcp_json_array_get_size(tools_json);
     if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0; // Empty array is valid
    }
    *count = (size_t)array_size;

    // Allocate array
    *tools = (mcp_tool_t**)calloc(*count, sizeof(mcp_tool_t*));
    if (*tools == NULL) {
        mcp_json_destroy(json);
        return -1; // Allocation failure
    }

    // Parse tools
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* tool_json = mcp_json_array_get_item(tools_json, (int)i);
        if (tool_json == NULL || mcp_json_get_type(tool_json) != MCP_JSON_OBJECT) {
            goto parse_error_tool; // Invalid item
        }

        // Parse name and description
        const char* name = NULL;
        const char* description = NULL;
        mcp_json_t* node = mcp_json_object_get_property(tool_json, "name");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &name);
        node = mcp_json_object_get_property(tool_json, "description");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &description);

        if (!name) goto parse_error_tool; // Name is mandatory

        // Create Tool Struct
        (*tools)[i] = mcp_tool_create(name, description);
        if ((*tools)[i] == NULL) {
            goto parse_error_tool; // Allocation failure
        }

        // Parse Input Schema
        mcp_json_t* schema_json = mcp_json_object_get_property(tool_json, "inputSchema");
        if (schema_json && mcp_json_get_type(schema_json) == MCP_JSON_OBJECT) {
            mcp_json_t* props_json = mcp_json_object_get_property(schema_json, "properties");
            mcp_json_t* req_json = mcp_json_object_get_property(schema_json, "required");

            if (props_json && mcp_json_get_type(props_json) == MCP_JSON_OBJECT) {
                char** prop_names = NULL;
                size_t prop_count = 0;
                if (mcp_json_object_get_property_names(props_json, &prop_names, &prop_count) == 0) {
                    for (size_t j = 0; j < prop_count; j++) {
                        const char* prop_name = prop_names[j];
                        mcp_json_t* prop_obj = mcp_json_object_get_property(props_json, prop_name);
                        if (prop_obj && mcp_json_get_type(prop_obj) == MCP_JSON_OBJECT) {
                            const char* type = NULL;
                            const char* desc = NULL;
                            bool required = false;

                            mcp_json_t* type_node = mcp_json_object_get_property(prop_obj, "type");
                            if (type_node && mcp_json_get_type(type_node) == MCP_JSON_STRING) mcp_json_get_string(type_node, &type);
                            mcp_json_t* desc_node = mcp_json_object_get_property(prop_obj, "description");
                            if (desc_node && mcp_json_get_type(desc_node) == MCP_JSON_STRING) mcp_json_get_string(desc_node, &desc);

                            // Check if required
                            if (req_json && mcp_json_get_type(req_json) == MCP_JSON_ARRAY) {
                                int req_count = mcp_json_array_get_size(req_json);
                                for (int k = 0; k < req_count; k++) {
                                    mcp_json_t* req_item = mcp_json_array_get_item(req_json, k);
                                    const char* req_name;
                                    if (req_item && mcp_json_get_type(req_item) == MCP_JSON_STRING && mcp_json_get_string(req_item, &req_name) == 0) {
                                        if (strcmp(prop_name, req_name) == 0) {
                                            required = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            // Add parameter
                            mcp_tool_add_param((*tools)[i], prop_name, type, desc, required);
                        }
                    }
                    // Free property names array
                    for (size_t j = 0; j < prop_count; j++) free(prop_names[j]);
                    free(prop_names);
                }
            }
        }
    }

    mcp_json_destroy(json);
    return 0; // Success

parse_error_tool:
    if (*tools) {
        for (size_t j = 0; j < *count; j++) {
            mcp_tool_free((*tools)[j]);
        }
        free(*tools);
        *tools = NULL;
    }
    *count = 0;
    mcp_json_destroy(json);
    return -1;
}

int mcp_json_parse_tool_result(
    const char* json_str,
    mcp_content_item_t*** content,
    size_t* count,
    bool* is_error
) {
     if (json_str == NULL || content == NULL || count == NULL || is_error == NULL) {
        return -1;
    }

    // Initialize output parameters
    *content = NULL;
    *count = 0;
    *is_error = false;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1;
    }

    // Get is_error flag
    mcp_json_t* is_error_node = mcp_json_object_get_property(json, "isError");
    if (is_error_node && mcp_json_get_type(is_error_node) == MCP_JSON_BOOLEAN) {
        mcp_json_get_boolean(is_error_node, is_error);
    }

    // Get content array
    mcp_json_t* content_array = mcp_json_object_get_property(json, "content");
    if (content_array == NULL || mcp_json_get_type(content_array) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        // It's valid to have no content, especially for errors
        return 0;
    }

    // Get count
    int array_size = mcp_json_array_get_size(content_array);
     if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0; // Empty content array is valid
    }
    *count = (size_t)array_size;

    // Allocate array
    *content = (mcp_content_item_t**)calloc(*count, sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        mcp_json_destroy(json);
        return -1; // Allocation failure
    }

    // Parse items
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* item_json = mcp_json_array_get_item(content_array, (int)i);
        if (item_json == NULL || mcp_json_get_type(item_json) != MCP_JSON_OBJECT) {
            goto parse_error_tool_result; // Invalid item
        }

        // Parse properties
        mcp_content_type_t type = MCP_CONTENT_TYPE_TEXT; // Default
        const char* mime_type = NULL;
        const char* text = NULL;
        size_t text_size = 0;

        mcp_json_t* node = mcp_json_object_get_property(item_json, "type");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) {
            const char* type_str;
            if (mcp_json_get_string(node, &type_str) == 0) {
                if (strcmp(type_str, "json") == 0) type = MCP_CONTENT_TYPE_JSON;
                else if (strcmp(type_str, "binary") == 0) type = MCP_CONTENT_TYPE_BINARY;
                // else keep default TEXT
            }
        }
        node = mcp_json_object_get_property(item_json, "mimeType");
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) mcp_json_get_string(node, &mime_type);
        node = mcp_json_object_get_property(item_json, "text"); // Assuming text for now
        if (node && mcp_json_get_type(node) == MCP_JSON_STRING) {
            if (mcp_json_get_string(node, &text) == 0 && text != NULL) {
                text_size = strlen(text);
            }
        }

        // Create Content Item Struct
        (*content)[i] = mcp_content_item_create(type, mime_type, text, text_size);
        if ((*content)[i] == NULL) {
            goto parse_error_tool_result; // Allocation failure
        }
    }

    mcp_json_destroy(json);
    return 0; // Success

parse_error_tool_result:
    if (*content) {
        for (size_t j = 0; j < *count; j++) {
            mcp_content_item_free((*content)[j]);
        }
        free(*content);
        *content = NULL;
    }
    *count = 0;
    mcp_json_destroy(json);
    return -1;
}
