/**
 * @file kmcp_tool_sdk.h
 * @brief SDK interface for third-party tool integration with KMCP
 */

#ifndef KMCP_TOOL_SDK_H
#define KMCP_TOOL_SDK_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"
#include "mcp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tool context structure
 */
typedef struct kmcp_tool_context kmcp_tool_context_t;

/**
 * @brief Tool capability flags
 */
typedef enum {
    KMCP_TOOL_CAP_NONE           = 0,      /**< No special capabilities */
    KMCP_TOOL_CAP_STREAMING      = 1 << 0, /**< Tool supports streaming responses */
    KMCP_TOOL_CAP_BINARY         = 1 << 1, /**< Tool supports binary data */
    KMCP_TOOL_CAP_ASYNC          = 1 << 2, /**< Tool supports asynchronous operation */
    KMCP_TOOL_CAP_CANCELLABLE    = 1 << 3, /**< Tool operations can be cancelled */
    KMCP_TOOL_CAP_BATCH          = 1 << 4, /**< Tool supports batch operations */
    KMCP_TOOL_CAP_STATEFUL       = 1 << 5, /**< Tool maintains state between calls */
    KMCP_TOOL_CAP_RESOURCE_HEAVY = 1 << 6, /**< Tool requires significant resources */
    KMCP_TOOL_CAP_PRIVILEGED     = 1 << 7  /**< Tool requires elevated privileges */
} kmcp_tool_capability_t;

/**
 * @brief Tool category
 */
typedef enum {
    KMCP_TOOL_CATEGORY_GENERAL,    /**< General purpose tool */
    KMCP_TOOL_CATEGORY_SYSTEM,     /**< System management tool */
    KMCP_TOOL_CATEGORY_NETWORK,    /**< Network-related tool */
    KMCP_TOOL_CATEGORY_SECURITY,   /**< Security-related tool */
    KMCP_TOOL_CATEGORY_DEVELOPMENT,/**< Development tool */
    KMCP_TOOL_CATEGORY_MEDIA,      /**< Media processing tool */
    KMCP_TOOL_CATEGORY_AI,         /**< AI/ML tool */
    KMCP_TOOL_CATEGORY_DATABASE,   /**< Database tool */
    KMCP_TOOL_CATEGORY_UTILITY,    /**< Utility tool */
    KMCP_TOOL_CATEGORY_CUSTOM      /**< Custom category */
} kmcp_tool_category_t;

/**
 * @brief Tool metadata structure
 */
typedef struct {
    const char* name;              /**< Tool name (required) */
    const char* version;           /**< Tool version (required) */
    const char* description;       /**< Tool description (optional) */
    const char* author;            /**< Tool author (optional) */
    const char* website;           /**< Tool website (optional) */
    const char* license;           /**< Tool license (optional) */
    const char** tags;             /**< Tool tags (optional) */
    size_t tags_count;             /**< Number of tags */
    kmcp_tool_category_t category; /**< Tool category */
    unsigned int capabilities;     /**< Tool capabilities (bitfield of kmcp_tool_capability_t) */
    const char** dependencies;     /**< Tool dependencies (optional) */
    size_t dependencies_count;     /**< Number of dependencies */
} kmcp_tool_metadata_t;

/**
 * @brief Tool initialization callback
 *
 * Called when the tool is first loaded. Use this to initialize any resources
 * needed by the tool.
 *
 * @param context Tool context
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
typedef kmcp_error_t (*kmcp_tool_init_fn)(kmcp_tool_context_t* context);

/**
 * @brief Tool cleanup callback
 *
 * Called when the tool is being unloaded. Use this to clean up any resources
 * allocated by the tool.
 *
 * @param context Tool context
 */
typedef void (*kmcp_tool_cleanup_fn)(kmcp_tool_context_t* context);

