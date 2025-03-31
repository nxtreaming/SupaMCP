#include <stdlib.h>
#include <string.h>
#include "mcp_types.h"

void mcp_resource_free(mcp_resource_t* resource) {
    if (resource == NULL) {
        return;
    }

    free(resource->uri);
    free(resource->name);
    free(resource->mime_type);
    free(resource->description);
    free(resource);
}

void mcp_resource_template_free(mcp_resource_template_t* tmpl) {
    if (tmpl == NULL) {
        return;
    }

    free(tmpl->uri_template);
    free(tmpl->name);
    free(tmpl->mime_type);
    free(tmpl->description);
    free(tmpl);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 6001) // Suppress warning about using uninitialized memory
#endif

void mcp_tool_free(mcp_tool_t* tool) {
    if (tool == NULL) {
        return;
    }

    free(tool->name);
    free(tool->description);

    if (tool->input_schema != NULL) {
        for (size_t i = 0; i < tool->input_schema_count; i++) {
            free(tool->input_schema[i].name);
            free(tool->input_schema[i].type);
            free(tool->input_schema[i].description);
        }
        free(tool->input_schema);
    }

    free(tool);
}

#ifdef _MSC_VER
#pragma warning(pop) // Restore warning settings
#endif

void mcp_content_item_free(mcp_content_item_t* item) {
    if (item == NULL) {
        return;
    }

    free(item->mime_type);
    free(item->data);
    free(item);
}

void mcp_message_free(mcp_message_t* message) {
    if (message == NULL) {
        return;
    }

    switch (message->type) {
        case MCP_MESSAGE_TYPE_REQUEST:
            free(message->request.method);
            free(message->request.params);
            break;
        case MCP_MESSAGE_TYPE_RESPONSE:
            // Freeing const char* - relies on implicit conversion to void*
            free((void*)message->response.error_message);
            free(message->response.result);
            break;
        case MCP_MESSAGE_TYPE_NOTIFICATION:
            free(message->notification.method);
            free(message->notification.params);
            break;
    }

    free(message);
}

