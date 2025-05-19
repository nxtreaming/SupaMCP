#include "internal/tcp_client_transport_internal.h"
#include "mcp_framing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
// Platform-specific headers are included via tcp_client_transport_internal.h
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

/**
 * @file mcp_tcp_client_transport.c
 * @brief Implementation of the TCP client transport layer.
 *
 * This file implements the TCP client transport layer, which provides
 * functionality for connecting to a TCP server, sending and receiving
 * messages, and handling reconnection in case of connection loss.
 */

/**
 * @brief Starts the TCP client transport.
 *
 * This function initializes the socket library, connects to the server,
 * and starts the receiver thread. If reconnection is enabled and the
 * connection fails, it will start the reconnection process.
 *
 * @param transport The transport handle
 * @param message_callback Callback function for received messages
 * @param user_data User data to pass to callbacks
 * @param error_callback Callback function for transport errors
 * @return 0 on success, -1 on error
 */
static int tcp_client_transport_start(
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

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Check if already running
    if (data->running) {
        mcp_log_warn("TCP client transport already running");
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

    // Update connection state to connecting
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTING);

    // Establish connection with timeout (5 seconds)
    const int CONNECT_TIMEOUT_MS = 5000;
    data->sock = mcp_socket_connect(data->host, data->port, CONNECT_TIMEOUT_MS);

    if (data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to server %s:%u", data->host, data->port);

        // If reconnection is enabled, start reconnection process
        if (data->reconnect_enabled) {
            mcp_log_info("Starting reconnection process after initial connection failure");
            start_reconnection_process(transport);

            // Mark as running even though not connected yet
            data->running = true;

            // Return success, reconnection will happen in background
            return 0;
        }

        // Otherwise, fail
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
        mcp_socket_cleanup();
        return -1;
    }

    // Connection successful
    data->connected = true;
    data->running = true;
    data->reconnect_attempt = 0; // Reset reconnection attempt counter

    // Update connection state to connected
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTED);

    mcp_log_info("Connected to %s:%u (socket %d)",
                data->host, data->port, (int)data->sock);

    // Start receiver thread
    if (mcp_thread_create(&data->receive_thread, tcp_client_receive_thread_func, transport) != 0) {
        mcp_log_error("Failed to create receiver thread");

        // Clean up on failure
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
        data->connected = false;
        data->running = false;

        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
        mcp_socket_cleanup();
        return -1;
    }

    mcp_log_info("TCP client transport started successfully");
    return 0;
}

/**
 * @brief Stops the TCP client transport.
 *
 * This function stops the reconnection process if active, signals the
 * receiver thread to stop, and closes the socket. It also updates the
 * connection state to disconnected.
 *
 * @param transport The transport handle
 * @return 0 on success, -1 on error
 */
static int tcp_client_transport_stop(mcp_transport_t* transport) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in stop function");
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Check if already stopped
    if (!data->running) {
        mcp_log_debug("TCP client transport already stopped");
        return 0;
    }

    mcp_log_info("Stopping TCP client transport...");

    // Stop reconnection process if active
    stop_reconnection_process(transport);

    // Signal receiver thread to stop
    data->running = false;

    // Disable reconnection to prevent auto-reconnect during shutdown
    data->reconnect_enabled = false;

    // Shutdown the socket to unblock the receiver thread
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_log_debug("Shutting down socket %d", (int)data->sock);

        // Platform-specific socket shutdown
#ifdef _WIN32
        shutdown(data->sock, SD_BOTH);
#else
        shutdown(data->sock, SHUT_RDWR);
#endif
        // Don't close socket here, let receiver thread or destroy handle it
    }

    // Wait for receiver thread to finish
    if (data->receive_thread) {
        mcp_log_debug("Waiting for receiver thread to finish...");
        mcp_thread_join(data->receive_thread, NULL);
        data->receive_thread = 0;
        mcp_log_debug("Receiver thread stopped");
    }

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

    // Close socket if not already closed by receiver
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
    }

    // Mark as disconnected
    data->connected = false;

    // Clean up socket library
    mcp_socket_cleanup();

    mcp_log_info("TCP client transport stopped successfully");
    return 0;
}

