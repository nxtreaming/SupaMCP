#include "internal/connection_pool_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

// Platform-specific includes for socket operations
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <errno.h>
#endif

// Default health check timeout if not specified
#define DEFAULT_HEALTH_CHECK_TIMEOUT_MS 2000

/**
 * @brief Performs a health check on a connection.
 *
 * This function checks if a connection is still valid by attempting to perform
 * a non-blocking read operation. If the connection is closed or has an error,
 * the function returns false. If the connection is still valid, the function
 * returns true.
 *
 * @param socket_fd The socket handle to check.
 * @param timeout_ms Timeout in milliseconds for the health check.
 * @return true if the connection is healthy, false otherwise.
 */
bool check_connection_health(socket_handle_t socket_fd, int timeout_ms) {
    if (socket_fd == INVALID_SOCKET_HANDLE) {
        return false;
    }

    // Save current socket flags to restore later
#ifdef _WIN32
    u_long mode;
    if (ioctlsocket(socket_fd, FIONREAD, &mode) == SOCKET_ERROR_HANDLE) {
        mcp_log_warn("Health check: ioctlsocket(FIONREAD) failed: %d", WSAGetLastError());
        return false;
    }

    // Set non-blocking mode
    u_long non_blocking = 1;
    if (ioctlsocket(socket_fd, FIONBIO, &non_blocking) == SOCKET_ERROR_HANDLE) {
        mcp_log_warn("Health check: ioctlsocket(FIONBIO) failed: %d", WSAGetLastError());
        return false;
    }
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        mcp_log_warn("Health check: fcntl(F_GETFL) failed: %s", strerror(errno));
        return false;
    }

    // Set non-blocking mode
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        mcp_log_warn("Health check: fcntl(F_SETFL, O_NONBLOCK) failed: %s", strerror(errno));
        return false;
    }
#endif

    // Use poll/select to check if the socket is readable
    bool is_healthy = true;

#ifdef _WIN32
    fd_set read_fds, error_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&error_fds);
    FD_SET(socket_fd, &read_fds);
    FD_SET(socket_fd, &error_fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int select_result = select(0, &read_fds, NULL, &error_fds, &tv);

    if (select_result == SOCKET_ERROR_HANDLE) {
        mcp_log_warn("Health check: select() failed: %d", WSAGetLastError());
        is_healthy = false;
    } else if (select_result > 0) {
        // Check if socket has an error
        if (FD_ISSET(socket_fd, &error_fds)) {
            mcp_log_warn("Health check: socket has error condition");
            is_healthy = false;
        }
        // Check if socket is readable
        else if (FD_ISSET(socket_fd, &read_fds)) {
            // Socket is readable, try to read to see if it's EOF
            char buffer[1];
            int recv_result = recv(socket_fd, buffer, 1, MSG_PEEK);

            if (recv_result == 0) {
                // Connection closed by peer
                mcp_log_warn("Health check: connection closed by peer");
                is_healthy = false;
            } else if (recv_result == SOCKET_ERROR_HANDLE) {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK) {
                    // Socket error
                    mcp_log_warn("Health check: recv() failed: %d", error);
                    is_healthy = false;
                }
                // WSAEWOULDBLOCK means no data available, which is good
            }
            // If recv_result > 0, there's data available, which is unexpected
            // but not necessarily an error
        }
    }

    // Restore blocking mode if needed
    non_blocking = 0;
    ioctlsocket(socket_fd, FIONBIO, &non_blocking);
#else
    struct pollfd pfd;
    pfd.fd = socket_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_result = poll(&pfd, 1, timeout_ms);

    if (poll_result == -1) {
        mcp_log_warn("Health check: poll() failed: %s", strerror(errno));
        is_healthy = false;
    } else if (poll_result > 0) {
        // Check if socket has an error or is readable
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            mcp_log_warn("Health check: socket has error condition: %d", pfd.revents);
            is_healthy = false;
        } else if (pfd.revents & POLLIN) {
            // Socket is readable, try to read to see if it's EOF
            char buffer[1];
            int recv_result = recv(socket_fd, buffer, 1, MSG_PEEK);

            if (recv_result == 0) {
                // Connection closed by peer
                mcp_log_warn("Health check: connection closed by peer");
                is_healthy = false;
            } else if (recv_result == -1) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    // Socket error
                    mcp_log_warn("Health check: recv() failed: %s", strerror(errno));
                    is_healthy = false;
                }
                // EWOULDBLOCK/EAGAIN means no data available, which is good
            }
            // If recv_result > 0, there's data available, which is unexpected
            // but not necessarily an error
        }
    }

    // Restore original flags
    fcntl(socket_fd, F_SETFL, flags);
#endif

    return is_healthy;
}

/**
 * @brief Performs health checks on all idle connections in the pool.
 *
 * This function iterates through all idle connections in the pool and performs
 * a health check on each one. If a connection is found to be unhealthy, it is
 * closed and removed from the pool.
 *
 * @param pool The connection pool.
 * @return The number of connections that failed the health check.
 */
