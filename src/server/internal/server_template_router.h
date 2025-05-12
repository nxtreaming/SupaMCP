#ifndef SERVER_TEMPLATE_ROUTER_H
#define SERVER_TEMPLATE_ROUTER_H

#include "server_internal.h"
#include <mcp_types.h>

/**
 * @brief Internal function to register a template-based resource handler
 *
 * @param server The server instance
 * @param template_uri The template URI pattern
 * @param handler The handler function for this template
 * @param user_data User data to pass to the handler
 * @return 0 on success, non-zero on error
 */
int mcp_server_register_template_handler_internal(
    mcp_server_t* server,
    const char* template_uri,
    mcp_server_resource_handler_t handler,
    void* user_data
);

/**
 * @brief Handles a resource request using template-based routing
 *
 * @param server The server instance
 * @param uri The URI to handle
 * @param content Pointer to receive the content items
 * @param content_count Pointer to receive the number of content items
 * @param error_message Pointer to receive an error message
 * @return An error code
 */
mcp_error_code_t mcp_server_handle_template_resource(
    mcp_server_t* server,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
);

/**
 * @brief Callback function to free template routes when the server is destroyed
 */
void mcp_server_free_template_routes(const void* key, void* value, void* user_data);

#endif /* SERVER_TEMPLATE_ROUTER_H */
