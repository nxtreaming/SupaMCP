#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <mcp_types.h>
#include <mcp_json_rpc.h>
#include <mcp_transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle representing an MCP client instance.
 */
typedef struct mcp_client mcp_client_t;

/**
 * @brief Configuration options for an MCP client.
 */
typedef struct {
    /** Timeout in milliseconds for waiting for a response to a request.
     *  A value of 0 or less might indicate an infinite wait (depending on transport). */
    uint32_t request_timeout_ms;
    // Add other configuration options here if needed
} mcp_client_config_t;

/**
 * @brief Creates an MCP client instance.
 *
 * Establishes communication using the provided transport and starts background
 * processing (e.g., a receive thread).
 *
 * @param config Pointer to the client configuration settings. The contents are copied.
 * @param transport An initialized transport handle for communication. The client
 *                  takes ownership of this handle and will destroy it when
 *                  mcp_client_destroy() is called.
 * @return A pointer to the created client instance, or NULL on failure (e.g., memory
 *         allocation error, transport start error).
 * @note The caller is responsible for destroying the returned client instance
 *       using mcp_client_destroy().
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport);

/**
 * @brief Destroys an MCP client instance.
 *
 * Stops any background processing, closes the transport connection, and frees
 * all resources associated with the client.
 *
 * @param client Pointer to the client instance to destroy. If NULL, the function does nothing.
 */
void mcp_client_destroy(mcp_client_t* client);

/**
 * @brief Sends a 'list_resources' request to the MCP server.
 *
 * @param client Pointer to the initialized client instance.
 * @param[out] resources Pointer to a variable that will receive the allocated array
 *                       of mcp_resource_t pointers. The caller is responsible for
 *                       freeing this array using mcp_client_free_resources().
 * @param[out] count Pointer to a variable that will receive the number of resources
 *                   in the returned array.
 * @return 0 on success, non-zero on error (e.g., transport error, timeout, parse error).
 */
int mcp_client_list_resources(
    mcp_client_t* client,
    mcp_resource_t*** resources,
    size_t* count
);

/**
 * @brief Sends a 'list_resource_templates' request to the MCP server.
 *
 * @param client Pointer to the initialized client instance.
 * @param[out] templates Pointer to a variable that will receive the allocated array
 *                       of mcp_resource_template_t pointers. The caller is responsible
 *                       for freeing this array using mcp_client_free_resource_templates().
 * @param[out] count Pointer to a variable that will receive the number of templates
 *                   in the returned array.
 * @return 0 on success, non-zero on error (e.g., transport error, timeout, parse error).
 */
int mcp_client_list_resource_templates(
    mcp_client_t* client,
    mcp_resource_template_t*** templates,
    size_t* count
);

/**
 * @brief Sends a 'read_resource' request to the MCP server for a specific URI.
 *
 * @param client Pointer to the initialized client instance.
 * @param uri The URI of the resource to read. Must not be NULL.
 * @param[out] content Pointer to a variable that will receive the allocated array
 *                     of mcp_content_item_t pointers representing the resource content.
 *                     The caller is responsible for freeing this array using
 *                     mcp_client_free_content().
 * @param[out] count Pointer to a variable that will receive the number of content items
 *                   in the returned array.
 * @return 0 on success, non-zero on error (e.g., transport error, timeout, parse error,
 *         resource not found).
 */
int mcp_client_read_resource(
    mcp_client_t* client,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* count
);

/**
 * @brief Sends a 'list_tools' request to the MCP server.
 *
 * @param client Pointer to the initialized client instance.
 * @param[out] tools Pointer to a variable that will receive the allocated array
 *                   of mcp_tool_t pointers. The caller is responsible for freeing
 *                   this array using mcp_client_free_tools().
 * @param[out] count Pointer to a variable that will receive the number of tools
 *                   in the returned array.
 * @return 0 on success, non-zero on error (e.g., transport error, timeout, parse error).
 */
int mcp_client_list_tools(
    mcp_client_t* client,
    mcp_tool_t*** tools,
    size_t* count
);

/**
 * @brief Sends a 'call_tool' request to the MCP server.
 *
 * @param client Pointer to the initialized client instance.
 * @param name The name of the tool to call. Must not be NULL.
 * @param arguments A JSON string representing the arguments for the tool, or NULL/"{}" for no arguments.
 * @param[out] content Pointer to a variable that will receive the allocated array
 *                     of mcp_content_item_t pointers representing the tool's output.
 *                     The caller is responsible for freeing this array using
 *                     mcp_client_free_content().
 * @param[out] count Pointer to a variable that will receive the number of content items
 *                   in the returned array.
 * @param[out] is_error Pointer to a boolean that will be set to true if the tool execution
 *                      itself resulted in an error (as indicated by the server response),
 *                      false otherwise.
 * @return 0 on successful communication and parsing of the response (check is_error
 *         for tool-specific errors), non-zero on communication or parsing errors.
 */
int mcp_client_call_tool(
    mcp_client_t* client,
    const char* name,
    const char* arguments,
    mcp_content_item_t*** content,
    size_t* count,
    bool* is_error
);

/**
 * @brief Frees an array of resources previously returned by mcp_client_list_resources.
 *
 * This function iterates through the array, frees each individual resource
 * using mcp_resource_free(), and then frees the array itself.
 *
 * @param resources Pointer to the array of resource pointers to free. Can be NULL.
 * @param count The number of elements in the resources array.
 */
void mcp_client_free_resources(mcp_resource_t** resources, size_t count);

/**
 * @brief Frees an array of resource templates previously returned by mcp_client_list_resource_templates.
 *
 * This function iterates through the array, frees each individual template
 * using mcp_resource_template_free(), and then frees the array itself.
 *
 * @param templates Pointer to the array of resource template pointers to free. Can be NULL.
 * @param count The number of elements in the templates array.
 */
void mcp_client_free_resource_templates(mcp_resource_template_t** templates, size_t count);

/**
 * @brief Frees an array of content items previously returned by mcp_client_read_resource or mcp_client_call_tool.
 *
 * This function iterates through the array, frees each individual content item
 * using mcp_content_item_free(), and then frees the array itself.
 *
 * @param content Pointer to the array of content item pointers to free. Can be NULL.
 * @param count The number of elements in the content array.
 */
void mcp_client_free_content(mcp_content_item_t** content, size_t count);

/**
 * @brief Frees an array of tools previously returned by mcp_client_list_tools.
 *
 * This function iterates through the array, frees each individual tool
 * using mcp_tool_free(), and then frees the array itself.
 *
 * @param tools Pointer to the array of tool pointers to free. Can be NULL.
 * @param count The number of elements in the tools array.
 */
void mcp_client_free_tools(mcp_tool_t** tools, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* MCP_CLIENT_H */
