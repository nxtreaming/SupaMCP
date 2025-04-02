#ifndef MOCK_TRANSPORT_H
#define MOCK_TRANSPORT_H

#include "../include/mcp_transport.h"
#include <stdbool.h>
#include <stddef.h>

// --- Mock Transport State ---

typedef struct mock_transport_data mock_transport_data_t;

// --- Mock Transport Creation ---

/**
 * @brief Creates a mock transport instance.
 * @return Pointer to the created transport, or NULL on failure.
 * @note Caller owns the returned transport and must destroy it using mcp_transport_destroy.
 */
mcp_transport_t* mock_transport_create(void);

// --- Mock Control Functions ---

/**
 * @brief Simulates receiving data from the "network".
 * Triggers the client's receive callback previously registered via mcp_transport_start.
 * @param transport Pointer to the mock transport instance.
 * @param data The data buffer to simulate receiving.
 * @param size The size of the data buffer.
 * @return 0 on success, -1 if no callback is registered or other error.
 */
int mock_transport_simulate_receive(mcp_transport_t* transport, const void* data, size_t size);

/**
 * @brief Simulates a transport-level error (e.g., disconnection).
 * Triggers the client's error callback previously registered via mcp_transport_start.
 * @param transport Pointer to the mock transport instance.
 * @param error_code The error code to simulate.
 * @return 0 on success, -1 if no callback is registered or other error.
 */
int mock_transport_simulate_error(mcp_transport_t* transport, int error_code);

/**
 * @brief Gets the last data buffer sent via the mock transport's send function.
 * @param transport Pointer to the mock transport instance.
 * @param[out] size Pointer to store the size of the last sent buffer.
 * @return Pointer to an internal copy of the last sent data, or NULL if nothing sent.
 * @note The returned pointer points to internal data owned by the mock transport. Do not free it.
 *       The data is overwritten on the next send call.
 */
const void* mock_transport_get_last_sent_data(mcp_transport_t* transport, size_t* size);

/**
 * @brief Clears the record of the last sent data.
 */
void mock_transport_clear_last_sent_data(mcp_transport_t* transport);


#endif // MOCK_TRANSPORT_H
