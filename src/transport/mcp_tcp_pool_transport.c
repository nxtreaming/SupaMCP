#include "internal/tcp_pool_transport_internal.h"
#include <mcp_log.h>
#include <mcp_socket_utils.h>
#include <mcp_framing.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <errno.h>
#endif

/**
 * @file mcp_tcp_pool_transport.c
 * @brief Implementation of TCP connection pool transport.
 *
 * This file implements a TCP client transport that uses a connection pool
 * to efficiently manage multiple connections to a server. It provides
 * automatic connection management, health checking, and request-response
 * handling.
 */

// Note: MAX_MESSAGE_SIZE is defined in tcp_pool_transport_internal.h

// Buffer pool configuration
#define POOL_BUFFER_SIZE 8192
#define POOL_NUM_BUFFERS 16

// Forward declarations for internal functions
static int tcp_pool_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
);

static int tcp_pool_transport_stop(mcp_transport_t* transport);
static void tcp_pool_transport_destroy(mcp_transport_t* transport);
static int tcp_pool_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static int tcp_pool_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);
static int tcp_pool_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms);

/**
 * @brief Starts the TCP pool transport.
 *
 * This function initializes the socket library, creates a connection pool
 * if not already created, and marks the transport as running.
 *
 * @param transport The transport handle
 * @param message_callback Callback function for received messages
 * @param user_data User data to pass to callbacks
 * @param error_callback Callback function for transport errors
 * @return 0 on success, -1 on error
 */
static int tcp_pool_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in start function");
        return -1;
    }

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    // Check if already running
    if (data->running) {
        mcp_log_debug("TCP pool transport already running");
        return 0;
    }

    // Store callbacks in the transport structure
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library");
        return -1;
    }

    // Create connection pool if not already created
    if (!data->connection_pool) {
        // Constants for health check configuration
        const int HEALTH_CHECK_INTERVAL_MS = 30000;  // 30 seconds
        const int HEALTH_CHECK_TIMEOUT_MS = 5000;    // 5 seconds

        mcp_log_info("Creating connection pool for %s:%d (min: %zu, max: %zu)",
                    data->host, data->port, data->min_connections, data->max_connections);

        data->connection_pool = mcp_connection_pool_create(
            data->host,
            data->port,
            data->min_connections,
            data->max_connections,
            data->idle_timeout_ms,
            data->connect_timeout_ms,
            HEALTH_CHECK_INTERVAL_MS,
            HEALTH_CHECK_TIMEOUT_MS
        );

        if (!data->connection_pool) {
            mcp_log_error("Failed to create connection pool for %s:%d", data->host, data->port);
            mcp_socket_cleanup();
            return -1;
        }

        mcp_log_debug("Connection pool created successfully");
    }

    // Mark as running
    data->running = true;

    mcp_log_info("TCP pool transport started for %s:%d", data->host, data->port);
    return 0;
}

/**
 * @brief Stops the TCP pool transport.
 *
 * This function marks the transport as not running and cleans up the socket
 * library. It does not destroy the connection pool, which is handled by the
 * destroy function.
 *
 * @param transport The transport handle
 * @return 0 on success, -1 on error
 */
static int tcp_pool_transport_stop(mcp_transport_t* transport) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in stop function");
        return -1;
    }

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    // Check if already stopped
    if (!data->running) {
        mcp_log_debug("TCP pool transport already stopped");
        return 0;
    }

    mcp_log_info("Stopping TCP pool transport for %s:%d", data->host, data->port);

    // Mark as not running
    data->running = false;

    // We don't destroy the connection pool here, just mark as not running
    // The pool will be destroyed in the destroy function
    // This allows for potential restart without recreating the pool

    // Clean up socket library
    mcp_socket_cleanup();

    mcp_log_info("TCP pool transport stopped");
    return 0;
}

/**
 * @brief Destroys the TCP pool transport.
 *
 * This function stops the transport if it's running, destroys the connection
 * pool, frees all allocated resources, and destroys the transport handle.
 *
 * @param transport The transport handle to destroy
 */
