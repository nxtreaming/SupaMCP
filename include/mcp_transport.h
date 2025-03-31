#ifndef MCP_TRANSPORT_H
#define MCP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque transport handle.
 *
 * The internal structure is defined in the implementation files.
 */
typedef struct mcp_transport mcp_transport_t;

/**
 * @brief Callback function type for received messages.
 *
 * @param user_data User data provided to mcp_transport_start.
 * @param data Pointer to the received message data.
 * @param size Size of the received message data in bytes.
 * @return 0 on success, non-zero indicates an error processing the message.
 */
typedef int (*mcp_transport_message_callback_t)(void* user_data, const void* data, size_t size);

/**
 * @brief Starts the transport layer and begins listening/processing messages.
 *
 * For connection-based transports, this might accept connections.
 * For message-based transports (like stdio), this might start a reading loop.
 * Received messages will be passed to the provided message_callback.
 *
 * @param transport The transport handle.
 * @param message_callback The function to call when a complete message is received.
 * @param user_data Arbitrary user data to be passed to the message_callback.
 * @return 0 on success, non-zero on error (e.g., failed to bind, listen, or start read loop).
 */
int mcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data
);

/**
 * @brief Stops the transport layer.
 *
 * Stops listening, closes connections, and halts any processing loops.
 *
 * @param transport The transport handle.
 * @return 0 on success, non-zero on error.
 */
int mcp_transport_stop(mcp_transport_t* transport);

/**
 * @brief Sends data through the transport layer.
 *
 * The transport implementation is responsible for any necessary framing
 * (e.g., adding delimiters or length prefixes) if required by the protocol.
 *
 * @param transport The transport handle.
 * @param data Pointer to the data to send.
 * @param size Size of the data in bytes.
 * @return 0 on success, non-zero on error (e.g., connection closed, write error).
 */
int mcp_transport_send(mcp_transport_t* transport, const void* data, size_t size);

/**
 * @brief Destroys the transport handle and frees associated resources.
 *
 * This should implicitly call stop if the transport is running.
 *
 * @param transport The transport handle.
 */
void mcp_transport_destroy(mcp_transport_t* transport);


// --- Concrete Transport Creation Function Declarations ---
// These functions are declared in separate headers (e.g., mcp_stdio_transport.h)
// and implemented in their respective source files. They return the generic
// mcp_transport_t handle.

/* Example (declarations moved to specific headers):
mcp_transport_t* mcp_stdio_transport_create(void);
mcp_transport_t* mcp_tcp_transport_create(const char* host, uint16_t port);
mcp_transport_t* mcp_websocket_transport_create(const char* url);
*/

#ifdef __cplusplus
}
#endif

#endif // MCP_TRANSPORT_H
