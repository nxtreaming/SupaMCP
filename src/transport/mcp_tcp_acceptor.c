#include "internal/transport_internal.h"
#include "internal/tcp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/**
 * @brief Thread function to accept incoming TCP connections.
 *
 * This function runs in a separate thread and continuously accepts new client
 * connections on the listening socket. When a new connection is accepted, it
 * finds an available client slot and adds a client handler task to the thread pool.
 *
 * The thread can be stopped by setting the running flag to false and either:
 * - On Windows: Closing the listening socket
 * - On POSIX: Writing to the stop pipe
 *
 * @param arg Pointer to the transport structure (mcp_transport_t*)
 * @return NULL when the thread exits
 */
void* tcp_accept_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Accept thread started with invalid transport data.");
        return NULL;
    }
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    mcp_log_info("Accept thread started for %s:%d", data->host, data->port);

    while (data->running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        socket_t client_socket = MCP_INVALID_SOCKET;
        bool connection_pending = false;

        // Use select/poll to wait for connection or stop signal
#ifdef _WIN32
        // Windows: Use select() with timeout to check for connections
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(data->listen_socket, &read_fds);

        // Check every 1 second to allow for clean shutdown
        struct timeval tv = {1, 0};

        int select_result = select(0, &read_fds, NULL, NULL, &tv);

        // Check stop flag after select
        if (!data->running) {
            mcp_log_debug("Accept thread received stop signal after select()");
            break;
        }

        if (select_result == MCP_SOCKET_ERROR) {
            int error = mcp_socket_get_lasterror();
            // WSAEINTR is not defined on Windows, but we check for other common errors
            if (error == WSAENOTSOCK) {
                // Socket was closed, likely due to shutdown
                mcp_log_debug("Listening socket was closed, accept thread exiting");
                break;
            } else if (error == WSAEINTR) {
                // Interrupted by signal, loop again
                mcp_log_debug("select() was interrupted by signal, continuing");
                continue;
            } else if (error == WSAEINVAL) {
                // Invalid argument, loop again
                mcp_log_debug("select() interrupted by signal, continuing");
                continue;
            } else {
                // Other errors
                mcp_log_error("select() failed in accept thread: %d", error);
                break;
            }
        } else if (select_result == 0) {
            // Timeout, loop again to check running flag
            continue;
        }

        // If select_result > 0, the listen socket is readable (connection pending)
        connection_pending = true;
#else
        // POSIX: Use poll() to wait for connections or stop signal
        struct pollfd pfd[2];
        pfd[0].fd = data->listen_socket;
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;

        // Read end of the stop pipe
        pfd[1].fd = data->stop_pipe[0];
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;

        // Wait with a 1 second timeout to allow for clean shutdown
        int poll_result = poll(pfd, 2, 1000);

        // Check stop flag after poll
        if (!data->running) {
            mcp_log_debug("Accept thread received stop signal after poll()");
            break;
        }

        if (poll_result < 0) {
            // Interrupted by signal, loop again
            if (errno == EINTR) {
                mcp_log_debug("poll() was interrupted by signal, continuing");
                continue;
            }
            mcp_log_error("poll() failed in accept thread: %s", strerror(errno));
            break;
        } else if (poll_result == 0) {
            // Timeout, loop again to check running flag
            continue;
        }

        // Check if stop pipe has data
        if (pfd[1].revents & POLLIN) {
            char dummy[8];
            // Read from pipe to clear it
            ssize_t bytes_read = read(data->stop_pipe[0], dummy, sizeof(dummy));
            if (bytes_read < 0) {
                mcp_log_warn("Failed to read from stop pipe: %s", strerror(errno));
            }
            mcp_log_info("Stop signal received on pipe, accept thread exiting");
            break;
        }

        // Check if listen socket has connection
        if (pfd[0].revents & POLLIN) {
            connection_pending = true;
        } else {
            // No incoming connection, loop again
            continue;
        }
#endif

        // Skip accept if no connection is pending
        if (!connection_pending) {
            continue;
        }

        // Accept the connection using the utility function
        client_socket = mcp_socket_accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == MCP_INVALID_SOCKET) {
            // Error logging is handled within mcp_socket_accept
            if (data->running) {
                mcp_log_debug("Accept failed, continuing accept loop");
            }
            // Continue listening even if one accept fails
            continue;
        } else {
            // Set TCP_NODELAY to reduce latency
            int flag = 1;
            int result = setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag)) ;
            if (result != 0)
                mcp_log_warn("Failed to set TCP_NODELAY on client socket: %d", mcp_socket_get_lasterror());
        }

        // Get client IP and port for logging
        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        // Find an available client slot
        mcp_mutex_lock(data->client_mutex);
        int client_index = tcp_find_free_client_slot(data);
        if (client_index == -1) {
            mcp_mutex_unlock(data->client_mutex);
            mcp_log_warn("Max client connections reached (%d). Rejecting connection from %s:%d",
                        data->max_clients, client_ip, client_port);
            mcp_socket_close(client_socket);
            tcp_stats_update_connection_rejected(&data->stats);
            continue;
        }

        // Get current time once for both timestamps
        time_t current_time = time(NULL);

        // Initialize the client connection with minimal lock time
        tcp_client_connection_t client_init = {
            .socket = client_socket,
            .address = client_addr,
            .transport = transport,
            .should_stop = false,
            .state = CLIENT_STATE_INITIALIZING,
            .last_activity_time = current_time,
            .connect_time = current_time,
            .messages_processed = 0,
            .client_index = client_index
        };

        // Copy the client IP and port (safely)
