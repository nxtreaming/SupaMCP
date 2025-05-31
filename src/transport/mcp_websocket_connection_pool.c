#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_websocket_connection_pool.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Forward declarations
static void* health_check_thread_func(void* arg);
static mcp_transport_t* create_connection(mcp_ws_connection_pool_t* pool);
static bool is_connection_healthy(mcp_transport_t* transport);

// Create a WebSocket connection pool
mcp_ws_connection_pool_t* mcp_ws_connection_pool_create(const mcp_ws_pool_config_t* config) {
    if (!config || config->min_connections == 0 || config->max_connections == 0 ||
        config->min_connections > config->max_connections) {
        mcp_log_error("Invalid connection pool configuration");
        return NULL;
    }

    // Allocate pool structure
    mcp_ws_connection_pool_t* pool = (mcp_ws_connection_pool_t*)calloc(1, sizeof(mcp_ws_connection_pool_t));
    if (!pool) {
        mcp_log_error("Failed to allocate connection pool");
        return NULL;
    }

    // Copy configuration
    pool->config = *config;

    // Allocate connection array
    pool->connections = (mcp_ws_conn_entry_t*)calloc(config->max_connections, sizeof(mcp_ws_conn_entry_t));
    if (!pool->connections) {
        mcp_log_error("Failed to allocate connection array");
        free(pool);
        return NULL;
    }

    // Initialize mutex and condition variable
    pool->pool_mutex = mcp_mutex_create();
    if (!pool->pool_mutex) {
        mcp_log_error("Failed to create pool mutex");
        free(pool->connections);
        free(pool);
        return NULL;
    }

    pool->pool_cond = mcp_cond_create();
    if (!pool->pool_cond) {
        mcp_log_error("Failed to create pool condition variable");
        mcp_mutex_destroy(pool->pool_mutex);
        free(pool->connections);
        free(pool);
        return NULL;
    }

    // Initialize pool state
    pool->total_connections = 0;
    pool->available_connections = 0;
    pool->running = true;
    pool->next_conn_id = 1;

    // Pre-create minimum number of connections
    mcp_log_info("Initializing connection pool with %u connections", config->min_connections);
    for (uint32_t i = 0; i < config->min_connections; i++) {
        mcp_transport_t* transport = create_connection(pool);
        if (transport) {
            // Add to pool
            pool->connections[i].transport = transport;
            pool->connections[i].state = WS_CONN_STATE_IDLE;
            pool->connections[i].last_used = time(NULL);
            pool->connections[i].id = pool->next_conn_id++;
            pool->connections[i].is_healthy = true;
            pool->total_connections++;
            pool->available_connections++;
        } else {
            mcp_log_warn("Failed to create initial connection %u", i);
        }
    }

    // Start health check thread
    if (mcp_thread_create(&pool->health_check_thread, health_check_thread_func, pool) != 0) {
        mcp_log_error("Failed to create health check thread");
        // Continue without health check thread
    }

    mcp_log_info("WebSocket connection pool created with %u/%u connections",
                pool->available_connections, pool->total_connections);
    return pool;
}

// Destroy a WebSocket connection pool
void mcp_ws_connection_pool_destroy(mcp_ws_connection_pool_t* pool) {
    if (!pool) {
        return;
    }

    // Stop health check thread
    pool->running = false;
    if (pool->health_check_thread) {
        mcp_thread_join(pool->health_check_thread, NULL);
    }

    // Destroy all connections
    for (uint32_t i = 0; i < pool->total_connections; i++) {
        if (pool->connections[i].transport) {
            mcp_transport_destroy(pool->connections[i].transport);
            pool->connections[i].transport = NULL;
        }
    }

    // Free resources
    mcp_mutex_destroy(pool->pool_mutex);
    mcp_cond_destroy(pool->pool_cond);
    free(pool->connections);
    free(pool);

    mcp_log_info("WebSocket connection pool destroyed");
}

