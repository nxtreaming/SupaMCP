#include <stdlib.h>
#include <string.h>
#include "mcp_types.h"

// --- Helper Functions ---

/**
 * @internal
 * @brief Standard C equivalent of strdup. Allocates memory and copies string.
 * @param s The null-terminated string to duplicate.
 * @return Pointer to the newly allocated duplicated string, or NULL on error (NULL input or malloc failure).
 * @note Caller is responsible for freeing the returned string.
 */
char* mcp_strdup(const char* s) { // Removed static keyword
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s) + 1; // +1 for null terminator
    char* new_s = (char*)malloc(len);
    if (new_s == NULL) {
        return NULL; // malloc failed
    }
    memcpy(new_s, s, len); // Copy including null terminator
    return new_s;
}


// --- Free Functions ---

/**
 * @brief Frees an mcp_resource_t structure and its contained strings.
 */
void mcp_resource_free(mcp_resource_t* resource) {
    if (resource == NULL) {
        return; // Nothing to free
    }

    // Free the duplicated strings within the struct (mcp_strdup uses malloc)
    free(resource->uri);
    free(resource->name);
    free(resource->mime_type);
    free(resource->description);
    // Free the struct itself (allocated by mcp_resource_create using malloc)
    free(resource);
}

/**
 * @brief Frees an mcp_resource_template_t structure and its contained strings.
 */
void mcp_resource_template_free(mcp_resource_template_t* tmpl) {
    if (tmpl == NULL) {
        return; // Nothing to free
    }

    // Free the duplicated strings within the struct (mcp_strdup uses malloc)
    free(tmpl->uri_template);
    free(tmpl->name);
    free(tmpl->mime_type);
    free(tmpl->description);
    // Free the struct itself (allocated by mcp_resource_template_create using malloc)
    free(tmpl);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 6001) // Suppress warning about using uninitialized memory 'tool->input_schema'
#endif
/**
 * @brief Frees an mcp_tool_t structure, its contained strings, and its input schema array.
 */
void mcp_tool_free(mcp_tool_t* tool) {
    if (tool == NULL) {
        return; // Nothing to free
    }

    // Free top-level strings (mcp_strdup uses malloc)
    free(tool->name);
    free(tool->description);

    // Free the input schema array and the strings within each schema element
    if (tool->input_schema != NULL) {
        for (size_t i = 0; i < tool->input_schema_count; i++) {
            // Free strings within each parameter schema (mcp_strdup uses malloc)
            free(tool->input_schema[i].name);
            free(tool->input_schema[i].type);
            free(tool->input_schema[i].description);
        }
        // Free the array of schema structs itself (allocated by realloc in add_param)
        free(tool->input_schema);
    }

    // Free the main tool struct (allocated by mcp_tool_create using malloc)
    free(tool);
}
#ifdef _MSC_VER
#pragma warning(pop) // Restore warning settings
#endif

/**
 * @brief Frees an mcp_content_item_t structure and its contained data/strings.
 */
void mcp_content_item_free(mcp_content_item_t* item) {
    if (item == NULL) {
        return; // Nothing to free
    }

    // Free the duplicated mime type string (mcp_strdup uses malloc)
    free(item->mime_type);
    // Free the copied data buffer (malloc uses malloc)
    free(item->data);
    // Free the struct itself (allocated by mcp_content_item_create using malloc)
    free(item);
}

/**
 * @brief Frees dynamically allocated members within a message union, based on message type.
 * Does NOT free the mcp_message_t struct itself.
 */
void mcp_message_release_contents(mcp_message_t* message) {
    if (message == NULL) {
        return;
    }

    // Free members based on the message type
    switch (message->type) {
        case MCP_MESSAGE_TYPE_REQUEST:
            // Method and params strings are typically malloc'd/mcp_strdup'd during parsing or creation
            free(message->request.method);
            free(message->request.params); // Assumes params is a malloc'd string (e.g., from stringify)
            // Nullify to prevent double free if called again
            message->request.method = NULL;
            message->request.params = NULL;
            break;
        case MCP_MESSAGE_TYPE_RESPONSE:
            // error_message and result are typically malloc'd/mcp_strdup'd during parsing or creation
            // Cast needed because error_message is const char* in struct, but we know it was malloc'd if set by parser/create
            free((void*)message->response.error_message);
            free(message->response.result); // Assumes result is a malloc'd string (e.g., from stringify)
            // Nullify to prevent double free
            message->response.error_message = NULL;
            message->response.result = NULL;
            break;
        case MCP_MESSAGE_TYPE_NOTIFICATION:
            // Method and params strings are typically malloc'd/mcp_strdup'd during parsing or creation
            free(message->notification.method);
            free(message->notification.params); // Assumes params is a malloc'd string
            // Nullify to prevent double free
            message->notification.method = NULL;
            message->notification.params = NULL;
            break;
        case MCP_MESSAGE_TYPE_INVALID:
            // No members to free for an invalid type
            break;
    }
    // Reset type? Optional, but might help catch use-after-release errors.
    // message->type = MCP_MESSAGE_TYPE_INVALID;
}


