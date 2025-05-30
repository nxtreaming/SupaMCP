#ifndef MCP_STDIO_TRANSPORT_H
#define MCP_STDIO_TRANSPORT_H

#include <mcp_transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a transport layer instance that uses standard input/output.
 *
 * This transport reads messages with length prefixes from stdin and sends messages
 * with length prefixes to stdout. It's suitable for inter-process communication
 * where the other process also uses length-prefixed framing.
 *
 * Note: The receive function of this transport always uses malloc() to allocate
 * the returned buffer, which must be freed by the caller using free().
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
