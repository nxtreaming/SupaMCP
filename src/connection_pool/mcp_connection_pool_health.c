#include "internal/connection_pool_internal.h"
#include "mcp_socket_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

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

// Health score thresholds
#define HEALTH_SCORE_HEALTHY 50       // Score above this is considered healthy
#define HEALTH_SCORE_PERFECT 100      // Maximum health score
#define HEALTH_SCORE_MIN_INCREASE 1   // Minimum increase for healthy connections
#define HEALTH_SCORE_MIN_DECREASE 10  // Minimum decrease for unhealthy connections

#define MAX_BATCH_SIZE 16

/**
 * @brief Sets a socket to non-blocking mode.
 *
 * @param socket_fd The socket handle.
 * @param original_flags Pointer to store the original flags (POSIX only).
 * @param original_mode Pointer to store the original mode (Windows only).
 * @return true if successful, false otherwise.
 */
static bool set_socket_nonblocking(socket_handle_t socket_fd,
#ifdef _WIN32
                                  u_long* original_mode
#else
                                  int* original_flags
#endif
                                  ) {
#ifdef _WIN32
    // Get current socket mode
    u_long mode = 0;
    if (ioctlsocket(socket_fd, FIONREAD, &mode) == SOCKET_ERROR_HANDLE) {
        mcp_log_warn("Health check: ioctlsocket(FIONREAD) failed: %d", WSAGetLastError());
        return false;
    }

    // Store original mode
    *original_mode = mode;

    // Set non-blocking mode
    u_long non_blocking = 1;
    if (ioctlsocket(socket_fd, FIONBIO, &non_blocking) == SOCKET_ERROR_HANDLE) {
        mcp_log_warn("Health check: ioctlsocket(FIONBIO) failed: %d", WSAGetLastError());
        return false;
    }
#else
    // Get current socket flags
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        mcp_log_warn("Health check: fcntl(F_GETFL) failed: %s", strerror(errno));
        return false;
    }

    // Store original flags
    *original_flags = flags;

    // Set non-blocking mode
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        mcp_log_warn("Health check: fcntl(F_SETFL, O_NONBLOCK) failed: %s", strerror(errno));
        return false;
    }
#endif
    return true;
}

/**
 * @brief Restores a socket to its original blocking mode.
 *
 * @param socket_fd The socket handle.
 * @param original_flags The original flags to restore (POSIX only).
 * @param original_mode The original mode to restore (Windows only).
 * @return true if successful, false otherwise.
 */
static bool restore_socket_blocking(socket_handle_t socket_fd,
#ifdef _WIN32
                                   u_long original_mode
#else
                                   int original_flags
#endif
                                   ) {
#ifdef _WIN32
    // Restore original mode (always set to blocking for now)
    u_long blocking = 0;
    (void)original_mode; // Suppress unused parameter warning
    if (ioctlsocket(socket_fd, FIONBIO, &blocking) == SOCKET_ERROR_HANDLE) {
        mcp_log_warn("Health check: ioctlsocket(FIONBIO) restore failed: %d", WSAGetLastError());
        return false;
    }
#else
    // Restore original flags
    if (fcntl(socket_fd, F_SETFL, original_flags) == -1) {
        mcp_log_warn("Health check: fcntl(F_SETFL) restore failed: %s", strerror(errno));
        return false;
    }
#endif
    return true;
}

/**
 * @brief Checks if a socket is readable or has an error.
 *
 * This function uses platform-specific mechanisms (select on Windows, poll on POSIX)
 * to check if a socket is readable or has an error condition.
 *
 * @param socket_fd The socket handle to check.
 * @param timeout_ms Timeout in milliseconds for the check.
 * @param is_readable Pointer to a bool that will be set to true if the socket is readable.
 * @param has_error Pointer to a bool that will be set to true if the socket has an error.
 * @return true if the check was successful, false if there was an error in the check itself.
 */
static bool check_socket_readable(socket_handle_t socket_fd, int timeout_ms, bool* is_readable, bool* has_error) {
    if (socket_fd == INVALID_SOCKET_HANDLE || !is_readable || !has_error) {
        return false;
    }

    *is_readable = false;
    *has_error = false;

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
        *has_error = true;
        return false;
    } else if (select_result > 0) {
        // Check if socket has an error
        if (FD_ISSET(socket_fd, &error_fds)) {
            *has_error = true;
        }
        // Check if socket is readable
        if (FD_ISSET(socket_fd, &read_fds)) {
            *is_readable = true;
        }
    }
    // If select_result == 0, timeout occurred, both flags remain false
#else
    struct pollfd pfd;
    pfd.fd = socket_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_result = poll(&pfd, 1, timeout_ms);

    if (poll_result == -1) {
        mcp_log_warn("Health check: poll() failed: %s", strerror(errno));
        *has_error = true;
        return false;
    } else if (poll_result > 0) {
        // Check if socket has an error
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            *has_error = true;
        }
        // Check if socket is readable
        if (pfd.revents & POLLIN) {
            *is_readable = true;
        }
    }
    // If poll_result == 0, timeout occurred, both flags remain false
