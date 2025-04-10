#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Handle Windows-specific compatibility issues
#ifdef _WIN32
    // Disable warning about nameless struct/union
    #pragma warning(disable: 4201)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Cache Line Alignment ---
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64 // Common cache line size
#endif

#ifdef _MSC_VER
#define MCP_CACHE_ALIGNED __declspec(align(CACHE_LINE_SIZE))
#else // GCC/Clang
#define MCP_CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#endif

// Forward declaration for object pool structure
struct mcp_object_pool_s;

/**
 * @brief The current version of the MCP protocol implemented by this library.
 */
#define MCP_PROTOCOL_VERSION "0.1.0"

/**
 * @brief Default maximum size for an MCP message (e.g., 1MB).
 * Used to prevent excessive memory allocation when receiving messages.
 */
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024)

/**
 * @brief Error codes for MCP operations, aligned with JSON-RPC 2.0 error codes where applicable.
 */
typedef enum {
    MCP_ERROR_NONE = 0,                     /**< No error occurred. */
    MCP_ERROR_PARSE_ERROR = -32700,         /**< Invalid JSON was received by the server. */
    MCP_ERROR_INVALID_REQUEST = -32600,     /**< The JSON sent is not a valid Request object. */
    MCP_ERROR_METHOD_NOT_FOUND = -32601,    /**< The method does not exist / is not available. */
    MCP_ERROR_INVALID_PARAMS = -32602,      /**< Invalid method parameter(s). */
    MCP_ERROR_INTERNAL_ERROR = -32603,      /**< Internal JSON-RPC error / Internal MCP library error. */
    MCP_ERROR_SERVER_ERROR_START = -32000,  /**< Start of reserved range for implementation-defined server-errors. */
    MCP_ERROR_SERVER_ERROR_END = -32099,    /**< End of reserved range for implementation-defined server-errors. */
    MCP_ERROR_TRANSPORT_ERROR = -32100,     /**< Custom: Error related to the transport layer (e.g., connection lost, send/receive failed). */
    MCP_ERROR_RESOURCE_NOT_FOUND = -32101,  /**< Custom: The requested resource was not found. */
    MCP_ERROR_TOOL_NOT_FOUND = -32102,      /**< Custom: The requested tool was not found. */
    MCP_ERROR_FORBIDDEN = -32103,           /**< Custom: Access to the requested resource or tool is forbidden. */
    // Add other custom error codes here if needed
} mcp_error_code_t;

/**
 * @brief Identifies the type of an MCP message (Request, Response, or Notification).
 */
typedef enum {
    MCP_MESSAGE_TYPE_REQUEST,       /**< A request message requiring a response. */
    MCP_MESSAGE_TYPE_RESPONSE,      /**< A response message to a previous request. */
    MCP_MESSAGE_TYPE_NOTIFICATION,  /**< A notification message not requiring a response. */
    MCP_MESSAGE_TYPE_INVALID,       /**< Represents an invalid or unparsed message type. */
} mcp_message_type_t;

/**
 * @brief Identifies the type of content within an mcp_content_item_t.
 */
typedef enum {
    MCP_CONTENT_TYPE_TEXT,          /**< Content is plain text (UTF-8 encoded). */
    MCP_CONTENT_TYPE_JSON,          /**< Content is a JSON string. */
    MCP_CONTENT_TYPE_BINARY,        /**< Content is binary data. */
} mcp_content_type_t;

/**
 * @brief Represents a static resource provided by an MCP server.
 * @note Strings are typically owned by the struct and freed by mcp_resource_free.
 */
typedef struct {
    char* uri;          /**< Unique Resource Identifier (e.g., "file:///path/to/file", "db://table/id"). */
    char* name;         /**< Human-readable name for the resource. */
    char* mime_type;    /**< Optional MIME type (e.g., "text/plain", "application/json"). */
    char* description;  /**< Optional description of the resource. */
} mcp_resource_t;

/**
 * @brief Represents a template for dynamically generating resource URIs.
 * @note Strings are typically owned by the struct and freed by mcp_resource_template_free.
 */
typedef struct {
    char* uri_template; /**< URI template string (RFC 6570 format, e.g., "weather://{city}/current"). */
    char* name;         /**< Human-readable name for the template. */
    char* mime_type;    /**< Optional default MIME type for resources generated by this template. */
    char* description;  /**< Optional description of the template. */
} mcp_resource_template_t;