/**
 * @brief Tool execute callback
 *
 * Called when the tool is executed. This is where the main functionality of the
 * tool should be implemented.
 *
 * @param context Tool context
 * @param params Tool parameters as a JSON object
 * @param result Pointer to store the result as a JSON object (output parameter)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
typedef kmcp_error_t (*kmcp_tool_execute_fn)(
    kmcp_tool_context_t* context,
    const mcp_json_t* params,
    mcp_json_t** result
);

/**
 * @brief Tool cancel callback
 *
 * Called when a tool operation is being cancelled. This is optional and only
 * needed for tools that support cancellation.
 *
 * @param context Tool context
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
typedef kmcp_error_t (*kmcp_tool_cancel_fn)(kmcp_tool_context_t* context);

/**
 * @brief Tool callbacks structure
 */
typedef struct {
    kmcp_tool_init_fn init;       /**< Initialization callback (required) */
    kmcp_tool_cleanup_fn cleanup; /**< Cleanup callback (required) */
    kmcp_tool_execute_fn execute; /**< Execute callback (required) */
    kmcp_tool_cancel_fn cancel;   /**< Cancel callback (optional) */
} kmcp_tool_callbacks_t;

/**
 * @brief Register a tool with KMCP
 *
 * Registers a tool with KMCP, making it available for use. The tool metadata
 * and callbacks are copied, so the caller can free the original structures
 * after this call.
 *
 * @param metadata Tool metadata (must not be NULL)
 * @param callbacks Tool callbacks (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_MEMORY_ALLOCATION if memory allocation fails
 *         - KMCP_ERROR_DUPLICATE if a tool with the same name is already registered
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_tool_register(
    const kmcp_tool_metadata_t* metadata,
    const kmcp_tool_callbacks_t* callbacks
);

/**
 * @brief Unregister a tool from KMCP
 *
 * Unregisters a tool from KMCP, making it no longer available for use.
 * This will call the tool's cleanup callback.
 *
 * @param tool_name Tool name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if tool_name is NULL
 *         - KMCP_ERROR_NOT_FOUND if the tool is not registered
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_tool_unregister(const char* tool_name);

/**
 * @brief Get tool context
 *
 * Retrieves the tool context for the current tool execution. This should only
 * be called from within a tool callback.
 *
 * @return kmcp_tool_context_t* Returns the tool context, or NULL if not called from a tool callback
 */
kmcp_tool_context_t* kmcp_tool_get_context(void);

/**
 * @brief Set tool user data
 *
 * Sets user data for the tool context. This can be used to store tool-specific
 * data that needs to be accessed across multiple callbacks.
 *
 * @param context Tool context (must not be NULL)
 * @param user_data User data pointer
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if context is NULL
 */
kmcp_error_t kmcp_tool_set_user_data(kmcp_tool_context_t* context, void* user_data);

/**
 * @brief Get tool user data
 *
 * Retrieves the user data for the tool context.
 *
 * @param context Tool context (must not be NULL)
 * @param user_data Pointer to store the user data (output parameter)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 */
kmcp_error_t kmcp_tool_get_user_data(kmcp_tool_context_t* context, void** user_data);

/**
 * @brief Log a message from a tool
 *
 * Logs a message from a tool. The message will be associated with the tool
 * and can be used for debugging or informational purposes.
 *
 * @param context Tool context (must not be NULL)
 * @param level Log level (0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=fatal)
 * @param format Format string (printf-style)
 * @param ... Arguments for the format string
 */
void kmcp_tool_log(kmcp_tool_context_t* context, int level, const char* format, ...);

/**
 * @brief Send progress update from a tool
 *
 * Sends a progress update from a tool. This can be used to indicate the
 * progress of a long-running operation.
 *
 * @param context Tool context (must not be NULL)
 * @param progress Progress value (0.0 to 1.0)
 * @param message Progress message (optional, can be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if context is NULL or progress is out of range
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_tool_send_progress(
    kmcp_tool_context_t* context,
    float progress,
    const char* message
);

/**
 * @brief Send a partial result from a tool
 *
 * Sends a partial result from a tool. This can be used for streaming results
 * or providing incremental updates during a long-running operation.
 *
 * @param context Tool context (must not be NULL)
 * @param partial_result Partial result as a JSON object (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_UNSUPPORTED if the tool does not support streaming
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_tool_send_partial_result(
    kmcp_tool_context_t* context,
    const mcp_json_t* partial_result
);

/**
 * @brief Check if a tool operation has been cancelled
 *
 * Checks if the current tool operation has been cancelled. This should be
 * called periodically during long-running operations to allow for cancellation.
 *
 * @param context Tool context (must not be NULL)
 * @return bool Returns true if the operation has been cancelled, false otherwise
 */
