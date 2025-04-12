#include "internal/transport_internal.h"
#include <stdlib.h>

// Generic transport functions that dispatch to implementation-specific function pointers

int mcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport || !transport->start) {
        return -1; // Invalid transport or missing start function
    }
    // Store callback info
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;
    return transport->start(transport, message_callback, user_data, error_callback);
}

int mcp_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->stop) {
        return -1; // Invalid transport or missing stop function
    }
    return transport->stop(transport);
}

// Deprecated: Use mcp_transport_sendv instead.
// This function now wraps mcp_transport_sendv for backward compatibility.
int mcp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !data || size == 0) {
        return -1;
    }
    // Create a single buffer structure on the stack
    mcp_buffer_t buffer;
    buffer.data = data;
    buffer.size = size;
    // Call the new vectored send function
    return mcp_transport_sendv(transport, &buffer, 1);
}

// New vectored send function
int mcp_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
     if (!transport || !transport->sendv || !buffers || buffer_count == 0) {
        // Also handle the case where the old 'send' exists but 'sendv' doesn't?
        // For now, assume if sendv exists, we use it. If not, error.
        // A more robust approach might fallback to calling transport->send multiple times
        // or creating a temporary combined buffer, but that defeats the purpose.
        return -1; // Invalid transport, missing sendv, or invalid arguments
    }
    return transport->sendv(transport, buffers, buffer_count);
}

void mcp_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }
    if (transport->destroy) {
        transport->destroy(transport); // Call specific cleanup
    }
    // TODO:
    // Free the generic transport struct itself? Or is it part of a larger struct?
    // Assuming the specific destroy handles freeing transport_data,
    // but the mcp_transport struct itself might be allocated differently.
    // For now, let's assume the caller or specific implementation handles freeing the main struct.
    // free(transport); // Potentially incorrect - comment out for now
}

int mcp_transport_receive(
    mcp_transport_t* transport,
    char** data, // [out]
    size_t* size, // [out]
    uint32_t timeout_ms
) {
    if (!transport || !transport->receive || !data || !size) {
        return -1; // Invalid transport, missing receive, or invalid arguments
    }
    return transport->receive(transport, data, size, timeout_ms);
}