// --- Create Functions ---

/**
 * @brief Creates and allocates an mcp_resource_t structure using malloc.
 * Duplicates input strings using mcp_strdup.
 */
mcp_resource_t* mcp_resource_create(
    const char* uri,
    const char* name,
    const char* mime_type,
    const char* description
) {
    // Allocate the main structure
    mcp_resource_t* resource = (mcp_resource_t*)malloc(sizeof(mcp_resource_t));
    if (resource == NULL) {
        return NULL; // Malloc failed
    }

    // Initialize all string pointers to NULL for safe cleanup on partial allocation failure
    resource->uri = NULL;
    resource->name = NULL;
    resource->mime_type = NULL;
    resource->description = NULL;

    // Duplicate URI string if provided
    if (uri != NULL) {
        resource->uri = mcp_strdup(uri);
        if (resource->uri == NULL) {
            mcp_resource_free(resource); // Cleanup partially allocated struct
            return NULL;
        }
    }

    // Duplicate name string if provided
    if (name != NULL) {
        resource->name = mcp_strdup(name);
        if (resource->name == NULL) {
            mcp_resource_free(resource); // Cleanup partially allocated struct
            return NULL;
        }
    }

    // Duplicate MIME type string if provided
    if (mime_type != NULL) {
        resource->mime_type = mcp_strdup(mime_type);
        if (resource->mime_type == NULL) {
            mcp_resource_free(resource); // Cleanup partially allocated struct
            return NULL;
        }
    }

    // Duplicate description string if provided
    if (description != NULL) {
        resource->description = mcp_strdup(description);
        if (resource->description == NULL) {
            mcp_resource_free(resource); // Cleanup partially allocated struct
            return NULL;
        }
    }

    return resource; // Success
}

/**
 * @brief Creates and allocates an mcp_resource_template_t structure using malloc.
 * Duplicates input strings using mcp_strdup.
 */
