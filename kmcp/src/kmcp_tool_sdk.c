/**
 * @file kmcp_tool_sdk.c
 * @brief Implementation of SDK interface for third-party tool integration with KMCP
 */

#include "kmcp_tool_sdk.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_hashtable.h"
#include "mcp_sync.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Define custom error base
#define KMCP_ERROR_CUSTOM_BASE        (-1000)

// Additional error codes not defined in kmcp_error.h
#define KMCP_ERROR_MUTEX_CREATION     (KMCP_ERROR_CUSTOM_BASE + 100)
#define KMCP_ERROR_NOT_INITIALIZED    (KMCP_ERROR_CUSTOM_BASE + 101)
#define KMCP_ERROR_DUPLICATE          (KMCP_ERROR_CUSTOM_BASE + 102)
#define KMCP_ERROR_UNSUPPORTED        (KMCP_ERROR_CUSTOM_BASE + 103)

/**
 * @brief Tool context structure
 */
struct kmcp_tool_context {
    char* tool_name;                 /**< Tool name */
    kmcp_tool_metadata_t metadata;   /**< Tool metadata */
    kmcp_tool_callbacks_t callbacks; /**< Tool callbacks */
    void* user_data;                 /**< User data */
    bool cancelled;                  /**< Whether the operation has been cancelled */
    mcp_mutex_t* mutex;              /**< Mutex for thread safety */
};

/**
 * @brief Tool registry structure
 */
typedef struct {
    mcp_hashtable_t* tools;          /**< Hash table of registered tools */
    mcp_mutex_t* mutex;              /**< Mutex for thread safety */
} kmcp_tool_registry_t;

/**
 * @brief Thread-local tool context
 */
#ifdef _WIN32
// Windows thread-local storage
static __declspec(thread) kmcp_tool_context_t* g_current_tool_context = NULL;
#else
// POSIX thread-local storage
static __thread kmcp_tool_context_t* g_current_tool_context = NULL;
#endif

/**
 * @brief Global tool registry
 */
static kmcp_tool_registry_t* g_tool_registry = NULL;

/**
 * @brief Initialize the tool registry
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
static kmcp_error_t initialize_tool_registry(void) {
    if (g_tool_registry != NULL) {
        return KMCP_SUCCESS;  // Already initialized
    }

    // Allocate registry
    g_tool_registry = (kmcp_tool_registry_t*)malloc(sizeof(kmcp_tool_registry_t));
    if (g_tool_registry == NULL) {
        mcp_log_error("Failed to allocate tool registry");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize mutex
    g_tool_registry->mutex = mcp_mutex_create();
    if (g_tool_registry->mutex == NULL) {
        mcp_log_error("Failed to create tool registry mutex");
        free(g_tool_registry);
        g_tool_registry = NULL;
        return KMCP_ERROR_MUTEX_CREATION;
    }

    // Initialize hash table
    g_tool_registry->tools = mcp_hashtable_create(
        16,                             // initial_capacity
        0.75f,                          // load_factor_threshold
        mcp_hashtable_string_hash,      // hash_func
        mcp_hashtable_string_compare,   // key_compare
        mcp_hashtable_string_dup,       // key_dup
        mcp_hashtable_string_free,      // key_free
        NULL                            // value_free (handled separately)
    );

    if (g_tool_registry->tools == NULL) {
        mcp_log_error("Failed to create tool registry hash table");
        mcp_mutex_destroy(g_tool_registry->mutex);
        free(g_tool_registry);
        g_tool_registry = NULL;
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Free a tool context
 *
 * @param context Tool context to free
 */
