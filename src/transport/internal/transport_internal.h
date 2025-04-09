#ifndef MCP_TRANSPORT_INTERNAL_H
#define MCP_TRANSPORT_INTERNAL_H

#include "mcp_transport.h"

// Define the internal structure for the transport handle
// This is included by mcp_transport.c and specific implementations like mcp_stdio_transport.c
struct mcp_transport {
    // Function pointers for specific transport implementation
    int (*start)(
        mcp_transport_t* transport,
        mcp_transport_message_callback_t message_callback,
        void* user_data,
        mcp_transport_error_callback_t error_callback
    );
    int (*stop)(mcp_transport_t* transport);
    int (*send)(mcp_transport_t* transport, const void* data, size_t size); // Keep for compatibility? Or remove?
    int (*sendv)(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count); // New vectored send
    // Receive function pointer (primarily for synchronous client usage)
    // Returns 0 on success, non-zero on error/timeout.
    // Allocates buffer for data, caller must free.
    int (*receive)(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);
    void (*destroy)(mcp_transport_t* transport); // Specific destroy logic

    // User data specific to this transport instance (e.g., file handles, socket descriptors)
    void* transport_data;
    // User data to be passed to the message and error callbacks
    void* callback_user_data;
    // The message callback itself (returns malloc'd response string)
    mcp_transport_message_callback_t message_callback;
    // The error callback
    mcp_transport_error_callback_t error_callback;
};

#endif // MCP_TRANSPORT_INTERNAL_H