/**
 * @brief Describes a parameter within a tool's input schema.
 * @note Strings are typically owned by the struct and freed when the parent tool is freed.
 */
typedef struct {
    char* name;         /**< Parameter name. */
    char* type;         /**< Parameter type (e.g., "string", "number", "boolean", "object", "array"). */
    char* description;  /**< Optional parameter description. */
    bool required;      /**< True if the parameter is required, false otherwise. */
} mcp_tool_param_schema_t;

/**
 * @brief Represents a tool provided by an MCP server.
 * @note Strings and the input_schema array are typically owned by the struct and freed by mcp_tool_free.
 */
typedef struct {
    char* name;                         /**< Unique tool name. */
    char* description;                  /**< Optional tool description. */
    mcp_tool_param_schema_t* input_schema; /**< Array describing the tool's input parameters. */
    size_t input_schema_count;          /**< Number of parameters in the input_schema array. */
} mcp_tool_t;

/**
 * @brief Represents a piece of content, typically part of a resource or tool response.
 * @note The `data` pointer and `mime_type` string are typically owned by the struct and freed by mcp_content_item_free.
 */
typedef struct {
    mcp_content_type_t type; /**< The type of the content (text, json, binary). */
    char* mime_type;         /**< Optional MIME type (e.g., "text/plain", "application/json"). */
    void* data;              /**< Pointer to the content data. Interpretation depends on `type`. */
    size_t data_size;        /**< Size of the content data in bytes. */
} mcp_content_item_t;

/**
 * @brief Represents an MCP request message.
 * @note Strings and the params structure are typically owned and freed by mcp_message_release_contents.
 */
typedef struct {
    uint64_t id;    /**< Request identifier (must be unique for concurrent requests from a client). */
    char* method;   /**< Name of the method/command to invoke. */
    void* params;   /**< Parameters for the method. Often points to a parsed mcp_json_t structure or a raw JSON string. */
} mcp_request_t;

/**
 * @brief Represents an MCP response message.
 * @note The result structure is typically owned and freed by mcp_message_release_contents. error_message usually points to a const string literal.
 */
typedef struct {
    uint64_t id;                /**< Identifier matching the corresponding request. */
    mcp_error_code_t error_code;/**< Error code if the request failed, MCP_ERROR_NONE otherwise. */
    const char* error_message;  /**< String description of the error, or NULL if no error. */
    void* result;               /**< Result of the request if successful, NULL otherwise. Often points to a parsed mcp_json_t structure or a raw JSON string. */
} mcp_response_t;

/**
 * @brief Represents an MCP notification message.
 * @note Strings and the params structure are typically owned and freed by mcp_message_release_contents.
 */
typedef struct {
    char* method;   /**< Name of the notification method. */
    void* params;   /**< Parameters for the notification. Often points to a parsed mcp_json_t structure or a raw JSON string. */
} mcp_notification_t;

/**
 * @brief Represents a generic MCP message, which can be a request, response, or notification.
 */
typedef struct {
    mcp_message_type_t type; /**< Discriminator indicating the message type. */
    union {
        mcp_request_t request;          /**< Valid if type is MCP_MESSAGE_TYPE_REQUEST. */
        mcp_response_t response;        /**< Valid if type is MCP_MESSAGE_TYPE_RESPONSE. */
        mcp_notification_t notification;/**< Valid if type is MCP_MESSAGE_TYPE_NOTIFICATION. */
    };
} mcp_message_t;

/**
 * @brief Frees the memory allocated for an mcp_resource_t structure and its internal strings.
 * @param resource Pointer to the resource to free. If NULL, the function does nothing.
 */
void mcp_resource_free(mcp_resource_t* resource);

/**
 * @brief Frees the memory allocated for an mcp_resource_template_t structure and its internal strings.
 * @param tmpl Pointer to the resource template to free. If NULL, the function does nothing.
 */
void mcp_resource_template_free(mcp_resource_template_t* tmpl);

/**
 * @brief Frees the memory allocated for an mcp_tool_t structure, its internal strings, and its input schema array.
 * @param tool Pointer to the tool to free. If NULL, the function does nothing.
 */
