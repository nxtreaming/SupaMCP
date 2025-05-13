#include "mcp_socket_utils.h"
#include "mcp_log.h"
#include "mcp_memory_pool.h"
#include "mcp_cache_aligned.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
    #include <windows.h>
    #include <mstcpip.h>
#else
    #include <sys/time.h>
    #include <poll.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/tcp.h>
#endif

/**
 * @brief Helper function to allocate memory from pool if available, or fallback to malloc
 *
 * @param size Size of memory to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
static inline void* socket_utils_alloc(size_t size) {
    if (mcp_memory_pool_system_is_initialized()) {
        return mcp_pool_alloc(size);
    } else {
        return malloc(size);
    }
}

/**
 * @brief Helper function to free memory allocated with socket_utils_alloc
 *
 * @param ptr Pointer to memory to free
 */
static inline void socket_utils_free(void* ptr) {
    if (!ptr) return;

    if (mcp_memory_pool_system_is_initialized()) {
        mcp_pool_free(ptr);
    } else {
        free(ptr);
    }
}

/**
 * @brief Helper function to log socket errors appropriately based on error code
 *
 * @param function_name Name of the function where error occurred
 * @param sock Socket descriptor
 * @param error_code Error code from mcp_socket_get_last_error()
 * @param is_debug Whether to log as debug (true) or error (false)
 */
static inline void log_socket_error(const char* function_name, socket_t sock, int error_code, bool is_debug) {
#ifdef _WIN32
    // Common Windows socket errors during normal operation
    bool is_normal_error = (error_code == 0 ||
                           error_code == WSAECONNRESET ||
                           error_code == WSAESHUTDOWN ||
                           error_code == WSAENOTCONN ||
                           error_code == WSAECONNABORTED);

    if (is_debug || is_normal_error) {
        mcp_log_debug("%s: Socket %d, error %d", function_name, (int)sock, error_code);
    } else {
        mcp_log_error("%s: Socket %d, error %d", function_name, (int)sock, error_code);
    }
#else
    // Common POSIX socket errors during normal operation
    bool is_normal_error = (error_code == 0 ||
                           error_code == EPIPE ||
                           error_code == ECONNRESET ||
                           error_code == ENOTCONN);

    if (is_debug || is_normal_error) {
        mcp_log_debug("%s: Socket %d, error %d (%s)", function_name, (int)sock, error_code, strerror(error_code));
    } else {
        mcp_log_error("%s: Socket %d, error %d (%s)", function_name, (int)sock, error_code, strerror(error_code));
    }
#endif
}

void mcp_sleep_ms(uint32_t milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

long long mcp_get_time_ms() {
#ifdef _WIN32
    // GetTickCount64 is simpler and often sufficient for intervals
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

int mcp_socket_init(void) {
#ifdef _WIN32
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        mcp_log_error("[MCP Socket] WSAStartup failed: %d", iResult);
        return -1;
    }
#endif
    return 0;
}

void mcp_socket_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

int mcp_socket_close(socket_t sock) {
#ifdef _WIN32
    return closesocket(sock);
#else
    return close(sock);
#endif
}

int mcp_socket_get_last_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

int mcp_socket_set_non_blocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 1; // 1 to enable non-blocking
    if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
        mcp_log_error("ioctlsocket(FIONBIO) failed: %d", mcp_socket_get_last_error());
        return -1;
    }
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        mcp_log_error("fcntl(F_GETFL) failed: %d (%s)", errno, strerror(errno));
        return -1;
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        mcp_log_error("fcntl(F_SETFL, O_NONBLOCK) failed: %d (%s)", errno, strerror(errno));
        return -1;
    }
#endif
    return 0;
}

/**
 * @brief Sets the TCP_NODELAY option on a socket to disable Nagle's algorithm.
 *
 * This function disables Nagle's algorithm, which buffers small packets and
 * waits for more data before sending, to reduce latency for small packets.
 *
 * @param sock The socket to set the option on.
 * @return 0 on success, -1 on error.
 */
int mcp_socket_set_nodelay(socket_t sock) {
    int flag = 1;
    int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(TCP_NODELAY) failed: %d", mcp_socket_get_last_error());
        return -1;
    }
    mcp_log_debug("TCP_NODELAY enabled on socket %d", (int)sock);
    return 0;
}

/**
 * @brief Sets the SO_REUSEADDR option on a socket.
 *
 * This function enables the SO_REUSEADDR option, which allows the socket
 * to be bound to an address that is already in use. This is useful for
 * server applications that need to restart quickly after a crash.
 *
 * @param sock The socket to set the option on.
 * @return 0 on success, -1 on error.
 */
int mcp_socket_set_reuseaddr(socket_t sock) {
    int flag = 1;
    int result = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&flag, sizeof(flag));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(SO_REUSEADDR) failed: %d", mcp_socket_get_last_error());
        return -1;
    }
    mcp_log_debug("SO_REUSEADDR enabled on socket %d", (int)sock);
    return 0;
}

/**
 * @brief Sets the SO_KEEPALIVE option on a socket.
 *
 * This function enables the SO_KEEPALIVE option, which causes the TCP stack
 * to send keepalive probes to detect if a connection is still alive.
 *
 * @param sock The socket to set the option on.
 * @return 0 on success, -1 on error.
 */
int mcp_socket_set_keepalive(socket_t sock) {
    int flag = 1;
    int result = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&flag, sizeof(flag));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(SO_KEEPALIVE) failed: %d", mcp_socket_get_last_error());
        return -1;
    }
    mcp_log_debug("SO_KEEPALIVE enabled on socket %d", (int)sock);
    return 0;
}

/**
 * @brief Sets the send and receive buffer sizes for a socket.
 *
 * This function sets the SO_SNDBUF and SO_RCVBUF options to control
 * the size of the socket's send and receive buffers.
 *
 * @param sock The socket to set the options on.
 * @param send_size The size of the send buffer in bytes (0 to leave unchanged).
 * @param recv_size The size of the receive buffer in bytes (0 to leave unchanged).
 * @return 0 on success, -1 on error.
 */
