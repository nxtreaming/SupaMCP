#ifndef MCP_TRANSPORT_INTERNAL_H
#define MCP_TRANSPORT_INTERNAL_H

#include "mcp_transport.h"

// Define the internal structure for the transport handle
// This is included by mcp_transport.c and specific implementations like mcp_stdio_transport.c
struct mcp_transport {
    // Function pointers for specific transport implementation
    int (*start)(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data);
    int (*stop)(mcp_transport_t* transport);
    int (*send)(mcp_transport_t* transport, const void* data, size_t size);
    // Receive function pointer (primarily for synchronous client usage)
    // Returns 0 on success, non-zero on error/timeout.
    // Allocates buffer for data, caller must free.
    int (*receive)(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);
    void (*destroy)(mcp_transport_t* transport); // Specific destroy logic

    // User data specific to this transport instance (e.g., file handles, socket descriptors)
    void* transport_data;
    // User data to be passed to the message callback
    void* callback_user_data;
    // The message callback itself
    mcp_transport_message_callback_t message_callback;
};

#endif // MCP_TRANSPORT_INTERNAL_H
