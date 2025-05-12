#ifndef TRANSPORT_INTERFACES_H
#define TRANSPORT_INTERFACES_H

#include "mcp_transport.h"

// Server-specific transport interface functions
typedef struct {
    // Initialize the transport
    int (*init)(mcp_transport_t* transport);

    // Destroy the transport and free resources
    void (*destroy)(mcp_transport_t* transport);

    // Start the transport with callbacks
    int (*start)(
        mcp_transport_t* transport,
        mcp_transport_message_callback_t message_callback,
        void* user_data,
        mcp_transport_error_callback_t error_callback
    );

    // Stop the transport
    int (*stop)(mcp_transport_t* transport);

    // Note: Server transport does not have send functions
    // Responses are sent directly by the client handler threads
} mcp_server_transport_t;

// Client-specific transport interface functions
typedef struct {
    // Initialize the transport
    int (*init)(mcp_transport_t* transport);

    // Destroy the transport and free resources
    void (*destroy)(mcp_transport_t* transport);

    // Start the transport with callbacks
    int (*start)(
        mcp_transport_t* transport,
        mcp_transport_message_callback_t message_callback,
        void* user_data,
        mcp_transport_error_callback_t error_callback
    );

    // Stop the transport
    int (*stop)(mcp_transport_t* transport);

    // Send data through the transport
    int (*send)(mcp_transport_t* transport, const void* data, size_t size);

    // Send data from multiple buffers through the transport
    int (*sendv)(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);

    // Receive data synchronously (optional, can be NULL)
    int (*receive)(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);
} mcp_client_transport_t;

// Transport type enumeration
typedef enum {
    MCP_TRANSPORT_TYPE_SERVER,
    MCP_TRANSPORT_TYPE_CLIENT
} mcp_transport_type_enum_t;

#endif // TRANSPORT_INTERFACES_H