int mcp_socket_set_buffer_size(socket_t sock, int send_size, int recv_size) {
    int result;

    if (send_size > 0) {
        result = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&send_size, sizeof(send_size));
        if (result == MCP_SOCKET_ERROR) {
            mcp_log_error("setsockopt(SO_SNDBUF) failed: %d", mcp_socket_get_last_error());
            return -1;
        }
    }

    if (recv_size > 0) {
        result = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&recv_size, sizeof(recv_size));
        if (result == MCP_SOCKET_ERROR) {
            mcp_log_error("setsockopt(SO_RCVBUF) failed: %d", mcp_socket_get_last_error());
            return -1;
        }
    }

    mcp_log_debug("Socket buffer sizes set (send: %d, recv: %d) for socket %d",
                 send_size, recv_size, (int)sock);
    return 0;
}

/**
 * @brief Applies common socket optimizations based on the socket's role.
 *
 * This function applies a set of common socket optimizations:
 * - For all sockets: TCP_NODELAY
 * - For server sockets: SO_REUSEADDR, larger receive buffer
 * - For client sockets: SO_KEEPALIVE, larger send buffer
 *
 * @param sock The socket to optimize.
 * @param is_server Whether this is a server socket (true) or client socket (false).
 * @return 0 if all optimizations succeeded, or a negative value indicating how many failed.
 */
int mcp_socket_optimize(socket_t sock, bool is_server) {
    int failures = 0;

    // Check for invalid socket
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Cannot optimize invalid socket");
        return -1;
    }

    // Common optimizations for all sockets
    if (mcp_socket_set_nodelay(sock) != 0) {
        mcp_log_warn("Failed to set TCP_NODELAY on socket %d", (int)sock);
        failures--;
    }

    if (is_server) {
        // Server-specific optimizations
#ifdef _WIN32
        // On Windows, prefer SO_EXCLUSIVEADDRUSE over SO_REUSEADDR for security
        // These options are mutually exclusive on Windows
        int flag = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&flag, sizeof(flag)) == MCP_SOCKET_ERROR) {
            mcp_log_debug("SO_EXCLUSIVEADDRUSE not available, falling back to SO_REUSEADDR");
            // Fall back to SO_REUSEADDR
            if (mcp_socket_set_reuseaddr(sock) != 0) {
                mcp_log_warn("Failed to set SO_REUSEADDR on server socket %d", (int)sock);
                failures--;
            }
        } else {
            mcp_log_debug("SO_EXCLUSIVEADDRUSE enabled on server socket %d", (int)sock);
        }
#else
        // On non-Windows platforms, use SO_REUSEADDR
        if (mcp_socket_set_reuseaddr(sock) != 0) {
            mcp_log_warn("Failed to set SO_REUSEADDR on server socket %d", (int)sock);
            failures--;
        }
#endif

        // Increase receive buffer size for server sockets
        if (mcp_socket_set_buffer_size(sock, 0, 65536) != 0) {
            failures--;
        }
    } else {
        // Client-specific optimizations
        if (mcp_socket_set_keepalive(sock) != 0) {
            mcp_log_warn("Failed to set SO_KEEPALIVE on client socket %d", (int)sock);
            failures--;
        }

        // Increase send buffer size for client sockets
        if (mcp_socket_set_buffer_size(sock, 65536, 0) != 0) {
            failures--;
        }
    }

    if (failures == 0) {
        mcp_log_debug("Socket %d successfully optimized (%s mode)",
                     (int)sock, is_server ? "server" : "client");
    } else {
        mcp_log_warn("Socket %d partially optimized with %d failures (%s mode)",
                    (int)sock, -failures, is_server ? "server" : "client");
    }

    return failures;
}

/**
 * @brief Sets the timeout for socket operations.
 *
 * This function sets both the send and receive timeouts for a socket.
 * A timeout of 0 means blocking mode (no timeout).
 *
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. 0 means no timeout (blocking mode).
 * @return 0 on success, -1 on error.
 */
int mcp_socket_set_timeout(socket_t sock, uint32_t timeout_ms) {
    if (sock == MCP_INVALID_SOCKET) {
        return -1;
    }

#ifdef _WIN32
    // Windows uses milliseconds directly
    DWORD timeout = timeout_ms;
    int result;

    // Set receive timeout
    result = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(SO_RCVTIMEO) failed: %d", mcp_socket_get_last_error());
        return -1;
    }

    // Set send timeout
    result = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(SO_SNDTIMEO) failed: %d", mcp_socket_get_last_error());
        return -1;
    }
#else
    // POSIX uses struct timeval
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int result;

    // Set receive timeout
    result = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(SO_RCVTIMEO) failed: %d (%s)", errno, strerror(errno));
        return -1;
    }

    // Set send timeout
    result = setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    if (result == MCP_SOCKET_ERROR) {
        mcp_log_error("setsockopt(SO_SNDTIMEO) failed: %d (%s)", errno, strerror(errno));
        return -1;
    }
#endif

    mcp_log_debug("Socket timeout set to %u ms for socket %d", timeout_ms, (int)sock);
    return 0;
}

/**
 * @brief Connects to a server address with timeout. Uses non-blocking connect internally.
 *
 * This function implements a non-blocking connect with timeout as specified in the header.
 * It tries each address returned by getaddrinfo until one succeeds or all fail.
 * The returned socket is set back to blocking mode before returning.
 *
 * @param host The hostname or IP address of the server
 * @param port The port number of the server
 * @param timeout_ms Timeout for the connection attempt in milliseconds
 * @return The connected socket descriptor on success, MCP_INVALID_SOCKET on failure or timeout
 */
