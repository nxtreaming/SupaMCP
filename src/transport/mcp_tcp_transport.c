#include "internal/transport_internal.h"
#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mcp_buffer_pool.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

/**
 * @file mcp_tcp_transport.c
 * @brief Implementation of TCP server transport functionality.
 *
 * This file implements the TCP server transport, which listens for incoming
 * connections, accepts them, and handles client messages using a thread pool.
 * It supports multiple concurrent clients and provides efficient message
 * processing with buffer pooling.
 */

// Note: These functions are declared in tcp_transport_internal.h
// and implemented in mcp_tcp_server_utils.c
// We're just using them here, not redefining them

/**
 * @brief Starts the TCP server transport.
 *
 * This function initializes the socket library, creates a listening socket,
 * and starts the accept and cleanup threads. It also initializes the necessary
 * synchronization primitives.
 *
 * @param transport The transport handle
 * @param message_callback Callback function for received messages
 * @param user_data User data to pass to callbacks
 * @param error_callback Callback function for transport errors
 * @return 0 on success, -1 on error
 */
static int tcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    // Validate parameters
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid transport handle in start function");
        return -1;
    }

    // Store callbacks in the main transport struct
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    // Check if already running
    if (data->running) {
        mcp_log_warn("TCP server transport already running");
        return 0;
    }

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library");
        return -1;
    }

    // Create listening socket using the utility function
    // SOMAXCONN is a reasonable default backlog for most operating systems
    data->listen_socket = mcp_socket_create_listener(data->host, data->port, SOMAXCONN);
    if (data->listen_socket == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to create listening socket on %s:%d", data->host, data->port);
        mcp_socket_cleanup();
        return -1;
    }

    // Mark as running
    data->running = true;

    // Initialize client mutex if not already created
    if (data->client_mutex == NULL) {
        data->client_mutex = mcp_mutex_create();
        if (data->client_mutex == NULL) {
            mcp_log_error("Failed to create client mutex");
            mcp_socket_close(data->listen_socket);
            data->running = false;
            mcp_socket_cleanup();
            return -1;
        }
    }

