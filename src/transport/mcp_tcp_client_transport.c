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

    // Establish connection using the new utility function (e.g., 5 second timeout)
    data->sock = mcp_socket_connect(data->host, data->port, 5000);
    if (data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to server %s:%u", data->host, data->port);
        mcp_socket_cleanup();
        return -1;
    }
    data->connected = true; // Mark as connected after successful mcp_socket_connect

    data->running = true;

    // Start receiver thread
    if (mcp_thread_create(&data->receive_thread, tcp_client_receive_thread_func, transport) != 0) {
        mcp_log_error("Failed to create client receiver thread.");
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
        data->connected = false;
        data->running = false;
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

    mcp_log_info("Stopping TCP Client Transport...");
    data->running = false; // Signal receiver thread to stop

    // Shutdown the socket to potentially unblock the receiver thread
    if (data->sock != MCP_INVALID_SOCKET) {
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
        return -1;
    }

    // Use the new unified exact send helper, pass NULL for stop_flag
    int result = mcp_socket_send_exact(data->sock, (const char*)data_buf, size, NULL);
    if (result != 0) { // mcp_socket_send_exact returns 0 on success, -1 on error/abort
        mcp_log_error("mcp_socket_send_exact failed (result: %d).", result);
        data->connected = false; // Mark as disconnected on send error
        if(transport->error_callback) {
            // Pass the last error code obtained via the utility function
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
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
        mcp_log_error("Client transport not running or connected for sendv.");
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
        if(transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
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
    free(data);
    transport->transport_data = NULL;
    free(transport); // Free the main transport struct
}

mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port) {
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

    // Create buffer pool
    data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
     if (data->buffer_pool == NULL) {
         mcp_log_error("Failed to create buffer pool for TCP client transport.");
         free(data->host);
         free(data);
         free(transport);
         return NULL;
     }

    // Assign function pointers
    transport->start = tcp_client_transport_start;
    transport->stop = tcp_client_transport_stop;
    transport->send = tcp_client_transport_send; // Keep old send
    transport->sendv = tcp_client_transport_sendv;
    transport->receive = tcp_client_transport_receive;
    transport->destroy = tcp_client_transport_destroy;
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}