socket_t mcp_socket_connect(const char* host, uint16_t port, uint32_t timeout_ms) {
    struct addrinfo hints, *servinfo = NULL, *p;
    int rv;
    char port_str[6]; // Stack allocation for small string
    socket_t sock = MCP_INVALID_SOCKET;

    // Default to a reasonable timeout if none specified
    if (timeout_ms == 0) {
        timeout_ms = 15000; // 15 seconds default
    }

    // Convert port to string using stack memory
    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo failed for %s:%s : %s", host, port_str, gai_strerror(rv));
        return MCP_INVALID_SOCKET;
    }

    // Try each address until we successfully connect
    for (p = servinfo; p != NULL; p = p->ai_next) {
        // Create the socket
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == MCP_INVALID_SOCKET) {
            log_socket_error("socket() failed", sock, mcp_socket_get_last_error(), false);
            continue;
        }

        // Set socket to non-blocking mode for timeout support
        if (mcp_socket_set_non_blocking(sock) != 0) {
            mcp_log_warn("Failed to set socket to non-blocking mode, falling back to blocking connect");

            // Fall back to blocking connect
            if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == MCP_SOCKET_ERROR) {
                log_socket_error("connect() failed", sock, mcp_socket_get_last_error(), false);
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }
        } else {
            // Non-blocking connect with timeout
            int result = connect(sock, p->ai_addr, (int)p->ai_addrlen);

            if (result == MCP_SOCKET_ERROR) {
                int error_code = mcp_socket_get_last_error();

                // Check if the error is as expected for non-blocking connect
#ifdef _WIN32
                bool would_block = (error_code == WSAEWOULDBLOCK);
#else
                bool would_block = (error_code == EINPROGRESS || error_code == EWOULDBLOCK);
#endif

                if (!would_block) {
                    // Unexpected error
                    log_socket_error("connect() failed", sock, error_code, false);
                    mcp_socket_close(sock);
                    sock = MCP_INVALID_SOCKET;
                    continue;
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

#ifdef _WIN32
                // Windows select first parameter is ignored
                result = select(0, NULL, &write_fds, &error_fds, &tv);
#else
                // POSIX select needs max fd + 1
                result = select(sock + 1, NULL, &write_fds, &error_fds, &tv);
#endif

                if (result == 0) {
                    // Timeout
                    mcp_log_warn("Connection to %s:%u timed out after %u ms", host, port, timeout_ms);
                    mcp_socket_close(sock);
                    sock = MCP_INVALID_SOCKET;
                    continue;
                } else if (result == MCP_SOCKET_ERROR) {
                    // Select error
                    log_socket_error("select() failed during connect", sock, mcp_socket_get_last_error(), false);
                    mcp_socket_close(sock);
                    sock = MCP_INVALID_SOCKET;
                    continue;
                }

                // Check if the socket is writable or has an error
                if (FD_ISSET(sock, &error_fds)) {
                    // Get the actual connect error
                    int error = 0;
                    socklen_t error_len = sizeof(error);

                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &error_len) == 0) {
                        if (error != 0) {
                            mcp_log_warn("Connection to %s:%u failed: %d", host, port, error);
                            mcp_socket_close(sock);
                            sock = MCP_INVALID_SOCKET;
                            continue;
                        }
                    } else {
                        log_socket_error("getsockopt(SO_ERROR) failed", sock, mcp_socket_get_last_error(), false);
                        mcp_socket_close(sock);
                        sock = MCP_INVALID_SOCKET;
                        continue;
                    }
                }

                if (!FD_ISSET(sock, &write_fds)) {
                    // Socket is not writable, connection failed
                    mcp_log_warn("Connection to %s:%u failed: socket not writable", host, port);
                    mcp_socket_close(sock);
                    sock = MCP_INVALID_SOCKET;
                    continue;
                }
            }

#ifdef _WIN32
            u_long mode = 0; // 0 to disable non-blocking
            if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
                log_socket_error("ioctlsocket(FIONBIO) failed", sock, mcp_socket_get_last_error(), false);
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }
#else
            // Set socket back to blocking mode
            int flags = fcntl(sock, F_GETFL, 0);
            if (flags == -1) {
                log_socket_error("fcntl(F_GETFL) failed", sock, errno, false);
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }
            if (fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) == -1) {
                log_socket_error("fcntl(F_SETFL, ~O_NONBLOCK) failed", sock, errno, false);
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }
#endif
        }

        // Apply client socket optimizations
        mcp_socket_optimize(sock, false);
        // We don't fail the connection just because some optimizations failed

        // Connection successful
        break;
    }

    // Free address info
    if (servinfo) {
        freeaddrinfo(servinfo);
        servinfo = NULL;
    }

    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to %s:%u", host, port);
    } else {
        mcp_log_debug("Connected to %s:%u on socket %d", host, port, (int)sock);
    }

    return sock;
}

int mcp_socket_send_exact(socket_t sock, const char* buf, size_t len, volatile bool* stop_flag) {
    size_t total_sent = 0;
    while (total_sent < len) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            mcp_log_debug("send_exact aborted by stop flag");
            return -1; // Aborted
        }

#ifdef _WIN32
        // Windows send takes int length
        int chunk_len = (len - total_sent > INT_MAX) ? INT_MAX : (int)(len - total_sent);
        int bytes_sent = send(sock, buf + total_sent, chunk_len, MCP_SEND_FLAGS);
#else
        // POSIX send takes size_t length
        ssize_t bytes_sent = send(sock, buf + total_sent, len - total_sent, MCP_SEND_FLAGS);
#endif

        if (bytes_sent == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();

            // Special case: error code 0 during shutdown is normal
            if (error_code == 0) {
                // This is a common case during normal shutdown
                mcp_log_debug("send_exact: Socket closed (socket %d, error: 0)", (int)sock);
                return -1; // Treat as error
            }
#ifdef _WIN32
            else if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN || error_code == WSAECONNABORTED) {
                // Normal socket close during shutdown, log as debug instead of warning
                mcp_log_debug("send_exact: Connection closed/reset (socket %d, error %d)", (int)sock, error_code);
                return -1; // Treat as error
            }
            if (error_code == WSAEWOULDBLOCK) {
                // Should not happen with blocking sockets, but handle defensively
                mcp_log_warn("send_exact got WOULDBLOCK on blocking socket?");
                // Consider waiting or retrying if using non-blocking internally
                continue;
            }