/**
 * @brief Common helper function to handle send errors and trigger reconnection if needed.
 *
 * This function logs the error, marks the connection as disconnected,
 * calls the error callback if set, and starts the reconnection process
 * if enabled.
 *
 * @param transport The transport handle
 * @param error_msg Error message to log
 * @return Always returns -1 to indicate error
 */
static int handle_send_error(mcp_transport_t* transport, const char* error_msg) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in handle_send_error");
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    int error_code = mcp_socket_get_lasterror();

    // Log the error with the socket error code
    mcp_log_error("%s (error code: %d)", error_msg, error_code);

    // Mark as disconnected on send error
    data->connected = false;

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

    // Call error callback if set
    if (transport->error_callback) {
        transport->error_callback(transport->callback_user_data, error_code);
    }

    // If reconnection is enabled, start reconnection process
    if (data->reconnect_enabled) {
        mcp_log_info("Starting reconnection process after send failure");
        start_reconnection_process(transport);
    }

    return -1;
}

/**
 * @brief Check if the transport is ready for sending or receiving data.
 *
 * This function checks if the transport is running, connected, and has a valid socket.
 * If not, it logs an error and starts the reconnection process if enabled.
 *
 * @param transport The transport handle
 * @param operation_name Name of the operation for logging (e.g., "send", "receive")
 * @return 0 if ready, -1 if not ready
 */
static int check_transport_ready(mcp_transport_t* transport, const char* operation_name) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in check_transport_ready");
        return -1;
    }

    if (!operation_name) {
        operation_name = "unknown operation";
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Check all conditions for transport readiness
    if (!data->running) {
        mcp_log_error("Transport not running for %s operation", operation_name);
        return -1;
    }

    if (!data->connected) {
        mcp_log_error("Transport not connected for %s operation", operation_name);

        // If reconnection is enabled and we're not already reconnecting, start reconnection
        if (data->reconnect_enabled && data->connection_state != MCP_CONNECTION_STATE_RECONNECTING) {
            mcp_log_info("Starting reconnection process before %s operation", operation_name);
            start_reconnection_process(transport);
        }

        return -1;
    }

    if (data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Invalid socket for %s operation", operation_name);

        // If socket is invalid but we think we're connected, fix the state
        if (data->connected) {
            data->connected = false;
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

            // Start reconnection if enabled
            if (data->reconnect_enabled && data->connection_state != MCP_CONNECTION_STATE_RECONNECTING) {
                mcp_log_info("Starting reconnection process due to invalid socket");
                start_reconnection_process(transport);
            }
        }

        return -1;
    }

    // All checks passed, transport is ready
    return 0;
}

/**
 * @brief Implementation for the standard send function.
 *
 * This function sends data over the TCP connection. It first checks if
 * the transport is ready for sending, then uses the socket utility function
 * to send the data.
 *
 * @param transport The transport handle
 * @param data_buf Pointer to the data buffer to send
 * @param size Size of the data buffer in bytes
 * @return 0 on success, -1 on error
 */
static int tcp_client_transport_send(mcp_transport_t* transport, const void* data_buf, size_t size) {
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

    // Check if transport is ready for sending
    if (check_transport_ready(transport, "send") != 0) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Log send operation at debug level
    mcp_log_debug("Sending %zu bytes to %s:%u", size, data->host, data->port);

    // Use the unified exact send helper
    int result = mcp_socket_send_exact(data->sock, (const char*)data_buf, size, NULL);
    if (result != 0) {
        return handle_send_error(transport, "Failed to send data");
    }

    return 0;
}

/**
 * @brief Implementation for the vectored send function.
 *
 * This function sends multiple buffers over the TCP connection using
 * vectored I/O (scatter/gather). It first checks if the transport is ready
 * for sending, then converts the buffer array to platform-specific iovec
 * structures and uses the socket utility function to send the data.
 *
 * @param transport The transport handle
 * @param buffers Array of buffer structures to send
 * @param buffer_count Number of buffers in the array
 * @return 0 on success, -1 on error
 */
