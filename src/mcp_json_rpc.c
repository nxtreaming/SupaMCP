#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "mcp_json.h"
#include "mcp_json_rpc.h"

char* mcp_json_format_request(uint64_t id, const char* method, const char* params) {
    if (method == NULL || params == NULL) {
        return NULL;
    }

    mcp_json_t* request = mcp_json_object_create(); // Removed NULL arena arg
    if (request == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    mcp_json_t* jsonrpc = mcp_json_string_create("2.0"); // Removed NULL arena arg
    if (jsonrpc == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }

    if (mcp_json_object_set_property(request, "jsonrpc", jsonrpc) != 0) {
        mcp_json_destroy(jsonrpc);
        mcp_json_destroy(request);
        return NULL;
    }

    // Add id
    mcp_json_t* id_json = mcp_json_number_create((double)id); // Removed NULL arena arg
    if (id_json == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }

    if (mcp_json_object_set_property(request, "id", id_json) != 0) {
        mcp_json_destroy(id_json);
        mcp_json_destroy(request);
        return NULL;
    }

    // Add method
    mcp_json_t* method_json = mcp_json_string_create(method); // Removed NULL arena arg
    if (method_json == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }

    if (mcp_json_object_set_property(request, "method", method_json) != 0) {
        mcp_json_destroy(method_json);
        mcp_json_destroy(request);
        return NULL;
    }

    // Add params
    mcp_json_t* params_json = mcp_json_parse(params); // Removed NULL arena arg
    if (params_json == NULL) {
        mcp_json_destroy(request);
        return NULL;
    }

    if (mcp_json_object_set_property(request, "params", params_json) != 0) {
        mcp_json_destroy(params_json);
        mcp_json_destroy(request);
        return NULL;
    }

    // Stringify the request
    char* json = mcp_json_stringify(request);
    mcp_json_destroy(request);
    return json;
}

char* mcp_json_format_response(uint64_t id, const char* result) {
    if (result == NULL) {
        return NULL;
    }

    mcp_json_t* response = mcp_json_object_create(); // Removed NULL arena arg
    if (response == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    mcp_json_t* jsonrpc = mcp_json_string_create("2.0"); // Removed NULL arena arg
    if (jsonrpc == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(response, "jsonrpc", jsonrpc) != 0) {
        mcp_json_destroy(jsonrpc);
        mcp_json_destroy(response);
        return NULL;
    }

    // Add id
    mcp_json_t* id_json = mcp_json_number_create((double)id); // Removed NULL arena arg
    if (id_json == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(response, "id", id_json) != 0) {
        mcp_json_destroy(id_json);
        mcp_json_destroy(response);
        return NULL;
    }

    // Add result
    mcp_json_t* result_json = mcp_json_parse(result); // Removed NULL arena arg
    if (result_json == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(response, "result", result_json) != 0) {
        mcp_json_destroy(result_json);
        mcp_json_destroy(response);
        return NULL;
    }

    // Stringify the response
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
}

char* mcp_json_format_error_response(uint64_t id, mcp_error_code_t error_code, const char* error_message) {
    mcp_json_t* response = mcp_json_object_create(); // Removed NULL arena arg
    if (response == NULL) {
        return NULL;
    }

    // Add jsonrpc version
    mcp_json_t* jsonrpc = mcp_json_string_create("2.0"); // Removed NULL arena arg
    if (jsonrpc == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(response, "jsonrpc", jsonrpc) != 0) {
        mcp_json_destroy(jsonrpc);
        mcp_json_destroy(response);
        return NULL;
    }

    // Add id
    mcp_json_t* id_json = mcp_json_number_create((double)id); // Removed NULL arena arg
    if (id_json == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(response, "id", id_json) != 0) {
        mcp_json_destroy(id_json);
        mcp_json_destroy(response);
        return NULL;
    }

    // Add error
    mcp_json_t* error = mcp_json_object_create(); // Removed NULL arena arg
    if (error == NULL) {
        mcp_json_destroy(response);
        return NULL;
    }

    // Add error code
    mcp_json_t* code = mcp_json_number_create((double)error_code); // Removed NULL arena arg
    if (code == NULL) {
        mcp_json_destroy(error);
        mcp_json_destroy(response);
        return NULL;
    }

    if (mcp_json_object_set_property(error, "code", code) != 0) {
        mcp_json_destroy(code);
        mcp_json_destroy(error);
        mcp_json_destroy(response);
        return NULL;
    }

    // Add error message
    if (error_message != NULL) {
        mcp_json_t* message = mcp_json_string_create(error_message); // Removed NULL arena arg
        if (message == NULL) {
            mcp_json_destroy(error);
            mcp_json_destroy(response);
            return NULL;
        }

        if (mcp_json_object_set_property(error, "message", message) != 0) {
            mcp_json_destroy(message);
            mcp_json_destroy(error);
            mcp_json_destroy(response);
            return NULL;
        }
    }

    if (mcp_json_object_set_property(response, "error", error) != 0) {
        mcp_json_destroy(error);
        mcp_json_destroy(response);
        return NULL;
    }

    // Stringify the response
    char* json = mcp_json_stringify(response);
    mcp_json_destroy(response);
    return json;
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

    mcp_json_t* json = mcp_json_parse(json_str); // Removed NULL arena arg
    if (json == NULL) {
        return -1;
    }

    if (mcp_json_get_type(json) != MCP_JSON_OBJECT) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get id
    mcp_json_t* id_json = mcp_json_object_get_property(json, "id");
    if (id_json == NULL || mcp_json_get_type(id_json) != MCP_JSON_NUMBER) {
        mcp_json_destroy(json);
        return -1;
    }

    double id_value;
    if (mcp_json_get_number(id_json, &id_value) != 0) {
        mcp_json_destroy(json);
        return -1;
    }
    // Validate ID value before casting
    if (id_value < 0 || id_value != floor(id_value) || id_value > (double)UINT64_MAX) {
         mcp_json_destroy(json);
         return -1; // Invalid ID value (negative, fractional, or out of range)
    }
    *id = (uint64_t)id_value;

    // Check for error
    mcp_json_t* error = mcp_json_object_get_property(json, "error");
    if (error != NULL && mcp_json_get_type(error) == MCP_JSON_OBJECT) {
        // Get error code
        mcp_json_t* code = mcp_json_object_get_property(error, "code");
        if (code == NULL || mcp_json_get_type(code) != MCP_JSON_NUMBER) {
            mcp_json_destroy(json);
            return -1;
        }

        double code_value;
        if (mcp_json_get_number(code, &code_value) != 0) {
            mcp_json_destroy(json);
            return -1;
        }
        // Validate error code value before casting
        if (code_value != floor(code_value) || code_value < INT_MIN || code_value > INT_MAX) {
            mcp_json_destroy(json);
            return -1; // Invalid error code value (fractional or out of range)
        }
        *error_code = (mcp_error_code_t)(int)code_value;

        // Get error message
        mcp_json_t* message = mcp_json_object_get_property(error, "message");
        if (message != NULL && mcp_json_get_type(message) == MCP_JSON_STRING) {
            const char* message_value;
            if (mcp_json_get_string(message, &message_value) == 0) {
                *error_message = mcp_strdup(message_value); // Use helper
            }
        }
    } else {
        // Get result
        mcp_json_t* result_json = mcp_json_object_get_property(json, "result");
        if (result_json != NULL) {
            *result = mcp_json_stringify(result_json);
        }
    }

    mcp_json_destroy(json);
    return 0;
}

char* mcp_json_format_read_resource_params(const char* uri) {
    if (uri == NULL) {
        return NULL;
    }

    mcp_json_t* params = mcp_json_object_create(); // Removed NULL arena arg
    if (params == NULL) {
        return NULL;
    }

    // Add uri
    mcp_json_t* uri_json = mcp_json_string_create(uri); // Removed NULL arena arg
    if (uri_json == NULL) {
        mcp_json_destroy(params);
        return NULL;
    }

    if (mcp_json_object_set_property(params, "uri", uri_json) != 0) {
        mcp_json_destroy(uri_json);
        mcp_json_destroy(params);
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

    mcp_json_t* params = mcp_json_object_create(); // Removed NULL arena arg
    if (params == NULL) {
        return NULL;
    }

    // Add name
    mcp_json_t* name_json = mcp_json_string_create(name); // Removed NULL arena arg
    if (name_json == NULL) {
        mcp_json_destroy(params);
        return NULL;
    }

    if (mcp_json_object_set_property(params, "name", name_json) != 0) {
        mcp_json_destroy(name_json);
        mcp_json_destroy(params);
        return NULL;
    }

    // Add arguments
    if (arguments != NULL) {
        mcp_json_t* arguments_json = mcp_json_parse(arguments); // Removed NULL arena arg
        if (arguments_json == NULL) {
            mcp_json_destroy(params);
            return NULL;
        }

        if (mcp_json_object_set_property(params, "arguments", arguments_json) != 0) {
            mcp_json_destroy(arguments_json);
            mcp_json_destroy(params);
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

    mcp_json_t* json = mcp_json_parse(json_str); // Removed NULL arena arg
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
        return 0;
    }

    *count = (size_t)array_size;

    // Allocate resources array
    *resources = (mcp_resource_t**)malloc(*count * sizeof(mcp_resource_t*));
    if (*resources == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse resources
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* resource_json = mcp_json_array_get_item(resources_json, (int)i);
        if (resource_json == NULL || mcp_json_get_type(resource_json) != MCP_JSON_OBJECT) {
            for (size_t j = 0; j < i; j++) {
                mcp_resource_free((*resources)[j]);
            }
            free(*resources);
            *resources = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }

        // Get uri
        mcp_json_t* uri_json = mcp_json_object_get_property(resource_json, "uri");
        const char* uri = NULL;
        if (uri_json != NULL && mcp_json_get_type(uri_json) == MCP_JSON_STRING) {
            mcp_json_get_string(uri_json, &uri);
        }

        // Get name
        mcp_json_t* name_json = mcp_json_object_get_property(resource_json, "name");
        const char* name = NULL;
        if (name_json != NULL && mcp_json_get_type(name_json) == MCP_JSON_STRING) {
            mcp_json_get_string(name_json, &name);
        }

        // Get mime_type
        mcp_json_t* mime_type_json = mcp_json_object_get_property(resource_json, "mimeType");
        const char* mime_type = NULL;
        if (mime_type_json != NULL && mcp_json_get_type(mime_type_json) == MCP_JSON_STRING) {
            mcp_json_get_string(mime_type_json, &mime_type);
        }

        // Get description
        mcp_json_t* description_json = mcp_json_object_get_property(resource_json, "description");
        const char* description = NULL;
        if (description_json != NULL && mcp_json_get_type(description_json) == MCP_JSON_STRING) {
            mcp_json_get_string(description_json, &description);
        }

        // Create resource
        (*resources)[i] = mcp_resource_create(uri, name, mime_type, description);
        if ((*resources)[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                mcp_resource_free((*resources)[j]);
            }
            free(*resources);
            *resources = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }
    }

    mcp_json_destroy(json);
    return 0;
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

    mcp_json_t* json = mcp_json_parse(json_str); // Removed NULL arena arg
    if (json == NULL) {
        return -1;
    }

    // Get resource templates array
    mcp_json_t* templates_json = mcp_json_object_get_property(json, "resourceTemplates");
    if (templates_json == NULL || mcp_json_get_type(templates_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get resource templates count
    int array_size = mcp_json_array_get_size(templates_json);
    if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0;
    }

    *count = (size_t)array_size;

    // Allocate resource templates array
    *templates = (mcp_resource_template_t**)malloc(*count * sizeof(mcp_resource_template_t*));
    if (*templates == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse resource templates
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* template_json = mcp_json_array_get_item(templates_json, (int)i);
        if (template_json == NULL || mcp_json_get_type(template_json) != MCP_JSON_OBJECT) {
            for (size_t j = 0; j < i; j++) {
                mcp_resource_template_free((*templates)[j]);
            }
            free(*templates);
            *templates = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }

        // Get uri_template
        mcp_json_t* uri_template_json = mcp_json_object_get_property(template_json, "uriTemplate");
        const char* uri_template = NULL;
        if (uri_template_json != NULL && mcp_json_get_type(uri_template_json) == MCP_JSON_STRING) {
            mcp_json_get_string(uri_template_json, &uri_template);
        }

        // Get name
        mcp_json_t* name_json = mcp_json_object_get_property(template_json, "name");
        const char* name = NULL;
        if (name_json != NULL && mcp_json_get_type(name_json) == MCP_JSON_STRING) {
            mcp_json_get_string(name_json, &name);
        }

        // Get mime_type
        mcp_json_t* mime_type_json = mcp_json_object_get_property(template_json, "mimeType");
        const char* mime_type = NULL;
        if (mime_type_json != NULL && mcp_json_get_type(mime_type_json) == MCP_JSON_STRING) {
            mcp_json_get_string(mime_type_json, &mime_type);
        }

        // Get description
        mcp_json_t* description_json = mcp_json_object_get_property(template_json, "description");
        const char* description = NULL;
        if (description_json != NULL && mcp_json_get_type(description_json) == MCP_JSON_STRING) {
            mcp_json_get_string(description_json, &description);
        }

        // Create resource template
        (*templates)[i] = mcp_resource_template_create(uri_template, name, mime_type, description);
        if ((*templates)[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                mcp_resource_template_free((*templates)[j]);
            }
            free(*templates);
            *templates = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }
    }

    mcp_json_destroy(json);
    return 0;
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

    mcp_json_t* json = mcp_json_parse(json_str); // Removed NULL arena arg
    if (json == NULL) {
        return -1;
    }

    // Get contents array
    mcp_json_t* contents_json = mcp_json_object_get_property(json, "contents");
    if (contents_json == NULL || mcp_json_get_type(contents_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get contents count
    int array_size = mcp_json_array_get_size(contents_json);
    if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0;
    }

    *count = (size_t)array_size;

    // Allocate contents array
    *content = (mcp_content_item_t**)malloc(*count * sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse contents
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* content_json = mcp_json_array_get_item(contents_json, (int)i);
        if (content_json == NULL || mcp_json_get_type(content_json) != MCP_JSON_OBJECT) {
            for (size_t j = 0; j < i; j++) {
                mcp_content_item_free((*content)[j]);
            }
            free(*content);
            *content = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }

        // Get type
        mcp_json_t* type_json = mcp_json_object_get_property(content_json, "type");
        mcp_content_type_t type = MCP_CONTENT_TYPE_TEXT;
        if (type_json != NULL && mcp_json_get_type(type_json) == MCP_JSON_STRING) {
            const char* type_str;
            if (mcp_json_get_string(type_json, &type_str) == 0) {
                if (strcmp(type_str, "text") == 0) {
                    type = MCP_CONTENT_TYPE_TEXT;
                } else if (strcmp(type_str, "json") == 0) {
                    type = MCP_CONTENT_TYPE_JSON;
                } else if (strcmp(type_str, "binary") == 0) {
                    type = MCP_CONTENT_TYPE_BINARY;
                }
            }
        }

        // Get mime_type
        mcp_json_t* mime_type_json = mcp_json_object_get_property(content_json, "mimeType");
        const char* mime_type = NULL;
        if (mime_type_json != NULL && mcp_json_get_type(mime_type_json) == MCP_JSON_STRING) {
            mcp_json_get_string(mime_type_json, &mime_type);
        }

        // Get text
        mcp_json_t* text_json = mcp_json_object_get_property(content_json, "text");
        const char* text = NULL;
        size_t text_size = 0;
        if (text_json != NULL && mcp_json_get_type(text_json) == MCP_JSON_STRING) {
            mcp_json_get_string(text_json, &text);
            if (text != NULL) {
                text_size = strlen(text);
            }
        }

        // Create content item
        (*content)[i] = mcp_content_item_create(type, mime_type, text, text_size);
        if ((*content)[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                mcp_content_item_free((*content)[j]);
            }
            free(*content);
            *content = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }
    }

    mcp_json_destroy(json);
    return 0;
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

    // Get tools count
    int array_size = mcp_json_array_get_size(tools_json);
    if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0;
    }

    *count = (size_t)array_size;

    // Allocate tools array
    *tools = (mcp_tool_t**)malloc(*count * sizeof(mcp_tool_t*));
    if (*tools == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse tools
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* tool_json = mcp_json_array_get_item(tools_json, (int)i);
        if (tool_json == NULL || mcp_json_get_type(tool_json) != MCP_JSON_OBJECT) {
            for (size_t j = 0; j < i; j++) {
                mcp_tool_free((*tools)[j]);
            }
            free(*tools);
            *tools = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }

        // Get name
        mcp_json_t* name_json = mcp_json_object_get_property(tool_json, "name");
        const char* name = NULL;
        if (name_json != NULL && mcp_json_get_type(name_json) == MCP_JSON_STRING) {
            mcp_json_get_string(name_json, &name);
        }

        // Get description
        mcp_json_t* description_json = mcp_json_object_get_property(tool_json, "description");
        const char* description = NULL;
        if (description_json != NULL && mcp_json_get_type(description_json) == MCP_JSON_STRING) {
            mcp_json_get_string(description_json, &description);
        }

        // Create tool
        (*tools)[i] = mcp_tool_create(name, description);
        if ((*tools)[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                mcp_tool_free((*tools)[j]);
            }
            free(*tools);
            *tools = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }

        // Get input schema
        mcp_json_t* input_schema_json = mcp_json_object_get_property(tool_json, "inputSchema");
        if (input_schema_json != NULL && mcp_json_get_type(input_schema_json) == MCP_JSON_OBJECT) {
            // Get properties
            mcp_json_t* properties_json = mcp_json_object_get_property(input_schema_json, "properties");
            if (properties_json != NULL && mcp_json_get_type(properties_json) == MCP_JSON_OBJECT) {
                // Get property names
                char** property_names = NULL;
                size_t property_count = 0;
                if (mcp_json_object_get_property_names(properties_json, &property_names, &property_count) == 0) {
                    // Get required properties
                    mcp_json_t* required_json = mcp_json_object_get_property(input_schema_json, "required");
                    char** required_properties = NULL;
                    size_t required_count = 0;

                    if (required_json != NULL && mcp_json_get_type(required_json) == MCP_JSON_ARRAY) {
                        int required_array_size = mcp_json_array_get_size(required_json);
                        if (required_array_size > 0) {
                            required_count = (size_t)required_array_size;
                            required_properties = (char**)malloc(required_count * sizeof(char*));

                            if (required_properties != NULL) {
                                for (size_t j = 0; j < required_count; j++) {
                                    required_properties[j] = NULL;
                                    mcp_json_t* required_property_json = mcp_json_array_get_item(required_json, (int)j);

                                    if (required_property_json != NULL && mcp_json_get_type(required_property_json) == MCP_JSON_STRING) {
                                        const char* required_property_name;
                                        if (mcp_json_get_string(required_property_json, &required_property_name) == 0) {
                                            required_properties[j] = mcp_strdup(required_property_name);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Add properties to tool
                    for (size_t j = 0; j < property_count; j++) {
                        const char* property_name = property_names[j];
                        mcp_json_t* property_json = mcp_json_object_get_property(properties_json, property_name);
                        if (property_json != NULL && mcp_json_get_type(property_json) == MCP_JSON_OBJECT) {
                            // Get type
                            mcp_json_t* type_json = mcp_json_object_get_property(property_json, "type");
                            const char* type = NULL;
                            if (type_json != NULL && mcp_json_get_type(type_json) == MCP_JSON_STRING) {
                                mcp_json_get_string(type_json, &type);
                            }

                            // Get description
                            mcp_json_t* param_description_json = mcp_json_object_get_property(property_json, "description");
                            const char* param_description = NULL;
                            if (param_description_json != NULL && mcp_json_get_type(param_description_json) == MCP_JSON_STRING) {
                                mcp_json_get_string(param_description_json, &param_description);
                            }

                            // Check if property is required
                            bool required = false;
                            for (size_t k = 0; k < required_count; k++) {
                                if (required_properties != NULL && required_properties[k] != NULL &&
                                    strcmp(required_properties[k], property_name) == 0) {
                                    required = true;
                                    break;
                                }
                            }

                            // Add parameter to tool
                            mcp_tool_add_param((*tools)[i], property_name, type, param_description, required);
                        }
                    }

                    // Free required properties
                    for (size_t j = 0; j < required_count; j++) {
                        free(required_properties[j]);
                    }
                    free(required_properties);
                }

                // Free property names
                for (size_t j = 0; j < property_count; j++) {
                    free(property_names[j]);
                }
                free(property_names);
            }
        }
    }

    mcp_json_destroy(json);
    return 0;
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

    mcp_json_t* json = mcp_json_parse(json_str); // Ensure only one argument is passed
    if (json == NULL) {
        return -1;
    }

    // Get is_error
    mcp_json_t* is_error_json = mcp_json_object_get_property(json, "isError");
    if (is_error_json != NULL && mcp_json_get_type(is_error_json) == MCP_JSON_BOOLEAN) {
        mcp_json_get_boolean(is_error_json, is_error);
    }

    // Get content array
    mcp_json_t* content_json = mcp_json_object_get_property(json, "content");
    if (content_json == NULL || mcp_json_get_type(content_json) != MCP_JSON_ARRAY) {
        mcp_json_destroy(json);
        return -1;
    }

    // Get content count
    int array_size = mcp_json_array_get_size(content_json);
    if (array_size <= 0) {
        mcp_json_destroy(json);
        return 0;
    }

    *count = (size_t)array_size;

    // Allocate content array
    *content = (mcp_content_item_t**)malloc(*count * sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        mcp_json_destroy(json);
        return -1;
    }

    // Parse content
    for (size_t i = 0; i < *count; i++) {
        mcp_json_t* item_json = mcp_json_array_get_item(content_json, (int)i);
        if (item_json == NULL || mcp_json_get_type(item_json) != MCP_JSON_OBJECT) {
            for (size_t j = 0; j < i; j++) {
                mcp_content_item_free((*content)[j]);
            }
            free(*content);
            *content = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }

        // Get type
        mcp_json_t* type_json = mcp_json_object_get_property(item_json, "type");
        mcp_content_type_t type = MCP_CONTENT_TYPE_TEXT;
        if (type_json != NULL && mcp_json_get_type(type_json) == MCP_JSON_STRING) {
            const char* type_str;
            if (mcp_json_get_string(type_json, &type_str) == 0) {
                if (strcmp(type_str, "text") == 0) {
                    type = MCP_CONTENT_TYPE_TEXT;
                } else if (strcmp(type_str, "json") == 0) {
                    type = MCP_CONTENT_TYPE_JSON;
                } else if (strcmp(type_str, "binary") == 0) {
                    type = MCP_CONTENT_TYPE_BINARY;
                }
            }
        }

        // Get mime_type
        mcp_json_t* mime_type_json = mcp_json_object_get_property(item_json, "mimeType");
        const char* mime_type = NULL;
        if (mime_type_json != NULL && mcp_json_get_type(mime_type_json) == MCP_JSON_STRING) {
            mcp_json_get_string(mime_type_json, &mime_type);
        }

        // Get text
        mcp_json_t* text_json = mcp_json_object_get_property(item_json, "text");
        const char* text = NULL;
        size_t text_size = 0;
        if (text_json != NULL && mcp_json_get_type(text_json) == MCP_JSON_STRING) {
            mcp_json_get_string(text_json, &text);
            if (text != NULL) {
                text_size = strlen(text);
            }
        }

        // Create content item
        (*content)[i] = mcp_content_item_create(type, mime_type, text, text_size);
        if ((*content)[i] == NULL) {
            for (size_t j = 0; j < i; j++) {
                mcp_content_item_free((*content)[j]);
            }
            free(*content);
            *content = NULL;
            *count = 0;
            mcp_json_destroy(json);
            return -1;
        }
    }

    mcp_json_destroy(json);
    return 0;
}