#else
            else if (error_code == EPIPE || error_code == ECONNRESET || error_code == ENOTCONN) {
                // Normal socket close during shutdown, log as debug instead of warning
                mcp_log_debug("send_exact: Connection closed/reset (socket %d, error %d - %s)", (int)sock, error_code, strerror(error_code));
                return -1; // Treat as error
            }
            if (error_code == EINTR) {
                mcp_log_debug("send_exact interrupted, retrying...");
                continue; // Retry if interrupted by signal
            }
            if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
                mcp_log_warn("send_exact got EAGAIN/EWOULDBLOCK on blocking socket?");
                continue;
            }
#endif
            mcp_log_error("send_exact failed (socket %d, len %zu): Error %d", (int)sock, len, error_code);
            return -1; // Other socket error
        }

        // send() can return 0, although docs say it shouldn't for TCP. Treat as error.
        if (bytes_sent == 0) {
            mcp_log_error("send_exact sent 0 bytes unexpectedly (socket %d)", (int)sock);
            return -1;
        }

        total_sent += (size_t)bytes_sent;
    }
    return 0; // Success
}

int mcp_socket_recv_exact(socket_t sock, char* buf, size_t len, volatile bool* stop_flag) {
    size_t total_read = 0;
    while (total_read < len) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            mcp_log_debug("recv_exact aborted by stop flag");
            return -1; // Aborted
        }

#ifdef _WIN32
        // Windows recv takes int length
        int chunk_len = (len - total_read > INT_MAX) ? INT_MAX : (int)(len - total_read);
        int bytes_read = recv(sock, buf + total_read, chunk_len, 0);
#else
        // POSIX recv takes size_t length
        ssize_t bytes_read = recv(sock, buf + total_read, len - total_read, 0);
#endif

        if (bytes_read == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();

            // Special case: error code 0 during shutdown is normal
            if (error_code == 0) {
                // This is a common case during normal shutdown
                mcp_log_debug("recv_exact: Socket closed (socket %d, error: 0)", (int)sock);
                return -1; // Connection closed/error
            }
#ifdef _WIN32
            else if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN || error_code == WSAECONNABORTED) {
                // Normal socket close during shutdown, log as debug instead of warning
                mcp_log_debug("recv_exact: Connection closed/reset (socket %d, error %d)", (int)sock, error_code);
                return -1; // Connection closed/error
            }
            if (error_code == WSAEWOULDBLOCK) {
                mcp_log_warn("recv_exact got WOULDBLOCK on blocking socket?");
                continue;
            }
#else
            else if (error_code == ECONNRESET || error_code == ENOTCONN) {
                // Normal socket close during shutdown, log as debug instead of warning
                mcp_log_debug("recv_exact: Connection closed/reset (socket %d, error %d - %s)", (int)sock, error_code, strerror(error_code));
                return -1; // Connection closed/error
            }
            if (error_code == EINTR) {
                mcp_log_debug("recv_exact interrupted, retrying...");
                continue; // Retry if interrupted by signal
            }
            if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
                mcp_log_warn("recv_exact got EAGAIN/EWOULDBLOCK on blocking socket?");
                continue;
            }
#endif
            mcp_log_error("recv_exact failed (socket %d, len %zu): Error %d", (int)sock, len, error_code);
            return -1; // Other socket error
        } else if (bytes_read == 0) {
            mcp_log_debug("recv_exact: Connection closed gracefully by peer (socket %d)", (int)sock);
            return -1; // Connection closed gracefully
        }

        total_read += (size_t)bytes_read;
    }
    return 0; // Success
}