static void free_tool_context(kmcp_tool_context_t* context) {
    if (context == NULL) {
        return;
    }

    // Call cleanup callback if provided
    if (context->callbacks.cleanup != NULL) {
        context->callbacks.cleanup(context);
    }

    // Free metadata strings
    free(context->tool_name);

    // Free metadata arrays
    if (context->metadata.tags != NULL) {
        for (size_t i = 0; i < context->metadata.tags_count; i++) {
            free((void*)context->metadata.tags[i]);
        }
        free(context->metadata.tags);
    }

    if (context->metadata.dependencies != NULL) {
        for (size_t i = 0; i < context->metadata.dependencies_count; i++) {
            free((void*)context->metadata.dependencies[i]);
        }
        free(context->metadata.dependencies);
    }

    // Free mutex
    if (context->mutex != NULL) {
        mcp_mutex_destroy(context->mutex);
    }

    // Free context
    free(context);
}

/**
 * @brief Value free function for the tool registry hash table
 *
 * @param key Key (not used)
 * @param value Value to free (kmcp_tool_context_t*)
 * @param user_data User data (not used)
 */
static void tool_registry_value_free(const void* key, void* value, void* user_data) {
    (void)key;       // Unused parameter
    (void)user_data; // Unused parameter

    if (value != NULL) {
        kmcp_tool_context_t* context = (kmcp_tool_context_t*)value;
        free_tool_context(context);
    }
}

/**
 * @brief Shutdown the tool registry
 */
static void shutdown_tool_registry(void) {
    if (g_tool_registry == NULL) {
        return;  // Not initialized
    }

    // Lock mutex
    mcp_mutex_lock(g_tool_registry->mutex);

    // Use foreach to free all tool contexts
    mcp_hashtable_foreach(g_tool_registry->tools, tool_registry_value_free, NULL);

    // Destroy hash table
    mcp_hashtable_destroy(g_tool_registry->tools);

    // Unlock and destroy mutex
    mcp_mutex_unlock(g_tool_registry->mutex);
    mcp_mutex_destroy(g_tool_registry->mutex);

    // Free registry
    free(g_tool_registry);
    g_tool_registry = NULL;
}

/**
 * @brief Register a tool with KMCP
 */