void mcp_tool_free(mcp_tool_t* tool);

/**
 * @brief Frees the memory allocated for an mcp_content_item_t structure and its internal data/strings.
 * @param item Pointer to the content item to free. If NULL, the function does nothing.
 */
void mcp_content_item_free(mcp_content_item_t* item);

/**
 * @brief Releases the heap-allocated contents *within* a message structure
 *        (e.g., method/param/result strings or parsed JSON structures),
 *        but does *not* free the mcp_message_t struct itself.
 *
 * This is typically used after parsing a message into a stack-allocated
 * mcp_message_t variable to clean up dynamically allocated sub-components
 * before the message variable goes out of scope.
 *
 * @param message Pointer to the message whose contents should be released.
 */
void mcp_message_release_contents(mcp_message_t* message);


// --- Array Free Functions ---

/**
 * @brief Frees an array of resources previously returned by parsing functions.
 * @param resources Pointer to the array of resource pointers to free. Can be NULL.
 * @param count The number of elements in the resources array.
 */
void mcp_free_resources(mcp_resource_t** resources, size_t count);

/**
 * @brief Frees an array of resource templates previously returned by parsing functions.
 * @param templates Pointer to the array of resource template pointers to free. Can be NULL.
 * @param count The number of elements in the templates array.
 */
void mcp_free_resource_templates(mcp_resource_template_t** templates, size_t count);

/**
 * @brief Frees an array of content items previously returned by parsing functions.
 * @param content Pointer to the array of content item pointers to free. Can be NULL.
 * @param count The number of elements in the content array.
 */
void mcp_free_content(mcp_content_item_t** content, size_t count);

/**
 * @brief Frees an array of tools previously returned by parsing functions.
 * @param tools Pointer to the array of tool pointers to free. Can be NULL.
 * @param count The number of elements in the tools array.
 */
void mcp_free_tools(mcp_tool_t** tools, size_t count);


// --- Create Functions ---

/**
 * @brief Creates a new mcp_resource_t structure on the heap.
 *
 * Allocates memory for the structure and copies the provided string arguments.
 *
 * @param uri Resource URI string (copied).
 * @param name Resource name string (copied). Can be NULL.
 * @param mime_type Resource MIME type string (copied). Can be NULL.
 * @param description Resource description string (copied). Can be NULL.
 * @return Pointer to the newly allocated mcp_resource_t, or NULL on error.
 * @note The caller is responsible for freeing the returned structure using mcp_resource_free().
 */
mcp_resource_t* mcp_resource_create(
    const char* uri,
    const char* name,
    const char* mime_type,
    const char* description
);

/**
 * @brief Creates a new mcp_resource_template_t structure on the heap.
 *
 * Allocates memory for the structure and copies the provided string arguments.
 *
 * @param uri_template Resource URI template string (copied).
 * @param name Resource name string (copied). Can be NULL.
 * @param mime_type Resource MIME type string (copied). Can be NULL.
 * @param description Resource description string (copied). Can be NULL.
 * @return Pointer to the newly allocated mcp_resource_template_t, or NULL on error.
 * @note The caller is responsible for freeing the returned structure using mcp_resource_template_free().
 */
mcp_resource_template_t* mcp_resource_template_create(
    const char* uri_template,
    const char* name,
    const char* mime_type,
    const char* description
);

/**
 * @brief Creates a new mcp_tool_t structure on the heap.
 *
 * Allocates memory for the structure and copies the provided string arguments.
 * The input schema is initially empty.
 *
 * @param name Tool name string (copied).
 * @param description Tool description string (copied). Can be NULL.
 * @return Pointer to the newly allocated mcp_tool_t, or NULL on error.
 * @note The caller is responsible for freeing the returned structure using mcp_tool_free().
 */
mcp_tool_t* mcp_tool_create(
    const char* name,
    const char* description
);

/**
 * @brief Adds a parameter definition to a tool's input schema.
 *
 * Reallocates the tool's internal schema array and copies the provided parameter strings.
 *
 * @param tool Pointer to the tool structure to modify.
 * @param name Parameter name string (copied).
 * @param type Parameter type string (copied).
 * @param description Parameter description string (copied). Can be NULL.
 * @param required True if the parameter is required, false otherwise.
 * @return 0 on success, non-zero on error (e.g., memory allocation failure).
 */
