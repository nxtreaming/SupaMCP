#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// --- Static Transport Interface Functions ---

// Note: Update tcp_transport_start signature to match interface
static int tcp_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    // Store callbacks
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (data->running) return 0; // Already running

    initialize_winsock(); // Initialize Winsock if on Windows

    // Create listening socket
    data->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (data->listen_socket == INVALID_SOCKET_VAL) {
        char err_buf[128];
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), sock_errno);
        log_message(LOG_LEVEL_ERROR, "socket creation failed: %d (%s)", sock_errno, err_buf);
#else
         if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "socket creation failed: %d (%s)", sock_errno, err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "socket creation failed: %d (strerror_r failed)", sock_errno);
         }
#endif
        return -1;
    }

    // Allow address reuse
    int optval = 1;
    setsockopt(data->listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    // Bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->port);
    if (inet_pton(AF_INET, data->host, &server_addr.sin_addr) <= 0) {
         log_message(LOG_LEVEL_ERROR, "Invalid address/ Address not supported: %s", data->host);
         close_socket(data->listen_socket);
         return -1;
    }

    if (bind(data->listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR_VAL) {
        char err_buf[128];
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), sock_errno);
        log_message(LOG_LEVEL_ERROR, "bind failed on %s:%d: %d (%s)", data->host, data->port, sock_errno, err_buf);
#else
         if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
             log_message(LOG_LEVEL_ERROR, "bind failed on %s:%d: %d (%s)", data->host, data->port, sock_errno, err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "bind failed on %s:%d: %d (strerror_r failed)", data->host, data->port, sock_errno);
         }
#endif
        close_socket(data->listen_socket);
        return -1;
    }

    // Listen
    if (listen(data->listen_socket, SOMAXCONN) == SOCKET_ERROR_VAL) {
        char err_buf[128];
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), sock_errno);
        log_message(LOG_LEVEL_ERROR, "listen failed: %d (%s)", sock_errno, err_buf);
#else
         if (strerror_r(sock_errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "listen failed: %d (%s)", sock_errno, err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "listen failed: %d (strerror_r failed)", sock_errno);
         }
#endif
        close_socket(data->listen_socket);
        return -1;
    }

    data->running = true;

    // Initialize mutex/critical section and stop pipe
#ifdef _WIN32
    InitializeCriticalSection(&data->client_mutex);
    // No pipe needed for Windows
#else
    if (pthread_mutex_init(&data->client_mutex, NULL) != 0) {
        char err_buf[128];
        if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
             log_message(LOG_LEVEL_ERROR, "Mutex init failed: %s", err_buf);
        } else {
             log_message(LOG_LEVEL_ERROR, "Mutex init failed: %d (strerror_r failed)", errno);
        }
        close_socket(data->listen_socket);
        data->running = false;
        return -1;
    }
    // Create stop pipe
    data->stop_pipe[0] = -1; // Initialize FDs
    data->stop_pipe[1] = -1;
    if (pipe(data->stop_pipe) != 0) {
        log_message(LOG_LEVEL_ERROR, "Stop pipe creation failed: %s", strerror(errno));
        close_socket(data->listen_socket);
        pthread_mutex_destroy(&data->client_mutex);
        data->running = false;
        return -1;
    }
    // Set read end to non-blocking
    int flags = fcntl(data->stop_pipe[0], F_GETFL, 0);
    if (flags == -1 || fcntl(data->stop_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
         log_message(LOG_LEVEL_ERROR, "Failed to set stop pipe read end non-blocking: %s", strerror(errno));
         close_stop_pipe(data);
         close_socket(data->listen_socket);
         pthread_mutex_destroy(&data->client_mutex);
         data->running = false;
         return -1;
    }
#endif

    // Start accept thread (using function from mcp_tcp_acceptor.c)
#ifdef _WIN32
    data->accept_thread = CreateThread(NULL, 0, tcp_accept_thread_func, transport, 0, NULL);
    if (data->accept_thread == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create accept thread (Error: %lu).", GetLastError());
        close_socket(data->listen_socket);
        DeleteCriticalSection(&data->client_mutex); // Clean up mutex
        data->running = false;
        return -1;
    }
#else
     if (pthread_create(&data->accept_thread, NULL, tcp_accept_thread_func, transport) != 0) {
        char err_buf[128];
         if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to create accept thread: %s", err_buf);
         } else {
             log_message(LOG_LEVEL_ERROR, "Failed to create accept thread: %d (strerror_r failed)", errno);
         }
        close_socket(data->listen_socket);
        pthread_mutex_destroy(&data->client_mutex);
        close_stop_pipe(data); // Clean up pipe on failure
        data->running = false;
        return -1;
    }
#endif

    log_message(LOG_LEVEL_INFO, "TCP Transport started listening on %s:%d", data->host, data->port);
    return 0;
}

static int tcp_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) return -1;
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

    if (!data->running) return 0;

    log_message(LOG_LEVEL_INFO, "Stopping TCP Transport...");
    data->running = false;

    // Signal and close the listening socket/pipe to interrupt accept/select
#ifdef _WIN32
    // Close the listening socket to interrupt accept()
    if (data->listen_socket != INVALID_SOCKET_VAL) {
        shutdown(data->listen_socket, SD_BOTH); // Try shutdown first
        close_socket(data->listen_socket);
        data->listen_socket = INVALID_SOCKET_VAL;
    }
