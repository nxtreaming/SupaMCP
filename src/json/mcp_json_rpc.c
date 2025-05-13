#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include "mcp_json.h"
#include "mcp_json_rpc.h"
#include "mcp_profiler.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"

/* Helper functions for common JSON operations */

/**
 * Add a JSON property to an object with error handling
 *
 * @param obj The JSON object to add the property to
 * @param key The property key
 * @param value The JSON value to add (ownership transferred to obj)
 * @return 0 on success, -1 on error (obj is destroyed on error)
 */
static int add_json_property(mcp_json_t* obj, const char* key, mcp_json_t* value) {
    if (value == NULL) {
        mcp_json_destroy(obj);
        return -1;
    }

    if (mcp_json_object_set_property(obj, key, value) != 0) {
        mcp_json_destroy(obj);
        return -1;
    }

    return 0;
}

/**
 * Create and add a string property to a JSON object
 *
 * @param obj The JSON object to add the property to
 * @param key The property key
 * @param value The string value
 * @return 0 on success, -1 on error (obj is destroyed on error)
 */
static int add_string_property(mcp_json_t* obj, const char* key, const char* value) {
    mcp_json_t* json_value = mcp_json_string_create(value);
    return add_json_property(obj, key, json_value);
}

/**
 * Create and add a number property to a JSON object
 *
 * @param obj The JSON object to add the property to
 * @param key The property key
 * @param value The number value
 * @return 0 on success, -1 on error (obj is destroyed on error)
 */
static int add_number_property(mcp_json_t* obj, const char* key, double value) {
    mcp_json_t* json_value = mcp_json_number_create(value);
    return add_json_property(obj, key, json_value);
}

/**
 * Initialize a basic JSON-RPC message with version and id
 *
 * @param id The message ID
 * @return A new JSON object with jsonrpc and id properties set, or NULL on error
 */
