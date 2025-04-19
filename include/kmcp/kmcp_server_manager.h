/**
 * @file kmcp_server_manager.h
 * @brief Manages connections and selection of multiple MCP servers
 */

#ifndef KMCP_SERVER_MANAGER_H
#define KMCP_SERVER_MANAGER_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"
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
    char* api_key;             /**< API key (for HTTP connections) */
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
kmcp_server_manager_t* kmcp_server_create();

/**
 * @brief Load server configurations from a config file
 *
 * Loads server configurations from a JSON configuration file and adds them to the manager.
 * The configuration file should contain a "servers" array with server configurations.
 *
 * @param manager Server manager (must not be NULL)
 * @param config_file Configuration file path (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_FILE_NOT_FOUND if the configuration file is not found
 *         - KMCP_ERROR_PARSE_FAILED if the configuration file cannot be parsed
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_server_load(kmcp_server_manager_t* manager, const char* config_file);

/**
 * @brief Add a server
 *
 * Adds a server configuration to the manager. The configuration is copied,
 * so the caller can free the original configuration after this call.
 *
 * @param manager Server manager (must not be NULL)
 * @param config Server configuration (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_MEMORY_ALLOCATION if memory allocation fails
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_server_add(kmcp_server_manager_t* manager, kmcp_server_config_t* config);

/**
 * @brief Connect to all servers
 *
 * Attempts to connect to all servers in the manager. If a server is already connected,
 * it will be skipped. For local process servers, this will start the process.
 *
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if manager is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if no servers could be connected
 *         - Other error codes for specific failures
 *
 * @note This function will attempt to connect to all servers, even if some fail.
 *       It will return success if at least one server was connected successfully.
 */
kmcp_error_t kmcp_server_connect(kmcp_server_manager_t* manager);

/**
 * @brief Disconnect from all servers
 *
 * Disconnects from all servers in the manager. For local process servers,
 * this will not terminate the process, as the server is designed to continue
 * running independently.
 *
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if manager is NULL
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_server_disconnect(kmcp_server_manager_t* manager);

/**
 * @brief Select a server for a tool
 *
 * Selects the most appropriate server for a given tool. The selection is based
 * on whether the server supports the tool and its current connection status.
 *
 * @param manager Server manager (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @return int Returns server index on success, -1 if no suitable server is found
 *
 * @note The returned index can be used with kmcp_server_get_connection()
 *       to get the actual server connection.
 */
int kmcp_server_select_tool(kmcp_server_manager_t* manager, const char* tool_name);

/**
 * @brief Select a server for a resource
 *
 * Selects the most appropriate server for a given resource. The selection is based
 * on whether the server supports the resource and its current connection status.
 *
 * @param manager Server manager (must not be NULL)
 * @param resource_uri Resource URI (must not be NULL)
 * @return int Returns server index on success, -1 if no suitable server is found
 *
 * @note The returned index can be used with kmcp_server_get_connection()
 *       to get the actual server connection.
 */
int kmcp_server_select_resource(kmcp_server_manager_t* manager, const char* resource_uri);

/**
 * @brief Get a server connection
 *
 * Retrieves a server connection by its index. The index is typically obtained
 * from kmcp_server_select_tool() or kmcp_server_select_resource().
 *
 * @param manager Server manager (must not be NULL)
 * @param index Server index (must be valid)
 * @return kmcp_server_connection_t* Returns server connection pointer on success, NULL on failure
 *
 * @note The returned pointer is owned by the manager and should not be freed by the caller
 */
kmcp_server_connection_t* kmcp_server_get_connection(kmcp_server_manager_t* manager, int index);

/**
 * @brief Get the number of servers
 *
 * Retrieves the number of servers currently managed by the server manager.
 *
 * @param manager Server manager (must not be NULL)
 * @return size_t Number of servers, or 0 if manager is NULL
 */
size_t kmcp_server_get_count(kmcp_server_manager_t* manager);

/**
 * @brief Reconnect to a server
 *
 * Attempts to reconnect to a server that has been disconnected.
 *
 * @param manager Server manager (must not be NULL)
 * @param server_index Index of the server to reconnect to
 * @param max_attempts Maximum number of reconnection attempts (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_reconnect(
    kmcp_server_manager_t* manager,
    int server_index,
    int max_attempts,
    int retry_interval_ms
);

/**
 * @brief Reconnect to all disconnected servers
 *
 * Attempts to reconnect to all servers that have been disconnected.
 *
 * @param manager Server manager (must not be NULL)
 * @param max_attempts Maximum number of reconnection attempts per server (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS if all reconnections were successful, or an error code on failure
 */
kmcp_error_t kmcp_server_reconnect_all(
    kmcp_server_manager_t* manager,
    int max_attempts,
    int retry_interval_ms
);

/**
 * @brief Check the health of all server connections
 *
 * Checks the health of all server connections and attempts to reconnect to any
 * disconnected servers. This function can be called periodically to ensure that
 * all servers are connected and healthy.
 *
 * @param manager Server manager (must not be NULL)
 * @param max_attempts Maximum number of reconnection attempts per server (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS if all servers are healthy, or an error code on failure
 */
kmcp_error_t kmcp_server_check_health(
    kmcp_server_manager_t* manager,
    int max_attempts,
    int retry_interval_ms
);

/**
 * @brief Start a background health check thread
 *
 * Starts a background thread that periodically checks the health of all server connections
 * and attempts to reconnect to any disconnected servers.
 *
 * @param manager Server manager (must not be NULL)
 * @param interval_ms Interval between health checks in milliseconds
 * @param max_attempts Maximum number of reconnection attempts per server (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_start_health_check(
    kmcp_server_manager_t* manager,
    int interval_ms,
    int max_attempts,
    int retry_interval_ms
);

/**
 * @brief Stop the background health check thread
 *
 * Stops the background health check thread if it is running.
 *
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_stop_health_check(kmcp_server_manager_t* manager);

/**
 * @brief Check the health of all server connections
 *
 * Checks the health of all server connections and attempts to reconnect to any
 * disconnected servers.
 *
 * @param manager Server manager (must not be NULL)
 * @param max_attempts Maximum number of reconnection attempts per server (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS if all servers are healthy, or an error code on failure
 */
kmcp_error_t kmcp_server_check_health(
    kmcp_server_manager_t* manager,
    int max_attempts,
    int retry_interval_ms
);

/**
 * @brief Destroy the server manager
 *
 * Destroys the server manager and frees all associated resources.
 * This includes disconnecting from all servers and freeing memory.
 *
 * @param manager Server manager (can be NULL, in which case this function does nothing)
 *
 * @note After calling this function, the manager pointer is no longer valid and should not be used.
 */
void kmcp_server_destroy(kmcp_server_manager_t* manager);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_SERVER_MANAGER_H */