#endif

    return true;
}

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

    // Start timing for performance measurement
    long long check_start_ms = mcp_get_time_ms();

    // Use default timeout if not specified
    if (timeout_ms <= 0) {
        timeout_ms = DEFAULT_HEALTH_CHECK_TIMEOUT_MS;
    }

    // Save current socket flags to restore later
#ifdef _WIN32
    u_long original_mode;
#else
    int original_flags;
#endif

    // Set socket to non-blocking mode
    if (!set_socket_nonblocking(socket_fd,
#ifdef _WIN32
                               &original_mode
#else
                               &original_flags
#endif
                               )) {
        return false;
    }

    // Check if socket is readable or has an error
    bool is_readable = false;
    bool has_error = false;
    bool is_healthy = true;

    if (!check_socket_readable(socket_fd, timeout_ms, &is_readable, &has_error)) {
        is_healthy = false;
    } else if (has_error) {
        mcp_log_warn("Health check: socket has error condition");
        is_healthy = false;
    } else if (is_readable) {
        // Socket is readable, try to read to see if it's EOF
        char buffer[1];
        int recv_result;

#ifdef _WIN32
        recv_result = recv(socket_fd, buffer, 1, MSG_PEEK);

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
#else
        recv_result = recv(socket_fd, buffer, 1, MSG_PEEK);

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
#endif
        // If recv_result > 0, there's data available, which is unexpected
        // but not necessarily an error
    }

    // Restore original socket flags
    restore_socket_blocking(socket_fd,
#ifdef _WIN32
                           original_mode
#else
                           original_flags
#endif
                           );

    // Calculate and log the time taken for slow health checks
    long long check_end_ms = mcp_get_time_ms();
    long long check_time_ms = check_end_ms - check_start_ms;

    if (check_time_ms > 100) {
        mcp_log_warn("Slow health check: %lld ms for socket %d",
                    check_time_ms, (int)socket_fd);
    }

    return is_healthy;
}

/**
 * @brief Performs health checks on all idle connections in the pool.
 *
 * This function iterates through all idle connections in the pool and performs
 * a health check on each one. If a connection is found to be unhealthy, it is
 * closed and removed from the pool.
 *
 * The function uses batch processing to minimize lock contention and improve
 * performance.
 *
 * @param pool The connection pool.
 * @return The number of connections that failed the health check.
 */