#else
    // Write to the stop pipe to interrupt select()
    if (data->stop_pipe[1] != -1) {
        char dummy = 's';
        ssize_t written = write(data->stop_pipe[1], &dummy, 1);
        if (written <= 0) {
             log_message(LOG_LEVEL_WARN, "Failed to write to stop pipe during stop: %s", strerror(errno));
        }
        // No need to close write end immediately, close_stop_pipe handles it
    }
    // Also close the listening socket
    if (data->listen_socket != INVALID_SOCKET_VAL) {
        shutdown(data->listen_socket, SHUT_RDWR); // Try shutdown first
        close_socket(data->listen_socket);
        data->listen_socket = INVALID_SOCKET_VAL;
    }
#endif


    // Wait for the accept thread to finish
#ifdef _WIN32
    if (data->accept_thread) {
        // Consider a timeout?
        WaitForSingleObject(data->accept_thread, 2000); // Wait 2 seconds
        CloseHandle(data->accept_thread);
        data->accept_thread = NULL;
    }
#else
    if (data->accept_thread) {
        // Join the accept thread
        pthread_join(data->accept_thread, NULL);
        data->accept_thread = 0;
    }
#endif
    log_message(LOG_LEVEL_DEBUG, "Accept thread stopped.");

    // Signal handler threads to stop and close connections
#ifdef _WIN32
    EnterCriticalSection(&data->client_mutex);
#else
    pthread_mutex_lock(&data->client_mutex);
#endif
    for (int i = 0; i < MAX_TCP_CLIENTS; ++i) {
        if (data->clients[i].active) {
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
            // close_socket(data->clients[i].socket); // Let handler thread close it

#ifdef _WIN32
            // Wait for handler threads?
            if (data->clients[i].thread_handle) {
                 WaitForSingleObject(data->clients[i].thread_handle, 1000); // Wait 1 sec
                 // Don't close handle here, handler thread should do it on exit
                 // CloseHandle(data->clients[i].thread_handle);
                 // data->clients[i].thread_handle = NULL;
            }
#else
            // Threads were detached, cannot join. Assume they exit on socket close/error/stop signal.
#endif
        } // end if(data->clients[i].active)
    } // end for loop

    // Clean up mutex and stop pipe
#ifdef _WIN32
    LeaveCriticalSection(&data->client_mutex);
    DeleteCriticalSection(&data->client_mutex);
#else
    pthread_mutex_unlock(&data->client_mutex);
    pthread_mutex_destroy(&data->client_mutex);
    close_stop_pipe(data); // Close pipe FDs
#endif

    log_message(LOG_LEVEL_INFO, "TCP Transport stopped.");

    // Cleanup Winsock on Windows
    cleanup_winsock(); // Use helper

    return 0;
}

// Server transport send function - handles message sending to client sockets
static int tcp_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL || size == 0) {
        log_message(LOG_LEVEL_ERROR, "Invalid parameters in tcp_transport_send");
        return -1;
    }
    
    // We don't have a direct way to get the target socket, it relies on message callback context
    // The actual sending is handled by the client handler thread using send_exact function
    // This function just exists to provide a non-NULL send function to avoid NULL pointer issues
    
    log_message(LOG_LEVEL_DEBUG, "Server transport send function called with %zu bytes", size);
    return 0; // Return success - actual sending is handled by client handler thread
}

static void tcp_transport_destroy(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) return;
     mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)transport->transport_data;

     tcp_transport_stop(transport); // Ensure everything is stopped and cleaned

     free(data->host);
     // Destroy the buffer pool
     mcp_buffer_pool_destroy(data->buffer_pool);
     free(data);
     transport->transport_data = NULL;
     // Generic destroy will free the transport struct itself
}


// --- Public Creation Function ---

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
     tcp_data->listen_socket = INVALID_SOCKET_VAL; // Use defined constant
     tcp_data->running = false;
     tcp_data->buffer_pool = NULL; // Initialize pool pointer
     
     // Explicitly initialize all client slots with INVALID_SOCKET_VAL
     // calloc only zero-initializes, but INVALID_SOCKET_VAL is not 0 on Windows
     for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
         tcp_data->clients[i].active = false;  // Redundant with calloc, but explicit
         tcp_data->clients[i].socket = INVALID_SOCKET_VAL;
     }
     
 #ifndef _WIN32
     tcp_data->stop_pipe[0] = -1; // Initialize pipe FDs
     tcp_data->stop_pipe[1] = -1;
 #endif

     // Create the buffer pool
     tcp_data->buffer_pool = mcp_buffer_pool_create(POOL_BUFFER_SIZE, POOL_NUM_BUFFERS);
     if (tcp_data->buffer_pool == NULL) {
         log_message(LOG_LEVEL_ERROR, "Failed to create buffer pool for TCP transport.");
         free(tcp_data->host);
         free(tcp_data);
         free(transport);
         return NULL;
     }

     // Initialize function pointers
    transport->start = tcp_transport_start;
    transport->stop = tcp_transport_stop;
    transport->send = tcp_transport_send; // Use our new send function
    transport->receive = NULL; // Set receive to NULL, not used by server transport
    transport->destroy = tcp_transport_destroy;
    transport->transport_data = tcp_data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL; // Initialize error callback

    return transport;
}
