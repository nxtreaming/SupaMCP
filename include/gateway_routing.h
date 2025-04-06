#ifndef MCP_GATEWAY_ROUTING_H
#define MCP_GATEWAY_ROUTING_H

#include "gateway.h"
#include "mcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Finds a suitable backend server for a given request based on routing rules.
 *
 * Iterates through the provided list of backend configurations and checks if the
 * request's method and parameters match the routing rules (resource prefixes or tool names)
 * defined for any backend. The first matching backend is returned.
 *
 * @param request Pointer to the parsed MCP request structure.
 * @param backends Pointer to the array of loaded backend configurations.
 * @param backend_count The number of elements in the backends array.
 * @return Pointer to the matching mcp_backend_info_t structure within the backends array,
 *         or NULL if no suitable backend is found based on the routing rules.
 */
const mcp_backend_info_t* find_backend_for_request(
    const mcp_request_t* request,
    const mcp_backend_info_t* backends,
    size_t backend_count
);

/**
 * @brief Forwards a request to a specified backend server.
 *
 * Handles getting a connection from the backend's pool, sending the request,
 * receiving the response, and releasing the connection.
 *
 * @param target_backend Pointer to the backend configuration, including the connection pool.
 * @param request Pointer to the original parsed MCP request structure.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code on failure.
 * @return A malloc'd JSON string containing the response received from the backend,
 *         or a malloc'd JSON error response string if forwarding failed.
 *         Returns NULL only on catastrophic internal failure (e.g., malloc failure).
 *         The caller is responsible for freeing the returned string.
 */
char* gateway_forward_request(
    const mcp_backend_info_t* target_backend,
    const mcp_request_t* request,
    int* error_code
);


#ifdef __cplusplus
}
#endif

#endif // MCP_GATEWAY_ROUTING_H
