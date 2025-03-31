#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include "mcp_types.h"
#include "mcp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Server configuration
 */
typedef struct {
    const char* name;
    const char* version;
    const char* description;
} mcp_server_config_t;

/**
 * Server capabilities
 */
typedef struct {
    bool resources_supported;
    bool tools_supported;
} mcp_server_capabilities_t;

/**
 * Server handle
 */
typedef struct mcp_server mcp_server_t;

/**
 * Resource request handler
 * 
 * @param server Server handle
 * @param uri Resource URI
 * @param user_data User data passed to the server
 * @param content Output content
 * @param content_count Output content count
 * @return 0 on success, non-zero on error
 */
typedef int (*mcp_server_resource_handler_t)(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count
);

/**
 * Tool request handler
 * 
 * @param server Server handle
 * @param name Tool name
 * @param params Tool parameters
 * @param user_data User data passed to the server
 * @param content Output content
 * @param content_count Output content count
 * @param is_error Output error flag
 * @return 0 on success, non-zero on error
 */
typedef int (*mcp_server_tool_handler_t)(
    mcp_server_t* server,
    const char* name,
    const void* params,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count,
    bool* is_error
);

/**
 * Create a server
 * 
 * @param config Server configuration
 * @param capabilities Server capabilities
 * @return Server handle or NULL on error
 */
mcp_server_t* mcp_server_create(
    const mcp_server_config_t* config,
    const mcp_server_capabilities_t* capabilities
);

/**
 * Start the server
 * 
 * @param server Server handle
 * @param transport Transport to use
 * @return 0 on success, non-zero on error
 */
int mcp_server_start(
    mcp_server_t* server,
    mcp_transport_t* transport
);

/**
 * Stop the server
 * 
 * @param server Server handle
 * @return 0 on success, non-zero on error
 */
int mcp_server_stop(mcp_server_t* server);

/**
 * Destroy the server
 * 
 * @param server Server handle
 */
void mcp_server_destroy(mcp_server_t* server);

/**
 * Set the resource handler
 * 
 * @param server Server handle
 * @param handler Resource handler
 * @param user_data User data to pass to the handler
 * @return 0 on success, non-zero on error
 */
int mcp_server_set_resource_handler(
    mcp_server_t* server,
    mcp_server_resource_handler_t handler,
    void* user_data
);

/**
 * Set the tool handler
 * 
 * @param server Server handle
 * @param handler Tool handler
 * @param user_data User data to pass to the handler
 * @return 0 on success, non-zero on error
 */
int mcp_server_set_tool_handler(
    mcp_server_t* server,
    mcp_server_tool_handler_t handler,
    void* user_data
);

/**
 * Add a resource
 * 
 * @param server Server handle
 * @param resource Resource to add
 * @return 0 on success, non-zero on error
 */
int mcp_server_add_resource(
    mcp_server_t* server,
    const mcp_resource_t* resource
);

/**
 * Add a resource template
 * 
 * @param server Server handle
 * @param template Resource template to add
 * @return 0 on success, non-zero on error
 */
int mcp_server_add_resource_template(
    mcp_server_t* server,
    const mcp_resource_template_t* tmpl
);

/**
 * Add a tool
 * 
 * @param server Server handle
 * @param tool Tool to add
 * @return 0 on success, non-zero on error
 */
int mcp_server_add_tool(
    mcp_server_t* server,
    const mcp_tool_t* tool
);

/**
 * Process a message
 * 
 * @param server Server handle
 * @param data Message data
 * @param size Message size
 * @return 0 on success, non-zero on error
 */
int mcp_server_process_message(
    mcp_server_t* server,
    const void* data,
    size_t size
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_SERVER_H */
