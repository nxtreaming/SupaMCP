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
    if (transport == NULL || transport->transport_data == NULL)
        return -1;

    // Store callbacks IN THE MAIN TRANSPORT STRUCT
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (data->running) {
        mcp_log_warn("TCP server transport is already running.");
        return 0;
    }

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library.");
        return -1;
    }

    // Create listening socket using the new utility function
    // SOMAXCONN is a reasonable default backlog
    data->listen_socket = mcp_socket_create_listener(data->host, data->port, SOMAXCONN);
    if (data->listen_socket == MCP_INVALID_SOCKET) {
        mcp_socket_cleanup();
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
        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->running = false;
        return -1;
    }
    // Set read end to non-blocking
    int flags = fcntl(data->stop_pipe[0], F_GETFL, 0);
    if (flags == -1 || fcntl(data->stop_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        mcp_log_error("Failed to set stop pipe read end non-blocking: %s", strerror(errno));
        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }
#endif

    // Start the cleanup thread
    data->cleanup_running = true;
    if (mcp_thread_create(&data->cleanup_thread, tcp_cleanup_thread_func, data) != 0) {
        mcp_log_error("Failed to create cleanup thread.");
        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

    // Start accept thread using abstraction
    if (mcp_thread_create(&data->accept_thread, tcp_accept_thread_func, transport) != 0) {
        mcp_log_error("Failed to create accept thread.");

        // Stop the cleanup thread
        data->cleanup_running = false;
        mcp_thread_join(data->cleanup_thread, NULL);

        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

    mcp_log_info("TCP Transport started listening on %s:%d with thread pool of %d threads",
                data->host, data->port, DEFAULT_THREAD_POOL_SIZE);
    return 0;
}

static int tcp_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL)
        return -1;

    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (!data->running)
        return 0;

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
    if (data->accept_thread) {
        mcp_thread_join(data->accept_thread, NULL);
        data->accept_thread = 0;
    }
    mcp_log_debug("Accept thread stopped.");

    // Stop the cleanup thread
    if (data->cleanup_running) {
        data->cleanup_running = false;
        if (data->cleanup_thread) {
            mcp_thread_join(data->cleanup_thread, NULL);
            data->cleanup_thread = 0;
        }
        mcp_log_debug("Cleanup thread stopped.");
    }

    // Signal all client connections to stop
    mcp_mutex_lock(data->client_mutex);
    for (int i = 0; i < data->max_clients; ++i) {
        // Check if the slot is not INACTIVE (i.e., it's INITIALIZING or ACTIVE)
        if (data->clients[i].state != CLIENT_STATE_INACTIVE) {
            data->clients[i].should_stop = true; // Signal handler thread to stop

            // Shutdown the socket to unblock recv
#ifdef _WIN32
            shutdown(data->clients[i].socket, SD_BOTH);
#else
            shutdown(data->clients[i].socket, SHUT_RDWR);
#endif

            // Close the socket
            mcp_socket_close(data->clients[i].socket);
            data->clients[i].socket = MCP_INVALID_SOCKET;

            // Mark as inactive
            data->clients[i].state = CLIENT_STATE_INACTIVE;
        }
    }
    mcp_mutex_unlock(data->client_mutex);

    // Wait for all thread pool tasks to complete (with shorter timeout)
    if (data->thread_pool) {
        mcp_log_debug("Waiting for thread pool tasks to complete (timeout: 2 seconds)...");
        mcp_thread_pool_wait(data->thread_pool, 2000); // 2 second timeout

        // Force thread pool shutdown after timeout
        mcp_log_debug("Destroying thread pool...");
        mcp_thread_pool_destroy(data->thread_pool);
        data->thread_pool = NULL;
    }

    // Clean up mutex and stop pipe
    mcp_mutex_destroy(data->client_mutex);
    data->client_mutex = NULL;

    mcp_log_info("TCP Transport stopped. Stats: %llu connections, %llu messages received, %llu messages sent",
                data->stats.total_connections, data->stats.messages_received, data->stats.messages_sent);

    // Cleanup socket library
    mcp_socket_cleanup();

    return 0;
}

