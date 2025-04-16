/**
 * @file gateway_config_manager.c
 * @brief Implementation of thread-safe gateway configuration manager using read-write locks
 */

#include "gateway_config_manager.h"
#include "gateway.h"
#include "gateway_routing.h"
#include "mcp_log.h"
#include "mcp_rwlock.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Internal structure for the gateway configuration manager
 */
struct gateway_config_manager {
    char* config_path;                 /**< Path to the configuration file */
    mcp_rwlock_t* config_lock;         /**< Read-write lock for thread-safe access */
    mcp_backend_info_t* backend_list;  /**< Array of backend configurations */
    size_t backend_count;              /**< Number of backends in the list */
};

/**
 * @brief Creates a new gateway configuration manager
 */
gateway_config_manager_t* gateway_config_manager_create(const char* config_path) {
    if (!config_path) {
        mcp_log_error("Cannot create gateway config manager with NULL config path");
        return NULL;
    }

    gateway_config_manager_t* manager = (gateway_config_manager_t*)malloc(sizeof(gateway_config_manager_t));
    if (!manager) {
        mcp_log_error("Failed to allocate memory for gateway config manager");
        return NULL;
    }

    // Initialize manager
    manager->config_path = mcp_strdup(config_path);
    manager->config_lock = mcp_rwlock_create();
    manager->backend_list = NULL;
    manager->backend_count = 0;

    if (!manager->config_path || !manager->config_lock) {
        mcp_log_error("Failed to initialize gateway config manager");
        if (manager->config_path) free(manager->config_path);
        mcp_rwlock_free(manager->config_lock);
        free(manager);
        return NULL;
    }

    // Load initial configuration
    mcp_error_code_t err = gateway_config_manager_reload(manager);
    if (err != MCP_ERROR_NONE) {
        mcp_log_error("Failed to load initial gateway configuration: %d", err);
        gateway_config_manager_destroy(manager);
        return NULL;
    }

    mcp_log_info("Gateway configuration manager created with %zu backends", manager->backend_count);
    return manager;
}

/**
 * @brief Destroys the gateway configuration manager
 */
void gateway_config_manager_destroy(gateway_config_manager_t* manager) {
    if (!manager) {
        return;
    }

    // Acquire write lock to ensure exclusive access during cleanup
    mcp_rwlock_write_lock(manager->config_lock);

    // Free backend list
    if (manager->backend_list) {
        mcp_free_backend_list(manager->backend_list, manager->backend_count);
        manager->backend_list = NULL;
        manager->backend_count = 0;
    }

    // Release lock before destroying it
    mcp_rwlock_write_unlock(manager->config_lock);

    // Free resources
    if (manager->config_path) free(manager->config_path);
    mcp_rwlock_free(manager->config_lock);
    free(manager);

    mcp_log_info("Gateway configuration manager destroyed");
}

/**
 * @brief Gets the backend list from the configuration manager
 */
const mcp_backend_info_t* gateway_config_manager_get_backends(
    gateway_config_manager_t* manager,
    size_t* backend_count
) {
    if (!manager || !backend_count) {
        if (backend_count) *backend_count = 0;
        return NULL;
    }

    // Acquire read lock - allows multiple threads to read simultaneously
    mcp_rwlock_read_lock(manager->config_lock);

    // Get backend list
    const mcp_backend_info_t* backends = manager->backend_list;
    *backend_count = manager->backend_count;

    // Release read lock
    mcp_rwlock_read_unlock(manager->config_lock);

    return backends;
}

/**
 * @brief Reloads the configuration from the file
 */
mcp_error_code_t gateway_config_manager_reload(gateway_config_manager_t* manager) {
    if (!manager) {
        return MCP_ERROR_INVALID_PARAMS;
    }

    mcp_backend_info_t* new_backend_list = NULL;
    size_t new_backend_count = 0;
    mcp_error_code_t err;

    // Load new configuration
    err = load_gateway_config(manager->config_path, &new_backend_list, &new_backend_count);
    if (err != MCP_ERROR_NONE) {
        mcp_log_error("Failed to load gateway configuration: %d", err);
        return err;
    }

    // Acquire write lock - ensures exclusive access during update
    mcp_rwlock_write_lock(manager->config_lock);

    // Free old backend list
    if (manager->backend_list) {
        mcp_free_backend_list(manager->backend_list, manager->backend_count);
    }

    // Update with new configuration
    manager->backend_list = new_backend_list;
    manager->backend_count = new_backend_count;

    // Release write lock
    mcp_rwlock_write_unlock(manager->config_lock);

    mcp_log_info("Gateway configuration reloaded with %zu backends", new_backend_count);
    return MCP_ERROR_NONE;
}

/**
 * @brief Finds a backend for a given request
 */
const mcp_backend_info_t* gateway_config_manager_find_backend(
    gateway_config_manager_t* manager,
    const mcp_request_t* request
) {
    if (!manager || !request) {
        return NULL;
    }

    // Acquire read lock - allows multiple threads to find backends simultaneously
    mcp_rwlock_read_lock(manager->config_lock);

    // Find backend
    const mcp_backend_info_t* backend = find_backend_for_request(
        request,
        manager->backend_list,
        manager->backend_count
    );

    // Release read lock
    mcp_rwlock_read_unlock(manager->config_lock);

    return backend;
}
