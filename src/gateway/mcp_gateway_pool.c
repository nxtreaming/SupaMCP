#include "mcp_gateway_pool.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include "mcp_client.h"
#include "mcp_transport_factory.h"
#include "mcp_sync.h"
#include "mcp_tcp_client_transport.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

// Forward declare helper if needed (assuming it's in utils or similar)
extern long long get_current_time_ms(void);

// Default pool settings (can be made configurable later)
#define DEFAULT_MIN_CONNECTIONS 1
#define DEFAULT_MAX_CONNECTIONS 5
#define DEFAULT_CONNECT_TIMEOUT_MS 5000 // 5 seconds
#define DEFAULT_IDLE_TIMEOUT_MS 60000 // 60 seconds
#define DEFAULT_GET_TIMEOUT_MS -1 // Default: wait indefinitely for get_connection

// Structure to represent an idle connection node in the list
typedef struct idle_connection_node {
    mcp_client_t* client;
    time_t idle_since; // Timestamp when it became idle
    struct idle_connection_node* next;
} idle_connection_node_t;

// Structure for a single backend's connection pool
typedef struct {
    char* backend_address;          // Key (e.g., "host:port")
    // Store pool-specific configuration directly
    size_t min_connections;
    size_t max_connections;
    int connect_timeout_ms;
    int idle_timeout_ms;            // Timeout for idle connections in the pool

    mcp_mutex_t* pool_lock;         // Mutex for this specific pool
    mcp_cond_t* pool_cond;          // Condition variable for waiting getters

    idle_connection_node_t* idle_list; // Linked list of idle client connections
    size_t idle_count;              // Number of idle connections
    size_t active_count;            // Number of connections currently in use
    size_t total_count;             // Total connections created for this backend

    // TODO: Add fields for maintenance thread if needed
} backend_pool_t;

// Internal structure for the manager
struct gateway_connection_pool_manager {
    mcp_hashtable_t* backend_pools; // Map: backend_address (char*) -> backend_pool_t*
    mcp_mutex_t* manager_lock;      // Mutex for thread-safe access to the hashtable
};

// Hash function for backend addresses (simple string hash)
static unsigned long address_hash_func(const void* key) {
    return mcp_hashtable_string_hash((const char*)key);
}

// Key comparison function for backend addresses
static bool address_key_compare(const void* key1, const void* key2) {
    return strcmp((const char*)key1, (const char*)key2) == 0;
}

// Value free function for the hashtable (frees the backend_pool_t)
static void backend_pool_free_func(void* value) {
    backend_pool_t* pool = (backend_pool_t*)value;
    if (pool) {
        mcp_log_debug("Destroying pool for backend: %s", pool->backend_address);

        // Destroy sync primitives first
        mcp_mutex_destroy(pool->pool_lock);
        mcp_cond_destroy(pool->pool_cond);

        // Close and destroy all idle client connections
        idle_connection_node_t* current = pool->idle_list;
        while (current) {
            idle_connection_node_t* next = current->next;
            if (current->client) {
                mcp_client_destroy(current->client); // Assumes mcp_client_destroy handles transport etc.
            }
            free(current);
            current = next;
        }

        // Free the address string and the pool struct itself
        free(pool->backend_address);
        free(pool);
    }
}

// Implementation of gateway_pool_manager_create
gateway_pool_manager_t* gateway_pool_manager_create(void) {
    gateway_pool_manager_t* manager = (gateway_pool_manager_t*)malloc(sizeof(gateway_pool_manager_t));
    if (!manager) {
        mcp_log_error("Failed to allocate gateway pool manager.");
        return NULL;
    }

    manager->manager_lock = mcp_mutex_create();
    if (!manager->manager_lock) {
        mcp_log_error("Failed to create gateway manager mutex.");
        free(manager);
        return NULL;
    }

    // Create hashtable: Key=backend_address (char*), Value=backend_pool_t*
    manager->backend_pools = mcp_hashtable_create(
        0,      // initial_capacity (0 uses default)
        0.75f,  // load_factor_threshold
        address_hash_func,
        address_key_compare,
        mcp_hashtable_string_dup, // key_dup function
        free,       // key_free function
        backend_pool_free_func // value_free function
    );
    if (!manager->backend_pools) {
        mcp_log_error("Failed to create backend pool hashtable.");
        mcp_mutex_destroy(manager->manager_lock);
        free(manager);
        return NULL;
    }

    mcp_log_info("Gateway connection pool manager created.");
    return manager;
}

