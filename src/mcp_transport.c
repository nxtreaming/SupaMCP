#include "mcp_transport_internal.h"
#include <stdlib.h>
#include <stdio.h>

// Internal struct mcp_transport is now defined in mcp_transport_internal.h

// --- Generic Transport Interface Implementation ---

int mcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data
) {
    if (transport == NULL || transport->start == NULL) {
        return -1;
    }
    // Store callback and user data for use by the implementation's start/receive logic
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    return transport->start(transport, message_callback, user_data);
}

int mcp_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->stop == NULL) {
        return -1;
    }
    return transport->stop(transport);
}

int mcp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
     if (transport == NULL || transport->send == NULL) {
        return -1;
    }
    return transport->send(transport, data, size);
}

void mcp_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return;
    }
    // Call the specific transport's destroy function first if it exists
    if (transport->destroy != NULL) {
        transport->destroy(transport);
    }
    // Free the generic transport handle itself
    free(transport);
}

// --- Concrete Implementation Files ---
// The actual implementations (like stdio, tcp) and their create functions
// (e.g., mcp_transport_stdio_create) will be in separate .c files
// (e.g., src/mcp_stdio_transport.c).
