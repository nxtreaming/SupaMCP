#include "mcp_socket_utils.h"
#include "mcp_log.h"

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

void mcp_sleep_ms(uint32_t milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
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

// Basic blocking connect implementation (similar to original client)
// TODO: Implement non-blocking connect with timeout as per header comment if needed.
socket_t mcp_socket_connect(const char* host, uint16_t port, uint32_t timeout_ms) {
    (void)timeout_ms; // Timeout not implemented in this basic version

    struct addrinfo hints, *servinfo, *p;
    int rv;
    char port_str[6];
    socket_t sock = MCP_INVALID_SOCKET;

    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo failed for %s:%s : %s", host, port_str, gai_strerror(rv));
        return MCP_INVALID_SOCKET;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == MCP_INVALID_SOCKET) {
            mcp_log_warn("socket() failed: %d", mcp_socket_get_last_error());
            continue;
        }

        // Simple blocking connect
        if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == MCP_SOCKET_ERROR) {
            mcp_log_warn("connect() to %s:%u failed: %d", host, port, mcp_socket_get_last_error());
            mcp_socket_close(sock);
            sock = MCP_INVALID_SOCKET;
            continue;
        }

        // Set TCP_NODELAY to disable Nagle's algorithm and reduce latency
        if (mcp_socket_set_nodelay(sock) != 0) {
            mcp_log_warn("Failed to set TCP_NODELAY on socket %d, continuing anyway", (int)sock);
            // We don't fail the connection just because TCP_NODELAY failed
        }

        break; // Success
    }

    freeaddrinfo(servinfo);

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
    size_t total_to_send = 0;
    for (int i = 0; i < iovcnt; ++i) {
#ifdef _WIN32
        total_to_send += iov[i].len;
#else
        total_to_send += iov[i].iov_len;
#endif
    }

    size_t total_sent = 0;

#ifdef _WIN32
    DWORD bytes_sent_this_call = 0;
    DWORD flags = 0; // No flags needed for basic WSASend

    while (total_sent < total_to_send) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            mcp_log_debug("send_vectors (Win) aborted by stop flag");
            return -1;
        }

        // WSASend might modify the iov array, but we manage adjustments manually if needed
        int result = WSASend(sock, iov, (DWORD)iovcnt, &bytes_sent_this_call, flags, NULL, NULL);

        if (result == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();

            // Special case: error code 0 during shutdown is normal
            if (error_code == 0) {
                // This is a common case during normal shutdown
                mcp_log_debug("send_vectors (Win): Socket closed (socket %d, error: 0)", (int)sock);
                return -1;
            }
            else if (error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAENOTCONN || error_code == WSAECONNABORTED) {
                // Normal socket close during shutdown, log as debug instead of warning
                mcp_log_debug("send_vectors (Win): Connection closed/reset (socket %d, error %d)", (int)sock, error_code);
                return -1;
            }
            if (error_code == WSAEWOULDBLOCK) {
                mcp_log_warn("send_vectors (Win) got WOULDBLOCK?");
                // Need to wait/retry if non-blocking
                continue;
            }
            mcp_log_error("send_vectors (Win) WSASend failed (socket %d): Error %d", (int)sock, error_code);
            return -1;
        }

        if (bytes_sent_this_call == 0) {
            mcp_log_error("send_vectors (Win) sent 0 bytes unexpectedly (socket %d)", (int)sock);
            return -1;
        }

        total_sent += bytes_sent_this_call;

        // Adjust iovec array for the next iteration if partial write occurred
        if (total_sent < total_to_send) {
            size_t sent_adjust = bytes_sent_this_call;
            int current_iov = 0;
            while (sent_adjust > 0 && current_iov < iovcnt) {
                if (sent_adjust < iov[current_iov].len) {
                    iov[current_iov].buf += sent_adjust;
                    iov[current_iov].len -= (ULONG)sent_adjust;
                    sent_adjust = 0;
                } else {
                    sent_adjust -= iov[current_iov].len;
                    iov[current_iov].len = 0; // Mark as fully sent
                    current_iov++;
                }
            }
            // Adjust pointers for the next WSASend call
            iov += current_iov;
            iovcnt -= current_iov;
            if (iovcnt <= 0) break; // Should have sent everything
        }
    } // end while