#ifndef _WIN32
    // Create stop pipe (POSIX only) for signaling the accept thread to stop
    data->stop_pipe[0] = -1; // Initialize file descriptors
    data->stop_pipe[1] = -1;

    if (pipe(data->stop_pipe) != 0) {
        mcp_log_error("Failed to create stop pipe: %s", strerror(errno));
        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->client_mutex = NULL;
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

    // Set read end to non-blocking mode
    int flags = fcntl(data->stop_pipe[0], F_GETFL, 0);
    if (flags == -1 || fcntl(data->stop_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        mcp_log_error("Failed to set stop pipe to non-blocking mode: %s", strerror(errno));
        close(data->stop_pipe[0]);
        close(data->stop_pipe[1]);
        data->stop_pipe[0] = -1;
        data->stop_pipe[1] = -1;
        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->client_mutex = NULL;
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }
#endif

    // Start the cleanup thread for removing idle connections
    data->cleanup_running = true;
    if (mcp_thread_create(&data->cleanup_thread, tcp_cleanup_thread_func, data) != 0) {
        mcp_log_error("Failed to create cleanup thread");

#ifndef _WIN32
        // Close the stop pipe
        if (data->stop_pipe[0] != -1) close(data->stop_pipe[0]);
        if (data->stop_pipe[1] != -1) close(data->stop_pipe[1]);
        data->stop_pipe[0] = -1;
        data->stop_pipe[1] = -1;
#endif

        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->client_mutex = NULL;
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

    // Start the accept thread for handling incoming connections
    if (mcp_thread_create(&data->accept_thread, tcp_accept_thread_func, transport) != 0) {
        mcp_log_error("Failed to create accept thread");

        // Stop the cleanup thread
        data->cleanup_running = false;
        mcp_thread_join(data->cleanup_thread, NULL);
        data->cleanup_thread = 0;

#ifndef _WIN32
        // Close the stop pipe
        if (data->stop_pipe[0] != -1) close(data->stop_pipe[0]);
        if (data->stop_pipe[1] != -1) close(data->stop_pipe[1]);
        data->stop_pipe[0] = -1;
        data->stop_pipe[1] = -1;
#endif

        mcp_socket_close(data->listen_socket);
        mcp_mutex_destroy(data->client_mutex);
        data->client_mutex = NULL;
        data->running = false;
        mcp_socket_cleanup();
        return -1;
    }

    mcp_log_info("TCP server transport started on %s:%d (thread pool: %d threads)",
                data->host, data->port, DEFAULT_THREAD_POOL_SIZE);
    return 0;
}

/**
 * @brief Stops the TCP server transport.
 *
 * This function stops the accept and cleanup threads, closes all client
 * connections, and cleans up resources. It also logs statistics about
 * the transport's operation.
 *
 * @param transport The transport handle
 * @return 0 on success, -1 on error
 */
static int tcp_transport_stop(mcp_transport_t* transport) {
    // Validate parameters
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid transport handle in stop function");
        return -1;
    }

    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    // Check if already stopped
    if (!data->running) {
        mcp_log_debug("TCP server transport already stopped");
        return 0;
    }

    mcp_log_info("Stopping TCP server transport...");

    // Mark as not running to signal threads to exit
    data->running = false;

    // Signal and close the listening socket/pipe to interrupt accept/select
#ifdef _WIN32
    // On Windows, close the listening socket to interrupt accept()
    if (data->listen_socket != MCP_INVALID_SOCKET) {
        // Try shutdown first to gracefully close any pending connections
        shutdown(data->listen_socket, SD_BOTH);
        mcp_socket_close(data->listen_socket);
        data->listen_socket = MCP_INVALID_SOCKET;
    }
#else
    // On POSIX systems, write to the stop pipe to interrupt select() or poll()
    if (data->stop_pipe[1] != -1) {
        char dummy = 's';
        ssize_t written = write(data->stop_pipe[1], &dummy, 1);
        if (written <= 0) {
            mcp_log_warn("Failed to write to stop pipe: %s", strerror(errno));
        }
        // No need to close write end immediately, we'll close it later
    }

    // Also close the listening socket
    if (data->listen_socket != MCP_INVALID_SOCKET) {
        shutdown(data->listen_socket, SHUT_RDWR);
        mcp_socket_close(data->listen_socket);
        data->listen_socket = MCP_INVALID_SOCKET;
    }
#endif

    // Wait for the accept thread to finish
    if (data->accept_thread) {
        mcp_log_debug("Waiting for accept thread to finish...");
        mcp_thread_join(data->accept_thread, NULL);
        data->accept_thread = 0;
        mcp_log_debug("Accept thread stopped");
    }

    // Stop the cleanup thread
    if (data->cleanup_running) {
        mcp_log_debug("Stopping cleanup thread...");
        data->cleanup_running = false;

        if (data->cleanup_thread) {
            mcp_thread_join(data->cleanup_thread, NULL);
            data->cleanup_thread = 0;
            mcp_log_debug("Cleanup thread stopped");
        }
    }

    // Close the stop pipe (POSIX only)
#ifndef _WIN32
    if (data->stop_pipe[0] != -1) {
        close(data->stop_pipe[0]);
        data->stop_pipe[0] = -1;
    }
    if (data->stop_pipe[1] != -1) {
        close(data->stop_pipe[1]);
        data->stop_pipe[1] = -1;
    }
#endif

    // Signal all client connections to stop
    if (data->client_mutex) {
        mcp_mutex_lock(data->client_mutex);

        for (int i = 0; i < data->max_clients; ++i) {
            // Check if the slot is active
            if (data->clients[i].state != CLIENT_STATE_INACTIVE) {
                // Signal handler thread to stop
                data->clients[i].should_stop = true;

                // Shutdown the socket to unblock recv operations
#ifdef _WIN32
                shutdown(data->clients[i].socket, SD_BOTH);
#else
                shutdown(data->clients[i].socket, SHUT_RDWR);
#endif

                // Close the socket
                if (data->clients[i].socket != MCP_INVALID_SOCKET) {
                    mcp_socket_close(data->clients[i].socket);
                    data->clients[i].socket = MCP_INVALID_SOCKET;
                }

                // Mark as inactive
                data->clients[i].state = CLIENT_STATE_INACTIVE;

                mcp_log_debug("Closed client connection %d", i);
            }
        }

        mcp_mutex_unlock(data->client_mutex);
    }

    // Wait for all thread pool tasks to complete with a timeout
    if (data->thread_pool) {
        const int THREAD_POOL_WAIT_TIMEOUT_MS = 2000; // 2 seconds

        mcp_log_debug("Waiting for thread pool tasks to complete (timeout: %d ms)...",
                     THREAD_POOL_WAIT_TIMEOUT_MS);

        mcp_thread_pool_wait(data->thread_pool, THREAD_POOL_WAIT_TIMEOUT_MS);

        // Force thread pool shutdown after timeout
        mcp_log_debug("Destroying thread pool...");
        mcp_thread_pool_destroy(data->thread_pool);
        data->thread_pool = NULL;
    }

    // Clean up mutex
    if (data->client_mutex) {
        mcp_mutex_destroy(data->client_mutex);
        data->client_mutex = NULL;
    }

    // Log statistics
    mcp_log_info("TCP server transport stopped. Stats: %llu connections, %llu messages received, %llu messages sent",
                data->stats.total_connections, data->stats.messages_received, data->stats.messages_sent);

    // Cleanup socket library
    mcp_socket_cleanup();

    return 0;
}

/**
 * @brief Destroys the TCP server transport.
 *
 * This function stops the transport if it's running, frees all allocated
 * resources, and destroys the transport handle.
 *
 * Note: Server transport does not implement send or sendv functions.
 * Responses are sent directly by the client handler threads.
 *
 * @param transport The transport handle to destroy
 */
static void tcp_transport_destroy(mcp_transport_t* transport) {
    // Validate parameters
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_debug("Invalid transport handle in destroy function");
        return;
    }

    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    mcp_log_info("Destroying TCP server transport");

    // Ensure everything is stopped and cleaned up
    tcp_transport_stop(transport);

    // Thread pool should already be destroyed in tcp_transport_stop
    // This is just a safety check
    if (data->thread_pool) {
        mcp_log_warn("Thread pool still exists after stop, destroying it now");
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

    mcp_log_info("TCP server transport destroyed");
}

/**
 * @brief Creates a new TCP server transport.
 *
 * This function creates a new TCP server transport that listens on the
 * specified host and port. It allocates and initializes all necessary
 * resources, including client slots, thread pool, and buffer pool.
 *
 * @param host The host to listen on (hostname, IP address, or NULL for any)
 * @param port The port to listen on
 * @param idle_timeout_ms Timeout in milliseconds for idle client connections
 * @return A new transport handle, or NULL on error
 */
mcp_transport_t* mcp_transport_tcp_create(
    const char* host,
    uint16_t port,
    uint32_t idle_timeout_ms
) {
    // Validate parameters
    if (host == NULL) {
        mcp_log_error("NULL host parameter in create function");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        mcp_log_error("Failed to allocate transport structure");
        return NULL;
    }

    // Zero-initialize the transport structure
    memset(transport, 0, sizeof(mcp_transport_t));

    // Allocate transport data structure
    mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)calloc(1, sizeof(mcp_tcp_transport_data_t));
    if (tcp_data == NULL) {
        mcp_log_error("Failed to allocate transport data structure");
        free(transport);
        return NULL;
    }

    // Duplicate host string
    tcp_data->host = mcp_strdup(host);
    if (tcp_data->host == NULL) {
        mcp_log_error("Failed to duplicate host string");
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
    // Initialize pipe file descriptors (POSIX only)
    tcp_data->stop_pipe[0] = -1;
    tcp_data->stop_pipe[1] = -1;
#endif

    // Initialize statistics
    tcp_stats_init(&tcp_data->stats);

    // Allocate client connection array
    tcp_data->clients = (tcp_client_connection_t*)calloc(tcp_data->max_clients, sizeof(tcp_client_connection_t));
    if (tcp_data->clients == NULL) {
        mcp_log_error("Failed to allocate client array (%d slots)", tcp_data->max_clients);
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
        tcp_data->clients[i].should_stop = false;
        tcp_data->clients[i].last_activity_time = 0;
    }

    // Create the client mutex for thread synchronization
    tcp_data->client_mutex = mcp_mutex_create();
    if (tcp_data->client_mutex == NULL) {
        mcp_log_error("Failed to create client mutex");
        free(tcp_data->clients);
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Create the buffer pool for efficient memory management
    tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
    if (tcp_data->buffer_pool == NULL) {
        mcp_log_error("Failed to create buffer pool (size: %d, count: %d)",
                     POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
        mcp_mutex_destroy(tcp_data->client_mutex);
        free(tcp_data->clients);
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Create the thread pool for handling client connections
    tcp_data->thread_pool = mcp_thread_pool_create(DEFAULT_THREAD_POOL_SIZE, CONNECTION_QUEUE_SIZE);
    if (tcp_data->thread_pool == NULL) {
        mcp_log_error("Failed to create thread pool (size: %d, queue: %d)",
                     DEFAULT_THREAD_POOL_SIZE, CONNECTION_QUEUE_SIZE);
        mcp_buffer_pool_destroy(tcp_data->buffer_pool);
        mcp_mutex_destroy(tcp_data->client_mutex);
        free(tcp_data->clients);
        free(tcp_data->host);
        free(tcp_data);
        free(transport);
        return NULL;
    }

    // Set transport type and protocol
    transport->type = MCP_TRANSPORT_TYPE_SERVER;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_TCP;

    // Initialize server operations
    transport->server.start = tcp_transport_start;
    transport->server.stop = tcp_transport_stop;
    transport->server.destroy = tcp_transport_destroy;

    // Set transport data
    transport->transport_data = tcp_data;

    // Initialize callbacks to NULL (will be set in start function)
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    mcp_log_info("Created TCP server transport on %s:%d (max clients: %d, thread pool: %d, idle timeout: %d ms)",
                tcp_data->host, tcp_data->port, tcp_data->max_clients,
                DEFAULT_THREAD_POOL_SIZE, idle_timeout_ms);

    return transport;
}
