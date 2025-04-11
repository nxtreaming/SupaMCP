#include "gateway_socket_utils.h"
#include "mcp_log.h"
#include "mcp_types.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes and definitions
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define sock_errno WSAGetLastError()
#define MSG_NOSIGNAL 0 // No MSG_NOSIGNAL on Windows, flags parameter is ignored by send/recv
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h> // For timeval
#include <poll.h>     // For poll
#define sock_errno errno
#define closesocket close
#endif

// Define SOCKET_ERROR if not defined (e.g., on POSIX)
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

// Sends exactly 'len' bytes from 'buf' over 'sock'.
// Returns 0 on success, -1 on socket error, -2 on timeout.
static int send_exact(SOCKET sock, const char* buf, size_t len, int timeout_ms) {
    size_t total_sent = 0;
    struct timeval tv;
    fd_set writefds;
    int select_ret;

    while (total_sent < len) {
        // Set timeout for select
        if (timeout_ms > 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
        }

        FD_ZERO(&writefds);
        FD_SET(sock, &writefds);

        // Wait until the socket is ready to write or timeout occurs
        select_ret = select((int)sock + 1, NULL, &writefds, NULL, (timeout_ms > 0) ? &tv : NULL);

        if (select_ret == SOCKET_ERROR) {
            mcp_log_error("select() failed during send_exact: %d", sock_errno);
            return -1; // Socket error
        } else if (select_ret == 0) {
            mcp_log_warn("send_exact timed out after %d ms", timeout_ms);
            return -2; // Timeout
        }

        // Socket is ready to write
        int bytes_sent = send(sock, buf + total_sent, (int)(len - total_sent), MSG_NOSIGNAL);
        if (bytes_sent == SOCKET_ERROR) {
            mcp_log_error("send() failed during send_exact: %d", sock_errno);
            return -1; // Socket error
        }
        if (bytes_sent == 0) {
             // This typically shouldn't happen with a ready socket, but indicates closure.
             mcp_log_error("send() returned 0, connection likely closed.");
             return -1;
        }
        total_sent += bytes_sent;
    }
    return 0; // Success
}

// Receives exactly 'len' bytes into 'buf' from 'sock'.
// Returns 0 on success, -1 on socket error, -2 on timeout, -3 on connection closed cleanly.
static int recv_exact(SOCKET sock, char* buf, size_t len, int timeout_ms) {
    size_t total_received = 0;
    struct timeval tv;
    fd_set readfds;
    int select_ret;

    while (total_received < len) {
        // Set timeout for select
        if (timeout_ms > 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
        }

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // Wait until the socket has data or timeout occurs
        select_ret = select((int)sock + 1, &readfds, NULL, NULL, (timeout_ms > 0) ? &tv : NULL);

        if (select_ret == SOCKET_ERROR) {
            mcp_log_error("select() failed during recv_exact: %d", sock_errno);
            return -1; // Socket error
        } else if (select_ret == 0) {
            mcp_log_warn("recv_exact timed out after %d ms", timeout_ms);
            return -2; // Timeout
        }

        // Socket has data
        int bytes_received = recv(sock, buf + total_received, (int)(len - total_received), 0);
        if (bytes_received == SOCKET_ERROR) {
            mcp_log_error("recv() failed during recv_exact: %d", sock_errno);
            return -1; // Socket error
        }
        if (bytes_received == 0) {
            // Connection closed cleanly by the peer
            mcp_log_info("recv() returned 0, connection closed by peer.");
            return -3;
        }
        total_received += bytes_received;
    }
    return 0; // Success
}

int gateway_send_message(SOCKET sock, const char* message, int timeout_ms) {
    if (sock == INVALID_SOCKET || message == NULL) {
        return -1;
    }

    size_t json_len = strlen(message);
    if (json_len == 0 || json_len > MAX_MCP_MESSAGE_SIZE) {
        mcp_log_error("Invalid message length (%zu) for gateway send.", json_len);
        return -1;
    }

    uint32_t net_len = htonl((uint32_t)json_len);
    size_t total_len = sizeof(net_len) + json_len;
    char* send_buffer = (char*)malloc(total_len); // No need for +1 here

    if (send_buffer == NULL) {
        mcp_log_error("Failed to allocate send buffer in gateway_send_message.");
        return -1;
    }

    memcpy(send_buffer, &net_len, sizeof(net_len));
    memcpy(send_buffer + sizeof(net_len), message, json_len);

    mcp_log_debug("Gateway sending %zu bytes (len=%zu) to socket %d", total_len, json_len, (int)sock);
    int send_status = send_exact(sock, send_buffer, total_len, timeout_ms);

    free(send_buffer);
    return send_status; // 0 on success, -1 on error, -2 on timeout
}

int gateway_receive_message(SOCKET sock, char** message_out, size_t* message_len_out, size_t max_size, int timeout_ms) {
    if (sock == INVALID_SOCKET || message_out == NULL || message_len_out == NULL) {
        return -1; // Invalid params
    }

    *message_out = NULL;
    *message_len_out = 0;

    char length_buf[4];
    uint32_t message_length_net, message_length_host;

    // 1. Read length prefix
    int recv_status = recv_exact(sock, length_buf, 4, timeout_ms);
    if (recv_status != 0) {
        // -1: error, -2: timeout, -3: closed
        return recv_status;
    }

    // 2. Decode length
    memcpy(&message_length_net, length_buf, 4);
    message_length_host = ntohl(message_length_net);

    // 3. Validate length
    if (message_length_host == 0 || message_length_host > max_size) {
        mcp_log_error("Invalid message length received in gateway: %u (max: %zu)", message_length_host, max_size);
        return -4; // Invalid length error
    }

    // 4. Allocate buffer (+1 for null terminator)
    char* message_buf = (char*)malloc(message_length_host + 1);
    if (message_buf == NULL) {
        mcp_log_error("Failed to allocate receive buffer in gateway_receive_message.");
        return -1; // Treat as internal error
    }

    // 5. Read message body
    recv_status = recv_exact(sock, message_buf, message_length_host, timeout_ms);
    if (recv_status != 0) {
        free(message_buf);
        // -1: error, -2: timeout, -3: closed
        return recv_status;
    }

    // 6. Null-terminate and return
    message_buf[message_length_host] = '\0';
    *message_out = message_buf;
    *message_len_out = message_length_host;

    mcp_log_debug("Gateway received %zu bytes from socket %d", message_length_host, (int)sock);
    return 0; // Success
}
