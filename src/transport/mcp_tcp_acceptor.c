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
// Use the abstracted signature: void* func(void* arg)
void* tcp_accept_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Accept thread started with invalid transport data.");
        return NULL; // Indicate error
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
        struct timeval tv = {1, 0}; // Check every 1 second
        int select_result = select(0, &read_fds, NULL, NULL, &tv);

        if (!data->running) break; // Check stop flag after select

        if (select_result == MCP_SOCKET_ERROR) {
            mcp_log_error("select() failed in accept thread: %d", mcp_socket_get_last_error()); // Use new function
            break; // Exit thread on select error
        } else if (select_result == 0) {
            continue; // Timeout, loop again to check running flag
        }
        // If select_result > 0, the listen socket is readable (connection pending)
#else // POSIX
        struct pollfd pfd[2];
        pfd[0].fd = data->listen_socket;
        pfd[0].events = POLLIN;
        pfd[1].fd = data->stop_pipe[0]; // Read end of the stop pipe
        pfd[1].events = POLLIN;

        int poll_result = poll(pfd, 2, -1); // Wait indefinitely until event or signal

        if (!data->running) break; // Check stop flag after poll

        if (poll_result < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, loop again
            mcp_log_error("poll() failed in accept thread: %s", strerror(errno));
            break; // Exit thread on poll error
        }

        // Check if stop pipe has data
        if (pfd[1].revents & POLLIN) {
            mcp_log_info("Stop signal received on pipe, accept thread exiting.");
            break; // Stop signal received
        }

        // Check if listen socket has connection
        if (!(pfd[0].revents & POLLIN)) {
            continue; // No incoming connection, loop again
        }
#endif

        // Accept the connection using the new utility function
        client_socket = mcp_socket_accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

        if (client_socket == MCP_INVALID_SOCKET) {
            // Error logging is handled within mcp_socket_accept
            if (data->running) {
                 mcp_log_debug("mcp_socket_accept returned invalid socket, continuing accept loop.");
            }
            continue; // Continue listening even if one accept fails
        }

        // Find an available client slot
        int client_index = -1;
        mcp_mutex_lock(data->client_mutex);
        for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
            if (data->clients[i].state == CLIENT_STATE_INACTIVE) {
                client_index = i;
                data->clients[i].state = CLIENT_STATE_INITIALIZING; // Mark as initializing
                data->clients[i].socket = client_socket;
                data->clients[i].address = client_addr;
                data->clients[i].transport = transport; // Pass transport handle
                data->clients[i].should_stop = false; // Reset stop flag
                data->clients[i].last_activity_time = time(NULL); // Initialize activity time
                break;
            }
        }
        mcp_mutex_unlock(data->client_mutex);

        if (client_index == -1) {
            mcp_log_warn("Max client connections reached (%d). Rejecting new connection.", MAX_TCP_CLIENTS);
            mcp_socket_close(client_socket);
            continue;
        }

        // Start a new thread to handle the client connection
        if (mcp_thread_create(&data->clients[client_index].thread_handle, tcp_client_handler_thread_func, &data->clients[client_index]) != 0) {
            mcp_log_error("Failed to create client handler thread for slot %d.", client_index);
            mcp_socket_close(client_socket);
            // Reset the slot state under lock
            mcp_mutex_lock(data->client_mutex);
            data->clients[client_index].state = CLIENT_STATE_INACTIVE;
            data->clients[client_index].socket = MCP_INVALID_SOCKET;
            mcp_mutex_unlock(data->client_mutex);
        } else {
            // Successfully started handler thread, mark state as ACTIVE under lock
            mcp_mutex_lock(data->client_mutex);
            // Check if state is still INITIALIZING (could have been changed by stop signal)
            if (data->clients[client_index].state == CLIENT_STATE_INITIALIZING) {
                 data->clients[client_index].state = CLIENT_STATE_ACTIVE;
                 char client_ip_str[INET_ADDRSTRLEN]; // Buffer for IP string
                 inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
                 mcp_log_info("Accepted connection from %s:%d on socket %d (slot %d)",
                             client_ip_str, ntohs(client_addr.sin_port), (int)client_socket, client_index);
            } else {
                 // State changed before we could mark active (likely stopped).
                 // Do NOT close the socket here - the handler thread is responsible for it now.
                 mcp_log_warn("Client slot %d state changed before activation. Handler thread will clean up.", client_index);
                 // Ensure state is marked inactive so acceptor doesn't reuse slot prematurely.
                 data->clients[client_index].state = CLIENT_STATE_INACTIVE;
                 data->clients[client_index].socket = MCP_INVALID_SOCKET; // Mark socket as invalid *in the slot*
                 // Thread handle might be dangling if create succeeded but state changed,
                 // but we can't safely join it here. Assume thread will exit cleanly.
                 data->clients[client_index].thread_handle = 0;
            }
            mcp_mutex_unlock(data->client_mutex);
        }
    } // End while(data->running)

    mcp_log_info("Accept thread finished.");
    return NULL; // Return NULL for void* compatibility
}
