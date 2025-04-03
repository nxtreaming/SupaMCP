#ifndef MCP_TRANSPORT_FACTORY_H
#define MCP_TRANSPORT_FACTORY_H

#include <mcp_transport.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enumeration of supported transport types.
 */
typedef enum mcp_transport_type {
    MCP_TRANSPORT_STDIO,  /**< Standard input/output transport */
    MCP_TRANSPORT_TCP,    /**< TCP server transport */
    MCP_TRANSPORT_TCP_CLIENT /**< TCP client transport */
    // Add future transport types here (e.g., WebSocket, etc.)
} mcp_transport_type_t;

/**
 * @brief Configuration options for transports.
 * 
 * This union contains configuration options for all transport types.
 * Only the fields relevant to the chosen transport type should be used.
 */
typedef union mcp_transport_config {
    struct {
        const char* host;         /**< Hostname or IP address to bind to (for TCP server) or connect to (for TCP client) */
        uint16_t port;            /**< Port number */
        uint32_t idle_timeout_ms; /**< Idle connection timeout in milliseconds (0 to disable, TCP server only) */
    } tcp;
    
    // Add configurations for other transport types as needed
    
} mcp_transport_config_t;

/**
 * @brief Creates a transport instance of the specified type with the given configuration.
 *
 * This factory function simplifies the creation of different transport types
 * by centralizing the creation logic and providing a unified interface.
 *
 * @param type The type of transport to create.
 * @param config Configuration options for the transport. The relevant fields
 *               should be set based on the chosen transport type.
 * @return A pointer to the created transport instance, or NULL on failure.
 *         The caller is responsible for destroying the transport using
 *         mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_factory_create(
    mcp_transport_type_t type,
    const mcp_transport_config_t* config
);

#ifdef __cplusplus
}
#endif

#endif // MCP_TRANSPORT_FACTORY_H
