#include "internal/tcp_client_transport_internal.h"
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
    if (!transport || !transport->transport_data) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (data->running) return 0; // Already running

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
            data->running = true; // Mark as running even though not connected yet
            return 0; // Return success, reconnection will happen in background
        }

        // Otherwise, fail
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
        mcp_socket_cleanup();
        return -1;
    }

    data->connected = true; // Mark as connected after successful mcp_socket_connect
    data->running = true;
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTED);
    mcp_log_info("TCP Client Transport connected to %s:%u (socket %d, connected=%d)", data->host, data->port, (int)data->sock, data->connected);

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
    if (!transport || !transport->transport_data) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running) return 0;

    // Stop reconnection process if active
    stop_reconnection_process(transport);

    mcp_log_info("Stopping TCP Client Transport...");
    data->running = false; // Signal receiver thread to stop
    data->reconnect_enabled = false; // Disable reconnection

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
        data->receive_thread = 0; // Reset handle
    }

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

    // Close socket if not already closed by receiver
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
    }
    data->connected = false;

    mcp_socket_cleanup(); // Cleanup socket library
    mcp_log_info("TCP Client Transport stopped.");
    return 0;
}

// Implementation for the old send function
static int tcp_client_transport_send(mcp_transport_t* transport, const void* data_buf, size_t size) {
    if (!transport || !transport->transport_data || !data_buf || size == 0) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected || data->sock == MCP_INVALID_SOCKET) { // Use new invalid socket macro
        mcp_log_error("Client transport not running or connected for send.");

        // If reconnection is enabled and we're not already reconnecting, start reconnection
        if (data->reconnect_enabled && data->connection_state != MCP_CONNECTION_STATE_RECONNECTING) {
            mcp_log_info("Starting reconnection process before send");
            start_reconnection_process(transport);
        }

        return -1;
    }

    // Use the new unified exact send helper, pass NULL for stop_flag
    int result = mcp_socket_send_exact(data->sock, (const char*)data_buf, size, NULL);
    if (result != 0) { // mcp_socket_send_exact returns 0 on success, -1 on error/abort
        mcp_log_error("mcp_socket_send_exact failed (result: %d).", result);
        data->connected = false; // Mark as disconnected on send error

        // Update connection state
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

        if(transport->error_callback) {
            // Pass the last error code obtained via the utility function
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }

        // If reconnection is enabled, start reconnection process
        if (data->reconnect_enabled) {
            mcp_log_info("Starting reconnection process after send failure");
            start_reconnection_process(transport);
        }

        return -1;
    }
    // Note: The old function returned -2 for stop signal, the new one returns -1.
    // The check for stop signal is handled within mcp_socket_send_exact.
    return 0; // Success
}

// Implementation for the new vectored send function
static int tcp_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected || data->sock == MCP_INVALID_SOCKET) { // Use new invalid socket macro
        mcp_log_error("Client transport not running or connected for sendv. running=%d, connected=%d, sock=%d",
                     data->running, data->connected, (int)data->sock);

        // If reconnection is enabled and we're not already reconnecting, start reconnection
        if (data->reconnect_enabled && data->connection_state != MCP_CONNECTION_STATE_RECONNECTING) {
            mcp_log_info("Starting reconnection process before sendv");
            start_reconnection_process(transport);
        }

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
#else // POSIX
        iov[i].iov_base = (void*)buffers[i].data;
        iov[i].iov_len = buffers[i].size;
#endif
    }

    // Use the new unified vectored send function, pass NULL for stop_flag
    // Note: iovcnt is int, potential overflow if buffer_count is huge
    int result = mcp_socket_send_vectors(data->sock, iov, (int)buffer_count, NULL);
    free(iov); // Free the allocated iovec array

    if (result != 0) { // mcp_socket_send_vectors returns 0 on success, -1 on error/abort
        mcp_log_error("mcp_socket_send_vectors failed (result: %d).", result);
        data->connected = false; // Mark as disconnected on send error

        // Update connection state
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_DISCONNECTED);

        if(transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }

        // If reconnection is enabled, start reconnection process
        if (data->reconnect_enabled) {
            mcp_log_info("Starting reconnection process after sendv failure");
            start_reconnection_process(transport);
        }

        return -1;
    }
    // Note: The old function returned -2 for stop signal, the new one returns -1.
    // The check for stop signal is handled within mcp_socket_send_vectors.

    return 0; // Success
}

// Implementation for the synchronous receive function
static int tcp_client_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
     if (!transport || !transport->transport_data || !data_out || !size_out) return -1;
     mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

     *data_out = NULL;
     *size_out = 0;

     if (!data->running || !data->connected || data->sock == MCP_INVALID_SOCKET) { // Use new invalid socket macro
         mcp_log_error("Client transport not running or connected for receive.");
         return -1;
     }

     // This synchronous receive is tricky with the async receiver thread model.
     // It's generally better to use the callback mechanism.
     // A simple implementation might just block here, but that conflicts
     // with the receiver thread. For now, return an error indicating it's not supported.
     (void)timeout_ms; // Mark timeout_ms as unused to suppress warning
     mcp_log_error("Synchronous receive is not supported by this TCP client transport implementation.");
     return -1; // Not supported
 }

static void tcp_client_transport_destroy(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) return;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    tcp_client_transport_stop(transport); // Ensure stop is called

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
    free(transport); // Free the main transport struct
}

mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port) {
    return mcp_tcp_client_create_reconnect(host, port, NULL);
}

mcp_transport_t* mcp_tcp_client_create_reconnect(
    const char* host,
    uint16_t port,
    const mcp_reconnect_config_t* reconnect_config
) {
    if (!host) return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) return NULL;

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
