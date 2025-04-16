/**
 * @file gateway_config_manager.h
 * @brief Thread-safe gateway configuration manager using read-write locks
 */

#ifndef MCP_GATEWAY_CONFIG_MANAGER_H
#define MCP_GATEWAY_CONFIG_MANAGER_H

#include "gateway.h"
#include "mcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque type for the gateway configuration manager
 */
typedef struct gateway_config_manager gateway_config_manager_t;

/**
 * @brief Creates a new gateway configuration manager
 * 
 * @param config_path Path to the gateway configuration file
 * @return Pointer to the newly created manager, or NULL on failure
 */
gateway_config_manager_t* gateway_config_manager_create(const char* config_path);

/**
 * @brief Destroys the gateway configuration manager
 * 
 * @param manager Pointer to the manager to destroy
 */
void gateway_config_manager_destroy(gateway_config_manager_t* manager);

/**
 * @brief Gets the backend list from the configuration manager
 * 
 * This function acquires a read lock on the configuration data, allowing
 * multiple threads to read the configuration simultaneously.
 * 
 * @param manager Pointer to the configuration manager
 * @param[out] backend_count Pointer to a variable that will receive the number of backends
 * @return Pointer to the backend list, or NULL on failure
 * @note The returned pointer is owned by the manager and should not be freed by the caller
 */
const mcp_backend_info_t* gateway_config_manager_get_backends(
    gateway_config_manager_t* manager,
    size_t* backend_count
);

/**
 * @brief Reloads the configuration from the file
 * 
 * This function acquires a write lock on the configuration data, ensuring
 * exclusive access while the configuration is being updated.
 * 
 * @param manager Pointer to the configuration manager
 * @return MCP_ERROR_NONE on success, or an appropriate error code
 */
mcp_error_code_t gateway_config_manager_reload(gateway_config_manager_t* manager);

/**
 * @brief Finds a backend for a given request
 * 
 * This function acquires a read lock on the configuration data, allowing
 * multiple threads to find backends simultaneously.
 * 
 * @param manager Pointer to the configuration manager
 * @param request Pointer to the request
 * @return Pointer to the matching backend, or NULL if no match is found
 * @note The returned pointer is owned by the manager and should not be freed by the caller
 */
const mcp_backend_info_t* gateway_config_manager_find_backend(
    gateway_config_manager_t* manager,
    const mcp_request_t* request
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_GATEWAY_CONFIG_MANAGER_H */