mcp_resource_template_t* mcp_resource_template_create(
    const char* uri_template,
    const char* name,
    const char* mime_type,
    const char* description
) {
    // Allocate the main structure
    mcp_resource_template_t* tmpl = (mcp_resource_template_t*)malloc(sizeof(mcp_resource_template_t));
    if (tmpl == NULL) {
        return NULL; // Malloc failed
    }

    // Initialize all string pointers to NULL for safe cleanup
    tmpl->uri_template = NULL;
    tmpl->name = NULL;
    tmpl->mime_type = NULL;
    tmpl->description = NULL;

    // Duplicate URI template string if provided
    if (uri_template != NULL) {
        tmpl->uri_template = mcp_strdup(uri_template);
        if (tmpl->uri_template == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    // Duplicate name string if provided
    if (name != NULL) {
        tmpl->name = mcp_strdup(name);
        if (tmpl->name == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    // Duplicate MIME type string if provided
    if (mime_type != NULL) {
        tmpl->mime_type = mcp_strdup(mime_type);
        if (tmpl->mime_type == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    // Duplicate description string if provided
    if (description != NULL) {
        tmpl->description = mcp_strdup(description);
        if (tmpl->description == NULL) {
            mcp_resource_template_free(tmpl);
            return NULL;
        }
    }

    return tmpl; // Success
}

/**
 * @brief Creates and allocates an mcp_tool_t structure using malloc.
 * Duplicates input strings using mcp_strdup. Initializes schema to empty.
 */
mcp_tool_t* mcp_tool_create(
    const char* name,
    const char* description
) {
    // Allocate the main structure
    mcp_tool_t* tool = (mcp_tool_t*)malloc(sizeof(mcp_tool_t));
    if (tool == NULL) {
        return NULL; // Malloc failed
    }

    // Initialize fields
    tool->name = NULL;
    tool->description = NULL;
    tool->input_schema = NULL; // No parameters initially
    tool->input_schema_count = 0;

    // Duplicate name string if provided
    if (name != NULL) {
        tool->name = mcp_strdup(name);
        if (tool->name == NULL) {
            mcp_tool_free(tool); // Use free function for cleanup
            return NULL;
        }
    } else {
        // Name is mandatory for a tool
        mcp_tool_free(tool);
        return NULL;
    }


    // Duplicate description string if provided
    if (description != NULL) {
        tool->description = mcp_strdup(description);
        if (tool->description == NULL) {
            mcp_tool_free(tool);
            return NULL;
        }
    }

    return tool; // Success
}

/**
 * @brief Adds a parameter definition to a tool's input schema using malloc/realloc/mcp_strdup.
 */
int mcp_tool_add_param(
    mcp_tool_t* tool,
    const char* name,
    const char* type,
    const char* description,
    bool required
) {
    if (tool == NULL || name == NULL || type == NULL) {
        return -1; // Invalid arguments
    }

    // Create a temporary parameter struct on the stack to hold duplicated strings
    mcp_tool_param_schema_t new_param;
    new_param.name = NULL;
    new_param.type = NULL;
    new_param.description = NULL;
    new_param.required = required;

    // Duplicate name string
    new_param.name = mcp_strdup(name);
    if (new_param.name == NULL) {
        return -1; // mcp_strdup failed
    }

    // Duplicate type string
    new_param.type = mcp_strdup(type);
    if (new_param.type == NULL) {
        free(new_param.name); // Clean up already allocated name
        return -1; // mcp_strdup failed
    }

    // Duplicate description string if provided
    if (description != NULL) {
        new_param.description = mcp_strdup(description);
        if (new_param.description == NULL) {
            free(new_param.name);
            free(new_param.type);
            return -1; // mcp_strdup failed
        }
    }

    // Resize the input_schema array using realloc
    mcp_tool_param_schema_t* new_schema = (mcp_tool_param_schema_t*)realloc(
        tool->input_schema, // Pointer to the existing array (or NULL if first time)
        (tool->input_schema_count + 1) * sizeof(mcp_tool_param_schema_t) // New size
    );

    if (new_schema == NULL) {
        // realloc failed, clean up the temporary parameter strings
        free(new_param.name);
        free(new_param.type);
        free(new_param.description);
        return -1; // Indicate failure
    }

    // Update the tool's schema pointer and count
    tool->input_schema = new_schema;
    // Copy the temporary parameter data (including allocated string pointers) to the new slot
    tool->input_schema[tool->input_schema_count] = new_param;
    tool->input_schema_count++;

    return 0; // Success
}

/**
 * @brief Creates and allocates an mcp_content_item_t structure using malloc.
 * Duplicates mime_type string using mcp_strdup and copies data using malloc/memcpy.
 */
mcp_content_item_t* mcp_content_item_create(
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size
) {
    // Allocate the main structure
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (item == NULL) {
        return NULL; // Malloc failed
    }

    // Initialize fields
    item->type = type;
    item->mime_type = NULL;
    item->data = NULL;
    item->data_size = 0;

    // Duplicate mime_type string if provided
    if (mime_type != NULL) {
        item->mime_type = mcp_strdup(mime_type);
        if (item->mime_type == NULL) {
            mcp_content_item_free(item); // Use free function for cleanup
            return NULL;
        }
    }

    // Allocate buffer and copy data if provided
    if (data != NULL && data_size > 0) {
        item->data = malloc(data_size);
        if (item->data == NULL) {
            mcp_content_item_free(item);
            return NULL;
        }
        memcpy(item->data, data, data_size);
        item->data_size = data_size;
    }
    // If data is NULL or data_size is 0, item->data remains NULL and item->data_size remains 0

    return item; // Success
}

/**
 * @brief Creates a deep copy of an mcp_content_item_t structure on the heap.
 */
mcp_content_item_t* mcp_content_item_copy(const mcp_content_item_t* original) {
    if (original == NULL) {
        return NULL;
    }

    // Use mcp_content_item_create to handle allocation and deep copying logic
    return mcp_content_item_create(
        original->type,
        original->mime_type,
        original->data,
        original->data_size
    );
}


// --- Deprecated Message Creation Functions ---
// These allocate the top-level mcp_message_t struct using malloc, which is often less flexible
// than stack allocation combined with mcp_message_release_contents.

mcp_message_t* mcp_request_create(
    uint64_t id,
    const char* method,
    const void* params // Assumed to be a string here
) {
    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize fields
    message->type = MCP_MESSAGE_TYPE_REQUEST;
    message->request.id = id;
    message->request.method = NULL;
    message->request.params = NULL;

    // Duplicate method string
    if (method != NULL) {
        message->request.method = mcp_strdup(method);
        if (message->request.method == NULL) {
            // No need to call release_contents as nothing else was allocated
            free(message);
            return NULL;
        }
    } else {
        // Method is mandatory for request
        free(message);
        return NULL;
    }


    // Duplicate params string (assuming input is string)
    if (params != NULL) {
        message->request.params = mcp_strdup((const char*)params);
        if (message->request.params == NULL) {
            mcp_message_release_contents(message); // Free already allocated method
            free(message);
            return NULL;
        }
    }

    return message;
}


// --- Array Free Functions (Moved from mcp_client.c) ---

/**
 * @brief Frees an array of resources and its contents.
 * @param resources Pointer to the array of resource pointers.
 * @param count Number of elements in the array.
 */
void mcp_free_resources(mcp_resource_t** resources, size_t count) {
    if (resources == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        mcp_resource_free(resources[i]); // Frees individual resource and its strings
    }
    free(resources); // Frees the array itself
}

/**
 * @brief Frees an array of resource templates and its contents.
 * @param templates Pointer to the array of template pointers.
 * @param count Number of elements in the array.
 */
void mcp_free_resource_templates(mcp_resource_template_t** templates, size_t count) {
    if (templates == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        mcp_resource_template_free(templates[i]); // Frees individual template and its strings
    }
    free(templates); // Frees the array itself
}

/**
 * @brief Frees an array of content items and its contents.
 * @param content Pointer to the array of content item pointers.
 * @param count Number of elements in the array.
 */
void mcp_free_content(mcp_content_item_t** content, size_t count) {
    if (content == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        mcp_content_item_free(content[i]); // Frees individual item and its data/strings
    }
    free(content); // Frees the array itself
}

/**
 * @brief Frees an array of tools and its contents.
 * @param tools Pointer to the array of tool pointers.
 * @param count Number of elements in the array.
 */
void mcp_free_tools(mcp_tool_t** tools, size_t count) {
    if (tools == NULL) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        mcp_tool_free(tools[i]); // Frees individual tool and its contents
    }
    free(tools); // Frees the array itself
}

mcp_message_t* mcp_response_create(
    uint64_t id,
    mcp_error_code_t error_code,
    const char* error_message,
    const void* result // Assumed to be a string here
) {
    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize fields
    message->type = MCP_MESSAGE_TYPE_RESPONSE;
    message->response.id = id;
    message->response.error_code = error_code;
    message->response.error_message = NULL;
    message->response.result = NULL;

    // Duplicate error message string if provided
    if (error_message != NULL) {
        // Need to cast away const for assignment, but free needs void* anyway
        message->response.error_message = mcp_strdup(error_message);
        if (message->response.error_message == NULL) {
            free(message);
            return NULL;
        }
    }

    // Duplicate result string if provided (and no error)
    if (error_code == MCP_ERROR_NONE && result != NULL) {
        message->response.result = mcp_strdup((const char*)result);
        if (message->response.result == NULL) {
            mcp_message_release_contents(message); // Free potential error message
            free(message);
            return NULL;
        }
    }

    return message;
}

mcp_message_t* mcp_notification_create(
    const char* method,
    const void* params // Assumed to be a string here
) {
    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize fields
    message->type = MCP_MESSAGE_TYPE_NOTIFICATION;
    message->notification.method = NULL;
    message->notification.params = NULL;

    // Duplicate method string
    if (method != NULL) {
        message->notification.method = mcp_strdup(method);
        if (message->notification.method == NULL) {
            free(message);
            return NULL;
        }
    } else {
        // Method is mandatory for notification
        free(message);
        return NULL;
    }


    // Duplicate params string if provided
    if (params != NULL) {
        message->notification.params = mcp_strdup((const char*)params);
        if (message->notification.params == NULL) {
            mcp_message_release_contents(message); // Free allocated method
            free(message);
            return NULL;
        }
    }

    return message;
}