static void tcp_pool_transport_destroy(mcp_transport_t* transport) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_debug("Invalid transport handle in destroy function");
        return;
    }

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    mcp_log_info("Destroying TCP pool transport for %s:%d", data->host, data->port);

    // Ensure transport is stopped before destroying
    tcp_pool_transport_stop(transport);

    // Destroy connection pool
    if (data->connection_pool) {
        mcp_log_debug("Destroying connection pool");
        mcp_connection_pool_destroy(data->connection_pool);
        data->connection_pool = NULL;
    }

    // Free host string
    if (data->host) {
        free(data->host);
        data->host = NULL;
    }

    // Destroy buffer pool
    if (data->buffer_pool) {
        mcp_log_debug("Destroying buffer pool");
        mcp_buffer_pool_destroy(data->buffer_pool);
        data->buffer_pool = NULL;
    }

    // Free the transport data structure
    free(data);
    transport->transport_data = NULL;

    // Free the main transport struct
    free(transport);

    mcp_log_info("TCP pool transport destroyed");
}

/**
 * @brief Sends data over the TCP connection and waits for a response.
 *
 * This function gets a connection from the pool, sends the data, waits for
 * a response, processes it via the message callback, and returns the connection
 * to the pool.
 *
 * @param transport The transport handle
 * @param data_buf Pointer to the data buffer to send
 * @param size Size of the data buffer in bytes
 * @return 0 on success, -1 on error
 */
static int tcp_pool_transport_send(mcp_transport_t* transport, const void* data_buf, size_t size) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in send function");
        return -1;
    }

    if (!data_buf) {
        mcp_log_error("NULL data buffer in send function");
        return -1;
    }

    if (size == 0) {
        mcp_log_warn("Attempted to send zero bytes");
        return -1;
    }

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    // Check if transport is running
    if (!data->running) {
        mcp_log_error("TCP pool transport not running for send operation");
        return -1;
    }

    // Get a connection from the pool with timeout
    mcp_log_debug("Getting connection from pool (timeout: %d ms)", data->request_timeout_ms);
    socket_handle_t sock = mcp_connection_pool_get(data->connection_pool, data->request_timeout_ms);

    if (sock == INVALID_SOCKET_HANDLE) {
        int error_code = mcp_socket_get_last_error();
        mcp_log_error("Failed to get connection from pool (error: %d)", error_code);

        // Call error callback if set
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, error_code);
        }

        return -1;
    }

    // Send the data
    mcp_log_debug("Sending %zu bytes of data", size);
    int result = mcp_socket_send_exact(sock, (const char*)data_buf, size, NULL);

    // Check send result
    if (result != 0) {
        int error_code = mcp_socket_get_last_error();
        mcp_log_error("Failed to send data (result: %d, error: %d)", result, error_code);

        // Release connection back to pool as invalid
        mcp_connection_pool_release(data->connection_pool, sock, false);

        // Call error callback if set
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, error_code);
        }

        return -1;
    }

    // Wait for response using framing protocol
    char* response = NULL;
    uint32_t response_len = 0;

    mcp_log_debug("Waiting for response");
    result = mcp_framing_recv_message(sock, &response, &response_len, MAX_MESSAGE_SIZE, NULL);

    // Process the response
    if (result == 0 && response != NULL) {
        mcp_log_debug("Received response (%u bytes)", response_len);

        // Call the message callback with the response if set
        if (transport->message_callback) {
            int error_code = 0;
            char* callback_response = transport->message_callback(
                transport->callback_user_data,
                response,
                (size_t)response_len,
                &error_code
            );

            // Check for callback errors
            if (error_code != 0) {
                mcp_log_warn("Message callback returned error code: %d", error_code);
            }

            // Free the response string if one was returned
            if (callback_response) {
                free(callback_response);
            }
        }

        // Free the allocated response buffer
        free(response);

        // Release the connection back to the pool as valid
        mcp_connection_pool_release(data->connection_pool, sock, true);

        return 0;
    } else {
        // Handle receive error
        int error_code = mcp_socket_get_last_error();
        mcp_log_error("Failed to receive response (result: %d, error: %d)", result, error_code);

        // Free response buffer if allocated
        if (response != NULL) {
            free(response);
        }

        // Release connection back to pool as invalid
        mcp_connection_pool_release(data->connection_pool, sock, false);

        // Call error callback if set
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, error_code);
        }

        return -1;
    }
}

/**
 * @brief Sends multiple buffers over the TCP connection and waits for a response.
 *
 * This function gets a connection from the pool, sends multiple buffers using
 * vectored I/O, waits for a response, processes it via the message callback,
 * and returns the connection to the pool.
 *
 * @param transport The transport handle
 * @param buffers Array of buffer structures to send
 * @param buffer_count Number of buffers in the array
 * @return 0 on success, -1 on error
 */
