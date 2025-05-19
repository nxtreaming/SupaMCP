#include "internal/connection_pool_internal.h"
#include "mcp_socket_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes needed for socket operations
#ifdef _WIN32
// Already included via internal header
#include <winsock2.h>
#include <ws2tcpip.h>
// Define poll-related constants for Windows
#ifndef POLLOUT
#define POLLOUT 0x0004
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#endif

// Forward declarations for static helper functions
static bool restore_socket_blocking(socket_handle_t sock);
static bool wait_for_connection(socket_handle_t sock, int timeout_ms);

/**
 * @brief Restores a socket to blocking mode.
 *
 * This function restores the specified socket to blocking mode.
 *
 * @param sock The socket handle to restore to blocking mode.
 * @return true if successful, false otherwise.
 */
static bool restore_socket_blocking(socket_handle_t sock) {
    if (sock == INVALID_SOCKET_HANDLE) {
        return false;
    }

    // Use the core function with default blocking mode
#ifdef _WIN32
    u_long original_mode = 0; // Default to blocking mode
#else
    int original_flags = 0;   // Will be ignored, just passing a value
#endif

    int result = mcp_socket_restore_blocking((socket_t)sock,
#ifdef _WIN32
                                           original_mode
#else
                                           original_flags
#endif
                                           );

    if (result != 0) {
        mcp_log_error("Failed to restore socket blocking mode");
        return false;
    }

    return true;
}

/**
 * @brief Waits for a connection to complete.
 *
 * This function waits for a non-blocking connection to complete or timeout.
 *
 * @param sock The socket handle that is connecting.
 * @param timeout_ms The timeout in milliseconds.
 * @return true if the connection completed successfully, false otherwise.
 */
static bool wait_for_connection(socket_handle_t sock, int timeout_ms) {
    if (sock == INVALID_SOCKET_HANDLE || timeout_ms <= 0) {
        return false;
    }

    // Wait for the socket to become writable (connection complete)
    fd_set write_fds, error_fds;
    FD_ZERO(&write_fds);
    FD_ZERO(&error_fds);
    FD_SET(sock, &write_fds);
    FD_SET(sock, &error_fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int result;
#ifdef _WIN32
    // Windows select first parameter is ignored
    result = select(0, NULL, &write_fds, &error_fds, &tv);
#else
    // POSIX select needs max fd + 1
    result = select(sock + 1, NULL, &write_fds, &error_fds, &tv);
#endif

    if (result == 0) {
        // Timeout
        mcp_log_warn("Connection timed out after %d ms", timeout_ms);
        return false;
    } else if (result < 0) {
        mcp_log_error("select() failed: %d", mcp_socket_get_lasterror());
        return false;
    }

    // Check if socket has an error
    if (FD_ISSET(sock, &error_fds)) {
        mcp_log_error("Socket has error condition");
        return false;
    }

    // Check if socket is writable
    if (!FD_ISSET(sock, &write_fds)) {
        mcp_log_error("Socket is not writable after select()");
        return false;
    }

    // Check for socket error
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0) {
        mcp_log_error("getsockopt(SO_ERROR) failed: %d", mcp_socket_get_lasterror());
        return false;
    }

    if (error != 0) {
        // Connection failed
        mcp_log_error("Connection failed: %d", error);
        return false;
    }

    return true;
}

/**
 * @brief Creates a TCP socket, connects to host:port with a timeout.
 *
 * This function creates a TCP socket and connects it to the specified host and port
 * with a timeout. It uses the DNS cache to resolve the hostname and tries each
 * address returned by the DNS resolver until one succeeds or all fail.
 *
 * @param host The hostname or IP address to connect to.
 * @param port The port number to connect to.
 * @param connect_timeout_ms The timeout for the connection attempt in milliseconds.
 * @return The connected socket descriptor or INVALID_SOCKET_HANDLE on failure.
 */
socket_handle_t create_new_connection(const char* host, int port, int connect_timeout_ms) {
    // Start timing for performance measurement
    long long connect_start_ms = mcp_get_time_ms();

    socket_handle_t sock = INVALID_SOCKET_HANDLE;
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    int rv;
    int err = 0;
    int attempts = 0;

    // Note: WSAStartup is assumed to be called once elsewhere (e.g., pool create or globally)
    // It's generally not safe to call WSAStartup/WSACleanup per connection.

    // Initialize DNS cache if not already initialized
    if (!g_dns_cache.initialized) {
        dns_cache_init();
    }

    // Set up hints for the type of socket we want
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    // Try to get address info from cache first
    servinfo = dns_cache_get(host, port, &hints);
    if (!servinfo) {
        mcp_log_error("Failed to resolve address for %s:%d", host, port);
        return INVALID_SOCKET_HANDLE;
    }

    // Loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        attempts++;

        // Create socket
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET_HANDLE) {
            mcp_log_warn("socket() failed: %d", mcp_socket_get_lasterror());
            continue;
        }

        // Apply socket optimizations
        mcp_socket_optimize(sock, false); // false = client socket

        int result = mcp_socket_set_non_blocking((socket_t)sock);
        if (result != 0) {
            mcp_log_error("Failed to set socket to non-blocking mode");
            mcp_socket_close(sock);
            sock = INVALID_SOCKET_HANDLE;
            continue;
        }

        // Initiate non-blocking connect
        rv = connect(sock, p->ai_addr, (int)p->ai_addrlen);

        // Check connect result
#ifdef _WIN32
        if (rv == SOCKET_ERROR_HANDLE) {
            err = WSAGetLastError();
            // WSAEWOULDBLOCK indicates the operation is in progress on Windows
            if (err != WSAEWOULDBLOCK) {
                mcp_log_warn("connect() failed immediately: %d", err);
                mcp_socket_close(sock);
                sock = INVALID_SOCKET_HANDLE;
                continue;
            }
            // Connection is in progress, need to wait
        } else {
            // Immediate success (rare but possible)
            mcp_log_debug("Immediate connection success to %s:%d", host, port);
        }
#else
        if (rv == -1) {
            err = errno;
            if (err != EINPROGRESS) {
                mcp_log_warn("connect() failed immediately: %s", strerror(err));
                mcp_socket_close(sock);
                sock = INVALID_SOCKET_HANDLE;
                continue;
            }
            // Connection is in progress, need to wait
        } else {
            // Immediate success
            mcp_log_debug("Immediate connection success to %s:%d", host, port);
        }
#endif

        // If connect didn't succeed immediately, wait for it to complete
        if (rv != 0) {
            if (!wait_for_connection(sock, connect_timeout_ms)) {
                mcp_socket_close(sock);
                sock = INVALID_SOCKET_HANDLE;
                continue;
            }
        }

        // If we get here, connection succeeded
        // Restore blocking mode if needed (uncomment if needed)
        // if (!restore_socket_blocking(sock)) {
        //     mcp_log_warn("Failed to restore blocking mode, but connection succeeded");
        // }

        break; // Successfully connected
    }

    // Release the DNS cache entry
    dns_cache_release(servinfo);

    // Calculate connection time
    long long connect_end_ms = mcp_get_time_ms();
    long long connect_time_ms = connect_end_ms - connect_start_ms;

    if (sock == INVALID_SOCKET_HANDLE) {
        mcp_log_error("Failed to connect to %s:%d after %d attempts (%lld ms)",
                     host, port, attempts, connect_time_ms);
    } else {
        mcp_log_debug("Successfully connected socket %d to %s:%d in %lld ms (attempts: %d)",
                     (int)sock, host, port, connect_time_ms, attempts);
    }

    return sock;
}