int mcp_socket_send_vectors(socket_t sock, mcp_iovec_t* iov, int iovcnt, volatile bool* stop_flag) {
    // Calculate total size to send
    size_t total_to_send = 0;
    for (int i = 0; i < iovcnt; ++i) {
#ifdef _WIN32
        total_to_send += iov[i].len;
#else
        total_to_send += iov[i].iov_len;
#endif
    }

    // If total size is 0, return success immediately
    if (total_to_send == 0) {
        return 0;
    }

    size_t total_sent = 0;

#ifdef _WIN32
    // Windows optimized implementation
    DWORD bytes_sent_this_call = 0;
    DWORD flags = 0;

    // Create temporary IOV array to avoid modifying the original array
    WSABUF* temp_iov = NULL;
    if (iovcnt > 1) {
        temp_iov = (WSABUF*)socket_utils_alloc(iovcnt * sizeof(WSABUF));
        if (!temp_iov) {
            mcp_log_error("Failed to allocate memory for temporary IOV array");
            return -1;
        }
        memcpy(temp_iov, iov, iovcnt * sizeof(WSABUF));
    }
    else {
        temp_iov = iov;
    }

    int current_iovcnt = iovcnt;
    int result = 0;

    while (total_sent < total_to_send) {
        // Check stop flag
        if (stop_flag && *stop_flag) {
            mcp_log_debug("send_vectors (Win) aborted by stop flag");
            if (temp_iov != iov) {
                socket_utils_free(temp_iov);
            }
            return -1;
        }

        result = WSASend(sock, temp_iov, (DWORD)current_iovcnt, &bytes_sent_this_call, flags, NULL, NULL);

        if (result == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();

            // Handle common errors
            if (error_code == 0 ||
                error_code == WSAECONNRESET ||
                error_code == WSAESHUTDOWN ||
                error_code == WSAENOTCONN ||
                error_code == WSAECONNABORTED) {
                mcp_log_debug("send_vectors (Win): Connection closed/reset (socket %d, error %d)", (int)sock, error_code);
                if (temp_iov != iov) {
                    socket_utils_free(temp_iov);
                }
                return -1;
            }

            if (error_code == WSAEWOULDBLOCK) {
                // Non-blocking socket would block, retry later
                mcp_log_debug("Socket would block, retrying...");
                continue;
            }

            // Other errors
            mcp_log_error("send_vectors (Win) WSASend failed (socket %d): Error %d", (int)sock, error_code);
            if (temp_iov != iov) {
                socket_utils_free(temp_iov);
            }
            return -1;
        }

        if (bytes_sent_this_call == 0) {
            mcp_log_error("send_vectors (Win) sent 0 bytes unexpectedly (socket %d)", (int)sock);
            if (temp_iov != iov) {
                socket_utils_free(temp_iov);
            }
            return -1;
        }

        total_sent += bytes_sent_this_call;

        // If not all data sent, adjust IOV array
        if (total_sent < total_to_send) {
            size_t bytes_remaining = bytes_sent_this_call;
            int i = 0;

            // Skip fully sent buffers
            while (i < current_iovcnt && bytes_remaining >= temp_iov[i].len) {
                bytes_remaining -= temp_iov[i].len;
                i++;
            }

            // Adjust partially sent buffer
            if (i < current_iovcnt && bytes_remaining > 0) {
                temp_iov[i].buf += bytes_remaining;
                temp_iov[i].len -= (ULONG)bytes_remaining;
            }

            // Move array pointer
            if (i > 0) {
                temp_iov += i;
                current_iovcnt -= i;
            }
        }
    }

    if (temp_iov != iov) {
        socket_utils_free(temp_iov);
    }
#else
    // POSIX optimized implementation
    struct iovec* temp_iov = NULL;
    if (iovcnt > 1) {
        temp_iov = (struct iovec*)socket_utils_alloc(iovcnt * sizeof(struct iovec));
        if (!temp_iov) {
            mcp_log_error("Failed to allocate memory for temporary IOV array");
            return -1;
        }
        memcpy(temp_iov, iov, iovcnt * sizeof(struct iovec));
    } else {
        temp_iov = iov;
    }

    int current_iovcnt = iovcnt;

    while (total_sent < total_to_send) {
        // Check stop flag
        if (stop_flag && *stop_flag) {
            mcp_log_debug("send_vectors (POSIX) aborted by stop flag");
            if (temp_iov != iov) {
                socket_utils_free(temp_iov);
            }
            return -1;
        }

        ssize_t bytes_sent = writev(sock, temp_iov, current_iovcnt);

        if (bytes_sent == MCP_SOCKET_ERROR) {
            int error_code = errno;

            if (error_code == EINTR) {
                // Interrupted by signal, retry
                mcp_log_debug("send_vectors (POSIX) interrupted, retrying...");
                continue;
            }

            if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
                // Non-blocking socket would block, retry later
                mcp_log_debug("Socket would block, retrying...");
                continue;
            }

            if (error_code == 0 ||
                error_code == EPIPE ||
                error_code == ECONNRESET ||
                error_code == ENOTCONN) {
                // Connection closed
                mcp_log_debug("send_vectors (POSIX): Connection closed/reset (socket %d, error %d - %s)",
                             (int)sock, error_code, strerror(error_code));
                if (temp_iov != iov) {
                    socket_utils_free(temp_iov);
                }
                return -1;
            }

            // Other errors
            mcp_log_error("send_vectors (POSIX) writev failed (socket %d): Error %d (%s)",
                         (int)sock, error_code, strerror(error_code));
            if (temp_iov != iov) {
                socket_utils_free(temp_iov);
            }
            return -1;
        }

        if (bytes_sent == 0) {
            mcp_log_error("send_vectors (POSIX) sent 0 bytes unexpectedly (socket %d)", (int)sock);
            if (temp_iov != iov) {
                socket_utils_free(temp_iov);
            }
            return -1;
        }

        total_sent += bytes_sent;

        // If not all data sent, adjust IOV array
        if (total_sent < total_to_send) {
            size_t bytes_remaining = bytes_sent;
            int i = 0;

            // Skip fully sent buffers
            while (i < current_iovcnt && bytes_remaining >= temp_iov[i].iov_len) {
                bytes_remaining -= temp_iov[i].iov_len;
                i++;
            }

            // Adjust partially sent buffer
            if (i < current_iovcnt && bytes_remaining > 0) {
                temp_iov[i].iov_base = (char*)temp_iov[i].iov_base + bytes_remaining;
                temp_iov[i].iov_len -= bytes_remaining;
            }

            // Move array pointer
            if (i > 0) {
                temp_iov += i;
                current_iovcnt -= i;
            }
        }
    }

    if (temp_iov != iov) {
        socket_utils_free(temp_iov);
    }
#endif

    return (total_sent == total_to_send) ? 0 : -1; // Success only if all bytes sent
}

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4324) // Disable padding warning
#endif

/**
 * @brief Cache-aligned structure for socket wait state to prevent false sharing
 */
typedef MCP_CACHE_ALIGNED struct {
    socket_t sock;                 // Socket to wait on
    int timeout_ms;                // Timeout in milliseconds
    volatile bool* stop_flag;      // Optional stop flag
    int result;                    // Result of the wait operation
    time_t start_time;             // Start time for timeout calculation
    // Padding to ensure the structure occupies a full cache line
    char padding[MCP_CACHE_LINE_SIZE - sizeof(socket_t) - sizeof(bool *)
                                     - 2 * sizeof(int) - sizeof(time_t)];
} socket_wait_state_t;

#ifdef _MSC_VER
#   pragma warning(pop) // Restore warning settings
#endif

/**
 * @brief Waits for a socket to become readable or until a timeout occurs.
 *
 * This function uses select() on Windows and poll() on POSIX systems to wait
 * for a socket to become readable. It periodically checks the stop_flag to
 * allow for early termination of the wait.
 *
 * @param sock The socket descriptor
 * @param timeout_ms Timeout in milliseconds. 0 means no wait, -1 means wait indefinitely
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early
 * @return 1 if readable, 0 if timeout, -1 on error or if aborted by stop_flag
 */