kmcp_error_t kmcp_tool_register(
    const kmcp_tool_metadata_t* metadata,
    const kmcp_tool_callbacks_t* callbacks
) {
    // Validate parameters
    if (metadata == NULL || callbacks == NULL) {
        mcp_log_error("Invalid parameter: metadata or callbacks is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    if (metadata->name == NULL || metadata->version == NULL) {
        mcp_log_error("Invalid parameter: tool name or version is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    if (callbacks->init == NULL || callbacks->cleanup == NULL || callbacks->execute == NULL) {
        mcp_log_error("Invalid parameter: required callbacks are NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize registry if needed
    kmcp_error_t result = initialize_tool_registry();
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Lock registry mutex
    mcp_mutex_lock(g_tool_registry->mutex);

    // Check if tool is already registered
    void* existing_tool = NULL;
    int get_result = mcp_hashtable_get(g_tool_registry->tools, metadata->name, &existing_tool);
    if (get_result == 0 && existing_tool != NULL) {
        mcp_log_error("Tool '%s' is already registered", metadata->name);
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_DUPLICATE;
    }

    // Create tool context
    kmcp_tool_context_t* context = (kmcp_tool_context_t*)calloc(1, sizeof(kmcp_tool_context_t));
    if (context == NULL) {
        mcp_log_error("Failed to allocate tool context");
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Copy tool name
    context->tool_name = mcp_strdup(metadata->name);
    if (context->tool_name == NULL) {
        mcp_log_error("Failed to duplicate tool name");
        free(context);
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Copy metadata
    context->metadata.name = context->tool_name;  // Already duplicated
    context->metadata.version = mcp_strdup(metadata->version);
    context->metadata.description = metadata->description ? mcp_strdup(metadata->description) : NULL;
    context->metadata.author = metadata->author ? mcp_strdup(metadata->author) : NULL;
    context->metadata.website = metadata->website ? mcp_strdup(metadata->website) : NULL;
    context->metadata.license = metadata->license ? mcp_strdup(metadata->license) : NULL;
    context->metadata.category = metadata->category;
    context->metadata.capabilities = metadata->capabilities;

    // Copy tags
    if (metadata->tags != NULL && metadata->tags_count > 0) {
        context->metadata.tags = (const char**)calloc(metadata->tags_count, sizeof(char*));
        if (context->metadata.tags == NULL) {
            mcp_log_error("Failed to allocate tags array");
            free_tool_context(context);
            mcp_mutex_unlock(g_tool_registry->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        context->metadata.tags_count = metadata->tags_count;
        for (size_t i = 0; i < metadata->tags_count; i++) {
            if (metadata->tags[i] != NULL) {
                ((char**)context->metadata.tags)[i] = mcp_strdup(metadata->tags[i]);
                if (context->metadata.tags[i] == NULL) {
                    mcp_log_error("Failed to duplicate tag");
                    free_tool_context(context);
                    mcp_mutex_unlock(g_tool_registry->mutex);
                    return KMCP_ERROR_MEMORY_ALLOCATION;
                }
            }
        }
    }

    // Copy dependencies
    if (metadata->dependencies != NULL && metadata->dependencies_count > 0) {
        context->metadata.dependencies = (const char**)calloc(metadata->dependencies_count, sizeof(char*));
        if (context->metadata.dependencies == NULL) {
            mcp_log_error("Failed to allocate dependencies array");
            free_tool_context(context);
            mcp_mutex_unlock(g_tool_registry->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        context->metadata.dependencies_count = metadata->dependencies_count;
        for (size_t i = 0; i < metadata->dependencies_count; i++) {
            if (metadata->dependencies[i] != NULL) {
                ((char**)context->metadata.dependencies)[i] = mcp_strdup(metadata->dependencies[i]);
                if (context->metadata.dependencies[i] == NULL) {
                    mcp_log_error("Failed to duplicate dependency");
                    free_tool_context(context);
                    mcp_mutex_unlock(g_tool_registry->mutex);
                    return KMCP_ERROR_MEMORY_ALLOCATION;
                }
            }
        }
    }

    // Copy callbacks
    context->callbacks = *callbacks;

    // Create mutex
    context->mutex = mcp_mutex_create();
    if (context->mutex == NULL) {
        mcp_log_error("Failed to create tool context mutex");
        free_tool_context(context);
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_MUTEX_CREATION;
    }

    // Add to registry
    int put_result = mcp_hashtable_put(g_tool_registry->tools, context->tool_name, context);
    if (put_result != 0) {
        mcp_log_error("Failed to add tool to registry");
        free_tool_context(context);
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_INTERNAL;
    }

    // Initialize tool
    g_current_tool_context = context;  // Set current context for initialization
    result = context->callbacks.init(context);
    g_current_tool_context = NULL;     // Clear current context

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to initialize tool '%s': %s", context->tool_name, kmcp_error_message(result));
        mcp_hashtable_remove(g_tool_registry->tools, context->tool_name);
        free_tool_context(context);
        mcp_mutex_unlock(g_tool_registry->mutex);
        return result;
    }

    // Unlock registry mutex
    mcp_mutex_unlock(g_tool_registry->mutex);

    mcp_log_info("Tool '%s' registered successfully", context->tool_name);
    return KMCP_SUCCESS;
}

/**
 * @brief Unregister a tool from KMCP
 */
kmcp_error_t kmcp_tool_unregister(const char* tool_name) {
    // Validate parameters
    if (tool_name == NULL) {
        mcp_log_error("Invalid parameter: tool_name is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check if registry is initialized
    if (g_tool_registry == NULL) {
        mcp_log_error("Tool registry is not initialized");
        return KMCP_ERROR_NOT_INITIALIZED;
    }

    // Lock registry mutex
    mcp_mutex_lock(g_tool_registry->mutex);

    // Get tool context
    void* value = NULL;
    int get_result = mcp_hashtable_get(g_tool_registry->tools, tool_name, &value);
    if (get_result != 0 || value == NULL) {
        mcp_log_error("Tool '%s' is not registered", tool_name);
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    kmcp_tool_context_t* context = (kmcp_tool_context_t*)value;

    // Remove from registry
    int remove_result = mcp_hashtable_remove(g_tool_registry->tools, tool_name);
    if (remove_result != 0) {
        mcp_log_error("Failed to remove tool from registry");
        mcp_mutex_unlock(g_tool_registry->mutex);
        return KMCP_ERROR_INTERNAL;
    }

    // Free tool context
    free_tool_context(context);

    // Unlock registry mutex
    mcp_mutex_unlock(g_tool_registry->mutex);

    mcp_log_info("Tool '%s' unregistered successfully", tool_name);
    return KMCP_SUCCESS;
}

/**
 * @brief Get tool context
 */
kmcp_tool_context_t* kmcp_tool_get_context(void) {
    return g_current_tool_context;
}

/**
 * @brief Set tool user data
 */
kmcp_error_t kmcp_tool_set_user_data(kmcp_tool_context_t* context, void* user_data) {
    // Validate parameters
    if (context == NULL) {
        mcp_log_error("Invalid parameter: context is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Lock context mutex
    mcp_mutex_lock(context->mutex);

    // Set user data
    context->user_data = user_data;

    // Unlock context mutex
    mcp_mutex_unlock(context->mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Get tool user data
 */
kmcp_error_t kmcp_tool_get_user_data(kmcp_tool_context_t* context, void** user_data) {
    // Validate parameters
    if (context == NULL || user_data == NULL) {
        mcp_log_error("Invalid parameter: context or user_data is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Lock context mutex
    mcp_mutex_lock(context->mutex);

    // Get user data
    *user_data = context->user_data;

    // Unlock context mutex
    mcp_mutex_unlock(context->mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Log a message from a tool
 */
void kmcp_tool_log(kmcp_tool_context_t* context, int level, const char* format, ...) {
    // Validate parameters
    if (context == NULL || format == NULL) {
        return;
    }

    // Format message
    va_list args;
    va_start(args, format);
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Log message with tool name prefix
    switch (level) {
        case 0:  // Trace
            mcp_log_trace("[Tool: %s] %s", context->tool_name, message);
            break;
        case 1:  // Debug
            mcp_log_debug("[Tool: %s] %s", context->tool_name, message);
            break;
        case 2:  // Info
            mcp_log_info("[Tool: %s] %s", context->tool_name, message);
            break;
        case 3:  // Warn
            mcp_log_warn("[Tool: %s] %s", context->tool_name, message);
            break;
        case 4:  // Error
            mcp_log_error("[Tool: %s] %s", context->tool_name, message);
            break;
        case 5:  // Fatal
            mcp_log_fatal("[Tool: %s] %s", context->tool_name, message);
            break;
        default:  // Default to info
            mcp_log_info("[Tool: %s] %s", context->tool_name, message);
            break;
    }
}

/**
 * @brief Send progress update from a tool
 */
kmcp_error_t kmcp_tool_send_progress(
    kmcp_tool_context_t* context,
    float progress,
    const char* message
) {
    // Validate parameters
    if (context == NULL) {
        mcp_log_error("Invalid parameter: context is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    if (progress < 0.0f || progress > 1.0f) {
        mcp_log_error("Invalid parameter: progress must be between 0.0 and 1.0");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // TODO: Implement progress reporting mechanism
    // For now, just log the progress
    if (message != NULL) {
        mcp_log_info("[Tool: %s] Progress: %.1f%% - %s",
            context->tool_name, progress * 100.0f, message);
    } else {
        mcp_log_info("[Tool: %s] Progress: %.1f%%",
            context->tool_name, progress * 100.0f);
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Send a partial result from a tool
 */
kmcp_error_t kmcp_tool_send_partial_result(
    kmcp_tool_context_t* context,
    const mcp_json_t* partial_result
) {
    // Validate parameters
    if (context == NULL || partial_result == NULL) {
        mcp_log_error("Invalid parameter: context or partial_result is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check if tool supports streaming
    if ((context->metadata.capabilities & KMCP_TOOL_CAP_STREAMING) == 0) {
        mcp_log_error("Tool '%s' does not support streaming", context->tool_name);
        return KMCP_ERROR_UNSUPPORTED;
    }

    // TODO: Implement partial result handling mechanism
    // For now, just log that a partial result was sent
    mcp_log_info("[Tool: %s] Partial result sent", context->tool_name);

    return KMCP_SUCCESS;
}

/**
 * @brief Check if a tool operation has been cancelled
 */
bool kmcp_tool_is_cancelled(kmcp_tool_context_t* context) {
    // Validate parameters
    if (context == NULL) {
        return false;
    }

    // Lock context mutex
    mcp_mutex_lock(context->mutex);

    // Get cancelled state
    bool cancelled = context->cancelled;

    // Unlock context mutex
    mcp_mutex_unlock(context->mutex);

    return cancelled;
}

/**
 * @brief Get tool parameter as string
 */
const char* kmcp_tool_get_string_param(
    const mcp_json_t* params,
    const char* key,
    const char* default_value
) {
    // Validate parameters
    if (params == NULL || key == NULL) {
        return default_value;
    }

    // Get property
    mcp_json_t* value = mcp_json_object_get_property(params, key);
    if (value == NULL || mcp_json_get_type(value) != MCP_JSON_STRING) {
        return default_value;
    }

    // Get string value
    const char* str_value = NULL;
    if (mcp_json_get_string(value, &str_value) != 0 || str_value == NULL) {
        return default_value;
    }

    return str_value;
}

/**
 * @brief Get tool parameter as integer
 */
int kmcp_tool_get_int_param(
    const mcp_json_t* params,
    const char* key,
    int default_value
) {
    // Validate parameters
    if (params == NULL || key == NULL) {
        return default_value;
    }

    // Get property
    mcp_json_t* value = mcp_json_object_get_property(params, key);
    if (value == NULL || mcp_json_get_type(value) != MCP_JSON_NUMBER) {
        return default_value;
    }

    // Get number value
    double num_value = 0.0;
    if (mcp_json_get_number(value, &num_value) != 0) {
        return default_value;
    }

    return (int)num_value;
}

/**
 * @brief Get tool parameter as boolean
 */
bool kmcp_tool_get_bool_param(
    const mcp_json_t* params,
    const char* key,
    bool default_value
) {
    // Validate parameters
    if (params == NULL || key == NULL) {
        return default_value;
    }

    // Get property
    mcp_json_t* value = mcp_json_object_get_property(params, key);
    if (value == NULL || mcp_json_get_type(value) != MCP_JSON_BOOLEAN) {
        return default_value;
    }

    // Get boolean value
    bool bool_value = default_value;
    if (mcp_json_get_boolean(value, &bool_value) != 0) {
        return default_value;
    }

    return bool_value;
}

/**
 * @brief Get tool parameter as number
 */
double kmcp_tool_get_number_param(
    const mcp_json_t* params,
    const char* key,
    double default_value
) {
    // Validate parameters
    if (params == NULL || key == NULL) {
        return default_value;
    }

    // Get property
    mcp_json_t* value = mcp_json_object_get_property(params, key);
    if (value == NULL || mcp_json_get_type(value) != MCP_JSON_NUMBER) {
        return default_value;
    }

    // Get number value
    double num_value = default_value;
    if (mcp_json_get_number(value, &num_value) != 0) {
        return default_value;
    }

    return num_value;
}

/**
 * @brief Get tool parameter as object
 */
const mcp_json_t* kmcp_tool_get_object_param(
    const mcp_json_t* params,
    const char* key
) {
    // Validate parameters
    if (params == NULL || key == NULL) {
        return NULL;
    }

    // Get property
    mcp_json_t* value = mcp_json_object_get_property(params, key);
    if (value == NULL || mcp_json_get_type(value) != MCP_JSON_OBJECT) {
        return NULL;
    }

    return value;
}

/**
 * @brief Get tool parameter as array
 */
const mcp_json_t* kmcp_tool_get_array_param(
    const mcp_json_t* params,
    const char* key
) {
    // Validate parameters
    if (params == NULL || key == NULL) {
        return NULL;
    }

    // Get property
    mcp_json_t* value = mcp_json_object_get_property(params, key);
    if (value == NULL || mcp_json_get_type(value) != MCP_JSON_ARRAY) {
        return NULL;
    }

    return value;
}

/**
 * @brief Create a success result
 */
mcp_json_t* kmcp_tool_create_success_result(const char* message) {
    // Create result object
    mcp_json_t* result = mcp_json_object_create();
    if (result == NULL) {
        return NULL;
    }

    // Add success status
    mcp_json_t* success = mcp_json_boolean_create(true);
    if (success == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }
    mcp_json_object_set_property(result, "success", success);

    // Add message if provided
    if (message != NULL) {
        mcp_json_t* msg = mcp_json_string_create(message);
        if (msg == NULL) {
            mcp_json_destroy(result);
            return NULL;
        }
        mcp_json_object_set_property(result, "message", msg);
    }

    return result;
}

/**
 * @brief Create an error result
 */
mcp_json_t* kmcp_tool_create_error_result(const char* message, int error_code) {
    // Validate parameters
    if (message == NULL) {
        return NULL;
    }

    // Create result object
    mcp_json_t* result = mcp_json_object_create();
    if (result == NULL) {
        return NULL;
    }

    // Add success status
    mcp_json_t* success = mcp_json_boolean_create(false);
    if (success == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }
    mcp_json_object_set_property(result, "success", success);

    // Add error message
    mcp_json_t* msg = mcp_json_string_create(message);
    if (msg == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }
    mcp_json_object_set_property(result, "error", msg);

    // Add error code
    mcp_json_t* code = mcp_json_number_create((double)error_code);
    if (code == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }
    mcp_json_object_set_property(result, "code", code);

    return result;
}

/**
 * @brief Create a data result
 */
mcp_json_t* kmcp_tool_create_data_result(const mcp_json_t* data) {
    // Validate parameters
    if (data == NULL) {
        return NULL;
    }

    // Create result object
    mcp_json_t* result = mcp_json_object_create();
    if (result == NULL) {
        return NULL;
    }

    // Add success status
    mcp_json_t* success = mcp_json_boolean_create(true);
    if (success == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }
    mcp_json_object_set_property(result, "success", success);

    // Add data (since we don't have deep copy, we'll just stringify and parse)
    char* data_str = mcp_json_stringify(data);
    if (data_str == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }

    mcp_json_t* data_copy = mcp_json_parse(data_str);
    free(data_str);

    if (data_copy == NULL) {
        mcp_json_destroy(result);
        return NULL;
    }

    mcp_json_object_set_property(result, "data", data_copy);

    return result;
}

/**
 * @brief Module initialization function
 */
#ifdef _WIN32
// Windows DLL entry point
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;  // Unused parameter
    (void)lpvReserved; // Unused parameter
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            // Initialize tool registry
            initialize_tool_registry();
            break;
        case DLL_PROCESS_DETACH:
            // Shutdown tool registry
            shutdown_tool_registry();
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}
#else
// POSIX constructor/destructor attributes
__attribute__((constructor))
static void kmcp_tool_sdk_init(void) {
    // Initialize tool registry
    kmcp_error_t result = initialize_tool_registry();
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to initialize tool registry: %s", kmcp_error_message(result));
    }
}

__attribute__((destructor))
static void kmcp_tool_sdk_cleanup(void) {
    // Shutdown tool registry
    shutdown_tool_registry();
}
#endif