static mcp_json_t* init_json_rpc_message(uint64_t id) {
    mcp_json_t* message = mcp_json_object_create();
    if (message == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    if (add_string_property(message, "jsonrpc", "2.0") != 0) {
        return NULL;
    }

    // Add id
    if (add_number_property(message, "id", (double)id) != 0) {
        return NULL;
    }

    return message;
}

/**
 * Helper function to get a string property from a JSON object
 *
 * @param obj The JSON object
 * @param key The property key
 * @param value Pointer to store the string value (not copied, points to internal data)
 * @return 0 on success, -1 if property doesn't exist or isn't a string
 */
static int get_string_property(mcp_json_t* obj, const char* key, const char** value) {
    mcp_json_t* node = mcp_json_object_get_property(obj, key);
    if (node && mcp_json_get_type(node) == MCP_JSON_STRING) {
        return mcp_json_get_string(node, value);
    }
    return -1;
}

char* mcp_json_format_request(uint64_t id, const char* method, const char* params) {
    if (method == NULL) {
        return NULL;
    }

    mcp_json_t* request = init_json_rpc_message(id);
    if (request == NULL) {
        return NULL;
    }

    // Add method
    if (add_string_property(request, "method", method) != 0) {
        return NULL;
    }

    // Add params if provided
    if (params != NULL) {
        mcp_json_t* params_json = mcp_json_parse(params);
        if (params_json == NULL) {
            mcp_log_warn("Invalid JSON provided for request params, omitting params field: %s", params);
            // Don't add params if parsing failed
        } else {
            if (add_json_property(request, "params", params_json) != 0) {
                return NULL;
            }
        }
    }

    char* json = mcp_json_stringify(request);
    mcp_json_destroy(request);
    return json;
}

// Result can be NULL (represents JSON null)
char* mcp_json_format_response(uint64_t id, const char* result) {
    mcp_json_t* response = init_json_rpc_message(id);
    if (response == NULL) {
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
        // Create JSON null
        result_json = mcp_json_null_create();
    }

    if (result_json == NULL) {
        // Failed to create JSON null
        mcp_json_destroy(response);
        return NULL;
    }

    if (add_json_property(response, "result", result_json) != 0) {
        return NULL;
    }

    // Stringify the response
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* mcp_json_format_error_response(uint64_t id, mcp_error_code_t error_code, const char* error_message) {
    mcp_json_t* response = init_json_rpc_message(id);
    if (response == NULL) {
        return NULL;
    }

    // Add error object
    mcp_json_t* error_obj = mcp_json_object_create();
    if (error_obj == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Add error code
    if (add_number_property(error_obj, "code", (double)error_code) != 0) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Add error message (use empty string if NULL)
    if (add_string_property(error_obj, "message", error_message ? error_message : "") != 0) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Add error object to response
    if (add_json_property(response, "error", error_obj) != 0) {
        return NULL;
    }

    // Stringify the response
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

/**
 * Helper function to get a number property from a JSON object
 *
 * @param obj The JSON object
 * @param key The property key
 * @param value Pointer to store the number value
 * @return 0 on success, -1 if property doesn't exist or isn't a number
 */
static int get_number_property(mcp_json_t* obj, const char* key, double* value) {
    mcp_json_t* node = mcp_json_object_get_property(obj, key);
    if (node && mcp_json_get_type(node) == MCP_JSON_NUMBER) {
        return mcp_json_get_number(node, value);
    }
    return -1;
}

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
    if (json == NULL || mcp_json_get_type(json) != MCP_JSON_OBJECT) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get id
    double id_value;
    if (get_number_property(json, "id", &id_value) != 0 ||
        id_value < 0 || id_value != floor(id_value) || id_value > (double)UINT64_MAX) {
        mcp_json_destroy(json);
        return -1;
    }
    *id = (uint64_t)id_value;

    // Check for error member first
    mcp_json_t* error_obj = mcp_json_object_get_property(json, "error");
    if (error_obj != NULL && mcp_json_get_type(error_obj) == MCP_JSON_OBJECT) {
        // Error response
        double code_value;
        if (get_number_property(error_obj, "code", &code_value) != 0 ||
            code_value != floor(code_value) || code_value < INT_MIN || code_value > INT_MAX) {
            mcp_json_destroy(json);
            return -1;
        }
        *error_code = (mcp_error_code_t)(int)code_value;

        // Get error message
        const char* msg_str = NULL;
        if (get_string_property(error_obj, "message", &msg_str) == 0 && msg_str != NULL) {
            *error_message = mcp_strdup(msg_str);
            if (*error_message == NULL) {
                mcp_json_destroy(json);
                return -1;
            }
        }

        // Result should not be present in error response
        if (mcp_json_object_has_property(json, "result")) {
            mcp_log_warn("JSON-RPC response contains both 'error' and 'result'.");
            // Technically invalid, but proceed with error info
        }
    } else if (mcp_json_object_has_property(json, "result")) {
        mcp_json_t* result_node = mcp_json_object_get_property(json, "result");
        // Result can be any valid JSON type, including null
        *result = mcp_json_stringify(result_node);
        if (*result == NULL) {
            mcp_json_destroy(json);
            return -1;
        }
        *error_code = MCP_ERROR_NONE;
    } else {
        // Invalid response: must contain either 'result' or 'error'
        mcp_json_destroy(json);
        return -1;
    }

    mcp_json_destroy(json);
    return 0;
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
    if (add_string_property(params, "uri", uri) != 0) {
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
    if (add_string_property(params, "name", name) != 0) {
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
        if (add_json_property(params, "arguments", arguments_json) != 0) {
            return NULL;
        }
    }

    // Stringify the params
    char* json = mcp_json_stringify(params);
    mcp_json_destroy(params);
    return json;
}

/**
 * Helper function to parse a JSON array of objects
 *
 * @param json_str The JSON string to parse
 * @param array_key The key of the array in the JSON object
 * @param count Output parameter for the number of items
 * @param parse_item Function to parse each item in the array
 * @param cleanup_items Function to clean up items on error
 * @param result Output parameter for the parsed array
 * @return 0 on success, -1 on error
 */
static int parse_json_array(
    const char* json_str,
    const char* array_key,
    size_t* count,
    void* (*parse_item)(mcp_json_t* item_json),
    void (*cleanup_items)(void** items, size_t count),
    void*** result
) {
    // Initialize output parameters
    *result = NULL;
    *count = 0;

    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        return -1;
    }

    // Get array
    mcp_json_t* array_json = mcp_json_object_get_property(json, array_key);
    if (array_json == NULL || mcp_json_get_type(array_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get count
    int array_size = mcp_json_array_get_size(array_json);
    if (array_size <= 0) {
        mcp_json_destroy(json);
        // Empty array is valid
        return 0;
    }
    *count = (size_t)array_size;

    // Allocate array
    *result = (void**)calloc(*count, sizeof(void*));
    if (*result == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse items
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* item_json = mcp_json_array_get_item(array_json, (int)i);
        if (item_json == NULL || mcp_json_get_type(item_json) != MCP_JSON_OBJECT) {
            goto parse_error;
        }

        // Parse item
        (*result)[i] = parse_item(item_json);
        if ((*result)[i] == NULL) {
            goto parse_error;
        }
    }

    mcp_json_destroy(json);
    return 0;

parse_error:
    cleanup_items((void**)*result, *count);
    *result = NULL;
    *count = 0;
    mcp_json_destroy(json);
    return -1;
}

/**
 * Parse a resource from a JSON object
 */
static void* parse_resource_item(mcp_json_t* resource_json) {
    const char* uri = NULL;
    const char* name = NULL;
    const char* mime_type = NULL;
    const char* description = NULL;

    get_string_property(resource_json, "uri", &uri);
    get_string_property(resource_json, "name", &name);
    get_string_property(resource_json, "mimeType", &mime_type);
    get_string_property(resource_json, "description", &description);

    if (!uri) {
        return NULL;
    }

    return mcp_resource_create(uri, name, mime_type, description);
}

/**
 * Clean up resources array
 */
static void cleanup_resources(void** resources, size_t count) {
    if (resources) {
        for (size_t j = 0; j < count; j++) {
            mcp_resource_free((mcp_resource_t*)resources[j]);
        }
        free(resources);
    }
}

int mcp_json_parse_resources(
    const char* json_str,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (json_str == NULL || resources == NULL || count == NULL) {
        return -1;
    }

    return parse_json_array(
        json_str,
        "resources",
        count,
        parse_resource_item,
        cleanup_resources,
        (void***)resources
    );
}

/**
 * Parse a resource template from a JSON object
 */
static void* parse_resource_template_item(mcp_json_t* template_json) {
    const char* uri_template = NULL;
    const char* name = NULL;
    const char* mime_type = NULL;
    const char* description = NULL;

    get_string_property(template_json, "uriTemplate", &uri_template);
    get_string_property(template_json, "name", &name);
    get_string_property(template_json, "mimeType", &mime_type);
    get_string_property(template_json, "description", &description);

    if (!uri_template) {
        return NULL;
    }

    return mcp_resource_template_create(uri_template, name, mime_type, description);
}

/**
 * Clean up resource templates array
 */
static void cleanup_resource_templates(void** templates, size_t count) {
    if (templates) {
        for (size_t j = 0; j < count; j++) {
            mcp_resource_template_free((mcp_resource_template_t*)templates[j]);
        }
        free(templates);
    }
}

int mcp_json_parse_resource_templates(
    const char* json_str,
    mcp_resource_template_t*** templates,
    size_t* count
) {
    if (json_str == NULL || templates == NULL || count == NULL) {
        return -1;
    }

    return parse_json_array(
        json_str,
        "resourceTemplates",
        count,
        parse_resource_template_item,
        cleanup_resource_templates,
        (void***)templates
    );
}

/**
 * Parse a content item from a JSON object
 */
static void* parse_content_item(mcp_json_t* item_json) {
    mcp_content_type_t type = MCP_CONTENT_TYPE_TEXT;
    const char* mime_type = NULL;
    const char* text = NULL;
    size_t text_size = 0;

    // Get type
    const char* type_str = NULL;
    if (get_string_property(item_json, "type", &type_str) == 0) {
        if (strcmp(type_str, "json") == 0)
            type = MCP_CONTENT_TYPE_JSON;
        else if (strcmp(type_str, "binary") == 0)
            type = MCP_CONTENT_TYPE_BINARY;
        // else keep default TEXT
    }

    // Get mime type
    get_string_property(item_json, "mimeType", &mime_type);

    // Get text
    if (get_string_property(item_json, "text", &text) == 0 && text != NULL) {
        text_size = strlen(text) + 1;
    }

    // Create Content Item Struct
    return mcp_content_item_create(type, mime_type, text, text_size);
}

/**
 * Clean up content items array
 */
static void cleanup_content_items(void** content, size_t count) {
    if (content) {
        for (size_t j = 0; j < count; j++) {
            mcp_content_item_free((mcp_content_item_t*)content[j]);
        }
        free(content);
    }
}

int mcp_json_parse_content(
    const char* json_str,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (json_str == NULL || content == NULL || count == NULL) {
        return -1;
    }

    return parse_json_array(
        json_str,
        "contents",
        count,
        parse_content_item,
        cleanup_content_items,
        (void***)content
    );
}

/**
 * Check if a property name is in the required array
 */
static bool is_property_required(mcp_json_t* req_json, const char* prop_name) {
    if (!req_json || mcp_json_get_type(req_json) != MCP_JSON_ARRAY) {
        return false;
    }

    int req_count = mcp_json_array_get_size(req_json);
    for (int k = 0; k < req_count; k++) {
        mcp_json_t* req_item = mcp_json_array_get_item(req_json, k);
        const char* req_name;
        if (req_item && mcp_json_get_type(req_item) == MCP_JSON_STRING &&
            mcp_json_get_string(req_item, &req_name) == 0) {
            if (strcmp(prop_name, req_name) == 0) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Parse a tool from a JSON object
 */
static void* parse_tool_item(mcp_json_t* tool_json) {
    const char* name = NULL;
    const char* description = NULL;

    get_string_property(tool_json, "name", &name);
    get_string_property(tool_json, "description", &description);

    if (!name) {
        return NULL;
    }

    // Create Tool Struct
    mcp_tool_t* tool = mcp_tool_create(name, description);
    if (tool == NULL) {
        return NULL;
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

                        get_string_property(prop_obj, "type", &type);
                        get_string_property(prop_obj, "description", &desc);

                        // Check if required
                        bool required = is_property_required(req_json, prop_name);

                        // Add parameter
                        mcp_tool_add_param(tool, prop_name, type, desc, required);
                    }
                }
                // Free property names array
                for (size_t j = 0; j < prop_count; j++) free(prop_names[j]);
                free(prop_names);
            }
        }
    }

    return tool;
}

/**
 * Clean up tools array
 */
static void cleanup_tools(void** tools, size_t count) {
    if (tools) {
        for (size_t j = 0; j < count; j++) {
            mcp_tool_free((mcp_tool_t*)tools[j]);
        }
        free(tools);
    }
}

int mcp_json_parse_tools(
    const char* json_str,
    mcp_tool_t*** tools,
    size_t* count
) {
    if (json_str == NULL || tools == NULL || count == NULL) {
        return -1;
    }

    return parse_json_array(
        json_str,
        "tools",
        count,
        parse_tool_item,
        cleanup_tools,
        (void***)tools
    );
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
        // Empty content array is valid
        return 0;
    }
    *count = (size_t)array_size;

    // Allocate array
    *content = (mcp_content_item_t**)calloc(*count, sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse items
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* item_json = mcp_json_array_get_item(content_array, (int)i);
        if (item_json == NULL || mcp_json_get_type(item_json) != MCP_JSON_OBJECT) {
            goto parse_error_tool_result;
        }

        // Use the same content item parsing logic we already defined
        (*content)[i] = (mcp_content_item_t*)parse_content_item(item_json);
        if ((*content)[i] == NULL) {
            goto parse_error_tool_result;
        }
    }

    mcp_json_destroy(json);
    return 0;

parse_error_tool_result:
    cleanup_content_items((void**)*content, *count);
    *content = NULL;
    *count = 0;
    mcp_json_destroy(json);
    return -1;
}
