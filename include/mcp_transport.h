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
 * @param error_code Pointer to an integer where an error code can be stored if the callback fails.
 * @return A dynamically allocated (malloc'd) string containing the JSON response to send back,
 *         or NULL if no response should be sent (e.g., for notifications) or an error occurred.
 *         The caller (transport layer) is responsible for freeing this string.
 */
typedef char* (*mcp_transport_message_callback_t)(void* user_data, const void* data, size_t size, int* error_code);

/**
 * @brief Callback function type for transport-level errors (e.g., disconnection).
 *
 * @param user_data User data provided to mcp_transport_start.
 * @param error_code An error code indicating the nature of the transport error.
 */
typedef void (*mcp_transport_error_callback_t)(void* user_data, int error_code);


/**
 * @brief Starts the transport layer and begins listening/processing messages.
 *
 * For connection-based transports, this might accept connections.
 * For message-based transports (like stdio), this might start a reading loop.
 * Received messages will be passed to the provided message_callback.
 *
 * @param transport The transport handle.
 * @param message_callback The function to call when a complete message is received.
 * @param user_data Arbitrary user data to be passed to the message_callback and error_callback.
 * @param error_callback The function to call when a transport-level error occurs (optional, can be NULL).
 * @return 0 on success, non-zero on error (e.g., failed to bind, listen, or start read loop).
 */
int mcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
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
 * @brief Sends raw data through the transport layer.
 *
 * The caller is typically responsible for ensuring the provided data includes
 * any necessary framing (e.g., length prefixes) required by the specific MCP
 * transport implementation or protocol convention. This function sends the
 * exact bytes provided in the buffer.
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

/**
 * @brief Receives a complete message from the transport (blocking, synchronous).
 *
 * This function attempts to read one complete message according to the
 * transport's framing mechanism (e.g., length-prefix, newline delimited).
 * It blocks until a message is received, an error occurs, or the timeout expires.
 * This is primarily intended for simple synchronous client implementations.
 * More complex clients or servers should typically use the asynchronous callback
 * mechanism initiated by mcp_transport_start.
 *
 * @param transport The transport handle.
 * @param data Pointer to a char pointer that will be allocated (using malloc)
 *             and filled with the received message data. The caller is responsible
 *             for freeing this buffer. NULL on error or timeout.
 * @param size Pointer to a size_t variable that will be filled with the size
 *             of the received message data (excluding null terminator).
 * @param timeout_ms Timeout in milliseconds to wait for a message. A value of 0
 *                   might indicate non-blocking, while a negative value might
 *                   indicate blocking indefinitely (behavior depends on implementation).
 * @return 0 on success, -1 on error, -2 on timeout (or other non-zero for specific errors).
 */
int mcp_transport_receive(
    mcp_transport_t* transport,
    char** data, // [out]
    size_t* size, // [out]
    uint32_t timeout_ms
);


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
