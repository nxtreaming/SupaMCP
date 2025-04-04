#include "internal/tcp_client_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes (minimal needed here)
#ifdef _WIN32
// Included via internal header
#else
#include <pthread.h>
#include <unistd.h>
#endif

// --- Static Transport Interface Functions ---

static int tcp_client_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
     if (transport == NULL || transport->transport_data == NULL) return -1;
     mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

     // Store callbacks and user data in the generic transport handle
     transport->message_callback = message_callback;
     transport->callback_user_data = user_data;
     transport->error_callback = error_callback;

    if (data->running) return 0; // Already running

    initialize_winsock_client(); // Use helper from socket utils

    // Attempt to connect using helper from socket utils
    if (connect_to_server(data) != 0) {
        cleanup_winsock_client(); // Cleanup winsock if connect failed
        return -1; // Connection failed
    }

    data->running = true;

    // Start receive thread using function from receiver module
#ifdef _WIN32
    data->receive_thread = CreateThread(NULL, 0, tcp_client_receive_thread_func, transport, 0, NULL);
    if (data->receive_thread == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create client receive thread (Error: %lu).", GetLastError());
        close_socket(data->sock);
        data->sock = INVALID_SOCKET_VAL;
        data->connected = false;
        data->running = false;
        cleanup_winsock_client(); // Cleanup winsock
        return -1;
    }
#else
     if (pthread_create(&data->receive_thread, NULL, tcp_client_receive_thread_func, transport) != 0) {
        char err_buf[128];
         if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to create client receive thread: %s", err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "Failed to create client receive thread: %d (strerror_r failed)", errno);
         }
        close_socket(data->sock);
        data->sock = INVALID_SOCKET_VAL;
        data->connected = false;
        data->running = false;
        // No winsock cleanup needed for POSIX here
        return -1;
    }
#endif

    log_message(LOG_LEVEL_INFO, "TCP Client Transport started for %s:%d", data->host, data->port);
    return 0;
}

static int tcp_client_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running) return 0;

    log_message(LOG_LEVEL_INFO, "Stopping TCP Client Transport...");
    data->running = false; // Signal thread to stop

    // Close the socket to interrupt recv()
    if (data->sock != INVALID_SOCKET_VAL) {
#ifdef _WIN32
        shutdown(data->sock, SD_BOTH);
#else
        shutdown(data->sock, SHUT_RDWR);
#endif
        close_socket(data->sock);
        data->sock = INVALID_SOCKET_VAL;
    }
    data->connected = false;

    // Wait for the receive thread to finish
#ifdef _WIN32
    if (data->receive_thread) {
        WaitForSingleObject(data->receive_thread, 2000); // Wait 2 seconds
        CloseHandle(data->receive_thread);
        data->receive_thread = NULL;
    }
#else
    if (data->receive_thread) {
        pthread_join(data->receive_thread, NULL);
        data->receive_thread = 0;
    }
#endif
    log_message(LOG_LEVEL_DEBUG, "Client receive thread stopped.");

    log_message(LOG_LEVEL_INFO, "TCP Client Transport stopped.");

    // Cleanup Winsock on Windows
    cleanup_winsock_client(); // Use helper from socket utils

    return 0;
}

// Send data with length prefix framing (transport adds the frame)
static int tcp_client_transport_send(mcp_transport_t* transport, const void* payload_data, size_t payload_size) {
    if (transport == NULL || transport->transport_data == NULL || payload_data == NULL || payload_size == 0) {
        return -1;
    }
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    if (!data->running || !data->connected) {
        log_message(LOG_LEVEL_ERROR, "Cannot send: TCP client transport not running or not connected.");
        return -1;
    }

    // Check payload size limit
    if (payload_size > MAX_MCP_MESSAGE_SIZE) {
        log_message(LOG_LEVEL_ERROR, "Cannot send: Payload size (%zu) exceeds limit (%d).", payload_size, MAX_MCP_MESSAGE_SIZE);
        return -1;
    }

    // Send the payload directly (length prefix is already added by the caller)
    // The 'payload_data' received here already contains [Length][JSON Data]
    int send_status = send_exact_client(data->sock, (const char*)payload_data, payload_size, &data->running);

    if (send_status != 0) {
        log_message(LOG_LEVEL_ERROR, "send_exact_client failed (status: %d)", send_status);
        // Assume connection is broken if send failed
        data->connected = false;
        // Consider stopping the transport or signaling error? For now, just return error.
        if (transport->error_callback) {
             transport->error_callback(transport->callback_user_data, MCP_ERROR_TRANSPORT_ERROR);
        }
        return -1;
    }

    return 0; // Success
}


static void tcp_client_transport_destroy(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) return;
     mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

     tcp_client_transport_stop(transport); // Ensure everything is stopped and cleaned

     free(data->host);
     // Destroy the buffer pool
     mcp_buffer_pool_destroy(data->buffer_pool);
     free(data);
     transport->transport_data = NULL;
     // Generic destroy will free the transport struct itself
}


// --- Public Creation Function ---

mcp_transport_t* mcp_transport_tcp_client_create(const char* host, uint16_t port) {
    if (host == NULL) return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) return NULL;

    mcp_tcp_client_transport_data_t* tcp_data = (mcp_tcp_client_transport_data_t*)calloc(1, sizeof(mcp_tcp_client_transport_data_t)); // Use calloc for zero-init
    if (tcp_data == NULL) {
        free(transport);
         return NULL;
     }

     // Use mcp_strdup from mcp_types.h
     tcp_data->host = mcp_strdup(host);
     if (tcp_data->host == NULL) {
         free(tcp_data);
         free(transport);
         return NULL;
     }

     tcp_data->port = port;
     tcp_data->sock = INVALID_SOCKET_VAL; // Use defined constant
     tcp_data->running = false;
     tcp_data->connected = false;
     tcp_data->transport_handle = transport; // Link back
     tcp_data->buffer_pool = NULL; // Initialize pool pointer

     // Create the buffer pool
     tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
     if (tcp_data->buffer_pool == NULL) {
         log_message(LOG_LEVEL_ERROR, "Failed to create buffer pool for TCP client transport.");
         free(tcp_data->host);
         free(tcp_data);
         free(transport);
         return NULL;
     }

     // Initialize function pointers
    transport->start = tcp_client_transport_start;
    transport->stop = tcp_client_transport_stop;
    transport->send = tcp_client_transport_send; // Use the client send function
    transport->receive = NULL; // Synchronous receive not supported/used by client transport
     transport->destroy = tcp_client_transport_destroy;
     transport->transport_data = tcp_data;
     transport->message_callback = NULL; // Set by start
     transport->callback_user_data = NULL; // Set by start
     transport->error_callback = NULL; // Set by start

     return transport;
 }