int mcp_socket_wait_readable(socket_t sock, int timeout_ms, volatile bool* stop_flag) {
    // Abort if stop_flag is provided and is true
    if (stop_flag && *stop_flag) return -1; // Aborted

    // Check for invalid socket
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("mcp_socket_wait_readable called with invalid socket");
        return -1;
    }

    // Allocate wait state from memory pool
    socket_wait_state_t* state = (socket_wait_state_t*)socket_utils_alloc(sizeof(socket_wait_state_t));
    if (!state) {
        mcp_log_error("Failed to allocate socket wait state");
        return -1;
    }

    // Initialize wait state
    state->sock = sock;
    state->timeout_ms = timeout_ms;
    state->stop_flag = stop_flag;
    state->result = 0;
    state->start_time = time(NULL);

#ifdef _WIN32
    fd_set read_fds;
    struct timeval tv;
    struct timeval* tv_ptr = NULL;

    // select timeout: NULL=infinite, 0=poll, >0=timeout
    if (timeout_ms < 0) {
        tv_ptr = NULL; // Infinite wait
    } else {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    // Periodically check stop_flag if timeout is long or infinite
    int check_interval_ms = 250; // Check stop flag more frequently (250ms)
    struct timeval interval_tv = {0, check_interval_ms * 1000};
    struct timeval* current_tv = (timeout_ms < 0 || timeout_ms > check_interval_ms) ? &interval_tv : tv_ptr;

    while (1) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            socket_utils_free(state);
            return -1; // Check before select
        }

        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        // Windows select first parameter is ignored
        int result = select(0, &read_fds, NULL, NULL, current_tv);

        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            socket_utils_free(state);
            return -1; // Check after select
        }

        if (result == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();
            if (error_code == WSAEINTR) {
                // Interrupted, loop again
                continue;
            }

            // Log the error
            log_socket_error("select() failed", sock, error_code, false);
            socket_utils_free(state);
            return -1; // Error
        } else if (result == 0) {
            // Timeout occurred
            if (timeout_ms < 0) {
                // Infinite wait, continue polling
                continue;
            } else if (timeout_ms == 0) {
                // Zero timeout (poll)
                socket_utils_free(state);
                return 0; // Timed out immediately
            } else {
                // Finite timeout
                time_t current_time = time(NULL);
                long elapsed_ms = (long)(difftime(current_time, state->start_time) * 1000);

                if (elapsed_ms >= timeout_ms) {
                    socket_utils_free(state);
                    return 0; // Final timeout expired
                }

                // Adjust remaining time and continue with interval check
                long remaining_ms = timeout_ms - elapsed_ms;
                if (remaining_ms <= 0) {
                    socket_utils_free(state);
                    return 0; // Safety check
                }

                // Use the smaller of check_interval_ms and remaining_ms
                if (remaining_ms < check_interval_ms) {
                    interval_tv.tv_sec = remaining_ms / 1000;
                    interval_tv.tv_usec = (remaining_ms % 1000) * 1000;
                }
                continue;
            }
        } else {
            // Socket is readable
            if (FD_ISSET(sock, &read_fds)) {
                socket_utils_free(state);
                return 1; // Readable
            }

            // Should not happen if result > 0
            mcp_log_warn("select returned > 0 but socket not set?");
            socket_utils_free(state);
            return -1;
        }
    } // end while

#else
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    // poll timeout: -1=infinite, 0=poll, >0=timeout_ms
    int check_interval_ms = 250; // Check stop flag more frequently (250ms)

    while (1) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            socket_utils_free(state);
            return -1; // Check before poll
        }

        // Calculate current timeout
        int current_timeout;
        if (timeout_ms < 0) {
            // Infinite wait, but check stop_flag periodically
            current_timeout = check_interval_ms;
        } else if (timeout_ms == 0) {
            // Immediate poll
            current_timeout = 0;
        } else {
            // Calculate remaining time
            time_t current_time = time(NULL);
            long elapsed_ms = (long)(difftime(current_time, state->start_time) * 1000);

            if (elapsed_ms >= timeout_ms) {
                socket_utils_free(state);
                return 0; // Timeout expired
            }

            long remaining_ms = timeout_ms - elapsed_ms;
            // Use the smaller of check_interval_ms and remaining_ms
            current_timeout = (remaining_ms < check_interval_ms) ? (int)remaining_ms : check_interval_ms;
        }

        int result = poll(&pfd, 1, current_timeout);

        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            socket_utils_free(state);
            return -1; // Check after poll
        }

        if (result < 0) {
            if (errno == EINTR) continue; // Interrupted, loop again

            // Log the error
            log_socket_error("poll() failed", sock, errno, false);
            socket_utils_free(state);
            return -1; // Error
        } else if (result == 0) {
            // Timeout occurred
            if (timeout_ms < 0) {
                // Infinite wait, continue polling
                continue;
            } else if (timeout_ms == 0) {
                // Zero timeout (poll)
                socket_utils_free(state);
                return 0; // Timed out immediately
            } else {
                // Check if the overall timeout has expired
                time_t current_time = time(NULL);
                if (difftime(current_time, state->start_time) * 1000 >= timeout_ms) {
                    socket_utils_free(state);
                    return 0; // Final timeout expired
                }
                // Continue polling
                continue;
            }
        } else {
            // Socket has events
            if (pfd.revents & POLLIN) {
                socket_utils_free(state);
                return 1; // Readable
            }
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                // Use debug level for normal socket events during shutdown
                log_socket_error("poll() reported error event", sock, pfd.revents, true);
                socket_utils_free(state);
                return -1; // Socket error indicated by poll
            }

            // Should not happen if result > 0
            mcp_log_warn("poll returned > 0 but no POLLIN or error event?");
            socket_utils_free(state);
            return -1;
        }
    } // end while
#endif
}

