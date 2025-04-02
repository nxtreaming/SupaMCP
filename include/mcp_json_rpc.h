#ifndef MCP_JSON_RPC_H
#define MCP_JSON_RPC_H

#include "mcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format a JSON-RPC request
 *
 * @param id Request ID
 * @param method Method name
 * @param params Parameters (JSON string)
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_format_request(uint64_t id, const char* method, const char* params);

/**
 * Format a JSON-RPC response
 *
 * @param id Response ID
 * @param result Result (JSON string)
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_format_response(uint64_t id, const char* result);

/**
 * Format a JSON-RPC error response
 *
 * @param id Response ID
 * @param error_code Error code
 * @param error_message Error message
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_format_error_response(uint64_t id, mcp_error_code_t error_code, const char* error_message);

/**
 * Parse a JSON-RPC response
 *
 * @param json JSON string
 * @param id Output request ID
 * @param error_code Output error code
 * @param error_message Output error message (must be freed by the caller)
 * @param result Output result (must be freed by the caller)
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_response(const char* json, uint64_t* id, mcp_error_code_t* error_code, char** error_message, char** result);

/**
 * Format read_resource parameters
 *
 * @param uri Resource URI
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_format_read_resource_params(const char* uri);

/**
 * Format call_tool parameters
 *
 * @param name Tool name
 * @param arguments Tool arguments (JSON string)
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_format_call_tool_params(const char* name, const char* arguments);

/**
 * Parse resources from a JSON-RPC response
 *
 * @param json JSON string
 * @param resources Output resources (must be freed by the caller)
 * @param count Output resource count
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_resources(const char* json, mcp_resource_t*** resources, size_t* count);

/**
 * Parse resource templates from a JSON-RPC response
 *
 * @param json JSON string
 * @param templates Output resource templates (must be freed by the caller)
 * @param count Output resource template count
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_resource_templates(const char* json, mcp_resource_template_t*** templates, size_t* count);

/**
 * Parse content from a JSON-RPC response
 *
 * @param json JSON string
 * @param content Output content (must be freed by the caller)
 * @param count Output content count
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_content(const char* json, mcp_content_item_t*** content, size_t* count);

/**
 * Parse tools from a JSON-RPC response
 *
 * @param json JSON string
 * @param tools Output tools (must be freed by the caller)
 * @param count Output tool count
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_tools(const char* json, mcp_tool_t*** tools, size_t* count);

/**
 * Parse tool result from a JSON-RPC response
 *
 * @param json JSON string
 * @param content Output content (must be freed by the caller)
 * @param count Output content count
 * @param is_error Output error flag
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_tool_result(const char* json, mcp_content_item_t*** content, size_t* count, bool* is_error);

#ifdef __cplusplus
}
#endif

#endif /* MCP_JSON_RPC_H */
