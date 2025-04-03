#include "internal/transport_internal.h"
#include <stdlib.h>
#include <stdio.h>

// --- Generic Transport Interface Implementation ---
// These functions provide the public API defined in mcp_transport.h.
// They perform basic validation and then delegate the actual work to the
// specific transport implementation via the function pointers stored in
// the mcp_transport_t struct.

/**
 * @brief Generic implementation of mcp_transport_start.
 * Stores callbacks and user data in the transport handle, then calls the
 * specific implementation's start function pointer.
 */
int mcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (transport == NULL || transport->start == NULL) {
        return -1;
    }
    // Store callbacks and user data for use by the implementation's start/receive logic
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;
    // Pass the callbacks and user_data to the specific implementation's start function
    return transport->start(
        transport,
        message_callback,
        user_data,
        error_callback
    );
}

/**
 * @brief Generic implementation of mcp_transport_stop.
 * Calls the specific implementation's stop function pointer.
 */
int mcp_transport_stop(mcp_transport_t* transport) {
    // Validate input and function pointer
    if (transport == NULL || transport->stop == NULL) {
        return -1; // Indicate error
    }
    return transport->stop(transport);
}

/**
 * @brief Generic implementation of mcp_transport_send.
 * Calls the specific implementation's send function pointer.
 */
int mcp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
     // Validate input and function pointer
     if (transport == NULL || transport->send == NULL) {
        return -1; // Indicate error
    }
    return transport->send(transport, data, size);
}

/**
 * @brief Generic implementation of mcp_transport_receive.
 * Calls the specific implementation's receive function pointer.
 * Initializes output parameters before the call.
 */
int mcp_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    // Validate input and function pointer
    if (transport == NULL || transport->receive == NULL || data == NULL || size == NULL) {
        // Ensure output pointers are safe to dereference even on error return
        if (data) *data = NULL;
        if (size) *size = 0;
        return -1;
    }
    // Initialize output params
    *data = NULL;
    *size = 0;
    return transport->receive(transport, data, size, timeout_ms);
}

/**
 * @brief Generic implementation of mcp_transport_destroy.
 * Calls the specific implementation's destroy function pointer (if provided),
 * then frees the generic transport handle itself.
 */
void mcp_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return; // Nothing to do
    }
    // Call the specific transport's destroy function first if it exists
    if (transport->destroy != NULL) {
        transport->destroy(transport);
    }
    // Free the generic transport handle itself
    free(transport);
}
