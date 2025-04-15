#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <mcp_transport.h>
#include <mcp_types.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for an MCP client instance.
 */
typedef struct mcp_client mcp_client_t;

/**
 * @brief Configuration for creating an MCP client.
 */
typedef struct {
    uint32_t request_timeout_ms; /**< Timeout in milliseconds for waiting for a response. 0 for default. */
    // Add other config options as needed (e.g., keepalive settings)
} mcp_client_config_t;

/**
 * @brief Creates a new MCP client instance.
 *
 * Initializes the client structure and takes ownership of the provided transport.
 * The transport should be configured but not started; the client will start it.
 *
 * @param config Client configuration settings.
 * @param transport Configured transport handle (e.g., from mcp_transport_tcp_client_create).
 *                  The client takes ownership and will destroy it when mcp_client_destroy is called.
 * @return Pointer to the created client instance, or NULL on failure.
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport);

/**
 * @brief Destroys an MCP client instance.
 *
 * Stops the client's transport, cleans up resources (including pending requests),
 * and destroys the associated transport handle.
 *
 * @param client The client instance to destroy.
 */
void mcp_client_destroy(mcp_client_t* client);

/**
 * @brief Send a request to the MCP server and receive a response.
 *
 * @param client The client instance.
 * @param method The method name.
 * @param params The parameters as a JSON string (can be NULL).
 * @param[out] result Pointer to receive the response string (must be freed by the caller).
 * @param[out] error_code Pointer to receive the error code if the server returns a JSON-RPC error.
 * @param[out] error_message Pointer to receive the error message string if the server returns an error.
 *                           The caller is responsible for freeing this string.
 * @return 0 on successful communication (check error_code for JSON-RPC errors),
 *         -1 on failure (e.g., transport error, timeout, parse error).
 */
int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
);

// --- Standard MCP Client API ---

/**
 * @brief Lists available resources from the server.
 *
 * @param client The client instance.
 * @param[out] resources Pointer to receive an array of mcp_resource_t pointers.
 *                       The caller is responsible for freeing this array and its contents
 *                       using mcp_free_resource_list().
 * @param[out] count Pointer to receive the number of resources in the array.
 * @return 0 on success, -1 on failure (e.g., transport error, parse error).
 */
int mcp_client_list_resources(mcp_client_t* client, mcp_resource_t*** resources, size_t* count);

/**
 * @brief Lists available resource templates from the server.
 *
 * @param client The client instance.
 * @param[out] templates Pointer to receive an array of mcp_resource_template_t pointers.
 *                       The caller is responsible for freeing this array and its contents
 *                       using mcp_free_resource_template_list().
 * @param[out] count Pointer to receive the number of templates in the array.
 * @return 0 on success, -1 on failure.
 */
int mcp_client_list_resource_templates(mcp_client_t* client, mcp_resource_template_t*** templates, size_t* count);

/**
 * @brief Reads the content of a specific resource from the server.
 *
 * @param client The client instance.
 * @param uri The URI of the resource to read.
 * @param[out] content Pointer to receive an array of mcp_content_item_t pointers.
 *                     The caller is responsible for freeing this array and its contents
 *                     using mcp_free_content_list().
 * @param[out] count Pointer to receive the number of content items in the array.
 * @return 0 on success, -1 on failure.
 */
int mcp_client_read_resource(mcp_client_t* client, const char* uri, mcp_content_item_t*** content, size_t* count);

/**
 * @brief Lists available tools from the server.
 *
 * @param client The client instance.
 * @param[out] tools Pointer to receive an array of mcp_tool_t pointers.
 *                   The caller is responsible for freeing this array and its contents
 *                   using mcp_free_tool_list().
 * @param[out] count Pointer to receive the number of tools in the array.
 * @return 0 on success, -1 on failure.
 */
int mcp_client_list_tools(mcp_client_t* client, mcp_tool_t*** tools, size_t* count);

/**
 * @brief Calls a specific tool on the server with given arguments.
 *
 * @param client The client instance.
 * @param name The name of the tool to call.
 * @param arguments A JSON string representing the tool arguments object.
 * @param[out] content Pointer to receive an array of mcp_content_item_t pointers representing the tool's output.
 *                     The caller is responsible for freeing this array and its contents
 *                     using mcp_free_content_list().
 * @param[out] count Pointer to receive the number of content items in the array.
 * @param[out] is_error Pointer to a boolean indicating if the tool execution resulted in an error state
 *                      (distinct from protocol or transport errors).
 * @return 0 on success (tool executed, check is_error for tool-level errors), -1 on failure (protocol/transport error).
 */
int mcp_client_call_tool(mcp_client_t* client, const char* name, const char* arguments, mcp_content_item_t*** content, size_t* count, bool* is_error);

/**
 * @brief Expands a resource template with parameters.
 *
 * @param client The client instance.
 * @param template_uri The URI template to expand (e.g., "example://{name}/resource").
 * @param params_json A JSON string containing parameter values (e.g., {"name": "test"}).
 * @param[out] expanded_uri Pointer to receive the malloc'd expanded URI string.
 *                          The caller is responsible for freeing this string.
 * @return 0 on success, -1 on failure.
 */
int mcp_client_expand_template(mcp_client_t* client, const char* template_uri, const char* params_json, char** expanded_uri);

/**
 * @brief Reads a resource using a template and parameters.
 *
 * This function expands the template with the provided parameters and then reads the resource.
 *
 * @param client The client instance.
 * @param template_uri The URI template to expand (e.g., "example://{name}/resource").
 * @param params_json A JSON string containing parameter values (e.g., {"name": "test"}).
 * @param[out] content Pointer to receive an array of mcp_content_item_t pointers.
 *                     The caller is responsible for freeing this array and its contents
 *                     using mcp_free_content_list().
 * @param[out] count Pointer to receive the number of content items in the array.
 * @return 0 on success, -1 on failure.
 */
int mcp_client_read_resource_with_template(mcp_client_t* client, const char* template_uri, const char* params_json, mcp_content_item_t*** content, size_t* count);

/**
 * @brief Sends a pre-formatted request and receives the raw response.
 *
 * This is useful for scenarios like gateways where the request JSON might already
 * be constructed or needs to be passed through with minimal modification.
 *
 * @param client The client instance.
 * @param method The method name string.
 * @param params_json A JSON string representing the parameters object.
 * @param id The request ID.
 * @param[out] response_json Pointer to receive the malloc'd raw JSON response string from the server.
 *                           The caller is responsible for freeing this string. NULL if an error occurs.
 * @param[out] error_code Pointer to receive the MCP error code if the server returns a JSON-RPC error object.
 *                        Set to MCP_ERROR_NONE on success or transport/parse error.
 * @param[out] error_message Pointer to receive the malloc'd error message string if the server returns an error.
 *                           The caller is responsible for freeing this string. NULL otherwise.
 * @return 0 on successful communication (check error_code for JSON-RPC errors),
 *         -1 on failure (e.g., transport error, timeout, parse error).
 *         On failure, response_json will be NULL. error_code and error_message might be set
 *         depending on the failure point.
 */
int mcp_client_send_raw_request(
    mcp_client_t* client,
    const char* method,
    const char* params_json,
    uint64_t id,
    char** response_json,
    mcp_error_code_t* error_code,
    char** error_message
);


#ifdef __cplusplus
}
#endif

#endif // MCP_CLIENT_H