/**
 * @brief Creates a listening socket bound to the specified host and port.
 *
 * This function creates a socket, binds it to the specified address and port,
 * and puts it in listening mode. It also sets appropriate socket options for
 * better performance.
 *
 * @param host The host address to bind to (e.g., "0.0.0.0" for all interfaces)
 * @param port The port number to bind to
 * @param backlog The maximum length of the queue of pending connections
 * @return The listening socket descriptor on success, MCP_INVALID_SOCKET on failure
 */
socket_t mcp_socket_create_listener(const char* host, uint16_t port, int backlog) {
    socket_t listen_sock = MCP_INVALID_SOCKET;
    struct addrinfo hints, *servinfo = NULL, *p;
    int rv;
    char port_str[6]; // Stack allocation for small string

    // Convert port to string using stack memory
    snprintf(port_str, sizeof(port_str), "%u", port);

    // Set up address hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // Use IPv4 for simplicity, matching client connect
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // Use my IP if host is NULL

    // Get address info
    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo for listener failed: %s", gai_strerror(rv));
        return MCP_INVALID_SOCKET;
    }

    // Try each address until we successfully bind
    for (p = servinfo; p != NULL; p = p->ai_next) {
        // Create socket
        if ((listen_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == MCP_INVALID_SOCKET) {
            log_socket_error("Listener socket() failed", MCP_INVALID_SOCKET, mcp_socket_get_last_error(), false);
            continue;
        }

        // Apply server socket optimizations
        if (mcp_socket_optimize(listen_sock, true) < -2) {
            // If more than 2 optimizations failed, consider it a critical failure
            mcp_log_error("Failed to apply critical socket optimizations on listener socket %d", (int)listen_sock);
            mcp_socket_close(listen_sock);
            listen_sock = MCP_INVALID_SOCKET;
            continue;
        }

        // Bind socket to address
        if (bind(listen_sock, p->ai_addr, (int)p->ai_addrlen) == MCP_SOCKET_ERROR) {
            log_socket_error("Listener bind() failed", listen_sock, mcp_socket_get_last_error(), false);
            mcp_socket_close(listen_sock);
            listen_sock = MCP_INVALID_SOCKET;
            continue;
        }

        // Successfully bound
        break;
    }

    // Free address info
    if (servinfo) {
        freeaddrinfo(servinfo);
        servinfo = NULL;
    }

    // Check if we successfully bound
    if (listen_sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to bind listener socket to %s:%u", host, port);
        return MCP_INVALID_SOCKET;
    }

    // Start listening
    if (listen(listen_sock, backlog) == MCP_SOCKET_ERROR) {
        log_socket_error("Listener listen() failed", listen_sock, mcp_socket_get_last_error(), false);
        mcp_socket_close(listen_sock);
        return MCP_INVALID_SOCKET;
    }

    mcp_log_info("Server listening on %s:%u (socket %d)", host, port, (int)listen_sock);
    return listen_sock;
}

/**
 * @brief Accepts a new connection on a listening socket.
 *
 * This function accepts a new connection on a listening socket and sets
 * appropriate socket options for better performance.
 *
 * @param listen_sock The listening socket descriptor
 * @param client_addr Optional pointer to store the client's address information
 * @param addr_len Optional pointer to store the size of the client_addr structure
 * @return The connected client socket descriptor on success, MCP_INVALID_SOCKET on failure
 */
socket_t mcp_socket_accept(socket_t listen_sock, struct sockaddr* client_addr, socklen_t* addr_len) {
    // Check for invalid listening socket
    if (listen_sock == MCP_INVALID_SOCKET) {
        mcp_log_error("mcp_socket_accept called with invalid listening socket");
        return MCP_INVALID_SOCKET;
    }

    // Accept the connection
    socket_t client_sock = accept(listen_sock, client_addr, addr_len);

    // Check if accept was successful
    if (client_sock != MCP_INVALID_SOCKET) {
        // Apply client socket optimizations
        mcp_socket_optimize(client_sock, false);
        // We don't close the socket just because some optimizations failed

        // Log successful connection
        mcp_log_debug("Accepted new connection on socket %d", (int)client_sock);
    } else {
        // Handle accept error
        int error_code = mcp_socket_get_last_error();

#ifdef _WIN32
        // Common non-fatal errors on Windows
        bool is_non_fatal = (error_code == WSAEWOULDBLOCK ||
                            error_code == WSAEINTR ||
                            error_code == WSAECONNABORTED);
#else
        // Common non-fatal errors on POSIX
        bool is_non_fatal = (error_code == EWOULDBLOCK ||
                            error_code == EAGAIN ||
                            error_code == EINTR ||
                            error_code == ECONNABORTED);
#endif

        // Log appropriately based on error type
        log_socket_error("accept() failed", listen_sock, error_code, is_non_fatal);
    }

    return client_sock;
}

/**
 * @brief Connects to a server address in non-blocking mode with timeout.
 *
 * This function creates a non-blocking socket and attempts to connect to the specified
 * server address. It waits for the connection to complete or timeout using select().
 * The returned socket remains in non-blocking mode.
 *
 * @param host The hostname or IP address of the server.
 * @param port The port number of the server.
 * @param timeout_ms Timeout for the connection attempt in milliseconds.
 * @return The connected non-blocking socket on success, MCP_INVALID_SOCKET on failure or timeout.
 */
