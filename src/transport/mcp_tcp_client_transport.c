#include "internal/tcp_client_transport_internal.h"
#include "mcp_framing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
// Platform-specific headers are now included via tcp_client_transport_internal.h
#else
#include <sys/uio.h>
#endif

static int tcp_client_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport || !transport->transport_data)
        return -1;

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (data->running) {
        mcp_log_warn("The TCP client transport is already running.");
        return 0;
    }

    // Store callbacks
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library.");
        return -1;
    }

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTING);

    // Establish connection using the new utility function (e.g., 5 second timeout)
    data->sock = mcp_socket_connect(data->host, data->port, 5000);
    if (data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to server %s:%u", data->host, data->port);
        // If reconnection is enabled, start reconnection process
        if (data->reconnect_enabled) {
            mcp_log_info("Starting reconnection process");
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

    // Mark as connected after successful mcp_socket_connect
    data->connected = true;
    data->running = true;
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTED);
    mcp_log_info("TCP Client Transport connected to %s:%u (socket %d, connected=%d)",
                 data->host, data->port, (int)data->sock, data->connected);

    // Start receiver thread
    if (mcp_thread_create(&data->receive_thread, tcp_client_receive_thread_func, transport) != 0) {
        mcp_log_error("Failed to create client receiver thread.");
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
        data->connected = false;
        data->running = false;
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
        mcp_socket_cleanup();
        return -1;
    }

    mcp_log_info("TCP Client Transport started.");
    return 0;
}

static int tcp_client_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data)
        return -1;

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running)
        return 0;

    // Stop reconnection process if active
    stop_reconnection_process(transport);

    mcp_log_info("Stopping TCP Client Transport...");
    // Signal receiver thread to stop
    data->running = false;
    // Disable reconnection
    data->reconnect_enabled = false;

    // Shutdown the socket to potentially unblock the receiver thread
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_log_info("Shutting down socket %d", (int)data->sock);
#ifdef _WIN32
        shutdown(data->sock, SD_BOTH);
#else
        shutdown(data->sock, SHUT_RDWR);
#endif
        // Don't close socket here, let receiver thread or destroy handle it
    }

    // Wait for receiver thread to finish
    if (data->receive_thread) {
        mcp_thread_join(data->receive_thread, NULL);
        data->receive_thread = 0;
    }

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

    // Close socket if not already closed by receiver
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
    }
    data->connected = false;

    mcp_socket_cleanup();
    mcp_log_info("TCP Client Transport stopped.");
    return 0;
}

/**
 * @brief Common helper function to handle send errors and trigger reconnection if needed
 *
 * @param transport The transport handle
 * @param error_msg Error message to log
 * @return Always returns -1 to indicate error
 */
static int handle_send_error(mcp_transport_t* transport, const char* error_msg) {
    if (!transport || !transport->transport_data)
        return -1;

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    mcp_log_error("%s", error_msg);

    // Mark as disconnected on send error
    data->connected = false;

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

    // Call error callback if set
    if (transport->error_callback) {
        transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
    }

    // If reconnection is enabled, start reconnection process
    if (data->reconnect_enabled) {
        mcp_log_info("Starting reconnection process after send failure");
        start_reconnection_process(transport);
    }

    return -1;
}

/**
 * @brief Check if the transport is ready for sending data
 *
 * @param transport The transport handle
 * @param operation_name Name of the operation for logging
 * @return 0 if ready, -1 if not ready
 */
static int check_transport_ready(mcp_transport_t* transport, const char* operation_name) {
    if (!transport || !transport->transport_data)
        return -1;

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    if (!data->running || !data->connected || data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Client transport not running or connected for %s. running=%d, connected=%d, sock=%d",
                      operation_name, data->running, data->connected, (int)data->sock);
        // If reconnection is enabled and we're not already reconnecting, start reconnection
        if (data->reconnect_enabled && data->connection_state != MCP_CONNECTION_STATE_RECONNECTING) {
            mcp_log_info("Starting reconnection process before %s", operation_name);
            start_reconnection_process(transport);
        }

        return -1;
    }

    return 0;
}

/**
 * @brief Implementation for the standard send function
 */
static int tcp_client_transport_send(mcp_transport_t* transport, const void* data_buf, size_t size) {
    if (!transport || !transport->transport_data || !data_buf || size == 0)
        return -1;

    // Check if transport is ready for sending
    if (check_transport_ready(transport, "send") != 0) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    // Use the unified exact send helper, pass NULL for stop_flag
    // mcp_socket_send_exact returns 0 on success, -1 on error/abort
    int result = mcp_socket_send_exact(data->sock, (const char*)data_buf, size, NULL);
    if (result != 0) {
        return handle_send_error(transport, "mcp_socket_send_exact failed");
    }

    return 0;
}