bool kmcp_tool_is_cancelled(kmcp_tool_context_t* context);

/**
 * @brief Get tool parameter as string
 *
 * Helper function to get a string parameter from the tool parameters.
 *
 * @param params Tool parameters as a JSON object (must not be NULL)
 * @param key Parameter key (must not be NULL)
 * @param default_value Default value to return if the parameter is not found or not a string
 * @return const char* Returns the parameter value, or default_value if not found or not a string
 */
const char* kmcp_tool_get_string_param(
    const mcp_json_t* params,
    const char* key,
    const char* default_value
);

/**
 * @brief Get tool parameter as integer
 *
 * Helper function to get an integer parameter from the tool parameters.
 *
 * @param params Tool parameters as a JSON object (must not be NULL)
 * @param key Parameter key (must not be NULL)
 * @param default_value Default value to return if the parameter is not found or not an integer
 * @return int Returns the parameter value, or default_value if not found or not an integer
 */
int kmcp_tool_get_int_param(
    const mcp_json_t* params,
    const char* key,
    int default_value
);

/**
 * @brief Get tool parameter as boolean
 *
 * Helper function to get a boolean parameter from the tool parameters.
 *
 * @param params Tool parameters as a JSON object (must not be NULL)
 * @param key Parameter key (must not be NULL)
 * @param default_value Default value to return if the parameter is not found or not a boolean
 * @return bool Returns the parameter value, or default_value if not found or not a boolean
 */
bool kmcp_tool_get_bool_param(
    const mcp_json_t* params,
    const char* key,
    bool default_value
);

/**
 * @brief Get tool parameter as number
 *
 * Helper function to get a number parameter from the tool parameters.
 *
 * @param params Tool parameters as a JSON object (must not be NULL)
 * @param key Parameter key (must not be NULL)
 * @param default_value Default value to return if the parameter is not found or not a number
 * @return double Returns the parameter value, or default_value if not found or not a number
 */
double kmcp_tool_get_number_param(
    const mcp_json_t* params,
    const char* key,
    double default_value
);

/**
 * @brief Get tool parameter as object
 *
 * Helper function to get an object parameter from the tool parameters.
 *
 * @param params Tool parameters as a JSON object (must not be NULL)
 * @param key Parameter key (must not be NULL)
 * @return const mcp_json_t* Returns the parameter value, or NULL if not found or not an object
 */
const mcp_json_t* kmcp_tool_get_object_param(
    const mcp_json_t* params,
    const char* key
);

/**
 * @brief Get tool parameter as array
 *
 * Helper function to get an array parameter from the tool parameters.
 *
 * @param params Tool parameters as a JSON object (must not be NULL)
 * @param key Parameter key (must not be NULL)
 * @return const mcp_json_t* Returns the parameter value, or NULL if not found or not an array
 */
const mcp_json_t* kmcp_tool_get_array_param(
    const mcp_json_t* params,
    const char* key
);

/**
 * @brief Create a success result
 *
 * Helper function to create a success result with a message.
 *
 * @param message Success message (optional, can be NULL)
 * @return mcp_json_t* Returns a new JSON object representing a success result
 */
mcp_json_t* kmcp_tool_create_success_result(const char* message);

/**
 * @brief Create an error result
 *
 * Helper function to create an error result with a message and error code.
 *
 * @param message Error message (must not be NULL)
 * @param error_code Error code
 * @return mcp_json_t* Returns a new JSON object representing an error result
 */
mcp_json_t* kmcp_tool_create_error_result(const char* message, int error_code);

/**
 * @brief Create a data result
 *
 * Helper function to create a result with data.
 *
 * @param data Result data as a JSON object (must not be NULL)
 * @return mcp_json_t* Returns a new JSON object representing a data result
 */
mcp_json_t* kmcp_tool_create_data_result(const mcp_json_t* data);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_TOOL_SDK_H */
