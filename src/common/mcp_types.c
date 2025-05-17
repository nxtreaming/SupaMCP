#include <stdlib.h>
#include <string.h>
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_object_pool.h"
#include "mcp_log.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"

/**
 * @brief Safely frees memory that could have been allocated by different methods.
 */
void mcp_safe_free(void* ptr, size_t size) {
    if (ptr == NULL) {
        return;
    }

    // Check if this is a pool-allocated block
    size_t block_size = mcp_pool_get_block_size(ptr);
    if (block_size > 0) {
        // It's a pool-allocated block, return it to the pool
        mcp_pool_free(ptr);
    } else if (mcp_memory_pool_system_is_initialized()) {
        // It might be allocated by thread cache
        mcp_thread_cache_free(ptr, size);
    } else {
        // It's a malloc-allocated block, use free
        free(ptr);
    }
}

/**
 * @brief Helper function to duplicate a string and handle error checking.
 *
 * @param src Source string to duplicate (can be NULL).
 * @param dest Pointer to the destination string pointer.
 * @return true if successful or src was NULL, false on allocation failure.
 */
static bool duplicate_string_safe(const char* src, char** dest) {
    if (src == NULL) {
        *dest = NULL;
        return true;
    }

    *dest = mcp_strdup(src);
    return (*dest != NULL);
}

/**
 * @brief Frees an mcp_resource_t structure and its contained strings.
 */
void mcp_resource_free(mcp_resource_t* resource) {
    if (resource == NULL) {
        return;
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
        return;
    }

    // Free the duplicated strings within the struct (mcp_strdup uses malloc)
    free(tmpl->uri_template);
    free(tmpl->name);
    free(tmpl->mime_type);
    free(tmpl->description);
    // Free the struct itself (allocated by mcp_resource_template_create using malloc)
    free(tmpl);
}

/**
 * @brief Frees an mcp_tool_t structure, its contained strings, and its input schema array.
 */
