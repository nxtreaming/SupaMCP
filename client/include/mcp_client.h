#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../include/mcp_types.h"
#include "../../include/mcp_json_rpc.h"
#include "../../include/mcp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MCP client structure
 */
typedef struct mcp_client mcp_client_t;

/**
 * @brief MCP client configuration options.
 */
typedef struct {
    uint32_t request_timeout_ms; // Timeout for waiting for responses in milliseconds
    // Add other configuration options here if needed
} mcp_client_config_t;

/**
 * @brief Create an MCP client instance.
 *
 * @param config Client configuration settings.
 * @param transport An initialized transport handle for communication. The client
 *                  will take ownership and destroy it via mcp_transport_destroy().
 * @return A pointer to the created client instance, or NULL on failure.
 *         The caller is responsible for destroying the client using mcp_client_destroy().
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport);

/**
 * Destroy an MCP client
 * 
 * @param client Client
 */
void mcp_client_destroy(mcp_client_t* client);

/**
 * List resources from the MCP server
 * 
 * @param client Client
 * @param resources Array of resources (must be freed by the caller)
 * @param count Number of resources
 * @return 0 on success, non-zero on error
 */
int mcp_client_list_resources(
    mcp_client_t* client,
    mcp_resource_t*** resources,
    size_t* count
);

/**
 * List resource templates from the MCP server
 * 
 * @param client Client
 * @param templates Array of resource templates (must be freed by the caller)
 * @param count Number of resource templates
 * @return 0 on success, non-zero on error
 */
int mcp_client_list_resource_templates(
    mcp_client_t* client,
    mcp_resource_template_t*** templates,
    size_t* count
);

/**
 * Read a resource from the MCP server
 * 
 * @param client Client
 * @param uri Resource URI
 * @param content Array of content items (must be freed by the caller)
 * @param count Number of content items
 * @return 0 on success, non-zero on error
 */
int mcp_client_read_resource(
    mcp_client_t* client,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* count
);

/**
 * List tools from the MCP server
 * 
 * @param client Client
 * @param tools Array of tools (must be freed by the caller)
 * @param count Number of tools
 * @return 0 on success, non-zero on error
 */
int mcp_client_list_tools(
    mcp_client_t* client,
    mcp_tool_t*** tools,
    size_t* count
);

/**
 * Call a tool on the MCP server
 * 
 * @param client Client
 * @param name Tool name
 * @param arguments Tool arguments (JSON string)
 * @param content Array of content items (must be freed by the caller)
 * @param count Number of content items
 * @param is_error Whether the tool call resulted in an error
 * @return 0 on success, non-zero on error
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
 * Free an array of resources
 * 
 * @param resources Array of resources
 * @param count Number of resources
 */
void mcp_client_free_resources(mcp_resource_t** resources, size_t count);

/**
 * Free an array of resource templates
 * 
 * @param templates Array of resource templates
 * @param count Number of resource templates
 */
void mcp_client_free_resource_templates(mcp_resource_template_t** templates, size_t count);

/**
 * Free an array of content items
 * 
 * @param content Array of content items
 * @param count Number of content items
 */
void mcp_client_free_content(mcp_content_item_t** content, size_t count);

/**
 * Free an array of tools
 * 
 * @param tools Array of tools
 * @param count Number of tools
 */
void mcp_client_free_tools(mcp_tool_t** tools, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* MCP_CLIENT_H */