// Get a connection from the pool
mcp_transport_t* mcp_ws_connection_pool_get(mcp_ws_connection_pool_t* pool, uint32_t timeout_ms) {
    if (!pool) {
        return NULL;
    }

    mcp_transport_t* transport = NULL;
    time_t start_time = time(NULL);

    mcp_mutex_lock(pool->pool_mutex);

    // Try to find an available connection
    while (pool->running && !transport) {
        // First, look for an idle connection
        for (uint32_t i = 0; i < pool->total_connections; i++) {
            if (pool->connections[i].state == WS_CONN_STATE_IDLE && 
                pool->connections[i].transport && 
                pool->connections[i].is_healthy) {
                
                // Found an idle connection
                transport = pool->connections[i].transport;
                pool->connections[i].state = WS_CONN_STATE_IN_USE;
                pool->connections[i].last_used = time(NULL);
                pool->available_connections--;
                
                mcp_log_debug("Got connection %u from pool (%u/%u available)",
                             pool->connections[i].id,
                             pool->available_connections,
                             pool->total_connections);
                break;
            }
        }

        // If no idle connection found, try to create a new one if below max
        if (!transport && pool->total_connections < pool->config.max_connections) {
            mcp_log_debug("No idle connection available, creating new connection");
            
            // Temporarily unlock mutex while creating connection
            mcp_mutex_unlock(pool->pool_mutex);
            transport = create_connection(pool);
            mcp_mutex_lock(pool->pool_mutex);
            
            if (transport) {
                // Add to pool
                uint32_t idx = pool->total_connections;
                pool->connections[idx].transport = transport;
                pool->connections[idx].state = WS_CONN_STATE_IN_USE;
                pool->connections[idx].last_used = time(NULL);
                pool->connections[idx].id = pool->next_conn_id++;
                pool->connections[idx].is_healthy = true;
                pool->total_connections++;
                
                mcp_log_debug("Created new connection %u (%u/%u total)",
                             pool->connections[idx].id,
                             pool->total_connections,
                             pool->config.max_connections);
            } else {
                mcp_log_error("Failed to create new connection");
            }
        }

        // If still no connection, wait for one to become available
        if (!transport) {
            if (timeout_ms > 0) {
                // Check if we've exceeded the timeout
                time_t now = time(NULL);
                double elapsed = difftime(now, start_time) * 1000;
                if (elapsed >= timeout_ms) {
                    mcp_log_warn("Timeout waiting for connection from pool");
                    break;
                }

                // Calculate remaining timeout
                uint32_t remaining_ms = (uint32_t)(timeout_ms - elapsed);
                uint32_t wait_ms = remaining_ms < 100 ? remaining_ms : 100;
                
                mcp_log_debug("Waiting for connection to become available (%u ms remaining)",
                             remaining_ms);
                
                // Wait with timeout
                mcp_cond_timedwait(pool->pool_cond, pool->pool_mutex, wait_ms);
            } else {
                // Wait indefinitely with a short timeout to allow periodic checks
                mcp_cond_timedwait(pool->pool_cond, pool->pool_mutex, 100);
            }
        }
    }

    mcp_mutex_unlock(pool->pool_mutex);
    return transport;
}

// Release a connection back to the pool
int mcp_ws_connection_pool_release(mcp_ws_connection_pool_t* pool, mcp_transport_t* transport) {
    if (!pool || !transport) {
        return -1;
    }

    mcp_mutex_lock(pool->pool_mutex);

    // Find the connection in the pool
    for (uint32_t i = 0; i < pool->total_connections; i++) {
        if (pool->connections[i].transport == transport) {
            // Check if the connection is healthy
            bool is_healthy = is_connection_healthy(transport);
            
            if (is_healthy) {
                // Return to idle state
                pool->connections[i].state = WS_CONN_STATE_IDLE;
                pool->connections[i].last_used = time(NULL);
                pool->connections[i].is_healthy = true;
                pool->available_connections++;
                
                mcp_log_debug("Released connection %u back to pool (%u/%u available)",
                             pool->connections[i].id,
                             pool->available_connections,
                             pool->total_connections);
            } else {
                // Connection is unhealthy, mark as invalid
                pool->connections[i].state = WS_CONN_STATE_INVALID;
                pool->connections[i].is_healthy = false;
                
                mcp_log_warn("Connection %u is unhealthy, marking as invalid",
                            pool->connections[i].id);
            }
            
            // Signal waiting threads
            mcp_cond_broadcast(pool->pool_cond);
            
            mcp_mutex_unlock(pool->pool_mutex);
            return 0;
        }
    }

    // Connection not found in pool
    mcp_log_warn("Attempted to release a connection not in the pool");
    mcp_mutex_unlock(pool->pool_mutex);
    return -1;
}

// Get statistics from the connection pool
int mcp_ws_connection_pool_get_stats(
    mcp_ws_connection_pool_t* pool,
    uint32_t* total_connections,
    uint32_t* available_connections,
    uint32_t* in_use_connections,
    uint32_t* connecting_connections,
    uint32_t* invalid_connections
) {
    if (!pool) {
        return -1;
    }

    mcp_mutex_lock(pool->pool_mutex);

    // Count connections by state
    uint32_t in_use = 0;
    uint32_t connecting = 0;
    uint32_t invalid = 0;

    for (uint32_t i = 0; i < pool->total_connections; i++) {
        switch (pool->connections[i].state) {
            case WS_CONN_STATE_IN_USE:
                in_use++;
                break;
            case WS_CONN_STATE_CONNECTING:
                connecting++;
                break;
            case WS_CONN_STATE_INVALID:
                invalid++;
                break;
            default:
                break;
        }
    }

    // Set output values
    if (total_connections) {
        *total_connections = pool->total_connections;
    }
    if (available_connections) {
        *available_connections = pool->available_connections;
    }
    if (in_use_connections) {
        *in_use_connections = in_use;
    }
    if (connecting_connections) {
        *connecting_connections = connecting;
    }
    if (invalid_connections) {
        *invalid_connections = invalid;
    }

    mcp_mutex_unlock(pool->pool_mutex);
    return 0;
}

