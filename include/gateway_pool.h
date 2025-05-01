#ifndef MCP_GATEWAY_POOL_H
#define MCP_GATEWAY_POOL_H

#include "gateway.h"
#include "mcp_client.h"
#include "mcp_hashtable.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque type for the gateway connection pool manager
typedef struct gateway_connection_pool_manager gateway_pool_manager_t;

/**
 * @brief Creates a new gateway connection pool manager.
 *
 * Initializes internal structures (like a hash map) to manage connection pools
 * for different backend servers.
 *
 * @return Pointer to the newly created manager, or NULL on failure.
 */
gateway_pool_manager_t* gateway_pool_manager_create(void);

/**
 * @brief Destroys the gateway connection pool manager.
 *
 * Closes all managed connections, destroys individual backend pools, and frees
 * the manager itself.
 *
 * @param manager Pointer to the manager to destroy.
 */
void gateway_pool_manager_destroy(gateway_pool_manager_t* manager);

/**
 * @brief Gets or creates a client connection handle for a specific backend.
 *
 * Looks up the connection pool associated with the backend's address. If a pool
 * exists, it attempts to retrieve an available connection. If no pool exists,
 * it creates one based on the backend's configuration (address, timeout, etc.)
 * and then retrieves a connection.
 *
 * This function might block if creating a new connection is necessary and synchronous.
 * An asynchronous version might be needed for high performance.
 *
 * @param manager The pool manager instance.
 * @param backend_info Configuration details of the target backend.
 * @return A connection handle (e.g., mcp_client_t* or a wrapper struct) ready for use,
 *         or NULL on failure (e.g., cannot connect, pool full, timeout).
 * @note The caller is responsible for releasing the connection handle back to the
 *       pool manager using gateway_pool_release_connection() when done.
 */
void* gateway_pool_get_connection(gateway_pool_manager_t* manager, const mcp_backend_info_t* backend_info); // Return type TBD (e.g., mcp_client_t*)

/**
 * @brief Releases a previously acquired connection handle back to the pool.
 *
 * Marks the connection as available for reuse within its backend-specific pool.
 *
 * @param manager The pool manager instance.
 * @param backend_info Configuration details of the target backend (used to find the correct pool).
 * @param connection_handle The connection handle previously obtained from gateway_pool_get_connection().
 */
void gateway_pool_release_connection(gateway_pool_manager_t* manager, const mcp_backend_info_t* backend_info, void* connection_handle);

#ifdef __cplusplus
}
#endif

#endif // MCP_GATEWAY_POOL_H
