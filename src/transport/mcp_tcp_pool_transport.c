#include "internal/tcp_pool_transport_internal.h"
#include <mcp_log.h>
#include <mcp_socket_utils.h>
#include <mcp_framing.h>
#include <stdlib.h>
#include <string.h>

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

// Implementation for the transport start function
static int tcp_pool_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport || !transport->transport_data) return -1;
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    if (data->running) return 0; // Already running

    // Store callbacks
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    // Initialize socket library if needed
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library.");
        return -1;
    }

    // Create connection pool if not already created
    if (!data->connection_pool) {
        data->connection_pool = mcp_connection_pool_create(
            data->host,
            data->port,
            data->min_connections,
            data->max_connections,
            data->idle_timeout_ms,
            data->connect_timeout_ms,
            30000,  // Health check interval: 30 seconds
            5000    // Health check timeout: 5 seconds
        );

        if (!data->connection_pool) {
            mcp_log_error("Failed to create connection pool for %s:%d", data->host, data->port);
            mcp_socket_cleanup();
            return -1;
        }
    }

    data->running = true;
    mcp_log_info("TCP Pool Transport started.");
    return 0;
}

// Implementation for the transport stop function
static int tcp_pool_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) return -1;
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    if (!data->running) return 0; // Already stopped

    mcp_log_info("Stopping TCP Pool Transport...");
    data->running = false;

    // We don't destroy the connection pool here, just mark as not running
    // The pool will be destroyed in the destroy function

    mcp_socket_cleanup();
    mcp_log_info("TCP Pool Transport stopped.");
    return 0;
}

// Implementation for the transport destroy function
static void tcp_pool_transport_destroy(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) return;
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    tcp_pool_transport_stop(transport); // Ensure stop is called

    // Destroy connection pool
    if (data->connection_pool) {
        mcp_connection_pool_destroy(data->connection_pool);
        data->connection_pool = NULL;
    }

    // Free resources
    if (data->host) {
        free(data->host);
        data->host = NULL;
    }

    // Destroy buffer pool
    if (data->buffer_pool) {
        mcp_buffer_pool_destroy(data->buffer_pool);
        data->buffer_pool = NULL;
    }

    // Free the data structure itself
    free(data);
    transport->transport_data = NULL;
    free(transport); // Free the main transport struct

    mcp_log_info("TCP Pool Transport destroyed.");
}

// Implementation for the send function
static int tcp_pool_transport_send(mcp_transport_t* transport, const void* data_buf, size_t size) {
    if (!transport || !transport->transport_data || !data_buf || size == 0) return -1;
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    if (!data->running) {
        mcp_log_error("TCP Pool Transport not running for send.");
        return -1;
    }

    // Get a connection from the pool
    socket_handle_t sock = mcp_connection_pool_get(data->connection_pool, data->request_timeout_ms);
    if (sock == INVALID_SOCKET_HANDLE) {
        mcp_log_error("Failed to get connection from pool for send.");
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }
        return -1;
    }

    // Send the data
    int result = mcp_socket_send_exact(sock, (const char*)data_buf, size, NULL);

    // Check result and release connection back to pool
    if (result != 0) {
        mcp_log_error("mcp_socket_send_exact failed (result: %d).", result);
        mcp_connection_pool_release(data->connection_pool, sock, false); // Mark as invalid
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }
        return -1;
    }

    // Wait for response
    char* response = NULL;
    size_t response_size = 0;

    // Use framing to receive the response
    uint32_t response_len = 0;
    result = mcp_framing_recv_message(sock, &response, &response_len, MAX_MESSAGE_SIZE, NULL);
    response_size = (size_t)response_len;

    // Process the response
    if (result == 0 && response != NULL) {
        // Call the message callback with the response
        if (transport->message_callback) {
            int error_code = 0;
            char* response_str = transport->message_callback(transport->callback_user_data, response, response_size, &error_code);
            // Free the response string if one was returned
            if (response_str) {
                free(response_str);
            }
        }
        free(response); // Free the allocated response buffer
    } else {
        mcp_log_error("Failed to receive response (result: %d).", result);
        mcp_connection_pool_release(data->connection_pool, sock, false); // Mark as invalid
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }
        return -1;
    }

    // Release the connection back to the pool
    mcp_connection_pool_release(data->connection_pool, sock, true); // Mark as valid
    return 0;
}