static int tcp_pool_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in sendv function");
        return -1;
    }

    if (!buffers) {
        mcp_log_error("NULL buffers array in sendv function");
        return -1;
    }

    if (buffer_count == 0) {
        mcp_log_warn("Attempted to send zero buffers");
        return -1;
    }

    // Check for integer overflow when converting to int
    if (buffer_count > INT_MAX) {
        mcp_log_error("Buffer count too large for sendv function: %zu", buffer_count);
        return -1;
    }

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    // Check if transport is running
    if (!data->running) {
        mcp_log_error("TCP pool transport not running for sendv operation");
        return -1;
    }

    // Calculate total bytes to send for logging
    size_t total_bytes = 0;
    for (size_t i = 0; i < buffer_count; ++i) {
        total_bytes += buffers[i].size;
    }

    // Get a connection from the pool with timeout
    mcp_log_debug("Getting connection from pool for sendv (timeout: %d ms)", data->request_timeout_ms);
    socket_handle_t sock = mcp_connection_pool_get(data->connection_pool, data->request_timeout_ms);

    if (sock == INVALID_SOCKET_HANDLE) {
        int error_code = mcp_socket_get_last_error();
        mcp_log_error("Failed to get connection from pool (error: %d)", error_code);

        // Call error callback if set
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, error_code);
        }

        return -1;
    }

    // Convert mcp_buffer_t to platform-specific mcp_iovec_t
    mcp_iovec_t* iov = (mcp_iovec_t*)malloc(buffer_count * sizeof(mcp_iovec_t));
    if (!iov) {
        mcp_log_error("Failed to allocate iovec array for sendv (%zu elements)", buffer_count);

        // Release connection back to pool as valid (allocation failure is not a connection issue)
        mcp_connection_pool_release(data->connection_pool, sock, true);

        return -1;
    }

    // Initialize iovec array with buffer data
    for (size_t i = 0; i < buffer_count; ++i) {
#ifdef _WIN32
        iov[i].buf = (CHAR*)buffers[i].data;
        iov[i].len = (ULONG)buffers[i].size;
#else
        iov[i].iov_base = (void*)buffers[i].data;
        iov[i].iov_len = buffers[i].size;
#endif
    }

    // Send the data using vectored I/O
    mcp_log_debug("Sending %zu bytes in %zu buffers", total_bytes, buffer_count);
    int result = mcp_socket_send_vectors(sock, iov, (int)buffer_count, NULL);

    // Free the allocated iovec array (no longer needed)
    free(iov);

    // Check send result
    if (result != 0) {
        int error_code = mcp_socket_get_last_error();
        mcp_log_error("Failed to send vectored data (result: %d, error: %d)", result, error_code);

        // Release connection back to pool as invalid
        mcp_connection_pool_release(data->connection_pool, sock, false);

        // Call error callback if set
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, error_code);
        }

        return -1;
    }

    // Wait for response using framing protocol
    char* response = NULL;
    uint32_t response_len = 0;

    mcp_log_debug("Waiting for response after vectored send");
    result = mcp_framing_recv_message(sock, &response, &response_len, MAX_MESSAGE_SIZE, NULL);

    // Process the response
    if (result == 0 && response != NULL) {
        mcp_log_debug("Received response after vectored send (%u bytes)", response_len);

        // Call the message callback with the response if set
        if (transport->message_callback) {
            int error_code = 0;
            char* callback_response = transport->message_callback(
                transport->callback_user_data,
                response,
                (size_t)response_len,
                &error_code
            );

            // Check for callback errors
            if (error_code != 0) {
                mcp_log_warn("Message callback returned error code: %d", error_code);
            }

            // Free the response string if one was returned
            if (callback_response) {
                free(callback_response);
            }
        }

        // Free the allocated response buffer
        free(response);

        // Release the connection back to the pool as valid
        mcp_connection_pool_release(data->connection_pool, sock, true);

        return 0;
    } else {
        // Handle receive error
        int error_code = mcp_socket_get_last_error();
        mcp_log_error("Failed to receive response after vectored send (result: %d, error: %d)",
                     result, error_code);

        // Free response buffer if allocated
        if (response != NULL) {
            free(response);
        }

        // Release connection back to pool as invalid
        mcp_connection_pool_release(data->connection_pool, sock, false);

        // Call error callback if set
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, error_code);
        }

        return -1;
    }
}

