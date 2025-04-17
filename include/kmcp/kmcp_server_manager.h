/**
 * @file kmcp_server_manager.h
 * @brief Manages connections and selection of multiple MCP servers
 */

#ifndef KMCP_SERVER_MANAGER_H
#define KMCP_SERVER_MANAGER_H

#include <stddef.h>
#include <stdbool.h>
#include "mcp_types.h"
#include "mcp_transport.h"
#include "mcp_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Server configuration structure
 */
typedef struct {
    char* name;                /**< Server name */
    char* command;             /**< Launch command (for local processes) */
    char** args;               /**< Command arguments */
    size_t args_count;         /**< Number of arguments */
    char* url;                 /**< HTTP URL (for HTTP connections) */
    char** env;                /**< Environment variables */
    size_t env_count;          /**< Number of environment variables */
    bool is_http;              /**< Whether this is an HTTP connection */
} kmcp_server_config_t;

/**
 * @brief Server connection structure
 */
typedef struct kmcp_server_connection kmcp_server_connection_t;

/**
 * @brief Server manager structure
 */
typedef struct kmcp_server_manager kmcp_server_manager_t;

/**
 * @brief Create a server manager
 *
 * @return kmcp_server_manager_t* Returns server manager pointer on success, NULL on failure
 */
kmcp_server_manager_t* kmcp_server_manager_create();

/**
 * @brief Load server configurations from a config file
 *
 * @param manager Server manager
 * @param config_file Configuration file path
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_server_manager_load(kmcp_server_manager_t* manager, const char* config_file);

/**
 * @brief Add a server
 *
 * @param manager Server manager
 * @param config Server configuration
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_server_manager_add(kmcp_server_manager_t* manager, kmcp_server_config_t* config);

/**
 * @brief Connect to all servers
 *
 * @param manager Server manager
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_server_manager_connect(kmcp_server_manager_t* manager);

/**
 * @brief Disconnect from all servers
 *
 * @param manager Server manager
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_server_manager_disconnect(kmcp_server_manager_t* manager);

/**
 * @brief Select an appropriate server for a tool
 *
 * @param manager Server manager
 * @param tool_name Tool name
 * @return int Returns server index on success, -1 on failure
 */
int kmcp_server_manager_select_tool(kmcp_server_manager_t* manager, const char* tool_name);

/**
 * @brief Select an appropriate server for a resource
 *
 * @param manager Server manager
 * @param resource_uri Resource URI
 * @return int Returns server index on success, -1 on failure
 */
int kmcp_server_manager_select_resource(kmcp_server_manager_t* manager, const char* resource_uri);

/**
 * @brief Get a server connection
 *
 * @param manager Server manager
 * @param index Server index
 * @return kmcp_server_connection_t* Returns server connection pointer on success, NULL on failure
 */
kmcp_server_connection_t* kmcp_server_manager_get_connection(kmcp_server_manager_t* manager, int index);

/**
 * @brief Get the number of servers
 *
 * @param manager Server manager
 * @return size_t Number of servers
 */
size_t kmcp_server_manager_get_count(kmcp_server_manager_t* manager);

/**
 * @brief Destroy the server manager
 *
 * @param manager Server manager
 */
void kmcp_server_manager_destroy(kmcp_server_manager_t* manager);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_SERVER_MANAGER_H */