static int tcp_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
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

    // Check if transport is ready for sending
    if (check_transport_ready(transport, "sendv") != 0) {
        return -1;
    }

    // Calculate total bytes to send for logging
    size_t total_bytes = 0;
    for (size_t i = 0; i < buffer_count; ++i) {
        total_bytes += buffers[i].size;
    }

    // Convert mcp_buffer_t to platform-specific mcp_iovec_t
    mcp_iovec_t* iov = (mcp_iovec_t*)malloc(buffer_count * sizeof(mcp_iovec_t));
    if (!iov) {
        mcp_log_error("Failed to allocate iovec array for sendv (%zu elements)", buffer_count);
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

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Log send operation at debug level
    mcp_log_debug("Sending %zu bytes in %zu buffers to %s:%u",
                 total_bytes, buffer_count, data->host, data->port);

    // Use the unified vectored send function
    int result = mcp_socket_send_vectors(data->sock, iov, (int)buffer_count, NULL);

    // Free the iovec array
    free(iov);

    // Check for errors
    if (result != 0) {
        return handle_send_error(transport, "Failed to send vectored data");
    }

    return 0;
}

/**
 * @brief Implementation for the synchronous receive function.
 *
 * This function attempts to receive a message from the socket with a timeout.
 * It's useful for request-response protocols where we need to wait for a
 * response after sending a request.
 *
 * @param transport The transport handle
 * @param data_out Pointer to receive the allocated message buffer (caller must free)
 * @param size_out Pointer to receive the message size
 * @param timeout_ms Timeout in milliseconds (0 for no timeout)
 * @return 0 on success, -1 on error, -2 on timeout
 */
static int tcp_client_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in receive function");
        return -1;
    }

    if (!data_out || !size_out) {
        mcp_log_error("Invalid output parameters in receive function");
        return -1;
    }

    // Initialize output parameters
    *data_out = NULL;
    *size_out = 0;

    // Check if transport is ready for receiving
    if (check_transport_ready(transport, "receive") != 0) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Log receive operation at debug level
    mcp_log_debug("Attempting to receive message from %s:%u (timeout: %u ms)",
                 data->host, data->port, timeout_ms);

    // Set socket timeout for receive operation
    if (mcp_socket_set_timeout(data->sock, timeout_ms) != 0) {
        mcp_log_error("Failed to set socket timeout for receive operation");
        return -1;
    }

    // Receive the message using the framing protocol
    uint32_t message_length = 0;
    char* message_buf = NULL;

    int frame_result = mcp_framing_recv_message(
        data->sock,
        &message_buf,
        &message_length,
        MAX_MCP_MESSAGE_SIZE,
        NULL
    );

    // Restore socket timeout to blocking mode
    mcp_socket_set_timeout(data->sock, 0);

    // Handle receive errors
    if (frame_result != 0) {
        int last_error = mcp_socket_get_lasterror();

        // Check if this is a timeout
        if (last_error == EAGAIN || last_error == EWOULDBLOCK || last_error == ETIMEDOUT) {
            mcp_log_debug("Receive operation timed out after %u ms", timeout_ms);
            return -2; // Special return code for timeout
        }

        // Handle connection errors
        if (last_error == ECONNRESET || last_error == ENOTCONN || last_error == EPIPE) {
            mcp_log_error("Connection lost during receive: %d", last_error);

            // Connection lost, mark as disconnected
            data->connected = false;
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

            // If reconnection is enabled, start reconnection process
            if (data->reconnect_enabled) {
                mcp_log_info("Starting reconnection process after connection loss");
                start_reconnection_process(transport);
            }
        } else {
            // Other errors
            mcp_log_error("Failed to receive message: %d (error: %d)", frame_result, last_error);
        }

        return -1;
    }

    // Successfully received a message
    mcp_log_debug("Received message (%u bytes)", message_length);

    // Set output parameters
    *data_out = message_buf;
    *size_out = message_length;

    return 0;
}

/**
 * @brief Destroys the TCP client transport.
 *
 * This function stops the transport if it's running, frees all allocated
 * resources, and destroys the transport handle.
 *
 * @param transport The transport handle to destroy
 */