// Implementation for the vectored send function
static int tcp_pool_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !transport->transport_data || !buffers || buffer_count == 0) return -1;
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    if (!data->running) {
        mcp_log_error("TCP Pool Transport not running for sendv.");
        return -1;
    }

    // Get a connection from the pool
    socket_handle_t sock = mcp_connection_pool_get(data->connection_pool, data->request_timeout_ms);
    if (sock == INVALID_SOCKET_HANDLE) {
        mcp_log_error("Failed to get connection from pool for sendv.");
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }
        return -1;
    }

    // Convert mcp_buffer_t to platform-specific mcp_iovec_t
    mcp_iovec_t* iov = (mcp_iovec_t*)malloc(buffer_count * sizeof(mcp_iovec_t));
    if (!iov) {
        mcp_log_error("Failed to allocate iovec array for sendv.");
        mcp_connection_pool_release(data->connection_pool, sock, true); // Mark as valid
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

    // Send the data
    int result = mcp_socket_send_vectors(sock, iov, (int)buffer_count, NULL);
    free(iov); // Free the allocated iovec array

    // Check result
    if (result != 0) {
        mcp_log_error("mcp_socket_send_vectors failed (result: %d).", result);
        mcp_connection_pool_release(data->connection_pool, sock, false); // Mark as invalid
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }
        return -1;
    }

    // Wait for response
    char* response = NULL;
    size_t response_size = 0;

    // Use framing to receive the response
    uint32_t response_len = 0;
    result = mcp_framing_recv_message(sock, &response, &response_len, MAX_MESSAGE_SIZE, NULL);
    response_size = (size_t)response_len;

    // Process the response
    if (result == 0 && response != NULL) {
        // Call the message callback with the response
        if (transport->message_callback) {
            int error_code = 0;
            char* response_str = transport->message_callback(transport->callback_user_data, response, response_size, &error_code);
            // Free the response string if one was returned
            if (response_str) {
                free(response_str);
            }
        }
        free(response); // Free the allocated response buffer
    } else {
        mcp_log_error("Failed to receive response (result: %d).", result);
        mcp_connection_pool_release(data->connection_pool, sock, false); // Mark as invalid
        if (transport->error_callback) {
            transport->error_callback(transport->callback_user_data, mcp_socket_get_last_error());
        }
        return -1;
    }

    // Release the connection back to the pool
    mcp_connection_pool_release(data->connection_pool, sock, true); // Mark as valid
    return 0;
}

// Implementation for the synchronous receive function
static int tcp_pool_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    if (!transport || !transport->transport_data || !data_out || !size_out) return -1;
    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)transport->transport_data;

    *data_out = NULL;
    *size_out = 0;

    if (!data->running) {
        mcp_log_error("TCP Pool Transport not running for receive.");
        return -1;
    }

    // This synchronous receive is not typically used with the pool transport
    // since we handle the request-response cycle in the send/sendv functions.
    // Return an error indicating it's not supported.
    (void)timeout_ms; // Mark timeout_ms as unused to suppress warning
    mcp_log_error("Synchronous receive is not supported by TCP Pool Transport.");
    return -1; // Not supported
}

// Create a TCP pool transport instance
mcp_transport_t* mcp_tcp_pool_transport_create(
    const char* host,
    uint16_t port,
    size_t min_connections,
    size_t max_connections,
    int idle_timeout_ms,
    int connect_timeout_ms,
    int request_timeout_ms
) {
    if (!host) return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) return NULL;

    mcp_tcp_pool_transport_data_t* data = (mcp_tcp_pool_transport_data_t*)calloc(1, sizeof(mcp_tcp_pool_transport_data_t));
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
    data->min_connections = min_connections;
    data->max_connections = max_connections;
    data->idle_timeout_ms = idle_timeout_ms;
    data->connect_timeout_ms = connect_timeout_ms;
    data->request_timeout_ms = request_timeout_ms;
    data->running = false;
    data->connection_pool = NULL;

    // Create buffer pool
    data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
    if (data->buffer_pool == NULL) {
        mcp_log_error("Failed to create buffer pool for TCP pool transport.");
        free(data->host);
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport type to client
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;

    // Initialize client operations
    transport->client.start = tcp_pool_transport_start;
    transport->client.stop = tcp_pool_transport_stop;
    transport->client.destroy = tcp_pool_transport_destroy;
    transport->client.send = tcp_pool_transport_send;
    transport->client.sendv = tcp_pool_transport_sendv;
    transport->client.receive = tcp_pool_transport_receive;

    // Set transport data and initialize callbacks
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}
