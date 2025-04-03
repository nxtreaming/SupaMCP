#include "gateway_pool.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include "mcp_client.h"
#include "mcp_transport_factory.h"
#include <stdlib.h>
#include <string.h>

// Internal structure for the manager
struct gateway_connection_pool_manager {
    mcp_hashtable_t* backend_pools; // Map: backend_address (char*) -> specific_pool_handle (void*)
    // TODO: Add mutex for thread safety if accessed by multiple threads
};

// TODO: Define structure for a single backend's pool (e.g., list of mcp_client_t)
typedef struct {
    // Placeholder - needs actual pool implementation
    char* backend_address;
    // list/queue of mcp_client_t* connections
    // mutex for pool access
} backend_pool_t;

// Hash function for backend addresses (simple string hash) - Correct return type
static unsigned long address_hash_func(const void* key) {
    // Use the function declared in mcp_hashtable.h
    return mcp_hashtable_string_hash((const char*)key);
}

// Key comparison function for backend addresses - Correct return type
static bool address_key_compare(const void* key1, const void* key2) {
    return strcmp((const char*)key1, (const char*)key2) == 0;
}

// Value free function for the hashtable (frees the backend_pool_t)
static void backend_pool_free_func(void* value) {
    backend_pool_t* pool = (backend_pool_t*)value;
    if (pool) {
        log_message(LOG_LEVEL_DEBUG, "Destroying pool for backend: %s", pool->backend_address);
        // TODO: Implement actual pool destruction (close connections, free list)
        free(pool->backend_address);
        free(pool);
    }
}

// Implementation of gateway_pool_manager_create
gateway_pool_manager_t* gateway_pool_manager_create(void) {
    gateway_pool_manager_t* manager = (gateway_pool_manager_t*)malloc(sizeof(gateway_pool_manager_t));
    if (!manager) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate gateway pool manager.");
        return NULL;
    }

    // Create hashtable: Key=backend_address (char*), Value=backend_pool_t*
    // Use default capacity (0), default load factor (0.75f), provide all functions.
    // Key is duplicated using mcp_strdup, freed using free. Value is freed using backend_pool_free_func.
    manager->backend_pools = mcp_hashtable_create(
        0,      // initial_capacity (0 uses default)
        0.75f,  // load_factor_threshold
        address_hash_func,
        address_key_compare,
        mcp_strdup, // key_dup function (mcp_strdup defined in mcp_types.h/c)
        free,       // key_free function
        backend_pool_free_func // value_free function
    );
    if (!manager->backend_pools) {
        log_message(LOG_LEVEL_ERROR, "Failed to create backend pool hashtable.");
        free(manager);
        return NULL;
    }

    log_message(LOG_LEVEL_INFO, "Gateway connection pool manager created.");
    return manager;
}

// Implementation of gateway_pool_manager_destroy
void gateway_pool_manager_destroy(gateway_pool_manager_t* manager) {
    if (!manager) return;

    log_message(LOG_LEVEL_INFO, "Destroying gateway connection pool manager...");
    // Hashtable destroy will call backend_pool_free_func for each entry
    mcp_hashtable_destroy(manager->backend_pools);
    free(manager);
    log_message(LOG_LEVEL_INFO, "Gateway connection pool manager destroyed.");
}

// Implementation of gateway_pool_get_connection (Placeholder)
void* gateway_pool_get_connection(gateway_pool_manager_t* manager, const mcp_backend_info_t* backend_info) {
    if (!manager || !backend_info || !backend_info->address) {
        return NULL;
    }

    // TODO: Lock manager mutex

    backend_pool_t* pool = NULL;
    // Correctly call mcp_hashtable_get with the output parameter
    if (mcp_hashtable_get(manager->backend_pools, backend_info->address, (void**)&pool) != 0) {
        // Pool doesn't exist, create it
        pool = NULL; // Ensure pool is NULL if get failed
        log_message(LOG_LEVEL_INFO, "Creating new connection pool for backend: %s (%s)", backend_info->name, backend_info->address);
        pool = (backend_pool_t*)malloc(sizeof(backend_pool_t));
        if (!pool) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate pool structure for backend: %s", backend_info->name);
             // TODO: Unlock mutex
             return NULL;
        }
        memset(pool, 0, sizeof(backend_pool_t));
        pool->backend_address = mcp_strdup(backend_info->address);
        if (!pool->backend_address) {
             log_message(LOG_LEVEL_ERROR, "Failed to duplicate backend address for pool: %s", backend_info->name);
             free(pool);
             // TODO: Unlock mutex
             return NULL;
        }

        // TODO: Initialize actual connection list/queue and mutex within backend_pool_t

        // Add the new pool to the manager's hashtable
        // The key is duplicated by the hashtable itself using the provided key_dup function (mcp_strdup)
        if (mcp_hashtable_put(manager->backend_pools, backend_info->address, pool) != 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to add new pool to manager hashtable for backend: %s", backend_info->name);
            // Don't free key_copy, hashtable owns it on failure too? Check hashtable impl. Assuming it does.
            backend_pool_free_func(pool); // Use the designated free function to clean up the value
            pool = NULL; // Ensure pool is NULL on failure
             // TODO: Unlock mutex
            return NULL;
        }
        // Hashtable now owns the duplicated key and the pool pointer
    }

    // If pool is still NULL here, something went wrong during creation/insertion
    if (!pool) {
         log_message(LOG_LEVEL_ERROR, "Pool is unexpectedly NULL after get/create attempt for backend: %s", backend_info->name);
         // TODO: Unlock mutex
         return NULL;
    }

    // TODO: Implement logic to get an available connection from the 'pool' structure
    // - Check for existing idle connection
    // - If none, create a new connection (up to a limit) using mcp_transport_factory_create and mcp_client_create
    // - This part might block or need to be async

    log_message(LOG_LEVEL_WARN, "Gateway connection pooling not fully implemented. Cannot get connection for backend: %s", backend_info->name);

    // TODO: Unlock mutex
    return NULL; // Placeholder
}

// Implementation of gateway_pool_release_connection (Placeholder)
void gateway_pool_release_connection(gateway_pool_manager_t* manager, const mcp_backend_info_t* backend_info, void* connection_handle) {
     if (!manager || !backend_info || !backend_info->address || !connection_handle) {
        return;
    }

    // TODO: Lock manager mutex (or pool-specific mutex)

    backend_pool_t* pool = NULL;
    // Correctly call mcp_hashtable_get
    if (mcp_hashtable_get(manager->backend_pools, backend_info->address, (void**)&pool) == 0 && pool != NULL) {
        // TODO: Implement logic to return the connection_handle (e.g., mcp_client_t*)
        // back to the 'pool' structure's list/queue of available connections.
        log_message(LOG_LEVEL_DEBUG, "Releasing connection for backend: %s", backend_info->name);
    } else {
         log_message(LOG_LEVEL_WARN, "Attempted to release connection for unknown backend pool: %s", backend_info->address);
         // If pool doesn't exist, maybe just destroy the connection?
         // mcp_client_destroy((mcp_client_t*)connection_handle); // Example
    }

     // TODO: Unlock mutex
}
