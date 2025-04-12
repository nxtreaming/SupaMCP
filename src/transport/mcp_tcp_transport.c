#include "internal/transport_internal.h"
#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mcp_buffer_pool.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include <mcp_thread_pool.h>

static int tcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    // Store callbacks IN THE MAIN TRANSPORT STRUCT
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (data->running) return 0; // Already running

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library.");
        return -1;
    }

    // Create listening socket using the new utility function
    // SOMAXCONN is a reasonable default backlog
    data->listen_socket = mcp_socket_create_listener(data->host, data->port, SOMAXCONN);
    if (data->listen_socket == MCP_INVALID_SOCKET) {
        // Error logged within mcp_socket_create_listener
        mcp_socket_cleanup(); // Cleanup on failure
        return -1;
    }

    data->running = true;

    // Initialize mutex using abstraction
    data->client_mutex = mcp_mutex_create();
    if (data->client_mutex == NULL) {
        mcp_log_error("Mutex creation failed.");
        mcp_socket_close(data->listen_socket);
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

#ifndef _WIN32
    // Create stop pipe (POSIX only)
    data->stop_pipe[0] = -1; // Initialize FDs
    data->stop_pipe[1] = -1;
    if (pipe(data->stop_pipe) != 0) {
        mcp_log_error("Stop pipe creation failed: %s", strerror(errno));
        close_socket(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex); // Use abstracted destroy
        data->running = false;
        return -1;
    }
    // Set read end to non-blocking
    int flags = fcntl(data->stop_pipe[0], F_GETFL, 0);
    if (flags == -1 || fcntl(data->stop_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
         mcp_log_error("Failed to set stop pipe read end non-blocking: %s", strerror(errno));
         close_stop_pipe(data);
         mcp_socket_close(data->listen_socket);
         mcp_mutex_destroy(data->client_mutex); // Use abstracted destroy
         data->running = false;
         mcp_socket_cleanup();
         return -1;
    }
#endif

    // Start accept thread using abstraction
    if (mcp_thread_create(&data->accept_thread, tcp_accept_thread_func, transport) != 0) {
        mcp_log_error("Failed to create accept thread.");
        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex); // Use abstracted destroy
#ifndef _WIN32
        close_stop_pipe(data);
#endif
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

    mcp_log_info("TCP Transport started listening on %s:%d", data->host, data->port);
    return 0;
}

static int tcp_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (!data->running) return 0;

    mcp_log_info("Stopping TCP Transport...");
    data->running = false;

    // Signal and close the listening socket/pipe to interrupt accept/select
#ifdef _WIN32
    // Close the listening socket to interrupt accept()
    if (data->listen_socket != MCP_INVALID_SOCKET) {
        shutdown(data->listen_socket, SD_BOTH); // Try shutdown first
        mcp_socket_close(data->listen_socket);
        data->listen_socket = MCP_INVALID_SOCKET;
    }
#else
    // Write to the stop pipe to interrupt select() or poll() in accept thread
    if (data->stop_pipe[1] != -1) {
        char dummy = 's';
        ssize_t written = write(data->stop_pipe[1], &dummy, 1);
        if (written <= 0) {
             mcp_log_warn("Failed to write to stop pipe during stop: %s", strerror(errno));
        }
        // No need to close write end immediately, close_stop_pipe handles it
    }
    // Also close the listening socket
    if (data->listen_socket != MCP_INVALID_SOCKET) {
        shutdown(data->listen_socket, SHUT_RDWR); // Try shutdown first
        mcp_socket_close(data->listen_socket);
        data->listen_socket = MCP_INVALID_SOCKET;
    }
#endif

    // Wait for the accept thread to finish using abstraction
    if (data->accept_thread) { // Check if thread handle is valid
        mcp_thread_join(data->accept_thread, NULL);
        data->accept_thread = 0; // Reset handle after join
    }
    mcp_log_debug("Accept thread stopped.");

    // Signal handler threads to stop and close connections
    mcp_mutex_lock(data->client_mutex);
    for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
        // Check if the slot is not INACTIVE (i.e., it's INITIALIZING or ACTIVE)
        if (data->clients[i].state != CLIENT_STATE_INACTIVE) {
            data->clients[i].should_stop = true; // Signal handler thread to stop
            // Shutdown might be cleaner than just close_socket to unblock recv
#ifdef _WIN32
            shutdown(data->clients[i].socket, SD_BOTH);
#else
            shutdown(data->clients[i].socket, SHUT_RDWR);
#endif
            // Note: Closing the socket here might be redundant if shutdown works,
            // but it ensures the resource is released. The handler thread will
            // also call close_socket upon exiting its loop.

            // Wait for handler threads? mcp_thread_join requires the handle.
            // If threads are detached (common for handlers), joining isn't possible.
            // Assuming handler threads exit cleanly upon socket error/close or should_stop flag.
            // if (data->clients[i].thread_handle) {
            //     mcp_thread_join(data->clients[i].thread_handle, NULL);
            //     data->clients[i].thread_handle = 0; // Reset handle
            // }
        } // end if(data->clients[i].state != CLIENT_STATE_INACTIVE)
    } // end for loop
    mcp_mutex_unlock(data->client_mutex);

    // Clean up mutex and stop pipe
    mcp_mutex_destroy(data->client_mutex);
    data->client_mutex = NULL;