// Health check thread function
static void* health_check_thread_func(void* arg) {
    mcp_ws_connection_pool_t* pool = (mcp_ws_connection_pool_t*)arg;
    if (!pool) {
        return NULL;
    }

    mcp_log_info("WebSocket connection pool health check thread started");

    while (pool->running) {
        // Sleep for health check interval
        mcp_sleep_ms(pool->config.health_check_ms);

        if (!pool->running) {
            break;
        }

        mcp_mutex_lock(pool->pool_mutex);

        time_t now = time(NULL);
        uint32_t closed_count = 0;
        uint32_t reconnected_count = 0;

        // Check each connection
        for (uint32_t i = 0; i < pool->total_connections; i++) {
            // Skip connections in use
            if (pool->connections[i].state == WS_CONN_STATE_IN_USE) {
                continue;
            }

            // Check idle timeout for idle connections
            if (pool->connections[i].state == WS_CONN_STATE_IDLE) {
                double idle_time = difftime(now, pool->connections[i].last_used) * 1000;
                
                // Close excess idle connections beyond min_connections
                if (idle_time >= pool->config.idle_timeout_ms && 
                    pool->available_connections > pool->config.min_connections) {
                    
                    mcp_log_debug("Closing idle connection %u (idle for %.1f seconds)",
                                 pool->connections[i].id, idle_time / 1000.0);
                    
                    // Destroy the connection
                    mcp_transport_destroy(pool->connections[i].transport);
                    
                    // Remove from pool by shifting remaining connections
                    if (i < pool->total_connections - 1) {
                        memmove(&pool->connections[i], 
                                &pool->connections[i + 1], 
                                (pool->total_connections - i - 1) * sizeof(mcp_ws_conn_entry_t));
                    }
                    
                    pool->total_connections--;
                    pool->available_connections--;
                    closed_count++;
                    
                    // Adjust index to account for the removed connection
                    i--;
                    continue;
                }
            }

            // Check health of invalid connections and try to reconnect
            if (pool->connections[i].state == WS_CONN_STATE_INVALID) {
                mcp_log_debug("Attempting to reconnect invalid connection %u",
                             pool->connections[i].id);
                
                // Destroy the old connection
                mcp_transport_destroy(pool->connections[i].transport);
                
                // Temporarily unlock mutex while creating connection
                mcp_mutex_unlock(pool->pool_mutex);
                mcp_transport_t* new_transport = create_connection(pool);
                mcp_mutex_lock(pool->pool_mutex);
                
                if (new_transport) {
                    // Update connection entry
                    pool->connections[i].transport = new_transport;
                    pool->connections[i].state = WS_CONN_STATE_IDLE;
                    pool->connections[i].last_used = now;
                    pool->connections[i].is_healthy = true;
                    pool->available_connections++;
                    reconnected_count++;
                    
                    mcp_log_info("Successfully reconnected connection %u",
                                pool->connections[i].id);
                } else {
                    mcp_log_warn("Failed to reconnect connection %u",
                                pool->connections[i].id);
                }
            }
        }

        if (closed_count > 0 || reconnected_count > 0) {
            mcp_log_info("Health check: closed %u idle connections, reconnected %u invalid connections",
                        closed_count, reconnected_count);
        }

        mcp_mutex_unlock(pool->pool_mutex);
    }

    mcp_log_info("WebSocket connection pool health check thread stopped");
    return NULL;
}

// Create a new WebSocket connection
static mcp_transport_t* create_connection(mcp_ws_connection_pool_t* pool) {
    if (!pool) {
        return NULL;
    }

    // Create WebSocket transport
    mcp_transport_t* transport = mcp_transport_websocket_client_create(&pool->config.ws_config);
    if (!transport) {
        mcp_log_error("Failed to create WebSocket transport");
        return NULL;
    }

    // Start the transport
    if (mcp_transport_start(transport, NULL, NULL, NULL) != 0) {
        mcp_log_error("Failed to start WebSocket transport");
        mcp_transport_destroy(transport);
        return NULL;
    }

    // Wait for connection to be established
    uint32_t timeout_ms = pool->config.connect_timeout_ms > 0 ? 
                          pool->config.connect_timeout_ms : 5000;
    
    time_t start_time = time(NULL);
    bool connected = false;
    
    while (difftime(time(NULL), start_time) * 1000 < timeout_ms) {
        int state = mcp_transport_websocket_client_is_connected(transport);
        if (state == 1) {
            connected = true;
            break;
        }
        
        // Wait a bit before checking again
        mcp_sleep_ms(50);
    }
    
    if (!connected) {
        mcp_log_error("Failed to establish WebSocket connection within timeout");
        mcp_transport_destroy(transport);
        return NULL;
    }
    
    mcp_log_debug("Successfully created new WebSocket connection");
    return transport;
}

// Check if a connection is healthy
static bool is_connection_healthy(mcp_transport_t* transport) {
    if (!transport) {
        return false;
    }
    
    // Check connection state
    int state = mcp_transport_websocket_client_is_connected(transport);
    return (state == 1);
}
