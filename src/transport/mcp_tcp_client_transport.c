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


// --- Static Transport Interface Functions ---

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

    initialize_winsock_client(); // Initialize Winsock if on Windows

    // Establish connection
    if (connect_to_server(data) != 0) {
        cleanup_winsock_client();
        return -1;
    }

    data->running = true;

    // Start receiver thread
    if (mcp_thread_create(&data->receive_thread, tcp_client_receive_thread_func, transport) != 0) { // Corrected function name
        mcp_log_error("Failed to create client receiver thread.");
        close_socket(data->sock);
        data->sock = INVALID_SOCKET_VAL;
        data->connected = false;
        data->running = false;
        cleanup_winsock_client();
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
    if (data->sock != INVALID_SOCKET_VAL) {
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
    if (data->sock != INVALID_SOCKET_VAL) {
        close_socket(data->sock);
        data->sock = INVALID_SOCKET_VAL;
    }
    data->connected = false;

    cleanup_winsock_client(); // Cleanup Winsock if on Windows
    mcp_log_info("TCP Client Transport stopped.");
    return 0;
}

// Implementation for the old send function
static int tcp_client_transport_send(mcp_transport_t* transport, const void* data_buf, size_t size) {
    if (!transport || !transport->transport_data || !data_buf || size == 0) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected || data->sock == INVALID_SOCKET_VAL) {
        mcp_log_error("Client transport not running or connected for send.");
        return -1;
    }

    // Use the client-specific exact send helper
    int result = send_exact_client(data->sock, (const char*)data_buf, size, &data->running);
    if (result == -1) {
        mcp_log_error("send_exact_client failed.");
        // Consider triggering error callback or attempting reconnect?
        data->connected = false; // Mark as disconnected on send error
        if(transport->error_callback) {
            transport->error_callback(transport->callback_user_data, sock_errno);
        }
        return -1;
    } else if (result == -2) {
        mcp_log_info("Send interrupted by stop signal.");
        return -1; // Treat stop signal as error for send
    }
    return 0; // Success
}

// Implementation for the new vectored send function
static int tcp_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected || data->sock == INVALID_SOCKET_VAL) {
        mcp_log_error("Client transport not running or connected for sendv.");
        return -1;
    }

    size_t total_len = 0;
    for (size_t i = 0; i < buffer_count; ++i) {
        total_len += buffers[i].size;
    }
    if (total_len == 0) return 0; // Nothing to send

    int result = -1;
#ifdef _WIN32
    // Need to convert mcp_buffer_t to WSABUF
    WSABUF* wsa_buffers = (WSABUF*)malloc(buffer_count * sizeof(WSABUF));
    if (!wsa_buffers) {
        mcp_log_error("Failed to allocate WSABUF array.");
        return -1;
    }
    for (size_t i = 0; i < buffer_count; ++i) {
        wsa_buffers[i].buf = (CHAR*)buffers[i].data; // Cast needed
        wsa_buffers[i].len = (ULONG)buffers[i].size; // Cast needed
    }
    result = send_vectors_client_windows(data->sock, wsa_buffers, (DWORD)buffer_count, total_len, &data->running);
    free(wsa_buffers);
#else // POSIX
    // Need to convert mcp_buffer_t to struct iovec
    struct iovec* iov = (struct iovec*)malloc(buffer_count * sizeof(struct iovec));
     if (!iov) {
        mcp_log_error("Failed to allocate iovec array.");
        return -1;
    }
    for (size_t i = 0; i < buffer_count; ++i) {
        iov[i].iov_base = (void*)buffers[i].data; // Cast needed for non-const
        iov[i].iov_len = buffers[i].size;
    }
    // Note: writev's iovcnt is int, potential overflow if buffer_count is huge
    result = send_vectors_client_posix(data->sock, iov, (int)buffer_count, total_len, &data->running);
    free(iov);
#endif

    if (result == -1) {
        mcp_log_error("send_vectors failed (client).");
        data->connected = false; // Mark as disconnected on send error
        if(transport->error_callback) {
            transport->error_callback(transport->callback_user_data, sock_errno);
        }
        return -1;
    } else if (result == -2) {
        mcp_log_info("Sendv interrupted by stop signal.");
        return -1; // Treat stop signal as error for send
    }

    return 0; // Success
}


// Implementation for the synchronous receive function
static int tcp_client_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
     if (!transport || !transport->transport_data || !data_out || !size_out) return -1;
     mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

     *data_out = NULL;
     *size_out = 0;

     if (!data->running || !data->connected || data->sock == INVALID_SOCKET_VAL) {
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

// --- Public Creation Function ---

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
    data->sock = INVALID_SOCKET_VAL;
    data->connected = false;
    data->running = false;
    data->receive_thread = 0; // Correct member name
    data->buffer_pool = NULL; // Initialize pool pointer

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