mcp_resource_t* mcp_resource_create(
    const char* uri,
    const char* name,
    const char* mime_type,
    const char* description
) {
    mcp_resource_t* resource = (mcp_resource_t*)malloc(sizeof(mcp_resource_t));
    if (resource == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL to ensure safe cleanup in case of failure
    resource->uri = NULL;
    resource->name = NULL;
    resource->mime_type = NULL;
    resource->description = NULL;

    // Allocate memory for URI if provided
    if (uri != NULL) {
        resource->uri = strdup(uri);
        if (resource->uri == NULL) {
            mcp_resource_free(resource);
            return NULL;
        }
    }

    // Allocate memory for name if provided
    if (name != NULL) {
        resource->name = strdup(name);
        if (resource->name == NULL) {
            mcp_resource_free(resource);
            return NULL;
        }
    }

    // Allocate memory for MIME type if provided
    if (mime_type != NULL) {
        resource->mime_type = strdup(mime_type);
        if (resource->mime_type == NULL) {
            mcp_resource_free(resource);
            return NULL;
        }
    }

    // Allocate memory for description if provided
    if (description != NULL) {
        resource->description = strdup(description);
        if (resource->description == NULL) {
            mcp_resource_free(resource);
            return NULL;
        }
    }

    return resource;
}

mcp_resource_template_t* mcp_resource_template_create(
    const char* uri_template,
    const char* name,
    const char* mime_type,
    const char* description
) {
    mcp_resource_template_t* tmpl = (mcp_resource_template_t*)malloc(sizeof(mcp_resource_template_t));
    if (tmpl == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL to ensure safe cleanup in case of failure
    tmpl->uri_template = NULL;
    tmpl->name = NULL;
    tmpl->mime_type = NULL;
    tmpl->description = NULL;

    // Allocate memory for URI template if provided
    if (uri_template != NULL) {
        tmpl->uri_template = strdup(uri_template);
        if (tmpl->uri_template == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    // Allocate memory for name if provided
    if (name != NULL) {
        tmpl->name = strdup(name);
        if (tmpl->name == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    // Allocate memory for MIME type if provided
    if (mime_type != NULL) {
        tmpl->mime_type = strdup(mime_type);
        if (tmpl->mime_type == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    // Allocate memory for description if provided
    if (description != NULL) {
        tmpl->description = strdup(description);
        if (tmpl->description == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    return tmpl;
}

mcp_tool_t* mcp_tool_create(
    const char* name,
    const char* description
) {
    mcp_tool_t* tool = (mcp_tool_t*)malloc(sizeof(mcp_tool_t));
    if (tool == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL to ensure safe cleanup in case of failure
    tool->name = NULL;
    tool->description = NULL;
    tool->input_schema = NULL;
    tool->input_schema_count = 0;

    // Allocate memory for name if provided
    if (name != NULL) {
        tool->name = strdup(name);
        if (tool->name == NULL) {
            mcp_tool_free(tool);
            return NULL;
        }
    }

    // Allocate memory for description if provided
    if (description != NULL) {
        tool->description = strdup(description);
        if (tool->description == NULL) {
            mcp_tool_free(tool);
            return NULL;
        }
    }

    return tool;
}

int mcp_tool_add_param(
    mcp_tool_t* tool,
    const char* name,
    const char* type,
    const char* description,
    bool required
) {
    if (tool == NULL || name == NULL || type == NULL) {
        return -1;
    }

    // Create a temporary parameter to avoid partial initialization in the actual schema
    mcp_tool_param_schema_t new_param;
    new_param.name = NULL;
    new_param.type = NULL;
    new_param.description = NULL;
    new_param.required = required;
    
    // Allocate memory for name
    new_param.name = strdup(name);
    if (new_param.name == NULL) {
        return -1;
    }
    
    // Allocate memory for type
    new_param.type = strdup(type);
    if (new_param.type == NULL) {
        free(new_param.name);
        return -1;
    }
    
    // Allocate memory for description if provided
    if (description != NULL) {
        new_param.description = strdup(description);
        if (new_param.description == NULL) {
            free(new_param.name);
            free(new_param.type);
            return -1;
        }
    }
    
    // Now that all allocations succeeded, resize the schema array
    mcp_tool_param_schema_t* new_schema = (mcp_tool_param_schema_t*)realloc(
        tool->input_schema,
        (tool->input_schema_count + 1) * sizeof(mcp_tool_param_schema_t)
    );

    if (new_schema == NULL) {
        free(new_param.name);
        free(new_param.type);
        free(new_param.description);
        return -1;
    }

    tool->input_schema = new_schema;
    
    // Copy the fully initialized parameter to the schema
    tool->input_schema[tool->input_schema_count] = new_param;
    tool->input_schema_count++;

    return 0;
}

mcp_content_item_t* mcp_content_item_create(
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size
) {
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (item == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL/0 to ensure safe cleanup in case of failure
    item->type = type;
    item->mime_type = NULL;
    item->data = NULL;
    item->data_size = 0;
    
    // Allocate memory for MIME type if provided
    if (mime_type != NULL) {
        item->mime_type = strdup(mime_type);
        if (item->mime_type == NULL) {
            mcp_content_item_free(item);
            return NULL;
        }
    }
    
    // Allocate memory for data if provided
    if (data != NULL && data_size > 0) {
        item->data = malloc(data_size);
        if (item->data == NULL) {
            mcp_content_item_free(item);
            return NULL;
        }
        
        memcpy(item->data, data, data_size);
        item->data_size = data_size;
    }

    return item;
}

mcp_message_t* mcp_request_create(
    uint64_t id,
    const char* method,
    const void* params
) {
    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL to ensure safe cleanup in case of failure
    message->type = MCP_MESSAGE_TYPE_REQUEST;
    message->request.id = id;
    message->request.method = NULL;
    message->request.params = NULL;

    // Allocate memory for method if provided
    if (method != NULL) {
        message->request.method = strdup(method);
        if (message->request.method == NULL) {
            mcp_message_free(message);
            return NULL;
        }
    }

    // Allocate memory for params if provided
    if (params != NULL) {
        message->request.params = strdup(params);
        if (message->request.params == NULL) {
            mcp_message_free(message);
            return NULL;
        }
    }

    return message;
}

mcp_message_t* mcp_response_create(
    uint64_t id,
    mcp_error_code_t error_code,
    const char* error_message,
    const void* result
) {
    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL to ensure safe cleanup in case of failure
    message->type = MCP_MESSAGE_TYPE_RESPONSE;
    message->response.id = id;
    message->response.error_code = error_code;
    message->response.error_message = NULL;
    message->response.result = NULL;

    // Allocate memory for error message if provided
    if (error_message != NULL) {
        message->response.error_message = strdup(error_message);
        if (message->response.error_message == NULL) {
            mcp_message_free(message);
            return NULL;
        }
    }

    // Allocate memory for result if provided
    if (result != NULL) {
        message->response.result = strdup(result);
        if (message->response.result == NULL) {
            mcp_message_free(message);
            return NULL;
        }
    }

    return message;
}

mcp_message_t* mcp_notification_create(
    const char* method,
    const void* params
) {
    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize all fields to NULL to ensure safe cleanup in case of failure
    message->type = MCP_MESSAGE_TYPE_NOTIFICATION;
    message->notification.method = NULL;
    message->notification.params = NULL;

    // Allocate memory for method if provided
    if (method != NULL) {
        message->notification.method = strdup(method);
        if (message->notification.method == NULL) {
            mcp_message_free(message);
            return NULL;
        }
    }

    // Allocate memory for params if provided
    if (params != NULL) {
        message->notification.params = strdup(params);
        if (message->notification.params == NULL) {
            mcp_message_free(message);
            return NULL;
        }
    }

    return message;
}
