#include "internal/tcp_client_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes needed for socket operations
#ifdef _WIN32
// Included via internal header
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/uio.h> // For writev, struct iovec
#endif

// --- Client Socket Utility Implementations ---

/**
 * @internal
 * @brief Initializes Winsock if on Windows. No-op otherwise.
 */
#ifdef _WIN32
void initialize_winsock_client() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        fprintf(stderr, "[MCP TCP Client] WSAStartup failed: %d\n", iResult);
        // Let caller handle the error, don't exit here.
    }
}

void cleanup_winsock_client() {
    WSACleanup();
}
#else
void initialize_winsock_client() { /* No-op on non-Windows */ }
void cleanup_winsock_client() { /* No-op on non-Windows */ }
#endif

/**
 * @internal
 * @brief Establishes a TCP connection to the configured server host and port.
 * Uses getaddrinfo for hostname resolution and attempts connection.
 * @param data Pointer to the transport's internal data structure containing host, port, and socket field.
 * @return 0 on successful connection, -1 on failure.
 */
int connect_to_server(mcp_tcp_client_transport_data_t* data) {
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char port_str[6]; // Max port length is 5 digits + null terminator

    snprintf(port_str, sizeof(port_str), "%u", data->port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // Force IPv4 to match server listener for simplicity
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(data->host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo failed: %s", gai_strerror(rv));
        return -1;
    }

    // Loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((data->sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET_VAL) {
            mcp_log_warn("Client socket creation failed: %d", sock_errno);
            continue;
        }

        // Blocking connect for client transport simplicity
        if (connect(data->sock, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR_VAL) {
            mcp_log_warn("Client connect failed: %d", sock_errno);
            close_socket(data->sock);
            data->sock = INVALID_SOCKET_VAL;
            continue;
        }

        break; // If we get here, we successfully connected
    }

    freeaddrinfo(servinfo); // All done with this structure

    if (p == NULL) {
        mcp_log_error("Client failed to connect to %s:%u", data->host, data->port);
        return -1;
    }

    mcp_log_info("Client connected to %s:%u on socket %d", data->host, data->port, (int)data->sock);
    data->connected = true;
    return 0;
 }

/**
 * @internal
 * @brief Helper function to reliably send a specified number of bytes over a socket.
 * Handles potential partial sends. Checks the running flag for interruption.
 * @param sock The socket descriptor.
 * @param buf Buffer containing the data to send.
 * @param len Number of bytes to send.
 * @param running_flag Pointer to the transport's running flag for interruption check.
 * @return 0 on success, -1 on socket error, -2 if interrupted by stop signal.
 */
int send_exact_client(socket_t sock, const char* buf, size_t len, volatile bool* running_flag) { // Changed to volatile bool*
    size_t total_sent = 0;
    while (total_sent < len) {
        // Check if the transport has been stopped
        if (running_flag && !(*running_flag)) return -2; // Interrupted by stop signal

#ifdef _WIN32
        int chunk_len = (len - total_sent > INT_MAX) ? INT_MAX : (int)(len - total_sent);
        int bytes_sent = send(sock, buf + total_sent, chunk_len, 0);
#else
        ssize_t bytes_sent = send(sock, buf + total_sent, len - total_sent, 0);
#endif

        if (bytes_sent == SOCKET_ERROR_VAL) {
            return -1; // Socket error
        }
        total_sent += bytes_sent;
    }
    return 0;
}

#ifdef _WIN32
// Helper function to send data from multiple buffers using WSASend (Windows) - Client version
// Returns 0 on success, -1 on error, -2 on stop signal
int send_vectors_client_windows(socket_t sock, WSABUF* buffers, DWORD buffer_count, size_t total_len, volatile bool* running_flag) { // Changed to volatile bool*
    DWORD bytes_sent_total = 0;
    DWORD flags = 0;

    while (bytes_sent_total < total_len) {
        if (running_flag && !(*running_flag)) return -2;

        DWORD current_bytes_sent = 0;
        int result = WSASend(sock, buffers, buffer_count, &current_bytes_sent, flags, NULL, NULL);

        if (result == SOCKET_ERROR) {
            mcp_log_error("WSASend failed (client): %d", WSAGetLastError());
            return -1; // Socket error
        }

        bytes_sent_total += current_bytes_sent;

        if (current_bytes_sent < total_len && bytes_sent_total < total_len) {
             mcp_log_warn("WSASend sent partial data (%lu / %zu) on client, handling not fully implemented.", current_bytes_sent, total_len);
             return -1; // Treat partial send as error for now
        }
    }
    return 0; // Success
}
#else // POSIX
// Helper function to send data from multiple buffers using writev (POSIX) - Client version
// Returns 0 on success, -1 on error, -2 on stop signal
int send_vectors_client_posix(socket_t sock, struct iovec* iov, int iovcnt, size_t total_len, volatile bool* running_flag) { // Changed to volatile bool*
    size_t total_sent = 0;
    while (total_sent < total_len) {
        if (running_flag && !(*running_flag)) return -2;

        ssize_t bytes_sent = writev(sock, iov, iovcnt);

        if (bytes_sent == -1) {
            int error_code = errno;
             if (error_code == EINTR) { // Interrupted by signal, check stop flag and retry
                 if (running_flag && !(*running_flag)) return -2;
                 continue;
            }
            mcp_log_error("writev failed (client): %d (%s)", error_code, strerror(error_code));
            return -1; // Other socket error
        }

        total_sent += bytes_sent;

        // Adjust iovec for the next iteration if partial write occurred
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
                    iov[current_iov].iov_len = 0; // Mark this vector as fully sent
                    current_iov++;
                }
            }
            // Adjust iov and iovcnt for the next writev call
            iov += current_iov;
            iovcnt -= current_iov;
        }
    }
    return 0; // Success
}
#endif


/**
 * @internal
 * @brief Helper function to reliably receive a specified number of bytes from a socket.
 * Handles potential partial reads. Checks the running flag for interruption.
 * @param sock The socket descriptor.
 * @param buf Buffer to store the received data.
 * @param len Number of bytes to receive.
 * @param running_flag Pointer to the transport's running flag for interruption check.
 * @return 1 on success, 0 on graceful connection close, -1 on socket error, -2 if interrupted by stop signal.
 */
int recv_exact_client(socket_t sock, char* buf, size_t len, volatile bool* running_flag) { // Changed to volatile bool*
    size_t total_read = 0;
    while (total_read < len) {
        // Check if the transport has been stopped
        if (running_flag && !(*running_flag)) return -2; // Interrupted by stop signal

#ifdef _WIN32
        int chunk_len = (len - total_read > INT_MAX) ? INT_MAX : (int)(len - total_read);
        int bytes_read = recv(sock, buf + total_read, chunk_len, 0);
#else
         ssize_t bytes_read = recv(sock, buf + total_read, len - total_read, 0);
#endif

        if (bytes_read == SOCKET_ERROR_VAL) {
            return -1; // Socket error
        } else if (bytes_read == 0) {
            return 0;  // Connection closed gracefully
        }
        total_read += bytes_read;
    }
    return 1;
}
