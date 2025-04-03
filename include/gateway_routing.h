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

#ifdef __cplusplus
}
#endif

#endif // MCP_GATEWAY_ROUTING_H