socket_t mcp_socket_connect_nonblocking(const char* host, uint16_t port, uint32_t timeout_ms) {
    struct addrinfo hints, *servinfo = NULL, *p;
    int rv;
    char port_str[6]; // Stack allocation for small string
    socket_t sock = MCP_INVALID_SOCKET;

    // Default to a reasonable timeout if none specified
    if (timeout_ms == 0) {
        timeout_ms = 15000; // 15 seconds default
    }

    // Convert port to string using stack memory
    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo failed for %s:%s : %s", host, port_str, gai_strerror(rv));
        return MCP_INVALID_SOCKET;
    }

    // Try each address until we successfully connect
    for (p = servinfo; p != NULL; p = p->ai_next) {
        // Create the socket
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == MCP_INVALID_SOCKET) {
            mcp_log_debug("socket() failed: %d", mcp_socket_get_last_error());
            continue;
        }

        // Set socket to non-blocking mode
        if (mcp_socket_set_non_blocking(sock) != 0) {
            mcp_log_error("Failed to set socket to non-blocking mode");
            mcp_socket_close(sock);
            sock = MCP_INVALID_SOCKET;
            continue;
        }

        // Attempt to connect
        int result = connect(sock, p->ai_addr, (int)p->ai_addrlen);

        if (result == 0) {
            // Immediate success (rare but possible)
            mcp_log_debug("Immediate connection success to %s:%u", host, port);
            break;
        }

        if (result == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();

            // Check if the error is as expected for non-blocking connect
#ifdef _WIN32
            bool would_block = (error_code == WSAEWOULDBLOCK);
#else
            bool would_block = (error_code == EINPROGRESS || error_code == EWOULDBLOCK);
#endif

            if (!would_block) {
                // Unexpected error
                mcp_log_debug("connect() failed with error: %d", error_code);
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }

            // Connection in progress, wait for completion or timeout
            fd_set write_fds, error_fds;
            FD_ZERO(&write_fds);
            FD_ZERO(&error_fds);
            FD_SET(sock, &write_fds);
            FD_SET(sock, &error_fds);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

#ifdef _WIN32
            // Windows select first parameter is ignored
            result = select(0, NULL, &write_fds, &error_fds, &tv);
#else
            // POSIX select needs max fd + 1
            result = select(sock + 1, NULL, &write_fds, &error_fds, &tv);
#endif

            if (result == 0) {
                // Timeout
                mcp_log_warn("Connection to %s:%u timed out after %u ms", host, port, timeout_ms);
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            } else if (result < 0) {
                // Select error
                mcp_log_error("select() failed during connect: %d", mcp_socket_get_last_error());
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }

            // Check if socket has an error
            if (FD_ISSET(sock, &error_fds)) {
                int error = 0;
                socklen_t len = sizeof(error);

                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0) {
                    mcp_log_error("getsockopt(SO_ERROR) failed: %d", mcp_socket_get_last_error());
                    mcp_socket_close(sock);
                    sock = MCP_INVALID_SOCKET;
                    continue;
                }

                if (error != 0) {
                    mcp_log_debug("Connection failed with error: %d", error);
                    mcp_socket_close(sock);
                    sock = MCP_INVALID_SOCKET;
                    continue;
                }
            }

            // Check if socket is writable (connected)
            if (!FD_ISSET(sock, &write_fds)) {
                mcp_log_warn("Socket not writable after select()");
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }

            // Double-check connection status
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len) < 0 || error != 0) {
                mcp_log_debug("Connection failed after select: %d", error ? error : mcp_socket_get_last_error());
                mcp_socket_close(sock);
                sock = MCP_INVALID_SOCKET;
                continue;
            }

            // Connection successful
            mcp_log_debug("Connected to %s:%u on socket %d (non-blocking)", host, port, (int)sock);
            break;
        }
    }

    // Free address info
    if (servinfo) {
        freeaddrinfo(servinfo);
    }

    if (sock != MCP_INVALID_SOCKET) {
        // Apply socket optimizations
        mcp_socket_optimize(sock, false); // false = client socket
    } else {
        mcp_log_error("Failed to connect to %s:%u", host, port);
    }

    return sock;
}

/**
 * @brief Sends multiple buffers in a batch operation.
 *
 * This function sends multiple buffers over a socket using vectored I/O internally.
 * It handles the conversion from cache-aligned mcp_socket_buffer_t to platform-specific
 * iovec structures. The cache alignment of the buffer structures helps prevent false
 * sharing in multi-threaded environments.
 *
 * @param sock The socket descriptor.
 * @param buffers Array of pointers to socket buffer structures.
 * @param buffer_count Number of buffers in the array.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error or if aborted by stop_flag.
 */
int mcp_socket_send_batch(socket_t sock, const mcp_socket_buffer_t** buffers, int buffer_count, volatile bool* stop_flag) {
    // Parameter validation
    if (sock == MCP_INVALID_SOCKET || !buffers || buffer_count <= 0) {
        mcp_log_error("Invalid parameters in mcp_socket_send_batch");
        return -1;
    }

    // Create IOV array using cache-aware memory allocation
    mcp_iovec_t* iov = (mcp_iovec_t*)socket_utils_alloc(buffer_count * sizeof(mcp_iovec_t));
    if (!iov) {
        mcp_log_error("Failed to allocate memory for IOV array");
        return -1;
    }

    // Calculate total bytes to send for logging
    size_t total_bytes = 0;

    // Fill IOV array from cache-aligned buffer structures
    for (int i = 0; i < buffer_count; i++) {
        if (!buffers[i] || !buffers[i]->buffer || buffers[i]->used == 0) {
            // Skip empty buffers
            continue;
        }

#ifdef _WIN32
        iov[i].buf = buffers[i]->buffer;
        iov[i].len = (ULONG)buffers[i]->used;
#else
        iov[i].iov_base = buffers[i]->buffer;
        iov[i].iov_len = buffers[i]->used;
#endif
        total_bytes += buffers[i]->used;
    }

    mcp_log_debug("Sending batch of %d buffers, total %zu bytes on socket %d",
                 buffer_count, total_bytes, (int)sock);

    // Use vectored send
    int result = mcp_socket_send_vectors(sock, iov, buffer_count, stop_flag);

    // Free IOV array
    socket_utils_free(iov);

    if (result == 0) {
        mcp_log_debug("Successfully sent %zu bytes in batch on socket %d",
                     total_bytes, (int)sock);
    } else {
        mcp_log_error("Failed to send batch on socket %d", (int)sock);
    }

    return result;
}