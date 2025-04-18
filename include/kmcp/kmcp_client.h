/**
 * @file kmcp_client.h
 * @brief Advanced MCP client API with multi-server management support
 */

#ifndef KMCP_CLIENT_H
#define KMCP_CLIENT_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"
#include "kmcp_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Client configuration
 */
typedef struct {
    char* name;                    /**< Client name */
    char* version;                 /**< Client version */
    bool use_manager;              /**< Whether to use server manager */
    uint32_t timeout_ms;           /**< Request timeout in milliseconds */
} kmcp_client_config_t;

/**
 * @brief Client structure
 */
typedef struct kmcp_client kmcp_client_t;

/**
 * @brief Create a client
 *
 * Creates a new KMCP client with the specified configuration.
 * The client provides a high-level interface for calling tools and accessing resources
 * across multiple MCP servers.
 *
 * @param config Client configuration (must not be NULL)
 * @return kmcp_client_t* Returns client pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the client using kmcp_client_close()
 * @see kmcp_client_close()
 */
kmcp_client_t* kmcp_client_create(kmcp_client_config_t* config);

/**
 * @brief Create a client from a configuration file
 *
 * Creates a new KMCP client by loading configuration from a JSON file.
 * The configuration file should contain client settings, server configurations,
 * and tool access control settings.
 *
 * @param config_file Configuration file path (must not be NULL)
 * @return kmcp_client_t* Returns client pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the client using kmcp_client_close()
 * @see kmcp_client_close()
 */
kmcp_client_t* kmcp_client_create_from_file(const char* config_file);

/**
 * @brief Call a tool
 *
 * Calls a tool on an appropriate server based on tool name.
 * The server selection is handled automatically by the client.
 *
 * @param client Client (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @param params_json Parameter JSON string (must not be NULL)
 * @param result_json Pointer to result JSON string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_PERMISSION_DENIED if tool access is denied
 *         - KMCP_ERROR_TOOL_NOT_FOUND if no server supports the tool
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the result_json string using free()
 */
kmcp_error_t kmcp_client_call_tool(
    kmcp_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
);

/**
 * @brief Get a resource
 *
 * Retrieves a resource from an appropriate server based on resource URI.
 * The server selection is handled automatically by the client.
 *
 * @param client Client (must not be NULL)
 * @param resource_uri Resource URI (must not be NULL)
 * @param content Pointer to content string, memory allocated by function, caller responsible for freeing
 * @param content_type Pointer to content type string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_RESOURCE_NOT_FOUND if no server supports the resource
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the content and content_type strings using free()
 */
kmcp_error_t kmcp_client_get_resource(
    kmcp_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
);

/**
 * @brief Get the server manager
 *
 * Retrieves the server manager associated with this client.
 * This can be used for direct server management operations.
 *
 * @param client Client (must not be NULL)
 * @return kmcp_server_manager_t* Server manager pointer, or NULL if client is NULL
 *
 * @note The returned pointer is owned by the client and should not be freed by the caller
 */
kmcp_server_manager_t* kmcp_client_get_manager(kmcp_client_t* client);

/**
 * @brief Close the client
 *
 * Closes the client and frees all associated resources.
 * This includes disconnecting from all servers and freeing memory.
 *
 * @param client Client (can be NULL, in which case this function does nothing)
 *
 * @note After calling this function, the client pointer is no longer valid and should not be used.
 */
void kmcp_client_close(kmcp_client_t* client);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_CLIENT_H */