/**
 * @brief Synchronous receive function (not supported by pool transport).
 *
 * This function is not supported by the TCP pool transport since the
 * request-response cycle is handled in the send/sendv functions.
 *
 * @param transport The transport handle
 * @param data_out Pointer to receive the allocated message buffer
 * @param size_out Pointer to receive the message size
 * @param timeout_ms Timeout in milliseconds
 * @return Always returns -1 (not supported)
 */
static int tcp_pool_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in receive function");
        return -1;
    }

    if (!data_out || !size_out) {
        mcp_log_error("Invalid output parameters in receive function");
        return -1;
    }

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    // Initialize output parameters
    *data_out = NULL;
    *size_out = 0;

    // Check if transport is running
    if (!data->running) {
        mcp_log_error("TCP pool transport not running for receive operation");
        return -1;
    }

    // Mark timeout_ms as unused to suppress warning
    (void)timeout_ms;

    // This synchronous receive is not typically used with the pool transport
    // since we handle the request-response cycle in the send/sendv functions.
    mcp_log_error("Synchronous receive is not supported by TCP pool transport");
    return -1; // Not supported
}

/**
 * @brief Creates a new TCP pool transport.
 *
 * This function creates a new TCP client transport that uses a connection pool
 * to efficiently manage multiple connections to a server. It allocates and
 * initializes all necessary resources.
 *
 * @param host The host to connect to (hostname or IP address)
 * @param port The port to connect to
 * @param min_connections Minimum number of connections to maintain in the pool
 * @param max_connections Maximum number of connections allowed in the pool
 * @param idle_timeout_ms Timeout in milliseconds for idle connections
 * @param connect_timeout_ms Timeout in milliseconds for connection attempts
 * @param request_timeout_ms Timeout in milliseconds for request operations
 * @return A new transport handle, or NULL on error
 */
mcp_transport_t* mcp_tcp_pool_transport_create(
    const char* host,
    uint16_t port,
    size_t min_connections,
    size_t max_connections,
    int idle_timeout_ms,
    int connect_timeout_ms,
    int request_timeout_ms
) {
    // Validate parameters
    if (!host) {
        mcp_log_error("NULL host parameter in create function");
        return NULL;
    }

    // Check for invalid port values (port 0 is reserved)
    if (port == 0) {
        mcp_log_error("Invalid port value: %u", port);
        return NULL;
    }

    // Validate connection pool parameters
    if (min_connections > max_connections) {
        mcp_log_error("Invalid connection pool parameters: min (%zu) > max (%zu)",
                     min_connections, max_connections);
        return NULL;
    }

    if (max_connections == 0) {
        mcp_log_error("Invalid connection pool parameters: max_connections cannot be 0");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) {
        mcp_log_error("Failed to allocate transport structure");
        return NULL;
    }

    // Zero-initialize the transport structure
    memset(transport, 0, sizeof(mcp_transport_t));

    // Allocate transport data structure
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)calloc(1, sizeof(mcp_tcp_pool_transport_data_t));
    if (!data) {
        mcp_log_error("Failed to allocate transport data structure");
        free(transport);
        return NULL;
    }

    // Duplicate host string
    data->host = mcp_strdup(host);
    if (!data->host) {
        mcp_log_error("Failed to duplicate host string");
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize basic properties
    data->port = port;
    data->min_connections = min_connections;
    data->max_connections = max_connections;
    data->idle_timeout_ms = idle_timeout_ms;
    data->connect_timeout_ms = connect_timeout_ms;
    data->request_timeout_ms = request_timeout_ms;
    data->running = false;
    data->connection_pool = NULL;

    // Create buffer pool for efficient memory management
    data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
    if (data->buffer_pool == NULL) {
        mcp_log_error("Failed to create buffer pool (size: %d, count: %d)",
                     POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
        free(data->host);
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport type and protocol
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_TCP;

    // Initialize client operations
    transport->client.start = tcp_pool_transport_start;
    transport->client.stop = tcp_pool_transport_stop;
    transport->client.destroy = tcp_pool_transport_destroy;
    transport->client.send = tcp_pool_transport_send;
    transport->client.sendv = tcp_pool_transport_sendv;
    transport->client.receive = tcp_pool_transport_receive;

    // Set transport data
    transport->transport_data = data;

    // Initialize callbacks to NULL (will be set in start function)
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    mcp_log_info("Created TCP pool transport for %s:%d (min: %zu, max: %zu, idle: %d ms, connect: %d ms, request: %d ms)",
                host, port, min_connections, max_connections,
                idle_timeout_ms, connect_timeout_ms, request_timeout_ms);

    return transport;
}
