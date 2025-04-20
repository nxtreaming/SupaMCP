/**
 * @file kmcp_registry.h
 * @brief Server registry integration for discovering MCP servers
 */

#ifndef KMCP_REGISTRY_H
#define KMCP_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "kmcp_error.h"
#include "kmcp_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registry structure
 */
typedef struct kmcp_registry kmcp_registry_t;

/**
 * @brief Server information structure
 */
typedef struct kmcp_server_info {
    char* id;                       /**< Server ID */
    char* name;                     /**< Server name */
    char* url;                      /**< Server URL */
    char* description;              /**< Server description */
    char* version;                  /**< Server version */
    char** capabilities;            /**< Server capabilities */
    size_t capabilities_count;      /**< Number of capabilities */
    char** tools;                   /**< Supported tools */
    size_t tools_count;             /**< Number of tools */
    char** resources;               /**< Supported resources */
    size_t resources_count;         /**< Number of resources */
    bool is_public;                 /**< Whether the server is public */
    time_t last_seen;               /**< Last time the server was seen */
} kmcp_server_info_t;

/**
 * @brief Registry configuration structure
 */
typedef struct kmcp_registry_config {
    const char* registry_url;       /**< Registry URL (must not be NULL) */
    const char* api_key;            /**< API key (can be NULL) */
    int cache_ttl_seconds;          /**< Cache time-to-live in seconds (0 for default) */
    int connect_timeout_ms;         /**< Connection timeout in milliseconds (0 for default) */
    int request_timeout_ms;         /**< Request timeout in milliseconds (0 for default) */
    int max_retries;                /**< Maximum number of retries (0 for default) */
} kmcp_registry_config_t;

/**
 * @brief Create a registry connection
 *
 * Creates a registry connection for discovering MCP servers.
 *
 * @param registry_url Registry URL (must not be NULL)
 * @return kmcp_registry_t* Returns registry connection pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the registry using kmcp_registry_close()
 * @see kmcp_registry_close()
 */
kmcp_registry_t* kmcp_registry_create(const char* registry_url);

/**
 * @brief Create a registry connection with custom configuration
 *
 * Creates a registry connection for discovering MCP servers using custom configuration.
 *
 * @param config Registry configuration (must not be NULL)
 * @return kmcp_registry_t* Returns registry connection pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the registry using kmcp_registry_close()
 * @see kmcp_registry_close()
 */
kmcp_registry_t* kmcp_registry_create_with_config(const kmcp_registry_config_t* config);

/**
 * @brief Close a registry connection
 *
 * @param registry Registry connection (can be NULL)
 */
void kmcp_registry_close(kmcp_registry_t* registry);

/**
 * @brief Get available servers from the registry
 *
 * Gets a list of available servers from the registry.
 *
 * @param registry Registry connection (must not be NULL)
 * @param servers Pointer to an array of server information structures (output parameter)
 * @param count Pointer to the number of servers (output parameter)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to registry fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the server information array using kmcp_registry_free_server_info()
 * @see kmcp_registry_free_server_info()
 */
kmcp_error_t kmcp_registry_get_servers(kmcp_registry_t* registry, kmcp_server_info_t** servers, size_t* count);

/**
 * @brief Search for servers in the registry
 *
 * Searches for servers in the registry based on a query string.
 *
 * @param registry Registry connection (must not be NULL)
 * @param query Search query (must not be NULL)
 * @param servers Pointer to an array of server information structures (output parameter)
 * @param count Pointer to the number of servers (output parameter)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to registry fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the server information array using kmcp_registry_free_server_info()
 * @see kmcp_registry_free_server_info()
 */
kmcp_error_t kmcp_registry_search_servers(kmcp_registry_t* registry, const char* query, kmcp_server_info_t** servers, size_t* count);

/**
 * @brief Get server information from the registry
 *
 * Gets detailed information about a server from the registry.
 *
 * @param registry Registry connection (must not be NULL)
 * @param server_id Server ID (must not be NULL)
 * @param server_info Pointer to a server information structure (output parameter)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to registry fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - KMCP_ERROR_SERVER_NOT_FOUND if the server is not found
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the server information using kmcp_registry_free_server_info()
 * @see kmcp_registry_free_server_info()
 */
kmcp_error_t kmcp_registry_get_server_info(kmcp_registry_t* registry, const char* server_id, kmcp_server_info_t** server_info);

/**
 * @brief Add a server from the registry to a server manager
 *
 * Adds a server from the registry to a server manager.
 *
 * @param registry Registry connection (must not be NULL)
 * @param manager Server manager (must not be NULL)
 * @param server_id Server ID (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to registry fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - KMCP_ERROR_SERVER_NOT_FOUND if the server is not found
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_registry_add_server(kmcp_registry_t* registry, kmcp_server_manager_t* manager, const char* server_id);

/**
 * @brief Add a server from the registry to a server manager by URL
 *
 * Adds a server from the registry to a server manager by URL.
 *
 * @param registry Registry connection (must not be NULL)
 * @param manager Server manager (must not be NULL)
 * @param url Server URL (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to registry fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - KMCP_ERROR_SERVER_NOT_FOUND if the server is not found
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_registry_add_server_by_url(kmcp_registry_t* registry, kmcp_server_manager_t* manager, const char* url);

/**
 * @brief Refresh the registry cache
 *
 * Refreshes the registry cache by fetching the latest server information.
 *
 * @param registry Registry connection (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if registry is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to registry fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_registry_refresh_cache(kmcp_registry_t* registry);

/**
 * @brief Free server information
 *
 * Frees memory allocated for server information.
 *
 * @param server_info Server information (can be NULL)
 */
void kmcp_registry_free_server_info(kmcp_server_info_t* server_info);

/**
 * @brief Free an array of server information structures
 *
 * Frees memory allocated for an array of server information structures.
 *
 * @param servers Array of server information structures (can be NULL)
 * @param count Number of servers in the array
 */
void kmcp_registry_free_server_info_array(kmcp_server_info_t* servers, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_REGISTRY_H */
