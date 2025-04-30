#include "internal/transport_internal.h"
#include <stdlib.h>
#include "mcp_log.h"

int mcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport) {
        return -1;
    }

    // Store callback info
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    // Dispatch to the appropriate start function based on transport type
    if (transport->type == MCP_TRANSPORT_TYPE_SERVER) {
        if (!transport->server.start) {
            return -1;
        }
        return transport->server.start(transport, message_callback, user_data, error_callback);
    } else {
        if (!transport->client.start) {
            return -1;
        }
        return transport->client.start(transport, message_callback, user_data, error_callback);
    }
}

int mcp_transport_stop(mcp_transport_t* transport) {
    if (!transport) {
        return -1;
    }

    // Dispatch to the appropriate stop function based on transport type
    if (transport->type == MCP_TRANSPORT_TYPE_SERVER) {
        if (!transport->server.stop) {
            return -1;
        }
        return transport->server.stop(transport);
    } else {
        if (!transport->client.stop) {
            return -1;
        }
        return transport->client.stop(transport);
    }
}

// Send function - only available for client transports
int mcp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !data || size == 0) {
        return -1;
    }

    // Check if this is a client transport
    if (transport->type != MCP_TRANSPORT_TYPE_CLIENT) {
        // Server transports don't support direct send operations
        // The actual sending happens in client handler threads
        mcp_log_error("mcp_transport_send called on a server transport, which doesn't support direct send operations");
        return -1;
    }

    // Check if the send function is available
    if (!transport->client.send) {
        // Create a single buffer structure on the stack and use sendv if available
        if (transport->client.sendv) {
            mcp_buffer_t buffer;

            buffer.data = data;
            buffer.size = size;
            return transport->client.sendv(transport, &buffer, 1);
        }
        return -1;
    }

    // Call the client transport's send function
    return transport->client.send(transport, data, size);
}

// Vectored send function - only available for client transports
int mcp_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !buffers || buffer_count == 0) {
        return -1;
    }

    // Check if this is a client transport
    if (transport->type != MCP_TRANSPORT_TYPE_CLIENT) {
        // Server transports don't support direct send operations
        // The actual sending happens in client handler threads
        mcp_log_error("mcp_transport_sendv called on a server transport, which doesn't support direct send operations");
        return -1;
    }

    // Check if the sendv function is available
    if (!transport->client.sendv) {
        // Fall back to calling send multiple times if available
        if (transport->client.send) {
            for (size_t i = 0; i < buffer_count; i++) {
                int result = transport->client.send(transport, buffers[i].data, buffers[i].size);
                if (result != 0) {
                    return result;
                }
            }
            return 0;
        }
        return -1;
    }

    // Call the client transport's sendv function
    return transport->client.sendv(transport, buffers, buffer_count);
}

void mcp_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    // Dispatch to the appropriate destroy function based on transport type
    // Note: The specific destroy functions are responsible for freeing the transport structure
    if (transport->type == MCP_TRANSPORT_TYPE_SERVER) {
        if (transport->server.destroy) {
            transport->server.destroy(transport);
        }
    } else {
        if (transport->client.destroy) {
            transport->client.destroy(transport);
        }
    }

    // We don't free the transport structure here because the specific destroy functions already do that
}

// Receive function - only available for client transports
int mcp_transport_receive(
    mcp_transport_t* transport,
    char** data, // [out]
    size_t* size, // [out]
    uint32_t timeout_ms
) {
    if (!transport || !data || !size) {
        return -1;
    }

    // Check if this is a client transport
    if (transport->type == MCP_TRANSPORT_TYPE_SERVER) {
        // Server transports don't support synchronous receive operations
        mcp_log_error("mcp_transport_receive called on a server transport, which doesn't support synchronous receive operations");
        return -1;
    }

    // Check if the receive function is available
    if (!transport->client.receive) {
        mcp_log_error("mcp_transport_receive called on a client transport that doesn't implement the receive function");
        return -1;
    }

    // Call the client transport's receive function
    return transport->client.receive(transport, data, size, timeout_ms);
}

const char* mcp_transport_get_client_ip(mcp_transport_t* transport) {
    if (!transport) {
        return NULL;
    }

    // For now, just return a default IP address
    // In a real implementation, this would be extracted from the transport data
    // based on the specific transport type (TCP, WebSocket, etc.)
    static const char* default_ip = "127.0.0.1";
    return default_ip;
}

mcp_transport_protocol_t mcp_transport_get_protocol(mcp_transport_t* transport) {
    if (!transport) {
        return MCP_TRANSPORT_PROTOCOL_UNKNOWN;
    }

    return transport->protocol_type;
}

void mcp_transport_set_protocol(mcp_transport_t* transport, mcp_transport_protocol_t protocol) {
    if (!transport) {
        return;
    }

    transport->protocol_type = protocol;
}