void mcp_tool_free(mcp_tool_t* tool) {
    if (tool == NULL) {
        return;
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

/**
 * @brief Frees an mcp_content_item_t structure and its contained data/strings.
 */
void mcp_content_item_free(mcp_content_item_t* item) {
    if (item == NULL) {
        return;
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

    // Reset type to help catch use-after-release errors
    message->type = MCP_MESSAGE_TYPE_INVALID;
}

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
        return NULL;
    }

    // Initialize all string pointers to NULL for safe cleanup on partial allocation failure
    resource->uri = NULL;
    resource->name = NULL;
    resource->mime_type = NULL;
    resource->description = NULL;

    // Duplicate strings using helper function
    if (!duplicate_string_safe(uri, &resource->uri) ||
        !duplicate_string_safe(name, &resource->name) ||
        !duplicate_string_safe(mime_type, &resource->mime_type) ||
        !duplicate_string_safe(description, &resource->description)) {
        // Cleanup on any allocation failure
        mcp_resource_free(resource);
        return NULL;
    }

    return resource;
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
    mcp_resource_template_t* tmpl = (mcp_resource_template_t*)malloc(sizeof(mcp_resource_template_t));
    if (tmpl == NULL) {
        return NULL;
    }

    // Initialize all string pointers to NULL for safe cleanup
    tmpl->uri_template = NULL;
    tmpl->name = NULL;
    tmpl->mime_type = NULL;
    tmpl->description = NULL;

    // Duplicate strings using helper function
    if (!duplicate_string_safe(uri_template, &tmpl->uri_template) ||
        !duplicate_string_safe(name, &tmpl->name) ||
        !duplicate_string_safe(mime_type, &tmpl->mime_type) ||
        !duplicate_string_safe(description, &tmpl->description)) {
        // Cleanup on any allocation failure
        mcp_resource_template_free(tmpl);
        return NULL;
    }

    return tmpl;
}

/**
 * @brief Creates and allocates an mcp_tool_t structure using malloc.
 * Duplicates input strings using mcp_strdup. Initializes schema to empty.
 */
mcp_tool_t* mcp_tool_create(
    const char* name,
    const char* description
) {
    // Name is mandatory for a tool
    if (name == NULL) {
        return NULL;
    }

    mcp_tool_t* tool = (mcp_tool_t*)malloc(sizeof(mcp_tool_t));
    if (tool == NULL) {
        return NULL;
    }

    // Initialize fields
    tool->name = NULL;
    tool->description = NULL;
    tool->input_schema = NULL; // No parameters initially
    tool->input_schema_count = 0;

    // Duplicate strings using helper function
    if (!duplicate_string_safe(name, &tool->name) ||
        !duplicate_string_safe(description, &tool->description)) {
        // Cleanup on any allocation failure
        mcp_tool_free(tool);
        return NULL;
    }

    return tool;
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
        return -1;
    }

    // Create a temporary parameter struct on the stack to hold duplicated strings
    mcp_tool_param_schema_t new_param;
    new_param.name = NULL;
    new_param.type = NULL;
    new_param.description = NULL;
    new_param.required = required;

    // Duplicate strings using helper function
    if (!duplicate_string_safe(name, &new_param.name) ||
        !duplicate_string_safe(type, &new_param.type) ||
        !duplicate_string_safe(description, &new_param.description)) {
        // Clean up any allocated strings
        free(new_param.name);
        free(new_param.type);
        free(new_param.description);
        return -1;
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

    return 0;
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
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (item == NULL) {
        return NULL;
    }

    // Initialize fields
    item->type = type;
    item->mime_type = NULL;
    item->data = NULL;
    item->data_size = 0;

    // Duplicate mime_type string using helper function
    if (!duplicate_string_safe(mime_type, &item->mime_type)) {
        mcp_content_item_free(item);
        return NULL;
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

    return item;
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

/**
 * @brief Acquires an mcp_content_item_t from an object pool and initializes it.
 * Uses standard malloc for internal data/mime_type copies.
 */
mcp_content_item_t* mcp_content_item_acquire_pooled(
    mcp_object_pool_t* pool,
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size
) {
    if (pool == NULL) {
        mcp_log_error("Attempted to acquire content item from NULL pool.");
        return NULL;
    }

    // Acquire the main structure from the pool
    mcp_content_item_t* item = (mcp_content_item_t*)mcp_object_pool_acquire(pool);
    if (item == NULL) {
        mcp_log_warn("Failed to acquire content item from pool (pool empty or max capacity reached).");
        return NULL; // Pool acquisition failed
    }

    // Initialize fields - IMPORTANT: Clear previous data first
    item->type = type;
    item->mime_type = NULL; // Initialize for safe cleanup
    item->data = NULL;      // Initialize for safe cleanup
    item->data_size = 0;

    // Duplicate mime_type string using helper function
    if (!duplicate_string_safe(mime_type, &item->mime_type)) {
        mcp_log_error("Failed to allocate memory for pooled content item mime_type.");
        mcp_object_pool_release(pool, item); // Release item back to pool on error
        return NULL;
    }

    // Allocate buffer and copy data if provided (using standard malloc)
    if (data != NULL && data_size > 0) {
        item->data = malloc(data_size);
        if (item->data == NULL) {
            mcp_log_error("Failed to allocate memory for pooled content item data buffer.");
            free(item->mime_type); // Free already allocated mime_type
            item->mime_type = NULL;
            mcp_object_pool_release(pool, item); // Release item back to pool on error
            return NULL;
        }
        memcpy(item->data, data, data_size);
        item->data_size = data_size;
    }
    // If data is NULL or data_size is 0, item->data remains NULL and item->data_size remains 0

    return item;
}

/**
 * @brief Releases a content item back to its object pool after freeing internal data.
 *
 * This function frees the internal data and mime_type strings of a content item
 * and then returns the item to its object pool. This is the proper way to release
 * content items that were acquired using mcp_content_item_acquire_pooled().
 *
 * @param pool The object pool the item was acquired from.
 * @param item The content item to release.
 * @return true if the item was successfully released, false otherwise.
 */
bool mcp_content_item_release_pooled(mcp_object_pool_t* pool, mcp_content_item_t* item) {
    if (pool == NULL || item == NULL) {
        return false;
    }

    // Free the internal data (but not the item struct itself)
    free(item->mime_type);
    free(item->data);

    // Reset fields to prevent use-after-free issues if the object is accessed erroneously
    item->mime_type = NULL;
    item->data = NULL;
    item->data_size = 0;

    // Return the item to the pool
    return mcp_object_pool_release(pool, item);
}

// --- Deprecated Message Creation Functions ---
// These allocate the top-level mcp_message_t struct using malloc, which is often less flexible
// than stack allocation combined with mcp_message_release_contents.

mcp_message_t* mcp_request_create(
    uint64_t id,
    const char* method,
    const void* params // Assumed to be a string here
) {
    // Method is mandatory for request
    if (method == NULL) {
        return NULL;
    }

    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize fields
    message->type = MCP_MESSAGE_TYPE_REQUEST;
    message->request.id = id;
    message->request.method = NULL;
    message->request.params = NULL;

    // Duplicate strings using helper function
    if (!duplicate_string_safe(method, &message->request.method) ||
        (params != NULL && !duplicate_string_safe((const char*)params, (char**)&message->request.params))) {
        // Cleanup on any allocation failure
        mcp_message_release_contents(message);
        free(message);
        return NULL;
    }

    return message;
}

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
        mcp_resource_free(resources[i]);
    }
    mcp_safe_free(resources, count * sizeof(mcp_resource_t*));
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
        mcp_resource_template_free(templates[i]);
    }
    mcp_safe_free(templates, count * sizeof(mcp_resource_template_t*));
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
        mcp_content_item_free(content[i]);
    }
    mcp_safe_free(content, count * sizeof(mcp_content_item_t*));
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
        mcp_tool_free(tools[i]);
    }
    mcp_safe_free(tools, count * sizeof(mcp_tool_t*));
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

    // Duplicate error message if provided
    if (error_message != NULL && !duplicate_string_safe(error_message, (char**)&message->response.error_message)) {
        free(message);
        return NULL;
    }

    // Duplicate result string if provided (and no error)
    if (error_code == MCP_ERROR_NONE && result != NULL &&
        !duplicate_string_safe((const char*)result, (char**)&message->response.result)) {
        mcp_message_release_contents(message);
        free(message);
        return NULL;
    }

    return message;
}

mcp_message_t* mcp_notification_create(
    const char* method,
    const void* params // Assumed to be a string here
) {
    // Method is mandatory for notification
    if (method == NULL) {
        return NULL;
    }

    mcp_message_t* message = (mcp_message_t*)malloc(sizeof(mcp_message_t));
    if (message == NULL) {
        return NULL;
    }

    // Initialize fields
    message->type = MCP_MESSAGE_TYPE_NOTIFICATION;
    message->notification.method = NULL;
    message->notification.params = NULL;

    // Duplicate strings using helper function
    if (!duplicate_string_safe(method, &message->notification.method) ||
        (params != NULL && !duplicate_string_safe((const char*)params, (char**)&message->notification.params))) {
        // Cleanup on any allocation failure
        mcp_message_release_contents(message);
        free(message);
        return NULL;
    }

    return message;
}