#else
    while (total_sent < total_to_send) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) {
            mcp_log_debug("send_vectors (POSIX) aborted by stop flag");
            return -1;
        }

        ssize_t bytes_sent = writev(sock, iov, iovcnt);

        if (bytes_sent == MCP_SOCKET_ERROR) {
            int error_code = mcp_socket_get_last_error();

            // Special case: error code 0 during shutdown is normal
            if (error_code == 0) {
                // This is a common case during normal shutdown
                mcp_log_debug("send_vectors (POSIX): Socket closed (socket %d, error: 0)", (int)sock);
                return -1;
            }
            else if (error_code == EPIPE || error_code == ECONNRESET || error_code == ENOTCONN) {
                // Normal socket close during shutdown, log as debug instead of warning
                mcp_log_debug("send_vectors (POSIX): Connection closed/reset (socket %d, error %d - %s)", (int)sock, error_code, strerror(error_code));
                return -1;
            }
            if (error_code == EINTR) {
                mcp_log_debug("send_vectors (POSIX) interrupted, retrying...");
                continue; // Retry if interrupted
            }
            if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
                mcp_log_warn("send_vectors (POSIX) got EAGAIN/EWOULDBLOCK?");
                continue;
            }
            mcp_log_error("send_vectors (POSIX) writev failed (socket %d): Error %d (%s)", (int)sock, error_code, strerror(error_code));
            return -1;
        }

        if (bytes_sent == 0) {
            mcp_log_error("send_vectors (POSIX) sent 0 bytes unexpectedly (socket %d)", (int)sock);
            return -1;
        }

        total_sent += (size_t)bytes_sent;

        // Adjust iovec array for the next iteration if partial write occurred
        if (total_sent < total_to_send) {
            size_t sent_adjust = bytes_sent;
            int current_iov = 0;
            while (sent_adjust > 0 && current_iov < iovcnt) {
                if (sent_adjust < iov[current_iov].iov_len) {
                    iov[current_iov].iov_base = (char*)iov[current_iov].iov_base + sent_adjust;
                    iov[current_iov].iov_len -= sent_adjust;
                    sent_adjust = 0;
                } else {
                    sent_adjust -= iov[current_iov].iov_len;
                    iov[current_iov].iov_len = 0; // Mark as fully sent
                    current_iov++;
                }
            }
            // Adjust pointers for the next writev call
            iov += current_iov;
            iovcnt -= current_iov;
            if (iovcnt <= 0) break; // Should have sent everything
        }
    } // end while
#endif

    return (total_sent == total_to_send) ? 0 : -1; // Success only if all bytes sent
}

int mcp_socket_wait_readable(socket_t sock, int timeout_ms, volatile bool* stop_flag) {
    // Abort if stop_flag is provided and is true
    if (stop_flag && *stop_flag) return -1; // Aborted

#ifdef _WIN32
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
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
    int check_interval_ms = 500; // Check stop flag every 500ms
    struct timeval interval_tv = {0, check_interval_ms * 1000};
    struct timeval* current_tv = (timeout_ms < 0 || timeout_ms > check_interval_ms) ? &interval_tv : tv_ptr;
    time_t start_time = time(NULL);

    while (1) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) return -1; // Check before select

        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);

        // Revert first parameter to 0 for Windows select, as it was originally
        int result = select(0, &read_fds, NULL, NULL, current_tv);

        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) return -1; // Check after select

        if (result == MCP_SOCKET_ERROR) {
            // Add extra logging here
            mcp_log_debug("select() returned SOCKET_ERROR for socket %d", (int)sock);
            int error_code = mcp_socket_get_last_error();
            if (error_code == WSAEINTR) {
                mcp_log_debug("select() interrupted (WSAEINTR), continuing loop.");
                continue; // Interrupted, loop again
            }
            // Log the actual error code retrieved (including 0 if it occurs)
            mcp_log_error("select failed (socket %d): WSAGetLastError() returned %d", (int)sock, error_code);
            return -1; // Error
        } else if (result == 0) {
            // Timeout occurred
            if (timeout_ms < 0) { // Infinite wait, continue polling
                current_tv = &interval_tv; // Reset interval timer
                continue;
            } else if (timeout_ms == 0) { // Zero timeout (poll)
                return 0; // Timed out immediately
            } else { // Finite timeout
                time_t current_time = time(NULL);
                if (difftime(current_time, start_time) * 1000 >= timeout_ms) {
                    return 0; // Final timeout expired
                }
                // Adjust remaining time and continue with interval check
                long remaining_ms = timeout_ms - (long)(difftime(current_time, start_time) * 1000);
                if (remaining_ms <= 0) return 0; // Should be caught above, but safety check
                current_tv = &interval_tv;
                if (remaining_ms < check_interval_ms) {
                    interval_tv.tv_sec = remaining_ms / 1000;
                    interval_tv.tv_usec = (remaining_ms % 1000) * 1000;
                }
                continue;
            }
        } else {
            // Socket is readable
            if (FD_ISSET(sock, &read_fds)) {
                return 1; // Readable
            }
            // Should not happen if result > 0
            mcp_log_warn("select returned > 0 but socket not set?");
            return -1;
        }
    } // end while