#ifndef _WIN32
    close_stop_pipe(data); // Close pipe FDs
#endif

    mcp_log_info("TCP Transport stopped.");

    // Cleanup socket library
    mcp_socket_cleanup();

    return 0;
}

// Server transport send function - STUB
// Actual sending happens in client handler thread
static int tcp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL || size == 0) {
        mcp_log_error("Invalid parameters in tcp_transport_send");
        return -1;
    }
    mcp_log_debug("Server transport send function called (stub) with %zu bytes", size);
    // This function is a stub because the server transport doesn't know which client to send to.
    // The client handler thread is responsible for sending responses back to its specific client.
    return 0; // Indicate success as no direct action is taken here.
}

// Server transport vectored send function - STUB
// Actual sending happens in client handler thread
static int tcp_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (transport == NULL || transport->transport_data == NULL || buffers == NULL || buffer_count == 0) {
        mcp_log_error("Invalid parameters in tcp_transport_sendv");
        return -1;
    }
    size_t total_size = 0;
    for(size_t i = 0; i < buffer_count; ++i) {
        total_size += buffers[i].size;
    }
    mcp_log_debug("Server transport sendv function called (stub) with %zu buffers, total size %zu bytes", buffer_count, total_size);
    // This function is a stub for the same reason as tcp_transport_send.
    return 0; // Indicate success as no direct action is taken here.
}

static void tcp_transport_destroy(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) return;
     mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

     tcp_transport_stop(transport); // Ensure everything is stopped and cleaned (including mutex)

     free(data->host);
     // Destroy the buffer pool
     mcp_buffer_pool_destroy(data->buffer_pool);
     free(data);
     transport->transport_data = NULL;
     // Generic destroy will free the transport struct itself
     free(transport); // Free the main transport struct allocated in create
}

mcp_transport_t* mcp_transport_tcp_create(
    const char* host,
    uint16_t port,
    uint32_t idle_timeout_ms) {
    if (host == NULL) return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) return NULL;

    mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)calloc(1, sizeof(mcp_tcp_transport_data_t)); // Use calloc for zero-init
    if (tcp_data == NULL) {
        free(transport);
         return NULL;
     }

     tcp_data->host = mcp_strdup(host); // Use helper
     if (tcp_data->host == NULL) {
         free(tcp_data);
         free(transport);
         return NULL;
     }

     tcp_data->port = port;
     tcp_data->idle_timeout_ms = idle_timeout_ms; // Store timeout
     tcp_data->listen_socket = MCP_INVALID_SOCKET;
     tcp_data->running = false;
     tcp_data->buffer_pool = NULL; // Initialize pool pointer
     tcp_data->client_mutex = NULL; // Initialize mutex pointer
     tcp_data->accept_thread = 0;   // Initialize thread handle

     // Explicitly initialize all client slots
     for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
         tcp_data->clients[i].state = CLIENT_STATE_INACTIVE; // Initialize state
         tcp_data->clients[i].socket = MCP_INVALID_SOCKET;
         tcp_data->clients[i].thread_handle = 0; // Initialize thread handle
         // Other fields are zeroed by calloc
     }

 #ifndef _WIN32
     tcp_data->stop_pipe[0] = -1; // Initialize pipe FDs
     tcp_data->stop_pipe[1] = -1;
 #endif

     // Create the buffer pool
     tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
     if (tcp_data->buffer_pool == NULL) {
         mcp_log_error("Failed to create buffer pool for TCP transport.");
         free(tcp_data->host);
         free(tcp_data);
         free(transport);
         return NULL;
     }

     // Initialize function pointers
    transport->start = tcp_transport_start;
    transport->stop = tcp_transport_stop;
    transport->send = tcp_transport_send; // Keep stub send
    transport->sendv = tcp_transport_sendv; // Assign new stub sendv
    transport->receive = NULL; // Set receive to NULL, not used by server transport
    transport->destroy = tcp_transport_destroy;
    transport->transport_data = tcp_data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL; // Initialize error callback

    return transport;
}