// Note: Server transport does not implement send or sendv functions
// Responses are sent directly by the client handler threads
static void tcp_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) return;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    mcp_log_info("Destroying TCP transport");

    // Ensure everything is stopped and cleaned (including mutex)
    tcp_transport_stop(transport);

    // Thread pool should already be destroyed in tcp_transport_stop
    // This is just a safety check
    if (data->thread_pool) {
        mcp_log_warn("Thread pool still exists in destroy function, destroying it now");
        mcp_thread_pool_destroy(data->thread_pool);
        data->thread_pool = NULL;
    }

    // Free the client array
    if (data->clients) {
        free(data->clients);
        data->clients = NULL;
    }

    // Destroy the buffer pool
    if (data->buffer_pool) {
        mcp_buffer_pool_destroy(data->buffer_pool);
        data->buffer_pool = NULL;
    }

    // Free the host string
    if (data->host) {
        free(data->host);
        data->host = NULL;
    }

    // Free the transport data
    free(data);
    transport->transport_data = NULL;

    // Free the transport struct itself
    free(transport);

    mcp_log_info("TCP transport destroyed");
}

mcp_transport_t* mcp_transport_tcp_create(
    const char* host,
    uint16_t port,
    uint32_t idle_timeout_ms) {
    if (host == NULL)
        return NULL;

    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL)
        return NULL;

    mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)calloc(1, sizeof(mcp_tcp_transport_data_t));
    if (tcp_data == NULL) {
        free(transport);
        return NULL;
    }

    tcp_data->host = mcp_strdup(host);
    if (tcp_data->host == NULL) {
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Initialize basic properties
    tcp_data->port = port;
    tcp_data->idle_timeout_ms = idle_timeout_ms;
    tcp_data->listen_socket = MCP_INVALID_SOCKET;
    tcp_data->running = false;
    tcp_data->cleanup_running = false;
    tcp_data->max_clients = MAX_TCP_CLIENTS;
    tcp_data->thread_pool = NULL;

#ifndef _WIN32
    tcp_data->stop_pipe[0] = -1; // Initialize pipe FDs
    tcp_data->stop_pipe[1] = -1;
#endif

    // Initialize statistics
    tcp_stats_init(&tcp_data->stats);

    // Allocate dynamic client array
    tcp_data->clients = (tcp_client_connection_t*)calloc(tcp_data->max_clients, sizeof(tcp_client_connection_t));
    if (tcp_data->clients == NULL) {
        mcp_log_error("Failed to allocate client array for TCP transport.");
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Initialize all client slots
    for (int i = 0; i < tcp_data->max_clients; i++) {
        tcp_data->clients[i].state = CLIENT_STATE_INACTIVE;
        tcp_data->clients[i].socket = MCP_INVALID_SOCKET;
        tcp_data->clients[i].client_index = i;
    }

    // Create the client mutex
    tcp_data->client_mutex = mcp_mutex_create();
    if (tcp_data->client_mutex == NULL) {
        mcp_log_error("Failed to create client mutex for TCP transport.");
        free(tcp_data->clients);
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Create the buffer pool
    tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
    if (tcp_data->buffer_pool == NULL) {
        mcp_log_error("Failed to create buffer pool for TCP transport.");
        mcp_mutex_destroy(tcp_data->client_mutex);
        free(tcp_data->clients);
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Create the thread pool
    tcp_data->thread_pool = mcp_thread_pool_create(DEFAULT_THREAD_POOL_SIZE, CONNECTION_QUEUE_SIZE);
    if (tcp_data->thread_pool == NULL) {
        mcp_log_error("Failed to create thread pool for TCP transport.");
        mcp_buffer_pool_destroy(tcp_data->buffer_pool);
        mcp_mutex_destroy(tcp_data->client_mutex);
        free(tcp_data->clients);
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Set transport type to server
    transport->type = MCP_TRANSPORT_TYPE_SERVER;

    // Initialize server operations
    transport->server.start = tcp_transport_start;
    transport->server.stop = tcp_transport_stop;
    transport->server.destroy = tcp_transport_destroy;

    // Set transport data and initialize callbacks
    transport->transport_data = tcp_data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    mcp_log_info("TCP transport created with %d max clients, %d thread pool size, and %d ms idle timeout",
                tcp_data->max_clients, DEFAULT_THREAD_POOL_SIZE, idle_timeout_ms);

    return transport;
}
