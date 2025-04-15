#include "mock_transport.h"
#include "../include/mcp_transport.h"
#include "../src/transport/internal/transport_internal.h"
#include "../src/transport/internal/transport_interfaces.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- Mock Transport State ---

struct mock_transport_data {
    // Store registered callbacks
    mcp_transport_message_callback_t receive_callback; // Correct type name
    void* receive_user_data;
    mcp_transport_error_callback_t error_callback;   // Correct type name
    // Store last sent data
    void* last_sent_data;
    size_t last_sent_size;
    // Add any other state needed for specific tests (e.g., flags to simulate errors)
    bool simulate_send_error;
};

// --- Mock Transport Interface Functions ---

static int mock_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;

    if (mock_data->simulate_send_error) {
        return -1; // Simulate a send failure
    }

    // Free previous data if any
    free(mock_data->last_sent_data);
    mock_data->last_sent_data = NULL;
    mock_data->last_sent_size = 0;

    // Copy the sent data
    if (data != NULL && size > 0) {
        mock_data->last_sent_data = malloc(size);
        if (mock_data->last_sent_data == NULL) {
            return -1; // Allocation failure
        }
        memcpy(mock_data->last_sent_data, data, size);
        mock_data->last_sent_size = size;
    }
    return 0; // Success
}

static int mock_start(mcp_transport_t* transport, mcp_transport_message_callback_t receive_cb, void* user_data, mcp_transport_error_callback_t error_cb) { // Correct type names
     if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;
    mock_data->receive_callback = receive_cb;
    mock_data->receive_user_data = user_data;
    mock_data->error_callback = error_cb;
    // In a real transport, this would start threads/async operations. Here, we just store callbacks.
    return 0; // Success
}

static int mock_stop(mcp_transport_t* transport) { // Change return type to int
    // No background operations to stop in the mock
    (void)transport; // Mark as unused
    return 0; // Return success
}

static void mock_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return;
    }
    if (transport->transport_data != NULL) {
        mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;
        free(mock_data->last_sent_data); // Free any stored sent data
        free(mock_data);
    }
    free(transport); // Free the main transport struct
}

// --- Mock Transport Creation ---

mcp_transport_t* mock_transport_create(void) {
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        return NULL;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)calloc(1, sizeof(mock_transport_data_t));
    if (mock_data == NULL) {
        free(transport);
        return NULL;
    }

    // Set transport type to client (mock transport is treated as a client transport)
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;

    // Initialize client operations
    transport->client.start = mock_start;
    transport->client.stop = mock_stop;
    transport->client.destroy = mock_destroy;
    transport->client.send = mock_send;
    transport->client.sendv = NULL; // No vectored send implementation for mock
    transport->client.receive = NULL; // No receive implementation for mock

    // Set transport data
    transport->transport_data = mock_data; // Link mock-specific data

    return transport;
}

// --- Mock Control Functions ---

int mock_transport_simulate_receive(mcp_transport_t* transport, const void* data, size_t size) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;

    if (mock_data->receive_callback == NULL) {
        return -1; // No callback registered
    }

    // Call the client's registered callback
    int error_code = 0;
    // The callback is expected to return NULL for client-side processing
    char* response_to_send_back = mock_data->receive_callback(mock_data->receive_user_data, data, size, &error_code);

    // Client callback should return NULL, free if it accidentally returns something
    free(response_to_send_back);

    return error_code == 0 ? 0 : -1; // Return 0 on success from callback perspective
}

int mock_transport_simulate_error(mcp_transport_t* transport, int error_code) {
     if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;

    if (mock_data->error_callback == NULL) {
        return -1; // No callback registered
    }

    // Call the client's registered error callback
    mock_data->error_callback(mock_data->receive_user_data, error_code);
    return 0; // Success
}

const void* mock_transport_get_last_sent_data(mcp_transport_t* transport, size_t* size) {
    if (transport == NULL || transport->transport_data == NULL || size == NULL) {
        if(size) *size = 0;
        return NULL;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;
    *size = mock_data->last_sent_size;
    return mock_data->last_sent_data;
}

void mock_transport_clear_last_sent_data(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) {
        return;
    }
    mock_transport_data_t* mock_data = (mock_transport_data_t*)transport->transport_data;
    free(mock_data->last_sent_data);
    mock_data->last_sent_data = NULL;
    mock_data->last_sent_size = 0;
}
