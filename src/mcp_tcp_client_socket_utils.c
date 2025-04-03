#include "mcp_tcp_client_transport_internal.h" // Use internal header
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
        log_message(LOG_LEVEL_ERROR, "getaddrinfo failed: %s", gai_strerror(rv));
        return -1;
    }

    // Loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((data->sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET_VAL) {
            log_message(LOG_LEVEL_WARN, "Client socket creation failed: %d", sock_errno);
            continue;
        }

        // Blocking connect for client transport simplicity
        if (connect(data->sock, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR_VAL) {
            log_message(LOG_LEVEL_WARN, "Client connect failed: %d", sock_errno);
            close_socket(data->sock);
            data->sock = INVALID_SOCKET_VAL;
            continue;
        }

        break; // If we get here, we successfully connected
    }

    freeaddrinfo(servinfo); // All done with this structure

    if (p == NULL) {
        log_message(LOG_LEVEL_ERROR, "Client failed to connect to %s:%u", data->host, data->port);
        return -1;
    }

    log_message(LOG_LEVEL_INFO, "Client connected to %s:%u on socket %d", data->host, data->port, (int)data->sock);
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
int send_exact_client(socket_t sock, const char* buf, size_t len, bool* running_flag) {
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
int recv_exact_client(socket_t sock, char* buf, size_t len, bool* running_flag) {
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
