#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
// Need ws2tcpip.h for WSABUF, WSASend etc. Already included via internal header? Check.
// If not: #include <ws2tcpip.h>
#else
#include <sys/uio.h> // For writev, struct iovec
#endif

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
    volatile bool* client_should_stop_flag) { // Changed to volatile bool*
    size_t total_sent = 0;
    while (total_sent < len) {
        // Read volatile flag
        if (client_should_stop_flag && *client_should_stop_flag) return -2;

#ifdef _WIN32
        int chunk_len = (len - total_sent > INT_MAX) ? INT_MAX : (int)(len - total_sent);
        int bytes_sent = send(sock, buf + total_sent, chunk_len, SEND_FLAGS);
#else
        ssize_t bytes_sent = send(sock, buf + total_sent, len - total_sent, SEND_FLAGS);
#endif

        if (bytes_sent == SOCKET_ERROR_VAL) {
            int error_code = sock_errno;
#ifdef _WIN32
            if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN) {
                return -3;
            }
#else
            if (error_code == EPIPE || error_code == ECONNRESET || error_code == ENOTCONN) {
                return -3;
            }
#endif
            return -1;
        }
        if (bytes_sent == 0) {
             return -1;
        }
        total_sent += bytes_sent;
    }
    return 0; // Success
}

#ifdef _WIN32
// Helper function to send data from multiple buffers using WSASend (Windows)
// Returns 0 on success, -1 on error, -2 on stop signal, -3 on connection closed/reset
int send_vectors_windows(socket_t sock, WSABUF* buffers, DWORD buffer_count, size_t total_len, volatile bool* stop_flag) { // Changed to volatile bool*
    DWORD bytes_sent_total = 0;
    DWORD flags = 0;

    while (bytes_sent_total < total_len) {
        // Read volatile flag
        if (stop_flag && *stop_flag) return -2;

        DWORD current_bytes_sent = 0;
        int result = WSASend(sock, buffers, buffer_count, &current_bytes_sent, flags, NULL, NULL);

        if (result == SOCKET_ERROR) {
            int error_code = WSAGetLastError();
            if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN) {
                return -3;
            }
            mcp_log_error("WSASend failed: %d", error_code);
            return -1;
        }

        bytes_sent_total += current_bytes_sent;

        if (current_bytes_sent < total_len && bytes_sent_total < total_len) {
             mcp_log_warn("WSASend sent partial data (%lu / %zu), handling not fully implemented.", current_bytes_sent, total_len);
             return -1;
        }
    }
    return 0; // Success
}
#else // POSIX
// Helper function to send data from multiple buffers using writev (POSIX)
// Returns 0 on success, -1 on error, -2 on stop signal, -3 on connection closed/reset
int send_vectors_posix(socket_t sock, struct iovec* iov, int iovcnt, size_t total_len, volatile bool* stop_flag) { // Changed to volatile bool*
    size_t total_sent = 0;
    while (total_sent < total_len) {
        // Read volatile flag
        if (stop_flag && *stop_flag) return -2;

        ssize_t bytes_sent = writev(sock, iov, iovcnt);

        if (bytes_sent == -1) {
            int error_code = errno;
            if (error_code == EPIPE || error_code == ECONNRESET || error_code == ENOTCONN) {
                return -3;
            }
            if (error_code == EINTR) {
                 if (stop_flag && *stop_flag) return -2;
                 continue;
            }
            mcp_log_error("writev failed: %d (%s)", error_code, strerror(error_code));
            return -1;
        }

        total_sent += bytes_sent;

        if (total_sent < total_len) {
            size_t sent_so_far = bytes_sent;
            int current_iov = 0;
            while (sent_so_far > 0 && current_iov < iovcnt) {
                if (sent_so_far < iov[current_iov].iov_len) {
                    iov[current_iov].iov_base = (char*)iov[current_iov].iov_base + sent_so_far;
                    iov[current_iov].iov_len -= sent_so_far;
                    sent_so_far = 0;
                } else {
                    sent_so_far -= iov[current_iov].iov_len;
                    iov[current_iov].iov_len = 0;
                    current_iov++;
                }
            }
            iov += current_iov;
            iovcnt -= current_iov;
        }
    }
    return 0; // Success
}
#endif


// Helper function to read exactly n bytes from a socket (checks client stop flag)
// Returns: 0 on success, -1 on error, -2 on stop signal, -3 on connection closed
int recv_exact(
    socket_t sock,
    char* buf,
    int len,
    volatile bool* client_should_stop_flag) { // Changed to volatile bool*
    int total_read = 0;
    while (total_read < len) {
        // Read volatile flag
        if (client_should_stop_flag && *client_should_stop_flag) return -2;

        int bytes_read = recv(sock, buf + total_read, len - total_read, 0);
        if (bytes_read == SOCKET_ERROR_VAL) {
            return -1;
        } else if (bytes_read == 0) {
            return -3;
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
    volatile bool* should_stop) { // Changed to volatile bool*
    // Read volatile flag
    if (should_stop && *should_stop) return -2;

#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    struct timeval tv;
    struct timeval* tv_ptr = NULL;

    if (timeout_ms > 0) {
        uint32_t select_timeout_ms = (timeout_ms < 500) ? timeout_ms : 500;
        tv.tv_sec = select_timeout_ms / 1000;
        tv.tv_usec = (select_timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000;
        tv_ptr = &tv;
    }

    int result = select(0, &read_fds, NULL, NULL, tv_ptr);

    // Read volatile flag again after select
    if (should_stop && *should_stop) return -2;

    if (result == SOCKET_ERROR_VAL) {
        int error_code = sock_errno;
        char err_buf[128];
        strerror_s(err_buf, sizeof(err_buf), error_code);
        mcp_log_error("select failed for socket %d: %d (%s)",
                   (int)sock, error_code, err_buf);

        if (error_code == WSAEINTR || error_code == WSAEWOULDBLOCK) {
            return 0;
        }

        return -1;
    } else if (result == 0) {
        return 0;
    } else {
        if (FD_ISSET(sock, &read_fds)) {
             return 1;
        } else {
             return 0;
        }
    }
#else // POSIX
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    int poll_timeout = (timeout_ms == 0) ? 500 : ((timeout_ms < 500) ? (int)timeout_ms : 500);

    int result = poll(&pfd, 1, poll_timeout);

    // Read volatile flag again after poll
    if (should_stop && *should_stop) return -2;

    if (result < 0) {
        if (errno == EINTR) return -2;
        return -1;
    } else if (result == 0) {
        return 0;
    } else {
        if (pfd.revents & POLLIN) {
            return 1;
        } else {
            return -1;
        }
    }
#endif
}