#ifdef _WIN32
        strncpy_s(client_init.client_ip, sizeof(client_init.client_ip), client_ip, _TRUNCATE);
#else
        // For non-Windows platforms, use standard strncpy with explicit null termination
        strncpy(client_init.client_ip, client_ip, sizeof(client_init.client_ip) - 1);
        client_init.client_ip[sizeof(client_init.client_ip) - 1] = '\0'; // Ensure null termination
#endif
        client_init.client_port = client_port;

        // Update the client slot under lock
        memcpy(&data->clients[client_index], &client_init, sizeof(tcp_client_connection_t));
        mcp_mutex_unlock(data->client_mutex);

        // Add the client to the thread pool
        if (mcp_thread_pool_add_task(data->thread_pool, tcp_client_handler_wrapper, &data->clients[client_index]) != 0) {
            mcp_log_error("Failed to add client handler task to thread pool for %s:%d (slot %d)",
                         client_ip, client_port, client_index);

            // Close the socket
            mcp_socket_close(client_socket);

            // Reset the slot state under lock
            mcp_mutex_lock(data->client_mutex);
            data->clients[client_index].state = CLIENT_STATE_INACTIVE;
            data->clients[client_index].socket = MCP_INVALID_SOCKET;
            mcp_mutex_unlock(data->client_mutex);

            tcp_stats_update_error(&data->stats);
        } else {
            // Successfully added to thread pool, mark state as ACTIVE under lock
            mcp_mutex_lock(data->client_mutex);

            // Check if state is still INITIALIZING (could have been changed by stop signal)
            if (data->clients[client_index].state == CLIENT_STATE_INITIALIZING) {
                data->clients[client_index].state = CLIENT_STATE_ACTIVE;

                // Update statistics
                tcp_stats_update_connection_accepted(&data->stats);

                mcp_log_info("Accepted connection from %s:%d on socket %d (slot %d)",
                            client_ip, client_port, (int)client_socket, client_index);
            } else {
                // State changed before we could mark active (likely stopped)
                mcp_log_warn("Client slot %d state changed before activation", client_index);
                data->clients[client_index].state = CLIENT_STATE_INACTIVE;
                data->clients[client_index].socket = MCP_INVALID_SOCKET;
            }

            mcp_mutex_unlock(data->client_mutex);
        }
    }

    mcp_log_info("Accept thread finished.");
    return NULL;
}
