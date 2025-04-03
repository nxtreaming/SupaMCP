#include "mcp_transport_factory.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_transport.h"
#include "mcp_tcp_client_transport.h"
#include <stdlib.h>

mcp_transport_t* mcp_transport_factory_create(
    mcp_transport_type_t type,
    const mcp_transport_config_t* config
) {
    // Input validation
    if (config == NULL && type != MCP_TRANSPORT_STDIO) {
        return NULL; // Config is required for non-stdio transports
    }

    // Create the appropriate transport based on the type
    switch (type) {
        case MCP_TRANSPORT_STDIO:
            return mcp_transport_stdio_create();
            
        case MCP_TRANSPORT_TCP:
            if (config == NULL) {
                return NULL;
            }
            return mcp_transport_tcp_create(
                config->tcp.host,
                config->tcp.port,
                config->tcp.idle_timeout_ms
            );
            
        case MCP_TRANSPORT_TCP_CLIENT:
            if (config == NULL) {
                return NULL;
            }
            return mcp_transport_tcp_client_create(
                config->tcp.host,
                config->tcp.port
            );
            
        default:
            // Unsupported transport type
            return NULL;
    }
}