int perform_health_checks(mcp_connection_pool_t* pool) {
    if (!pool || pool->health_check_interval_ms <= 0) {
        return 0;
    }

    // Start timing for performance measurement
    long long health_check_start_ms = mcp_get_time_ms();

    int failed_count = 0;
    int checked_count = 0;
    time_t current_time = time(NULL);

    // Lock the pool before accessing the idle list
    pool_lock(pool);

    // First pass: identify connections that need health checks
    // and mark them for checking
    typedef struct {
        mcp_pooled_connection_t* conn;
        socket_handle_t socket_fd;
    } health_check_item_t;

    // Allocate a temporary array for connections to check
    // We'll check up to 16 connections at once to reduce lock contention
    health_check_item_t to_check[MAX_BATCH_SIZE];
    int check_count = 0;

    mcp_pooled_connection_t* current = pool->idle_head;

    while (current && check_count < MAX_BATCH_SIZE) {
        // Check if it's time to perform a health check on this connection
        double time_since_last_check = difftime(current_time, current->last_health_check);
        if (time_since_last_check * 1000 >= pool->health_check_interval_ms) {
            // Mark connection as being checked
            current->is_being_checked = true;

            // Add to batch
            to_check[check_count].conn = current;
            to_check[check_count].socket_fd = current->socket_fd;
            check_count++;
        }

        current = current->next;
    }

    // If no connections need checking, we're done
    if (check_count == 0) {
        pool_unlock(pool);
        return 0;
    }

    // Temporarily unlock the pool while performing health checks
    pool_unlock(pool);

    // Perform health checks on all connections in the batch
    bool health_results[MAX_BATCH_SIZE]; // Now using the #define MAX_BATCH_SIZE
    for (int i = 0; i < check_count; i++) {
        health_results[i] = check_connection_health(to_check[i].socket_fd, pool->health_check_timeout_ms);
        checked_count++;
    }

    // Re-lock the pool
    pool_lock(pool);

    // Update health check statistics
    pool->health_checks_performed += checked_count;

    // Process health check results
    for (int i = 0; i < check_count; i++) {
        socket_handle_t socket_to_check = to_check[i].socket_fd;

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
            continue;
        }

        // Update connection health status
        check_current->is_being_checked = false;
        check_current->last_health_check = current_time;

        // Get the health check result
        bool is_healthy = health_results[i];

        // Update health score based on check result
        int old_score = check_current->health_score;
        int new_score = update_connection_health_score(check_current, is_healthy);

        // Log health score change if significant
        if (abs(new_score - old_score) > 5) {
            mcp_log_debug("Health check: connection %d health score updated from %d to %d",
                         (int)socket_to_check, old_score, new_score);
        }

        // Check if connection should be removed based on health score
        if (!is_connection_healthy_by_score(check_current)) {
            // Connection is unhealthy, remove it from the pool
            mcp_log_warn("Health check: removing unhealthy connection %d (score: %d)",
                        (int)socket_to_check, check_current->health_score);

            // Remove from idle list
            if (remove_idle_connection(pool, check_current, check_prev)) {
                // Close and free the connection
                close_and_free_connection(pool, check_current);

                // Update statistics
                pool->failed_health_checks++;
                failed_count++;
            }
        }
    }

    // Calculate and log the time taken for health checks
    long long health_check_end_ms = mcp_get_time_ms();
    long long health_check_time_ms = health_check_end_ms - health_check_start_ms;

    if (checked_count > 0) {
        mcp_log_debug("Health check: checked %d connections in %lld ms (%lld ms per connection), %d failed",
                     checked_count, health_check_time_ms,
                     health_check_time_ms / checked_count, failed_count);
    }

    if (health_check_time_ms > 500) { // Log slow health checks
        mcp_log_warn("Slow health check: %lld ms for %d connections",
                    health_check_time_ms, checked_count);
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
 * - The algorithm takes into account the connection's usage history
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

    // Factor in connection usage history
    // Connections that have been used more are more likely to be reliable
    float usage_factor = 1.0f;
    if (conn->use_count > 0) {
        // Cap the usage factor at 1.5 (50% bonus)
        usage_factor = 1.0f + (float)conn->use_count / 20.0f;
        if (usage_factor > 1.5f) {
            usage_factor = 1.5f;
        }
    }

    // Update score based on health check result
    if (is_healthy) {
        // Healthy connection: increase score gradually
        // The closer to 100, the slower the increase
        int increase = (int)(((HEALTH_SCORE_PERFECT - current_score) / 5.0f) * usage_factor);
        if (increase < HEALTH_SCORE_MIN_INCREASE) {
            increase = HEALTH_SCORE_MIN_INCREASE; // Minimum increase
        }

        current_score += increase;
        if (current_score > HEALTH_SCORE_PERFECT) {
            current_score = HEALTH_SCORE_PERFECT;
        }
    } else {
        // Unhealthy connection: decrease score significantly
        // The higher the score, the more dramatic the decrease
        int decrease = current_score / 4; // 25% decrease
        if (decrease < HEALTH_SCORE_MIN_DECREASE) {
            decrease = HEALTH_SCORE_MIN_DECREASE; // Minimum decrease
        }

        // Connections with higher usage get a slightly smaller penalty
        if (conn->use_count > 10) {
            decrease = (int)(decrease * 0.8f); // 20% less penalty for well-used connections
        }

        current_score -= decrease;
        if (current_score < 0) {
            current_score = 0;
        }
    }

    // Update the connection's health score
    conn->health_score = current_score;

    return current_score;
}

/**
 * @brief Determines if a connection is healthy based on its health score.
 *
 * This function checks if a connection's health score is above the healthy threshold.
 * It also considers the connection's usage history - connections that have been
 * used successfully many times get a slight benefit of the doubt.
 *
 * @param conn The pooled connection to check.
 * @return true if the connection is considered healthy, false otherwise.
 */
bool is_connection_healthy_by_score(mcp_pooled_connection_t* conn) {
    if (!conn) {
        return false;
    }

    // Base health check - score must be above threshold
    if (conn->health_score > HEALTH_SCORE_HEALTHY) {
        return true;
    }

    // Give a small benefit of the doubt to connections that have been used a lot
    // and are just slightly below the threshold
    if (conn->use_count > 20 && conn->health_score > (HEALTH_SCORE_HEALTHY - 5)) {
        return true;
    }

    // Connection is unhealthy
    return false;
}


/**
 * @brief Initializes a pooled connection with default health values.
 *
 * This function sets the initial health values for a new connection.
 * New connections start with perfect health and are marked as not being checked.
 *
 * @param conn The pooled connection to initialize.
 */
void init_connection_health(mcp_pooled_connection_t* conn) {
    if (conn) {
        conn->last_health_check = time(NULL);
        conn->health_score = HEALTH_SCORE_PERFECT; // Start with perfect health
        conn->is_being_checked = false;

        // Log initialization
        mcp_log_debug("Initialized health for connection %d with score %d",
                     (int)conn->socket_fd, conn->health_score);
    }
}