// Implementation of gateway_pool_manager_destroy
void gateway_pool_manager_destroy(gateway_pool_manager_t* manager) {
    if (!manager) return;

    mcp_log_info("Destroying gateway connection pool manager...");
    // Lock manager before destroying hashtable might be safer depending on usage
    mcp_mutex_lock(manager->manager_lock);
    // Hashtable destroy will call backend_pool_free_func for each entry
    mcp_hashtable_destroy(manager->backend_pools);
    manager->backend_pools = NULL; // Avoid double free if called again
    mcp_mutex_unlock(manager->manager_lock);

    // Destroy manager mutex
    mcp_mutex_destroy(manager->manager_lock);
    manager->manager_lock = NULL;

    free(manager);
    mcp_log_info("Gateway connection pool manager destroyed.");
}

// Helper to create a new backend pool structure
static backend_pool_t* create_backend_pool(const mcp_backend_info_t* backend_info) {
    backend_pool_t* pool = (backend_pool_t*)calloc(1, sizeof(backend_pool_t)); // Use calloc
    if (!pool) {
        mcp_log_error("Failed to allocate pool structure for backend: %s", backend_info->name);
        return NULL;
    }

    pool->backend_address = mcp_strdup(backend_info->address);
    if (!pool->backend_address) {
        mcp_log_error("Failed to duplicate backend address for pool: %s", backend_info->name);
        free(pool);
        return NULL;
    }

    // Set pool configuration (using defaults or values from backend_info if available)
    // Use backend_info->timeout_ms for connect timeout, define separate idle timeout
    pool->connect_timeout_ms = (backend_info->timeout_ms > 0) ? (int)backend_info->timeout_ms : DEFAULT_CONNECT_TIMEOUT_MS;
    pool->idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS; // Use default for now
    pool->min_connections = DEFAULT_MIN_CONNECTIONS; // Use default for now
    pool->max_connections = DEFAULT_MAX_CONNECTIONS; // Use default for now
    // TODO: Allow overriding defaults via backend_info if fields are added later

    // Initialize sync primitives for this specific pool
    pool->pool_lock = mcp_mutex_create();
    pool->pool_cond = mcp_cond_create();

    if (!pool->pool_lock || !pool->pool_cond) {
        mcp_log_error("Failed to initialize sync primitives for backend pool: %s", backend_info->name);
        mcp_mutex_destroy(pool->pool_lock);
        mcp_cond_destroy(pool->pool_cond);
        free(pool->backend_address);
        free(pool);
        return NULL;
    }

    pool->idle_list = NULL;
    pool->idle_count = 0;
    pool->active_count = 0;
    pool->total_count = 0;

    // TODO: Implement pre-population with min_connections here or in a separate thread

    mcp_log_info("Created new connection pool for backend: %s (%s) [Min:%zu, Max:%zu, ConnectT:%dms, IdleT:%dms]",
                 backend_info->name, pool->backend_address,
                 pool->min_connections, pool->max_connections,
                 pool->connect_timeout_ms, pool->idle_timeout_ms);
    return pool;
}