/**
 * @brief Implementation for the vectored send function
 */
static int tcp_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0)
        return -1;

    // Check if transport is ready for sending
    if (check_transport_ready(transport, "sendv") != 0) {
        return -1;
    }

    // Convert mcp_buffer_t to platform-specific mcp_iovec_t
    mcp_iovec_t* iov = (mcp_iovec_t*)malloc(buffer_count * sizeof(mcp_iovec_t));
    if (!iov) {
        mcp_log_error("Failed to allocate iovec array for sendv.");
        return -1;
    }

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
    // Use the unified vectored send function, pass NULL for stop_flag
    // Note: iovcnt is int, potential overflow if buffer_count is huge
    int result = mcp_socket_send_vectors(data->sock, iov, (int)buffer_count, NULL);
    free(iov);

    // mcp_socket_send_vectors returns 0 on success, -1 on error/abort
    if (result != 0) {
        return handle_send_error(transport, "mcp_socket_send_vectors failed");
    }

    return 0;
}

/**
 * @brief Implementation for the synchronous receive function
 *
 * This function attempts to receive a message from the socket with a timeout.
 * It's useful for HTTP-like protocols where we need to wait for a response
 * after sending a request.
 */
static int tcp_client_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    if (!transport || !transport->transport_data || !data_out || !size_out)
        return -1;

    // Initialize output parameters
    *data_out = NULL;
    *size_out = 0;

    // Check if transport is ready for receiving
    if (check_transport_ready(transport, "receive") != 0) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
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

    if (frame_result != 0) {
        // Check if this is a timeout (EAGAIN/EWOULDBLOCK/ETIMEDOUT)
        int last_error = mcp_socket_get_last_error();

        if (last_error == EAGAIN || last_error == EWOULDBLOCK || last_error == ETIMEDOUT) {
            mcp_log_debug("Receive operation timed out after %u ms", timeout_ms);
            return -2; // Special return code for timeout
        }

        // Handle other errors
        mcp_log_error("Failed to receive message: %d (error: %d)", frame_result, last_error);

        // Check if connection is still valid
        if (last_error == ECONNRESET || last_error == ENOTCONN || last_error == EPIPE) {
            // Connection lost, mark as disconnected
            data->connected = false;
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

            // If reconnection is enabled, start reconnection process
            if (data->reconnect_enabled) {
                mcp_log_info("Starting reconnection process after receive failure");
                start_reconnection_process(transport);
            }
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

static void tcp_client_transport_destroy(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data)
        return;

    tcp_client_transport_stop(transport);

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    free(data->host);
    // Destroy buffer pool
    mcp_buffer_pool_destroy(data->buffer_pool);

    // Destroy reconnection mutex
    if (data->reconnect_mutex) {
        mcp_mutex_destroy(data->reconnect_mutex);
        data->reconnect_mutex = NULL;
    }

    free(data);
    transport->transport_data = NULL;
    free(transport);
}

mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port) {
    return mcp_tcp_client_create_reconnect(host, port, NULL);
}

mcp_transport_t* mcp_tcp_client_create_reconnect(
    const char* host,
    uint16_t port,
    const mcp_reconnect_config_t* reconnect_config
) {
    if (!host)
        return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport)
        return NULL;

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)calloc(1, sizeof(mcp_tcp_client_transport_data_t));
    if (!data) {
        free(transport);
        return NULL;
    }

    data->host = mcp_strdup(host);
    if (!data->host) {
        free(data);
        free(transport);
        return NULL;
    }
    data->port = port;
    data->sock = MCP_INVALID_SOCKET;
    data->connected = false;
    data->running = false;
    data->receive_thread = 0;
    data->buffer_pool = NULL;

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

    // Initialize mutex
    data->reconnect_mutex = mcp_mutex_create();
    if (data->reconnect_mutex == NULL) {
        mcp_log_error("Failed to create reconnection mutex.");
        free(data->host);
        free(data);
        free(transport);
        return NULL;
    }

    // Create buffer pool
    data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
    if (data->buffer_pool == NULL) {
        mcp_log_error("Failed to create buffer pool for TCP client transport.");
        if (data->reconnect_mutex) {
            mcp_mutex_destroy(data->reconnect_mutex);
            data->reconnect_mutex = NULL;
        }
        free(data->host);
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport type to client
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;

    // Initialize client operations
    transport->client.start = tcp_client_transport_start;
    transport->client.stop = tcp_client_transport_stop;
    transport->client.destroy = tcp_client_transport_destroy;
    transport->client.send = tcp_client_transport_send;
    transport->client.sendv = tcp_client_transport_sendv;
    transport->client.receive = tcp_client_transport_receive;

    // Set transport data and initialize callbacks
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}