#else
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    // poll timeout: -1=infinite, 0=poll, >0=timeout_ms
    int current_timeout = timeout_ms;
    int check_interval_ms = 500; // Check stop flag every 500ms
    time_t start_time = time(NULL);

    while (1) {
        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) return -1; // Check before poll

        // Adjust timeout for periodic stop_flag check
        int poll_wait = current_timeout;
        if (current_timeout < 0 || current_timeout > check_interval_ms) {
            poll_wait = check_interval_ms;
        }

        int result = poll(&pfd, 1, poll_wait);

        // Abort if stop_flag is provided and is true
        if (stop_flag && *stop_flag) return -1; // Check after poll

        if (result < 0) {
            if (errno == EINTR) continue; // Interrupted, loop again
            mcp_log_error("poll failed (socket %d): Error %d (%s)", (int)sock, errno, strerror(errno));
            return -1; // Error
        } else if (result == 0) {
            // Timeout occurred
            if (current_timeout < 0) { // Infinite wait, continue polling
                continue;
            } else if (current_timeout == 0) { // Zero timeout (poll)
                return 0; // Timed out immediately
            } else { // Finite timeout
                time_t current_time = time(NULL);
                if (difftime(current_time, start_time) * 1000 >= timeout_ms) {
                    return 0; // Final timeout expired
                }
                // Continue polling with adjusted remaining time logic implicitly handled by loop
                continue;
            }
        } else {
            // Socket has events
            if (pfd.revents & POLLIN) {
                return 1; // Readable
            }
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                // Use debug level for normal socket events during shutdown
                mcp_log_debug("poll reported event %d on socket %d", pfd.revents, (int)sock);
                return -1; // Socket error indicated by poll
            }
            // Should not happen if result > 0
            mcp_log_warn("poll returned > 0 but no POLLIN or error event?");
            return -1;
        }
    } // end while
#endif
}

socket_t mcp_socket_create_listener(const char* host, uint16_t port, int backlog) {
    socket_t listen_sock = MCP_INVALID_SOCKET;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char port_str[6];
    int yes = 1;

    snprintf(port_str, sizeof(port_str), "%u", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // Use IPv4 for simplicity, matching client connect
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // Use my IP

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        mcp_log_error("getaddrinfo for listener failed: %s", gai_strerror(rv));
        return MCP_INVALID_SOCKET;
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((listen_sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == MCP_INVALID_SOCKET) {
            mcp_log_warn("Listener socket() failed: %d", mcp_socket_get_last_error());
            continue;
        }

        // Allow reuse of address/port
        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes)) == MCP_SOCKET_ERROR) {
            mcp_log_warn("setsockopt(SO_REUSEADDR) failed: %d", mcp_socket_get_last_error());
            mcp_socket_close(listen_sock);
            listen_sock = MCP_INVALID_SOCKET;
            continue;
        }

        // Set TCP_NODELAY to disable Nagle's algorithm and reduce latency
        if (mcp_socket_set_nodelay(listen_sock) != 0) {
            mcp_log_warn("Failed to set TCP_NODELAY on listener socket %d, continuing anyway", (int)listen_sock);
            // We don't fail just because TCP_NODELAY failed
        }

        if (bind(listen_sock, p->ai_addr, (int)p->ai_addrlen) == MCP_SOCKET_ERROR) {
            mcp_log_warn("Listener bind() to %s:%u failed: %d", host, port, mcp_socket_get_last_error());
            mcp_socket_close(listen_sock);
            listen_sock = MCP_INVALID_SOCKET;
            continue;
        }

        break; // Successfully bound
    }

    freeaddrinfo(servinfo);

    if (listen_sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to bind listener socket to %s:%u", host, port);
        return MCP_INVALID_SOCKET;
    }

    if (listen(listen_sock, backlog) == MCP_SOCKET_ERROR) {
        mcp_log_error("Listener listen() failed: %d", mcp_socket_get_last_error());
        mcp_socket_close(listen_sock);
        return MCP_INVALID_SOCKET;
    }

    mcp_log_info("Server listening on %s:%u (socket %d)", host, port, (int)listen_sock);
    return listen_sock;
}

socket_t mcp_socket_accept(socket_t listen_sock, struct sockaddr* client_addr, socklen_t* addr_len) {
    // Note: accept() is typically blocking. Non-blocking accept requires extra handling.
    socket_t client_sock = accept(listen_sock, client_addr, addr_len);

    if (client_sock != MCP_INVALID_SOCKET) {
        // Set TCP_NODELAY to disable Nagle's algorithm and reduce latency
        if (mcp_socket_set_nodelay(client_sock) != 0) {
            mcp_log_warn("Failed to set TCP_NODELAY on accepted socket %d, continuing anyway", (int)client_sock);
            // We don't close the socket just because TCP_NODELAY failed
        }
    }

    if (client_sock == MCP_INVALID_SOCKET) {
        int error_code = mcp_socket_get_last_error();
        // Log common non-fatal errors at debug/warn level
#ifdef _WIN32
        if (error_code != WSAEWOULDBLOCK && error_code != WSAEINTR && error_code != WSAECONNABORTED) {
            mcp_log_error("accept() failed: %d", error_code);
        } else {
            mcp_log_debug("accept() returned non-fatal error: %d", error_code);
        }
#else
        if (error_code != EWOULDBLOCK && error_code != EAGAIN && error_code != EINTR && error_code != ECONNABORTED) {
            mcp_log_error("accept() failed: %d (%s)", error_code, strerror(error_code));
        } else {
            mcp_log_debug("accept() returned non-fatal error: %d (%s)", error_code, strerror(error_code));
        }
#endif
    }
    return client_sock;
}
