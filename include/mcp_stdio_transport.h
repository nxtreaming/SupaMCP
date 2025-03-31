#ifndef MCP_STDIO_TRANSPORT_H
#define MCP_STDIO_TRANSPORT_H

#include "mcp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a transport layer instance that uses standard input/output.
 *
 * This transport reads messages line by line from stdin and sends messages
 * line by line to stdout. It's suitable for simple inter-process communication
 * where the other process also uses stdio.
 *
 * @return A pointer to the created transport instance, or NULL on failure.
 *         The caller is responsible for destroying the transport using
 *         mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_stdio_create(void);

#ifdef __cplusplus
}
#endif

#endif // MCP_STDIO_TRANSPORT_H