int perform_health_checks(mcp_connection_pool_t* pool) {
    if (!pool || pool->health_check_interval_ms <= 0) {
        return 0;
    }

    int failed_count = 0;
    time_t current_time = time(NULL);

    // Lock the pool before accessing the idle list
    pool_lock(pool);

    mcp_pooled_connection_t* prev = NULL;
    mcp_pooled_connection_t* current = pool->idle_head;

    while (current) {
        // Check if it's time to perform a health check on this connection
        double time_since_last_check = difftime(current_time, current->last_health_check);
        if (time_since_last_check * 1000 < pool->health_check_interval_ms) {
            // Not time for a health check yet
            prev = current;
            current = current->next;
            continue;
        }

        // Mark connection as being checked
        current->is_being_checked = true;
        socket_handle_t socket_to_check = current->socket_fd;

        // Temporarily unlock the pool while performing the health check
        pool_unlock(pool);

        // Perform health check
        bool is_healthy = check_connection_health(socket_to_check, pool->health_check_timeout_ms);

        // Re-lock the pool
        pool_lock(pool);

        // Update health check statistics
        pool->health_checks_performed++;

        // Find the connection again (it might have been removed while we were unlocked)
        mcp_pooled_connection_t* check_prev = NULL;
        mcp_pooled_connection_t* check_current = pool->idle_head;
        bool found = false;

        while (check_current) {
            if (check_current->socket_fd == socket_to_check && check_current->is_being_checked) {
                found = true;
                break;
            }
            check_prev = check_current;
            check_current = check_current->next;
        }

        if (!found) {
            // Connection was removed while we were unlocked
            mcp_log_debug("Health check: connection %d was removed while being checked", (int)socket_to_check);
            // Reset prev and current to continue from the beginning of the list
            prev = NULL;
            current = pool->idle_head;
            continue;
        }

        // Update connection health status
        check_current->is_being_checked = false;
        check_current->last_health_check = current_time;

        // Update health score based on check result
        int new_score = update_connection_health_score(check_current, is_healthy);

        // Log health score change
        mcp_log_debug("Health check: connection %d health score updated from %d to %d",
                     (int)socket_to_check, check_current->health_score, new_score);

        // Check if connection should be removed based on health score
        if (!is_connection_healthy_by_score(check_current)) {
            // Connection is unhealthy, remove it from the pool
            mcp_log_warn("Health check: removing unhealthy connection %d (score: %d)",
                        (int)socket_to_check, check_current->health_score);

            // Remove node from double-linked list
            if (check_prev) {
                check_prev->next = check_current->next;
                if (check_current->next) {
                    check_current->next->prev = check_prev;
                } else {
                    // This was the tail
                    pool->idle_tail = check_prev;
                }
            } else {
                // Removing the head of the list
                pool->idle_head = check_current->next;
                if (check_current->next) {
                    check_current->next->prev = NULL;
                } else {
                    // This was the only node
                    pool->idle_tail = NULL;
                }
            }

            // Close the connection and free the node
            close_connection(check_current->socket_fd);
            free(check_current);

            // Update counts
            pool->idle_count--;
            pool->total_count--;
            pool->failed_health_checks++;
            failed_count++;

            // Reset prev and current to continue from the beginning of the list
            prev = NULL;
            current = pool->idle_head;
        } else {
            // Connection is healthy enough to keep, move to next
            prev = check_current;
            current = check_current->next;
        }
    }

    pool_unlock(pool);

    return failed_count;
}

/**
 * @brief Updates the health score of a connection based on a health check result.
 *
 * This function implements a progressive health scoring algorithm:
 * - If the connection is healthy, the score increases gradually up to 100
 * - If the connection is unhealthy, the score decreases significantly
 * - A connection is considered unhealthy when its score falls below a threshold
 *
 * @param conn The pooled connection to update.
 * @param is_healthy Whether the connection passed the health check.
 * @return The updated health score.
 */
int update_connection_health_score(mcp_pooled_connection_t* conn, bool is_healthy) {
    if (!conn) {
        return 0;
    }

    // Current health score
    int current_score = conn->health_score;

    // Update score based on health check result
    if (is_healthy) {
        // Healthy connection: increase score gradually
        // The closer to 100, the slower the increase
        int increase = (100 - current_score) / 5;
        if (increase < 1) increase = 1; // Minimum increase of 1

        current_score += increase;
        if (current_score > 100) current_score = 100;
    } else {
        // Unhealthy connection: decrease score significantly
        // The higher the score, the more dramatic the decrease
        int decrease = current_score / 4; // 25% decrease
        if (decrease < 10) decrease = 10; // Minimum decrease of 10

        current_score -= decrease;
        if (current_score < 0) current_score = 0;
    }

    // Update the connection's health score
    conn->health_score = current_score;

    return current_score;
}

/**
 * @brief Determines if a connection is healthy based on its health score.
 *
 * @param conn The pooled connection to check.
 * @return true if the connection is considered healthy, false otherwise.
 */
bool is_connection_healthy_by_score(mcp_pooled_connection_t* conn) {
    if (!conn) {
        return false;
    }

    // A connection is considered healthy if its score is above 50
    return conn->health_score > 50;
}


/**
 * @brief Initializes a pooled connection with default health values.
 *
 * @param conn The pooled connection to initialize.
 */
void init_connection_health(mcp_pooled_connection_t* conn) {
    if (conn) {
        conn->last_health_check = time(NULL);
        conn->health_score = 100; // Start with perfect health
        conn->is_being_checked = false;
    }
}