// Implementation of gateway_pool_get_connection
// Adding timeout parameter
void* gateway_pool_get_connection(gateway_pool_manager_t* manager, const mcp_backend_info_t* backend_info /*, int timeout_ms = DEFAULT_GET_TIMEOUT_MS */) {
    // Use a default timeout for now, can be passed later
    int timeout_ms = DEFAULT_GET_TIMEOUT_MS;

    if (!manager || !backend_info || !backend_info->address) {
        mcp_log_error("gateway_pool_get_connection: Invalid arguments.");
        return NULL;
    }

    backend_pool_t* pool = NULL;
    mcp_client_t* client_connection = NULL;
    idle_connection_node_t* idle_node = NULL;
    time_t now;
    long long start_time_ms = 0;
    bool use_timeout = (timeout_ms >= 0); // Use timeout if >= 0 (-1 means wait indefinitely)

    if (use_timeout) {
        start_time_ms = get_current_time_ms(); // Assuming get_current_time_ms() exists
    }

    // Lock the manager mutex to safely access the hashtable
    mcp_mutex_lock(manager->manager_lock);

    // Try to find the existing pool for this backend
    if (mcp_hashtable_get(manager->backend_pools, backend_info->address, (void**)&pool) != 0) {
        // Pool doesn't exist, create it
        pool = create_backend_pool(backend_info);
        if (!pool) {
            mcp_mutex_unlock(manager->manager_lock);
            return NULL; // Failed to create pool
        }
        // Add the new pool to the manager's hashtable
        if (mcp_hashtable_put(manager->backend_pools, pool->backend_address, pool) != 0) {
            mcp_log_error("Failed to add new pool to manager hashtable for backend: %s", backend_info->name);
            backend_pool_free_func(pool); // Clean up the newly created pool
            pool = NULL;
            mcp_mutex_unlock(manager->manager_lock);
            return NULL;
        }
    }
    mcp_mutex_unlock(manager->manager_lock);

    if (!pool) {
        mcp_log_error("Pool is unexpectedly NULL after get/create attempt for backend: %s", backend_info->name);
        return NULL;
    }

    mcp_mutex_lock(pool->pool_lock);

    // --- Start Connection Retrieval Logic ---
    while (client_connection == NULL) {
        now = time(NULL); // Get current time for timeout checks

        // 1. Check idle list & handle idle timeouts
        idle_connection_node_t* prev_idle_node = NULL;
        idle_node = pool->idle_list;
        while (idle_node) {
            bool timed_out = (pool->idle_timeout_ms > 0) &&
                             (difftime(now, idle_node->idle_since) * 1000.0 > pool->idle_timeout_ms);

            if (timed_out) {
                mcp_log_info("Idle connection timed out for %s. Closing.", pool->backend_address);
                if (prev_idle_node) {
                    prev_idle_node->next = idle_node->next;
                } else {
                    pool->idle_list = idle_node->next;
                }
                pool->idle_count--;
                pool->total_count--;
                mcp_client_t* client_to_destroy = idle_node->client;
                idle_connection_node_t* node_to_free = idle_node;
                idle_node = idle_node->next;
                mcp_mutex_unlock(pool->pool_lock); // Unlock before destroy
                mcp_client_destroy(client_to_destroy);
                free(node_to_free);
                mcp_mutex_lock(pool->pool_lock); // Relock
            } else {
                if (prev_idle_node) {
                    prev_idle_node->next = idle_node->next;
                } else {
                    pool->idle_list = idle_node->next;
                }
                pool->idle_count--;
                pool->active_count++;
                client_connection = idle_node->client;
                free(idle_node);
                mcp_log_debug("Reusing idle connection for %s", pool->backend_address);
                goto found_connection;
            }
        } // End while(idle_node)

        // 2. Check if we can create a new connection
        if (pool->total_count < pool->max_connections) {
            size_t current_total = pool->total_count;
            pool->total_count++;
            mcp_mutex_unlock(pool->pool_lock);

            mcp_log_debug("Creating new connection (%zu/%zu) for %s", current_total + 1, pool->max_connections, pool->backend_address);
            char* host_part = mcp_strdup(pool->backend_address);
            uint16_t port_part = 0;
            char* colon = NULL;
            if (host_part) {
                colon = strchr(host_part, ':');
                if (colon) {
                    *colon = '\0';
                    if (*(colon + 1) != '\0')
                        port_part = (uint16_t)atoi(colon + 1);
                    else {
                        mcp_log_error("Invalid port format...");
                        port_part = 0;
                    }
                } else {
                    mcp_log_error("Port missing..."); 
                    port_part = 0;
                }
            } else {
                mcp_log_error("Failed to duplicate address..."); 
                /* Handle error */
            }

            mcp_transport_t* transport = NULL;
            if (port_part > 0 && host_part && *host_part != '\0') {
                // Use the function from the included header
                transport = mcp_transport_tcp_client_create(host_part, port_part);
            } else {
                mcp_log_error("Invalid host or port...");
            }
            free(host_part);

            if (!transport) {
                mcp_log_error("Failed to create transport for %s", pool->backend_address);
                mcp_mutex_lock(pool->pool_lock);
                pool->total_count--;
                mcp_mutex_unlock(pool->pool_lock);
                goto found_connection; // Exit loop, client_connection is NULL
            }

            mcp_client_config_t client_config = { .request_timeout_ms = pool->connect_timeout_ms };
            mcp_client_t* new_client = mcp_client_create(&client_config, transport);

            if (!new_client) {
                mcp_log_error("Failed to create client for %s", pool->backend_address);
                mcp_mutex_lock(pool->pool_lock);
                pool->total_count--;
                mcp_mutex_unlock(pool->pool_lock);
                goto found_connection; // Exit loop, client_connection is NULL
            }

            mcp_mutex_lock(pool->pool_lock);
            if (pool->pool_lock == NULL) {
                mcp_mutex_unlock(pool->pool_lock);
                mcp_log_warn("Pool destroyed while creating connection...");
                mcp_client_destroy(new_client);
                return NULL;
            }
            pool->active_count++;
            client_connection = new_client;
            mcp_log_debug("Created and started new client connection for %s", pool->backend_address);
            goto found_connection;
        }

        // 3. Pool is full, wait for a connection to be released
        mcp_log_debug("Pool for %s is full (%zu/%zu), waiting...", pool->backend_address, pool->active_count + pool->idle_count, pool->max_connections);

        int wait_result;
        int remaining_timeout_ms = timeout_ms; // Initialize with original timeout

        if (use_timeout) {
            long long elapsed_ms = get_current_time_ms() - start_time_ms;
            remaining_timeout_ms = timeout_ms - (int)elapsed_ms;
            if (remaining_timeout_ms <= 0) {
                mcp_log_warn("Timeout expired before waiting for connection to %s", pool->backend_address);
                goto found_connection; // Exit loop, client_connection is NULL
            }
        }

        if (remaining_timeout_ms < 0) { // Wait indefinitely
            wait_result = mcp_cond_wait(pool->pool_cond, pool->pool_lock);
        } else { // Wait with timeout
            wait_result = mcp_cond_timedwait(pool->pool_cond, pool->pool_lock, (uint32_t)remaining_timeout_ms);
        }

        if (wait_result == ETIMEDOUT) {
            mcp_log_warn("Timed out waiting for connection to %s", pool->backend_address);
            goto found_connection; // Exit loop, client_connection is NULL
        } else if (wait_result != 0) {
            mcp_log_error("Failed waiting for connection pool condition for %s (err: %d)", pool->backend_address, wait_result);
            goto found_connection; // Exit loop on wait error, client_connection is NULL
        }
        // Loop continues after being signaled or spurious wakeup to re-check conditions
    }
    // --- End Connection Retrieval Logic ---

found_connection:
    mcp_mutex_unlock(pool->pool_lock);
    return client_connection; // Return the mcp_client_t* or NULL
}

