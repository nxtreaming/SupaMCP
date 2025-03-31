#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Handle Windows-specific compatibility issues
#ifdef _WIN32
    // Redefine strdup to _strdup on Windows to avoid deprecation warnings
    #define strdup          _strdup
    
    // Disable warning about nameless struct/union
    #pragma warning(disable: 4201)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MCP protocol version
 */
#define MCP_PROTOCOL_VERSION "0.1.0"

/**
 * Error codes for MCP operations
 */
typedef enum {
    MCP_ERROR_NONE = 0,
    MCP_ERROR_PARSE_ERROR = -32700,
    MCP_ERROR_INVALID_REQUEST = -32600,
    MCP_ERROR_METHOD_NOT_FOUND = -32601,
    MCP_ERROR_INVALID_PARAMS = -32602,
    MCP_ERROR_INTERNAL_ERROR = -32603,
    MCP_ERROR_SERVER_ERROR_START = -32000,
    MCP_ERROR_SERVER_ERROR_END = -32099,
    MCP_ERROR_TRANSPORT_ERROR = -32100,
} mcp_error_code_t;

/**
 * MCP message type
 */
typedef enum {
    MCP_MESSAGE_TYPE_REQUEST,
    MCP_MESSAGE_TYPE_RESPONSE,
    MCP_MESSAGE_TYPE_NOTIFICATION,
    MCP_MESSAGE_TYPE_INVALID,
} mcp_message_type_t;

/**
 * MCP content type
 */
typedef enum {
    MCP_CONTENT_TYPE_TEXT,
    MCP_CONTENT_TYPE_JSON,
    MCP_CONTENT_TYPE_BINARY,
} mcp_content_type_t;

/**
 * MCP resource type
 */
typedef struct {
    char* uri;
    char* name;
    char* mime_type;
    char* description;
} mcp_resource_t;

/**
 * MCP resource template
 */
typedef struct {
    char* uri_template;
    char* name;
    char* mime_type;
    char* description;
} mcp_resource_template_t;

/**
 * MCP tool parameter schema
 */
typedef struct {
    char* name;
    char* type;
    char* description;
    bool required;
} mcp_tool_param_schema_t;

/**
 * MCP tool definition
 */
typedef struct {
    char* name;
    char* description;
    mcp_tool_param_schema_t* input_schema;
    size_t input_schema_count;
} mcp_tool_t;

/**
 * MCP content item
 */
typedef struct {
    mcp_content_type_t type;
    char* mime_type;
    void* data;
    size_t data_size;
} mcp_content_item_t;

/**
 * MCP request
 */
typedef struct {
    uint64_t id;
    char* method;
    void* params;
} mcp_request_t;

/**
 * MCP response
 */
typedef struct {
    uint64_t id;
    mcp_error_code_t error_code;
    char* error_message;
    void* result;
} mcp_response_t;

/**
 * MCP notification
 */
typedef struct {
    char* method;
    void* params;
} mcp_notification_t;

/**
 * MCP message
 */
typedef struct {
    mcp_message_type_t type;
    union {
        mcp_request_t request;
        mcp_response_t response;
        mcp_notification_t notification;
    };
} mcp_message_t;

/**
 * Free a resource
 */
void mcp_resource_free(mcp_resource_t* resource);

/**
 * Free a resource template
 */
void mcp_resource_template_free(mcp_resource_template_t* tmpl);

/**
 * Free a tool
 */
void mcp_tool_free(mcp_tool_t* tool);

/**
 * Free a content item
 */
void mcp_content_item_free(mcp_content_item_t* item);

/**
 * Free a message
 */
void mcp_message_free(mcp_message_t* message);

/**
 * Create a resource
 * 
 * @param uri Resource URI
 * @param name Resource name
 * @param mime_type Resource MIME type
 * @param description Resource description
 * @return Resource or NULL on error
 */
mcp_resource_t* mcp_resource_create(
    const char* uri,
    const char* name,
    const char* mime_type,
    const char* description
);

/**
 * Create a resource template
 * 
 * @param uri_template Resource URI template
 * @param name Resource name
 * @param mime_type Resource MIME type
 * @param description Resource description
 * @return Resource template or NULL on error
 */
mcp_resource_template_t* mcp_resource_template_create(
    const char* uri_template,
    const char* name,
    const char* mime_type,
    const char* description
);

/**
 * Create a tool
 * 
 * @param name Tool name
 * @param description Tool description
 * @return Tool or NULL on error
 */
mcp_tool_t* mcp_tool_create(
    const char* name,
    const char* description
);

/**
 * Add a parameter to a tool
 * 
 * @param tool Tool
 * @param name Parameter name
 * @param type Parameter type
 * @param description Parameter description
 * @param required Whether the parameter is required
 * @return 0 on success, non-zero on error
 */
int mcp_tool_add_param(
    mcp_tool_t* tool,
    const char* name,
    const char* type,
    const char* description,
    bool required
);

/**
 * Create a content item
 * 
 * @param type Content type
 * @param mime_type Content MIME type
 * @param data Content data
 * @param data_size Content data size
 * @return Content item or NULL on error
 */
mcp_content_item_t* mcp_content_item_create(
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size
);

/**
 * Create a request message
 * 
 * @param id Request ID
 * @param method Request method
 * @param params Request parameters
 * @return Message or NULL on error
 */
mcp_message_t* mcp_request_create(
    uint64_t id,
    const char* method,
    const void* params
);

/**
 * Create a response message
 * 
 * @param id Response ID
 * @param error_code Response error code
 * @param error_message Response error message
 * @param result Response result
 * @return Message or NULL on error
 */
mcp_message_t* mcp_response_create(
    uint64_t id,
    mcp_error_code_t error_code,
    const char* error_message,
    const void* result
);

/**
 * Create a notification message
 * 
 * @param method Notification method
 * @param params Notification parameters
 * @return Message or NULL on error
 */
mcp_message_t* mcp_notification_create(
    const char* method,
    const void* params
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_TYPES_H */
