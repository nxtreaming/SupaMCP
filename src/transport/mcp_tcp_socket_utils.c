#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// --- Platform Initialization/Cleanup ---

#ifdef _WIN32
void initialize_winsock() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        fprintf(stderr, "[MCP TCP Transport] WSAStartup failed: %d\n", iResult);
        exit(EXIT_FAILURE); // Critical error
    }
}

void cleanup_winsock() {
    WSACleanup();
}
#else
void initialize_winsock() { /* No-op on non-Windows */ }
void cleanup_winsock() { /* No-op on non-Windows */ }

// Helper to close stop pipe FDs (POSIX only)
void close_stop_pipe(mcp_tcp_transport_data_t* data) {
    if (data == NULL) return;
    if (data->stop_pipe[0] != -1) {
        close(data->stop_pipe[0]);
        data->stop_pipe[0] = -1;
    }
    if (data->stop_pipe[1] != -1) {
        close(data->stop_pipe[1]);
        data->stop_pipe[1] = -1;
    }
}
#endif


// --- Socket Read/Write Helpers ---

// Helper function to send exactly n bytes to a socket (checks client stop flag)
// Returns 0 on success, -1 on error, -2 on stop signal, -3 on connection closed/reset
int send_exact(
    socket_t sock,
    const char* buf,
    size_t len,
    bool* client_should_stop_flag) {
    size_t total_sent = 0;
    while (total_sent < len) {
        if (client_should_stop_flag && *client_should_stop_flag) return -2; // Interrupted by stop signal

#ifdef _WIN32
        int chunk_len = (len - total_sent > INT_MAX) ? INT_MAX : (int)(len - total_sent);
        int bytes_sent = send(sock, buf + total_sent, chunk_len, SEND_FLAGS);
#else
        ssize_t bytes_sent = send(sock, buf + total_sent, len - total_sent, SEND_FLAGS);
#endif

        if (bytes_sent == SOCKET_ERROR_VAL) {
            // Check if the error is due to a broken pipe (connection closed by peer)
            int error_code = sock_errno;
#ifdef _WIN32
            if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN) {
                return -3; // Indicate connection closed/reset
            }
#else
            if (error_code == EPIPE || error_code == ECONNRESET || error_code == ENOTCONN) {
                return -3; // Indicate connection closed/reset
            }
#endif
            return -1; // Other socket error
        }
        if (bytes_sent == 0) {
             // Should not happen with blocking sockets unless len was 0
             return -1;
        }
        total_sent += bytes_sent;
    }
    return 0; // Success
}


// Helper function to read exactly n bytes from a socket (checks client stop flag)
// Returns: 0 on success, -1 on error, -2 on stop signal, -3 on connection closed
int recv_exact(
    socket_t sock,
    char* buf,
    int len, // Note: Using int for length consistent with recv's third arg type
    bool* client_should_stop_flag) {
    int total_read = 0;
    while (total_read < len) {
        if (client_should_stop_flag && *client_should_stop_flag) return -2; // Interrupted by stop signal

        int bytes_read = recv(sock, buf + total_read, len - total_read, 0);
        if (bytes_read == SOCKET_ERROR_VAL) {
            return -1; // Socket error
        } else if (bytes_read == 0) {
            return -3;  // Connection closed gracefully
        }
        total_read += bytes_read;
    }
    return 0; // Success
}

/**
 * @internal
 * @brief Waits for readability on a socket or a stop signal.
 * Uses poll() on POSIX and select() on Windows for simplicity.
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. 0 means no timeout (wait indefinitely).
 * @param should_stop Pointer to the thread's stop flag.
 * @return 1 if socket is readable, 0 if timeout occurred, -1 on error, -2 if stop signal received.
 */
int wait_for_socket_read(
    socket_t sock,
    uint32_t timeout_ms,
    bool* should_stop) {
    if (should_stop && *should_stop) return -2;

#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    struct timeval tv;
    struct timeval* tv_ptr = NULL;

    if (timeout_ms > 0) {
        // Use a smaller timeout for select to check should_stop more often
        uint32_t select_timeout_ms = (timeout_ms < 500) ? timeout_ms : 500; // Check every 500ms max
        tv.tv_sec = select_timeout_ms / 1000;
        tv.tv_usec = (select_timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    } else {
        // If timeout_ms is 0, we still need a small timeout for select
        // to periodically check the should_stop flag.
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000; // 500ms check interval
        tv_ptr = &tv;
    }

    int result = select(0, &read_fds, NULL, NULL, tv_ptr);

    if (should_stop && *should_stop) return -2; // Check again after select

    if (result == SOCKET_ERROR_VAL) {
        int error_code = sock_errno;
        char err_buf[128];
        strerror_s(err_buf, sizeof(err_buf), error_code);
        log_message(LOG_LEVEL_ERROR, "select failed for socket %d: %d (%s)", 
                   (int)sock, error_code, err_buf);
        
        // Non-fatal errors, continue waiting instead of immediately disconnecting
        if (error_code == WSAEINTR || error_code == WSAEWOULDBLOCK) {
            return 0; // Return timeout result, letting the caller continue waiting
        }
        
        return -1; // Other select errors
    } else if (result == 0) {
        return 0; // timeout (or intermediate check)
    } else {
        // Check if the socket is actually in the set (it should be if result > 0)
        if (FD_ISSET(sock, &read_fds)) {
             return 1; // readable
        } else {
             // This case should ideally not happen if select returns > 0
             return 0; // Treat as timeout if socket not set unexpectedly
        }
    }
#else // POSIX
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    // Use a smaller timeout for poll to check should_stop more often
    int poll_timeout = (timeout_ms == 0) ? 500 : ((timeout_ms < 500) ? (int)timeout_ms : 500); // Check every 500ms max, or 500ms if original timeout was 0

    int result = poll(&pfd, 1, poll_timeout);

    if (should_stop && *should_stop) return -2; // Check again after poll

    if (result < 0) {
        if (errno == EINTR) return -2; // Interrupted, treat like stop signal
        return -1; // poll error
    } else if (result == 0) {
        return 0; // timeout (or intermediate check)
    } else {
        if (pfd.revents & POLLIN) {
            return 1; // readable
        } else {
            // Other event (e.g., POLLERR, POLLHUP), treat as error/closed
            return -1;
        }
    }
#endif
}