// Implementation of gateway_pool_release_connection
void gateway_pool_release_connection(gateway_pool_manager_t* manager, const mcp_backend_info_t* backend_info, void* connection_handle) {
    if (!manager || !backend_info || !backend_info->address || !connection_handle) {
        mcp_log_error("gateway_pool_release_connection: Invalid arguments.");
        return;
    }

    mcp_client_t* client = (mcp_client_t*)connection_handle;
    backend_pool_t* pool = NULL;

    // Lock manager to safely access hashtable
    mcp_mutex_lock(manager->manager_lock);
    // Find the pool for this backend
    if (mcp_hashtable_get(manager->backend_pools, backend_info->address, (void**)&pool) != 0 || pool == NULL) {
        mcp_log_warn("Attempted to release connection for unknown or missing backend pool: %s. Destroying connection.", backend_info->address);
        mcp_mutex_unlock(manager->manager_lock);
        mcp_client_destroy(client); // Destroy the orphaned connection
        return;
    }
    // Unlock manager mutex after getting the pool pointer
    mcp_mutex_unlock(manager->manager_lock);

    // Lock the specific pool's mutex
    mcp_mutex_lock(pool->pool_lock);

    // TODO: Add connection validity check if needed (e.g., ping before adding to idle)

    // Add connection back to idle list
    idle_connection_node_t* node = (idle_connection_node_t*)malloc(sizeof(idle_connection_node_t));
    if (node) {
        node->client = client;
        node->idle_since = time(NULL);
        node->next = pool->idle_list;
        pool->idle_list = node;
        pool->idle_count++;
        if (pool->active_count > 0) pool->active_count--; // Decrement active count
        mcp_log_debug("Returned connection to idle pool for backend: %s", pool->backend_address);

        // Signal a waiting getter
        mcp_cond_signal(pool->pool_cond);
    } else {
        mcp_log_error("Failed to allocate node for idle connection for backend %s. Destroying connection.", pool->backend_address);
        if (pool->active_count > 0) pool->active_count--;
        // Decrement total_count as the connection is being destroyed
        if (pool->total_count > 0) pool->total_count--;
        mcp_client_destroy(client);
        // Optionally signal waiters even if adding failed, as total count decreased (a slot might open up)
        mcp_cond_signal(pool->pool_cond);
    }

    mcp_mutex_unlock(pool->pool_lock);
}
