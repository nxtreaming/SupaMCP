#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Thread function to accept incoming connections
#ifdef _WIN32
DWORD WINAPI tcp_accept_thread_func(LPVOID arg) {
#else
void* tcp_accept_thread_func(void* arg) {
#endif
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr)); // Initialize client_addr
    socklen_t client_addr_len = sizeof(client_addr);
    socket_t client_socket = INVALID_SOCKET_VAL; // Initialize to invalid
    char client_ip_str[INET6_ADDRSTRLEN]; // Use INET6 for future-proofing

    log_message(LOG_LEVEL_INFO, "Accept thread started, listening on %s:%d", data->host, data->port);

#ifdef _WIN32
    // Windows: Use select with a timeout to allow checking data->running periodically
    fd_set read_fds;
    struct timeval tv;
    while (data->running) {
        FD_ZERO(&read_fds);
        // Only set FD if socket is valid (it might be closed during stop)
        if (data->listen_socket != INVALID_SOCKET_VAL) {
            FD_SET(data->listen_socket, &read_fds);
        } else {
            Sleep(100); // Avoid busy-waiting if socket is closed
            continue;
        }
        tv.tv_sec = 1; // Check every second
        tv.tv_usec = 0;

        int activity = select(0, &read_fds, NULL, NULL, &tv);

        if (!data->running) break; // Check flag again after select

        if (activity == SOCKET_ERROR_VAL) {
             if (data->running) {
                 int error_code = sock_errno;
                 if (error_code != WSAEINTR && error_code != WSAENOTSOCK && error_code != WSAEINVAL) { // Ignore interrupt/socket closed
                     char err_buf[128];
                     strerror_s(err_buf, sizeof(err_buf), error_code);
                     log_message(LOG_LEVEL_ERROR, "select failed in accept thread: %d (%s)", error_code, err_buf);
                 } else {
                     log_message(LOG_LEVEL_DEBUG, "Select interrupted likely due to stop signal.");
                     break;
                 }
             } else {
                 break;
             }
             continue;
        }

        if (activity > 0 && data->listen_socket != INVALID_SOCKET_VAL && FD_ISSET(data->listen_socket, &read_fds)) {
            // Accept connection
            client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

            if (client_socket == INVALID_SOCKET_VAL) {
                 if (data->running) {
                     char err_buf[128];
                     strerror_s(err_buf, sizeof(err_buf), sock_errno);
                     log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
                 }
                 continue;
            }
            // --- Common connection handling logic (Windows) ---
            const char* client_ip = NULL;
            if (InetNtop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
                 client_ip = client_ip_str;
            } else {
                 log_message(LOG_LEVEL_WARN, "InetNtop failed: %d", sock_errno);
                 client_ip = "?.?.?.?";
            }
            log_message(LOG_LEVEL_INFO, "Accepted connection from %s:%d on socket %p",
                   client_ip, ntohs(client_addr.sin_port), (void*)client_socket);

            // Find an empty slot and launch handler thread (common logic below)
            goto handle_connection;
        }
        // If activity == 0, it was a timeout, loop continues to check data->running

#else // POSIX
    struct pollfd fds[2];
    fds[0].fd = data->listen_socket;
    fds[0].events = POLLIN;
    fds[1].fd = data->stop_pipe[0]; // Read end of stop pipe
    fds[1].events = POLLIN;

    while (data->running) {
        int activity = poll(fds, 2, -1); // Wait indefinitely

        if (!data->running) break; // Check flag again after poll

        if (activity < 0 && errno != EINTR) {
            log_message(LOG_LEVEL_ERROR, "poll error in accept thread: %s", strerror(errno));
            continue;
        }

        // Check if stop pipe has data
        if (fds[1].revents & POLLIN) {
            log_message(LOG_LEVEL_DEBUG, "Stop signal received in accept thread.");
            char buf[16];
            while (read(data->stop_pipe[0], buf, sizeof(buf)) > 0); // Drain pipe
            break; // Exit loop
        }

        // Check for incoming connection
        if (fds[0].revents & POLLIN) {
            client_socket = accept(data->listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);

            if (client_socket == INVALID_SOCKET_VAL) {
                 if (data->running) {
                     char err_buf[128];
                     if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                        log_message(LOG_LEVEL_ERROR, "accept failed: %d (%s)", sock_errno, err_buf);
                     } else {
                        log_message(LOG_LEVEL_ERROR, "accept failed: %d (strerror_r failed)", sock_errno);
                     }
                 }
                 continue;
            }
            // --- Common connection handling logic (POSIX) ---
            const char* client_ip = NULL;
            if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
                client_ip = client_ip_str;
            } else {
                 char err_buf[128];
                 if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
                     log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (%s)", sock_errno, err_buf);
                 } else {
                     log_message(LOG_LEVEL_WARN, "inet_ntop failed: %d (strerror_r failed)", sock_errno);
                 }
                client_ip = "?.?.?.?";
            }
            log_message(LOG_LEVEL_INFO, "Accepted connection from %s:%d on socket %d",
                   client_ip, ntohs(client_addr.sin_port), client_socket);

            // Find an empty slot and launch handler thread (common logic below)
            goto handle_connection;
        }
