#ifndef TRANSPORT_INTERNAL_H
#define TRANSPORT_INTERNAL_H

#include "mcp_transport.h"
#include "transport_interfaces.h"

// Define the internal structure for the transport handle
#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4201) // Disable warning for nameless struct/union
#endif
struct mcp_transport {
    // Transport type (server or client)
    mcp_transport_type_enum_t type;

    // Transport protocol type (TCP, HTTP, etc.)
    mcp_transport_protocol_t protocol_type;

    // Function pointers for specific transport implementation
    union {
        mcp_server_transport_t server;
        mcp_client_transport_t client;
    };

    // User data specific to this transport instance (e.g., file handles, socket descriptors)
    void* transport_data;
    // User data to be passed to the message and error callbacks
    void* callback_user_data;
    // The message callback itself (returns malloc'd response string)
    mcp_transport_message_callback_t message_callback;
    // The error callback
    mcp_transport_error_callback_t error_callback;
};
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

#endif // TRANSPORT_INTERNAL_H