int mcp_tool_add_param(
    mcp_tool_t* tool,
    const char* name,
    const char* type,
    const char* description,
    bool required
);

/**
 * @brief Creates a new mcp_content_item_t structure on the heap.
 *
 * Allocates memory for the structure, copies the mime_type string, and copies
 * the content data based on its size.
 *
 * @param type The type of the content.
 * @param mime_type Content MIME type string (copied). Can be NULL.
 * @param data Pointer to the content data to be copied.
 * @param data_size Size of the content data in bytes.
 * @return Pointer to the newly allocated mcp_content_item_t, or NULL on error.
 * @note The caller is responsible for freeing the returned structure using mcp_content_item_free().
 */
mcp_content_item_t* mcp_content_item_create(
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size
);

/**
 * @brief Creates a deep copy of an mcp_content_item_t structure on the heap.
 *
 * Allocates memory for the new structure and performs deep copies of the
 * mime_type string and the data buffer.
 *
 * @param original Pointer to the content item to copy.
 * @return Pointer to the newly allocated copy, or NULL on error.
 * @note The caller is responsible for freeing the returned structure using mcp_content_item_free().
 */
mcp_content_item_t* mcp_content_item_copy(const mcp_content_item_t* original);

/**
 * @brief Acquires an mcp_content_item_t from an object pool and initializes it.
 *
 * Acquires an object from the provided pool, copies the mime_type string,
 * and copies the content data based on its size. The acquired object's
 * internal fields (`data`, `mime_type`) will be allocated using standard malloc.
 *
 * @param pool The object pool to acquire the item from.
 * @param type The type of the content.
 * @param mime_type Content MIME type string (copied). Can be NULL.
 * @param data Pointer to the content data to be copied.
 * @param data_size Size of the content data in bytes.
 * @return Pointer to the acquired and initialized mcp_content_item_t, or NULL on error (pool empty or allocation failure).
 * @note The caller is responsible for releasing the returned structure back to the pool using mcp_object_pool_release()
 *       AND freeing the internal `data` and `mime_type` fields manually before release, or by calling a dedicated release function.
 *       (Consider adding mcp_content_item_release_pooled which handles internal freeing + pool release).
 */
mcp_content_item_t* mcp_content_item_acquire_pooled(
    struct mcp_object_pool_s* pool, // Use the forward-declared struct
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size
);


/**
 * @brief Creates a new heap-allocated mcp_message_t representing a request.
 * @deprecated This function allocates the top-level message struct, which is often
 *             less convenient than stack allocation + mcp_message_release_contents.
 *             Consider constructing the message on the stack if possible.
 * @param id Request ID.
 * @param method Request method string (copied).
 * @param params Request parameters (pointer is copied, ownership depends on context).
 * @return Pointer to the newly allocated mcp_message_t, or NULL on error.
 * @note Caller is responsible for freeing the returned struct AND its contents.
 */
mcp_message_t* mcp_request_create(
    uint64_t id,
    const char* method,
    const void* params
);

/**
 * @brief Creates a new heap-allocated mcp_message_t representing a response.
 * @deprecated This function allocates the top-level message struct. See mcp_request_create note.
 * @param id Response ID.
 * @param error_code Response error code.
 * @param error_message Response error message (pointer is copied, typically points to const data).
 * @param result Response result (pointer is copied, ownership depends on context).
 * @return Pointer to the newly allocated mcp_message_t, or NULL on error.
 * @note Caller is responsible for freeing the returned struct AND its contents.
 */
mcp_message_t* mcp_response_create(
    uint64_t id,
    mcp_error_code_t error_code,
    const char* error_message,
    const void* result
);

/**
 * @brief Creates a new heap-allocated mcp_message_t representing a notification.
 * @deprecated This function allocates the top-level message struct. See mcp_request_create note.
 * @param method Notification method string (copied).
 * @param params Notification parameters (pointer is copied, ownership depends on context).
 * @return Pointer to the newly allocated mcp_message_t, or NULL on error.
 * @note Caller is responsible for freeing the returned struct AND its contents.
 */
mcp_message_t* mcp_notification_create(
    const char* method,
    const void* params
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_TYPES_H */
