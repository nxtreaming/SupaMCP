/**
 * @file gateway_config_manager.c
 * @brief Implementation of thread-safe gateway configuration manager using read-write locks
 */

#include "mcp_gateway_config_manager.h"
#include "mcp_gateway.h"
#include "mcp_gateway_routing.h"
#include "mcp_log.h"
#include "mcp_rwlock.h"
#include "mcp_hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Structure for method-to-backend cache entry
 */
typedef struct {
    char* method;                      /**< Method name (key) */
    const mcp_backend_info_t* backend; /**< Pointer to backend (not owned) */
} method_backend_cache_entry_t;

/**
 * @brief Internal structure for the gateway configuration manager
 */
struct gateway_config_manager {
    char* config_path;                 /**< Path to the configuration file */
    mcp_rwlock_t* config_lock;         /**< Read-write lock for thread-safe access */
    mcp_backend_info_t* backend_list;  /**< Array of backend configurations */
    size_t backend_count;              /**< Number of backends in the list */
    mcp_hashtable_t* method_cache;     /**< Cache for method-to-backend mapping */
};

// We use the standard string hash and compare functions from mcp_hashtable.h

/**
 * @brief Free function for cache entries
 *
 * This function matches the mcp_value_free_func_t signature
 */
static void cache_entry_free(void* entry) {
    if (entry) {
        method_backend_cache_entry_t* cache_entry = (method_backend_cache_entry_t*)entry;
        // Note: Don't free method string as it's owned by the hashtable key management
        // Note: Don't free backend as it's owned by the config
        free(cache_entry);
    }
}

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
    manager->method_cache = mcp_hashtable_create(64, 0.75f,
                                                 mcp_hashtable_string_hash,
                                                 mcp_hashtable_string_compare,
                                                 mcp_hashtable_string_dup,
                                                 mcp_hashtable_string_free,
                                                 cache_entry_free);

    if (!manager->config_path || !manager->config_lock || !manager->method_cache) {
        mcp_log_error("Failed to initialize gateway config manager");
        if (manager->config_path) free(manager->config_path);
        mcp_rwlock_free(manager->config_lock);
        if (manager->method_cache) mcp_hashtable_destroy(manager->method_cache);
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
    if (manager->method_cache) mcp_hashtable_destroy(manager->method_cache);
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

    // Clear the method cache since backend list has changed
    if (manager->method_cache) {
        mcp_hashtable_clear(manager->method_cache);
        mcp_log_debug("Method-to-backend cache cleared due to configuration reload");
    }

    // Release write lock
    mcp_rwlock_write_unlock(manager->config_lock);

    mcp_log_info("Gateway configuration reloaded with %zu backends", new_backend_count);
    return MCP_ERROR_NONE;
}

/**
 * @brief Finds a backend for a given request
 *
 * Optimized implementation using method-to-backend cache for faster lookups
 */
const mcp_backend_info_t* gateway_config_manager_find_backend(
    gateway_config_manager_t* manager,
    const mcp_request_t* request
) {
    if (!manager || !request || !request->method) {
        return NULL;
    }

    const mcp_backend_info_t* backend = NULL;
    method_backend_cache_entry_t* cache_entry = NULL;

    // First check the cache (without locking)
    if (manager->method_cache) {
        void* entry_ptr = NULL;
        if (mcp_hashtable_get(manager->method_cache, request->method, &entry_ptr) == 0 && entry_ptr) {
            cache_entry = (method_backend_cache_entry_t*)entry_ptr;
            backend = cache_entry->backend;

            // If we found a valid backend in the cache, return it immediately
            if (backend) {
                mcp_log_debug("Cache hit for method '%s' -> backend '%s'",
                             request->method, backend->name);
                return backend;
            }
        }
    }

    // Cache miss, acquire read lock
    mcp_rwlock_read_lock(manager->config_lock);

    // Find backend using the routing logic
    backend = find_backend_for_request(
        request,
        manager->backend_list,
        manager->backend_count
    );

    // Release read lock before updating cache
    mcp_rwlock_read_unlock(manager->config_lock);

    // Update the cache with the result (even if NULL, to cache negative results)
    if (manager->method_cache && request->method) {
        // Create a new cache entry
        cache_entry = (method_backend_cache_entry_t*)malloc(sizeof(method_backend_cache_entry_t));
        if (cache_entry) {
            cache_entry->method = mcp_strdup(request->method);
            cache_entry->backend = backend;

            if (cache_entry->method) {
                // Add to cache (will replace any existing entry)
                mcp_hashtable_put(manager->method_cache, cache_entry->method, cache_entry);
                mcp_log_debug("Added cache entry for method '%s'", request->method);
            } else {
                // Failed to duplicate method string
                free(cache_entry);
            }
        }
    }

    return backend;
}
