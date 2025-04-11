#include "internal/connection_pool_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Platform-specific includes needed for socket operations
#ifdef _WIN32
// Already included via internal header
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#endif

// Creates a TCP socket, connects to host:port with a timeout.
// Returns connected socket descriptor or INVALID_SOCKET_HANDLE on failure.
socket_handle_t create_new_connection(const char* host, int port, int connect_timeout_ms) {
    socket_handle_t sock = INVALID_SOCKET_HANDLE;
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    char port_str[6]; // Max port number is 65535
    int rv;
    int err = 0; // Initialize err

    // Note: WSAStartup is assumed to be called once elsewhere (e.g., pool create or globally)
    // It's generally not safe to call WSAStartup/WSACleanup per connection.

    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo failed for %s:%s : %s", host, port_str, gai_strerror(rv));
        return INVALID_SOCKET_HANDLE;
    }

    // Loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET_HANDLE) {
            #ifdef _WIN32
                mcp_log_warn("socket() failed: %d", WSAGetLastError());
            #else
                mcp_log_warn("socket() failed: %s", strerror(errno));
            #endif
            continue;
        }

        // Set non-blocking for timeout connect
#ifdef _WIN32
        u_long mode = 1; // 1 to enable non-blocking socket
        if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR_HANDLE) {
            mcp_log_error("ioctlsocket(FIONBIO) failed: %d", WSAGetLastError());
            closesocket(sock);
            sock = INVALID_SOCKET_HANDLE;
            continue;
        }
#else
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) {
             mcp_log_error("fcntl(F_GETFL) failed: %s", strerror(errno));
             close(sock);
             sock = INVALID_SOCKET_HANDLE;
             continue;
        }
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
            mcp_log_error("fcntl(F_SETFL, O_NONBLOCK) failed: %s", strerror(errno));
            close(sock);
            sock = INVALID_SOCKET_HANDLE;
            continue;
        }
#endif

        // Initiate non-blocking connect
        rv = connect(sock, p->ai_addr, (int)p->ai_addrlen); // Cast addrlen

#ifdef _WIN32
        if (rv == SOCKET_ERROR_HANDLE) {
            err = WSAGetLastError();
            // WSAEINPROGRESS is not typically returned on Windows for non-blocking connect,
            // WSAEWOULDBLOCK indicates the operation is in progress.
            if (err != WSAEWOULDBLOCK) {
                mcp_log_warn("connect() failed immediately: %d", err);
                closesocket(sock);
                sock = INVALID_SOCKET_HANDLE;
                continue;
            }
            // Connection is in progress (WSAEWOULDBLOCK), use poll/select to wait
            err = WSAEWOULDBLOCK; // Set err for unified handling below
        }
        // else rv == 0 means immediate success (less common for non-blocking)
#else // POSIX
        if (rv == -1) {
            err = errno;
            if (err != EINPROGRESS) {
                mcp_log_warn("connect() failed immediately: %s", strerror(err));
                close(sock);
                sock = INVALID_SOCKET_HANDLE;
                continue;
            }
             // Connection is in progress (EINPROGRESS), use poll/select to wait
        }
        // else rv == 0 means immediate success
#endif

        // If connect returned 0 (immediate success) or EINPROGRESS/WSAEWOULDBLOCK (in progress)
        if (rv == 0 || err == EINPROGRESS || err == WSAEWOULDBLOCK) {
            // If connect succeeded immediately (rv == 0), skip waiting
            if (rv != 0) {
                struct pollfd pfd;
                pfd.fd = sock;
                pfd.events = POLLOUT; // Check for writability

#ifdef _WIN32
                // Use WSAPoll if available, otherwise fallback or require newer Windows SDK
                // For simplicity, using select as a fallback here, but WSAPoll is preferred.
                fd_set write_fds;
                struct timeval tv;
                FD_ZERO(&write_fds);
                FD_SET(sock, &write_fds);
                tv.tv_sec = connect_timeout_ms / 1000;
                tv.tv_usec = (connect_timeout_ms % 1000) * 1000;
                rv = select(0, NULL, &write_fds, NULL, &tv);
#else
                rv = poll(&pfd, 1, connect_timeout_ms);
#endif

                if (rv <= 0) { // Timeout (rv==0) or error (rv<0)
                    if (rv == 0) {
                         mcp_log_warn("connect() timed out after %d ms.", connect_timeout_ms);
                    } else {
                         #ifdef _WIN32
                            mcp_log_error("select() failed during connect: %d", WSAGetLastError());
                         #else
                            mcp_log_error("poll() failed during connect: %s", strerror(errno));
                         #endif
                    }
                    close_connection(sock);
                    sock = INVALID_SOCKET_HANDLE;
                    continue; // Try next address
                }
                // rv > 0, socket is ready, check for errors
            }

            // Check SO_ERROR to confirm connection success after waiting or immediate success
            int optval = 0;
            socklen_t optlen = sizeof(optval);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&optval, &optlen) == SOCKET_ERROR_HANDLE) {
                 #ifdef _WIN32
                    mcp_log_error("getsockopt(SO_ERROR) failed: %d", WSAGetLastError());
                 #else
                    mcp_log_error("getsockopt(SO_ERROR) failed: %s", strerror(errno));
                 #endif
                 close_connection(sock);
                 sock = INVALID_SOCKET_HANDLE;
                 continue; // Try next address
            }

            if (optval != 0) { // Connect failed
                #ifdef _WIN32
                    mcp_log_warn("connect() failed after wait: SO_ERROR=%d (WSA: %d)", optval, optval); // Use optval as error code
                #else
                     mcp_log_warn("connect() failed after wait: %s", strerror(optval)); // Use optval as errno
                #endif
                close_connection(sock);
                sock = INVALID_SOCKET_HANDLE;
                continue; // Try next address
            }
            // Connection successful!
        } else {
             // This case should not be reached if connect returned other errors handled above
             mcp_log_error("Unexpected state after connect() call (rv=%d, err=%d)", rv, err);
             close_connection(sock);
             sock = INVALID_SOCKET_HANDLE;
             continue;
        }


        // If we get here with a valid socket, connection succeeded.
        // Optionally, switch back to blocking mode if desired.
        // For simplicity, leave it non-blocking for now.

        break; // If we get here, we must have connected successfully
    }

    freeaddrinfo(servinfo); // All done with this structure

    if (sock == INVALID_SOCKET_HANDLE) {
        mcp_log_error("Failed to connect to %s:%d after trying all addresses.", host, port);
    } else {
         mcp_log_debug("Successfully connected socket %d to %s:%d.", (int)sock, host, port);
    }

    return sock;
}

void close_connection(socket_handle_t socket_fd) {
    if (socket_fd != INVALID_SOCKET_HANDLE) {
        #ifdef _WIN32
            closesocket(socket_fd);
            // Note: WSACleanup should be called once when the application exits,
            // not after closing each socket.
        #else
            close(socket_fd);
        #endif
    }
}
