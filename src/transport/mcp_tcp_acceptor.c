#include "internal/transport_internal.h"
#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mcp_log.h"
#include "mcp_sync.h"
#include <mcp_thread_pool.h>

// Thread function to accept incoming connections
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

        // Use select/poll to wait for connection or stop signal
#ifdef _WIN32
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(data->listen_socket, &read_fds);
        // Check every 1 second
        struct timeval tv = {1, 0};

        int select_result = select(0, &read_fds, NULL, NULL, &tv);

        // Check stop flag after select
        if (!data->running)
            break;

        if (select_result == MCP_SOCKET_ERROR) {
            mcp_log_error("select() failed in accept thread: %d", mcp_socket_get_last_error());
            break;
        } else if (select_result == 0) {
            // Timeout, loop again to check running flag
            continue;
        }
        // If select_result > 0, the listen socket is readable (connection pending)
#else
        struct pollfd pfd[2];
        pfd[0].fd = data->listen_socket;
        pfd[0].events = POLLIN;
        // Read end of the stop pipe
        pfd[1].fd = data->stop_pipe[0];
        pfd[1].events = POLLIN;

        // Wait indefinitely until event or signal
        int poll_result = poll(pfd, 2, -1);

        // Check stop flag after poll
        if (!data->running)
            break;

        if (poll_result < 0) {
            // Interrupted by signal, loop again
            if (errno == EINTR)
                continue;
            mcp_log_error("poll() failed in accept thread: %s", strerror(errno));
            break;
        }

        // Check if stop pipe has data
        if (pfd[1].revents & POLLIN) {
            mcp_log_info("Stop signal received on pipe, accept thread exiting.");
            break;
        }

        // Check if listen socket has connection
        if (!(pfd[0].revents & POLLIN)) {
            // No incoming connection, loop again
            continue;
        }
#endif

        // Accept the connection using the new utility function
        client_socket = mcp_socket_accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == MCP_INVALID_SOCKET) {
            // Error logging is handled within mcp_socket_accept
            if (data->running) {
                mcp_log_debug("mcp_socket_accept returned invalid socket, continuing accept loop.");
            }
            // Continue listening even if one accept fails
            continue;
        }

        // Find an available client slot
        int client_index = tcp_find_free_client_slot(data);

        if (client_index == -1) {
            mcp_log_warn("Max client connections reached (%d). Rejecting new connection.", data->max_clients);
            mcp_socket_close(client_socket);
            tcp_stats_update_connection_rejected(&data->stats);
            continue;
        }

        // Initialize the client connection
        mcp_mutex_lock(data->client_mutex);
        tcp_client_connection_t* client = &data->clients[client_index];
        client->state = CLIENT_STATE_INITIALIZING;
        client->socket = client_socket;
        client->address = client_addr;
        client->transport = transport;
        client->should_stop = false;
        client->last_activity_time = time(NULL);
        client->connect_time = time(NULL);
        client->messages_processed = 0;
        client->client_index = client_index;

        // Store client IP and port for logging
        inet_ntop(AF_INET, &client_addr.sin_addr, client->client_ip, sizeof(client->client_ip));
        client->client_port = ntohs(client_addr.sin_port);

        mcp_mutex_unlock(data->client_mutex);

        // Add the client to the thread pool
        if (mcp_thread_pool_add_task(data->thread_pool, tcp_client_handler_wrapper, client) != 0) {
            mcp_log_error("Failed to add client handler task to thread pool for slot %d.", client_index);
            mcp_socket_close(client_socket);

            // Reset the slot state under lock
            mcp_mutex_lock(data->client_mutex);
            client->state = CLIENT_STATE_INACTIVE;
            client->socket = MCP_INVALID_SOCKET;
            mcp_mutex_unlock(data->client_mutex);

            tcp_stats_update_error(&data->stats);
        } else {
            // Successfully added to thread pool, mark state as ACTIVE under lock
            mcp_mutex_lock(data->client_mutex);

            // Check if state is still INITIALIZING (could have been changed by stop signal)
            if (client->state == CLIENT_STATE_INITIALIZING) {
                client->state = CLIENT_STATE_ACTIVE;
                mcp_log_info("Accepted connection from %s:%d on socket %d (slot %d)",
                             client->client_ip, client->client_port, (int)client_socket, client_index);

                // Update statistics
                tcp_stats_update_connection_accepted(&data->stats);
            } else {
                // State changed before we could mark active (likely stopped)
                mcp_log_warn("Client slot %d state changed before activation.", client_index);
                client->state = CLIENT_STATE_INACTIVE;
                client->socket = MCP_INVALID_SOCKET;
            }

            mcp_mutex_unlock(data->client_mutex);
        }
    }

    mcp_log_info("Accept thread finished.");
    return NULL;
}