#endif // Platform-specific accept loop end

handle_connection: {
    // --- Common logic for handling accepted connection ---
    int client_index = -1;
    
#ifdef _WIN32
    EnterCriticalSection(&data->client_mutex);
#else
    pthread_mutex_lock(&data->client_mutex);
#endif

    // Periodically clean up connections that were closed but not properly marked
    // 1. First, clean up existing invalid connections
    int inactive_count = 0;
    for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
        if (data->clients[i].active) {
            // Check if socket is no longer valid
            if (data->clients[i].socket == INVALID_SOCKET_VAL) {
                // This slot is marked as active but has an invalid socket, should clean up
                log_message(LOG_LEVEL_INFO, "Cleaning up stale client slot %d with invalid socket", i);
                data->clients[i].active = false;
                inactive_count++;
            }
        }
    }
    
    if (inactive_count > 0) {
        log_message(LOG_LEVEL_INFO, "Cleaned up %d stale client connection slot(s)", inactive_count);
    }
    
    // 2. Find an available slot for the new connection
    for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
        if (!data->clients[i].active) {
            client_index = i;
            // Initialize all fields (within the lock)
            data->clients[i].active = true;
            data->clients[i].socket = client_socket;
            data->clients[i].address = client_addr;
            data->clients[i].transport = transport;
            data->clients[i].should_stop = false;
            data->clients[i].last_activity_time = time(NULL);
            break;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&data->client_mutex);
#else
    pthread_mutex_unlock(&data->client_mutex);
#endif

    if (client_index != -1) {
        tcp_client_connection_t* client_conn = &data->clients[client_index];
        
        // Only create handler thread after all initialization is complete
#ifdef _WIN32
        client_conn->thread_handle = CreateThread(NULL, 0, tcp_client_handler_thread_func, client_conn, 0, NULL);
        if (client_conn->thread_handle == NULL) {
            log_message(LOG_LEVEL_ERROR, "Failed to create handler thread for client %d.", client_index);
            close_socket(client_socket);
            // Safely mark slot inactive again
            EnterCriticalSection(&data->client_mutex);
            client_conn->active = false;
            LeaveCriticalSection(&data->client_mutex);
        }
#else
        if (pthread_create(&client_conn->thread_handle, NULL, tcp_client_handler_thread_func, client_conn) != 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to create handler thread: %s", strerror(errno));
            close_socket(client_socket);
            // Safely mark slot inactive again
            pthread_mutex_lock(&data->client_mutex);
            client_conn->active = false;
            pthread_mutex_unlock(&data->client_mutex);
        } else {
            pthread_detach(client_conn->thread_handle); // Detach thread
        }
#endif
    } else {
        // No free slot available
        const char* reject_client_ip = NULL;
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
            reject_client_ip = client_ip_str;
        } else {
            reject_client_ip = "?.?.?.?";
        }
        log_message(LOG_LEVEL_WARN, "Max clients (%d) reached, rejecting connection from %s:%d",
                MAX_TCP_CLIENTS, reject_client_ip, ntohs(client_addr.sin_port));
        close_socket(client_socket);
    }
} // End of handle_connection block

continue_accept:
    ; // Empty statement to ensure there's something after the label
#ifndef _WIN32 // Closing brace for the 'if (fds[0].revents & POLLIN)' block in POSIX case
        }
#endif
    } // End of while(data->running) loop

    log_message(LOG_LEVEL_INFO, "Accept thread exiting.");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