static void tcp_client_transport_destroy(mcp_transport_t* transport) {
    // Validate parameters
    if (!transport || !transport->transport_data) {
        mcp_log_debug("Invalid transport handle in destroy function");
        return;
    }

    mcp_log_info("Destroying TCP client transport");

    // Stop the transport if it's running
    tcp_client_transport_stop(transport);

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Free host string
    if (data->host) {
        free(data->host);
        data->host = NULL;
    }

    // Destroy buffer pool
    if (data->buffer_pool) {
        mcp_buffer_pool_destroy(data->buffer_pool);
        data->buffer_pool = NULL;
    }

    // Destroy reconnection mutex
    if (data->reconnect_mutex) {
        mcp_mutex_destroy(data->reconnect_mutex);
        data->reconnect_mutex = NULL;
    }

    // Free transport data
    free(data);
    transport->transport_data = NULL;

    // Free transport handle
    free(transport);

    mcp_log_debug("TCP client transport destroyed");
}

mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port) {
    return mcp_tcp_client_create_reconnect(host, port, NULL);
}

/**
 * @brief Creates a TCP client transport with reconnection support.
 *
 * This function creates a new TCP client transport that can connect to
 * the specified host and port. It supports automatic reconnection if
 * the connection is lost.
 *
 * @param host The host to connect to (hostname or IP address)
 * @param port The port to connect to
 * @param reconnect_config Reconnection configuration, or NULL for defaults
 * @return A new transport handle, or NULL on error
 */
mcp_transport_t* mcp_tcp_client_create_reconnect(
    const char* host,
    uint16_t port,
    const mcp_reconnect_config_t* reconnect_config
) {
    // Validate input parameters
    if (!host) {
        mcp_log_error("NULL host parameter in create function");
        return NULL;
    }

    // Check for invalid port values (port 0 is reserved)
    if (port == 0) {
        mcp_log_error("Invalid port value: %u", port);
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
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)calloc(1, sizeof(mcp_tcp_client_transport_data_t));
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
    data->sock = MCP_INVALID_SOCKET;
    data->connected = false;
    data->running = false;
    data->transport_handle = transport; // Set back-reference

    // Initialize reconnection fields
    if (reconnect_config) {
        data->reconnect_config = *reconnect_config;
        data->reconnect_enabled = reconnect_config->enable_reconnect;
    } else {
        data->reconnect_config = MCP_DEFAULT_RECONNECT_CONFIG;
        data->reconnect_enabled = MCP_DEFAULT_RECONNECT_CONFIG.enable_reconnect;
    }

    data->reconnect_attempt = 0;
    data->reconnect_thread = 0;
    data->reconnect_thread_running = false;
    data->connection_state = MCP_CONNECTION_STATE_DISCONNECTED;
    data->state_callback = NULL;
    data->state_callback_user_data = NULL;

    // Initialize mutex for thread synchronization
    data->reconnect_mutex = mcp_mutex_create();
    if (data->reconnect_mutex == NULL) {
        mcp_log_error("Failed to create reconnection mutex");
        free(data->host);
        free(data);
        free(transport);
        return NULL;
    }

    // Create buffer pool for efficient memory management
    data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
    if (data->buffer_pool == NULL) {
        mcp_log_error("Failed to create buffer pool");
        mcp_mutex_destroy(data->reconnect_mutex);
        free(data->host);
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport type and protocol
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_TCP;

    // Initialize client operations
    transport->client.start = tcp_client_transport_start;
    transport->client.stop = tcp_client_transport_stop;
    transport->client.destroy = tcp_client_transport_destroy;
    transport->client.send = tcp_client_transport_send;
    transport->client.sendv = tcp_client_transport_sendv;
    transport->client.receive = tcp_client_transport_receive;

    // Set transport data
    transport->transport_data = data;

    mcp_log_info("Created TCP client transport for %s:%u (reconnect: %s)",
                host, port, data->reconnect_enabled ? "enabled" : "disabled");

    return transport;
}
