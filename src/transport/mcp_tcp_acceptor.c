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
                 // Check for specific errors
                 if (error_code == WSAENOTSOCK || error_code == WSAEINVAL) {
                     // Listening socket is likely closed or invalid, stop the accept loop
                     log_message(LOG_LEVEL_ERROR, "select failed in accept thread: Listening socket invalid (Error: %d). Stopping accept thread.", error_code);
                     data->running = false; // Signal loop termination
                     break;
                 } else if (error_code != WSAEINTR) { // Ignore interrupt, log others
                     char err_buf[128];
                     strerror_s(err_buf, sizeof(err_buf), error_code);
                     log_message(LOG_LEVEL_ERROR, "select failed in accept thread: %d (%s)", error_code, err_buf);
                     // Consider adding a small delay here to prevent tight error loops
                     Sleep(100);
                 } else {
                     // WSAEINTR likely means we are stopping
                     log_message(LOG_LEVEL_DEBUG, "Select interrupted (WSAEINTR) in accept thread, likely stopping.");
                     // No need to break here, the outer loop condition data->running will handle it
                 }
             } else {
                 // Not running, break loop
                 break;
             }
             continue; // Continue loop unless explicitly broken
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

            // --- Start Handle Connection Logic (Windows) ---
            { // Use a block scope for handle_connection logic
                int client_index = -1;
                tcp_client_connection_t* client_conn_ptr = NULL; // Pointer to the slot found

                // 1. Find an available slot and mark it as initializing *atomically* under mutex protection
                EnterCriticalSection(&data->client_mutex);
                for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
                    if (data->clients[i].state == CLIENT_STATE_INACTIVE) { // Find INACTIVE slot
                        client_index = i;
                        client_conn_ptr = &data->clients[i];
                        // Mark as INITIALIZING and set basic info within the lock
                        client_conn_ptr->state = CLIENT_STATE_INITIALIZING;
                        client_conn_ptr->socket = client_socket; // Assign the newly accepted socket
                        client_conn_ptr->address = client_addr;
                        client_conn_ptr->transport = transport;
                        client_conn_ptr->should_stop = false;
                        client_conn_ptr->last_activity_time = time(NULL);
                        client_conn_ptr->thread_handle = NULL;
                        break; // Found a slot
                    }
                }
                LeaveCriticalSection(&data->client_mutex);

                // 2. Process based on whether a slot was found
                if (client_index != -1 && client_conn_ptr != NULL) {
                    // Slot found and marked INITIALIZING, now create the handler thread (outside the mutex)
                    client_conn_ptr->thread_handle = CreateThread(NULL, 0, tcp_client_handler_thread_func, client_conn_ptr, 0, NULL);
                    if (client_conn_ptr->thread_handle == NULL) {
                        log_message(LOG_LEVEL_ERROR, "Failed to create handler thread for client %d.", client_index);
                        close_socket(client_conn_ptr->socket); // Close the socket if thread creation failed
                        // Re-acquire lock to revert state
                        EnterCriticalSection(&data->client_mutex);
                        client_conn_ptr->socket = INVALID_SOCKET_VAL;
                        client_conn_ptr->state = CLIENT_STATE_INACTIVE; // Revert state
                        LeaveCriticalSection(&data->client_mutex);
                    } else {
                        // Thread created successfully, update state (can be done in handler too)
                        // For simplicity here, we assume creation means active for acceptor's view
                        // Re-acquire lock to update state
                        EnterCriticalSection(&data->client_mutex);
                        if (client_conn_ptr->state == CLIENT_STATE_INITIALIZING) { // Check if state is still initializing
                           client_conn_ptr->state = CLIENT_STATE_ACTIVE;
                        }
                        LeaveCriticalSection(&data->client_mutex);
                    }
                } else {
                    // No free slot available - reject the connection
                    const char* reject_client_ip = NULL;
                    if (InetNtop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str)) != NULL) {
                        reject_client_ip = client_ip_str;
                    } else {
                        reject_client_ip = "?.?.?.?";
                    }
                    log_message(LOG_LEVEL_WARN, "Max clients (%d) reached, rejecting connection from %s:%d",
                            MAX_TCP_CLIENTS, reject_client_ip, ntohs(client_addr.sin_port));
                    close_socket(client_socket);
                }
            } // End Handle Connection Logic (Windows)
            // --- End Handle Connection Logic (Windows) ---
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

            // --- Start Handle Connection Logic (POSIX) ---
            { // Use a block scope for handle_connection logic
                int client_index = -1;
                tcp_client_connection_t* client_conn_ptr = NULL; // Pointer to the slot found

                // 1. Find an available slot and mark it as initializing *atomically* under mutex protection
                pthread_mutex_lock(&data->client_mutex);
                for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
                    if (data->clients[i].state == CLIENT_STATE_INACTIVE) { // Find INACTIVE slot
                        client_index = i;
                        client_conn_ptr = &data->clients[i];
                        // Mark as INITIALIZING and set basic info within the lock
                        client_conn_ptr->state = CLIENT_STATE_INITIALIZING;
                        client_conn_ptr->socket = client_socket; // Assign the newly accepted socket
                        client_conn_ptr->address = client_addr;
                        client_conn_ptr->transport = transport;
                        client_conn_ptr->should_stop = false;
                        client_conn_ptr->last_activity_time = time(NULL);
                        client_conn_ptr->thread_handle = 0;
                        break; // Found a slot
                    }
                }
                pthread_mutex_unlock(&data->client_mutex);

                // 2. Process based on whether a slot was found
                if (client_index != -1 && client_conn_ptr != NULL) {
                    // Slot found and marked INITIALIZING, now create the handler thread (outside the mutex)
                    if (pthread_create(&client_conn_ptr->thread_handle, NULL, tcp_client_handler_thread_func, client_conn_ptr) != 0) {
                        log_message(LOG_LEVEL_ERROR, "Failed to create handler thread: %s", strerror(errno));
                        close_socket(client_conn_ptr->socket); // Close the socket if thread creation failed
                        // Re-acquire lock to revert state
                        pthread_mutex_lock(&data->client_mutex);
                        client_conn_ptr->socket = INVALID_SOCKET_VAL;
                        client_conn_ptr->state = CLIENT_STATE_INACTIVE; // Revert state
                        pthread_mutex_unlock(&data->client_mutex);
                    } else {
                        // Thread created successfully, update state (can be done in handler too)
                        // Re-acquire lock to update state
                        pthread_mutex_lock(&data->client_mutex);
                         if (client_conn_ptr->state == CLIENT_STATE_INITIALIZING) { // Check if state is still initializing
                            client_conn_ptr->state = CLIENT_STATE_ACTIVE;
                         }
                        pthread_mutex_unlock(&data->client_mutex);
                        pthread_detach(client_conn_ptr->thread_handle); // Detach thread as we don't join it
                    } else {
                        pthread_detach(client_conn_ptr->thread_handle);
                    }
                } else {
                    // No free slot available - reject the connection
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
            } // End Handle Connection Logic (POSIX)
            // --- End Handle Connection Logic (POSIX) ---
        }
#endif // Platform-specific accept loop end
    } // End of while(data->running) loop

    log_message(LOG_LEVEL_INFO, "Accept thread exiting.");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
