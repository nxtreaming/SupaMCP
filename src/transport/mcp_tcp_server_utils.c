/**
 * @file mcp_tcp_server_utils.c
 * @brief Utility functions for TCP server transport.
 *
 * This file implements various utility functions for the TCP server transport,
 * including client connection management, statistics tracking, and the cleanup
 * thread for removing idle connections.
 */
#include "internal/transport_internal.h"
#include "internal/tcp_transport_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_local.h"
#include "mcp_thread_pool.h"
#include "mcp_socket_utils.h"
#include "mcp_sys_utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

/**
 * @brief Client handler wrapper function for thread pool.
 *
 * This function is called by the thread pool when a client connection is ready to be processed.
 * It serves as an adapter between the thread pool's task interface and the client handler function.
 *
 * @param arg Pointer to the client connection
 */
void tcp_client_handler_wrapper(void* arg) {
    tcp_client_connection_t* client = (tcp_client_connection_t*)arg;
    if (!client) {
        mcp_log_error("Client handler wrapper called with NULL client");
        return;
    }

    // Check if the client connection is still valid before starting handler
    // This prevents starting handlers for connections that were closed during shutdown
    if (client->socket == MCP_INVALID_SOCKET || client->should_stop ||
        client->state == CLIENT_STATE_INACTIVE) {
        mcp_log_debug("Skipping client handler for invalid/stopped connection (index: %d, socket: %d, should_stop: %s, state: %d)",
                     client->client_index, (int)client->socket,
                     client->should_stop ? "true" : "false", (int)client->state);
        return;
    }

    // Additional check with transport data to ensure server is still running
    if (client->transport && client->transport->transport_data) {
        mcp_tcp_transport_data_t* tcp_data = (mcp_tcp_transport_data_t*)client->transport->transport_data;
        if (!tcp_data->running) {
            mcp_log_debug("Skipping client handler for connection (index: %d) - server is shutting down",
                         client->client_index);
            return;
        }
    }

    mcp_log_debug("Handling client connection from %s:%d (index: %d)",
                 client->client_ip, client->client_port, client->client_index);

    tcp_client_handler_thread_func(client);

    mcp_log_debug("Finished handling client connection from %s:%d (index: %d)",
                 client->client_ip, client->client_port, client->client_index);
}

/**
 * @brief Updates the last activity time for a client connection.
 *
 * This function updates the timestamp of the last activity for a client connection,
 * which is used by the cleanup thread to detect and close idle connections.
 *
 * @param client Pointer to the client connection
 */
void tcp_update_client_activity(tcp_client_connection_t* client) {
    if (!client) {
        mcp_log_debug("tcp_update_client_activity called with NULL client");
        return;
    }

    client->last_activity_time = time(NULL);
}

/**
 * @brief Finds a free slot in the clients array.
 *
 * This function searches through the clients array to find an inactive slot
 * that can be used for a new client connection.
 *
 * @param data Pointer to the TCP transport data
 * @return int Index of the free slot, or -1 if no free slot is available
 */
int tcp_find_free_client_slot(mcp_tcp_transport_data_t* data) {
    if (!data || !data->clients) {
        mcp_log_error("Invalid data or clients array in tcp_find_free_client_slot");
        return -1;
    }

    for (int i = 0; i < data->max_clients; i++) {
        if (data->clients[i].state == CLIENT_STATE_INACTIVE) {
            mcp_log_debug("Found free client slot at index %d", i);
            return i;
        }
    }

    mcp_log_warn("No free client slots available (max: %d)", data->max_clients);
    return -1;
}

/**
 * @brief Closes a client connection and releases associated resources.
 *
 * This function safely closes a client connection, shutting down the socket,
 * updating the connection state, and updating statistics. It uses a mutex
 * to ensure thread safety.
 *
 * @param data Pointer to the TCP transport data
 * @param client_index Index of the client in the clients array
 */
void tcp_close_client_connection(mcp_tcp_transport_data_t* data, int client_index) {
    if (!data) {
        mcp_log_error("NULL data parameter in tcp_close_client_connection");
        return;
    }

    if (client_index < 0 || client_index >= data->max_clients) {
        mcp_log_error("Invalid client index %d (max: %d)", client_index, data->max_clients);
        return;
    }

    if (!data->client_mutex) {
        mcp_log_error("NULL client mutex in tcp_close_client_connection");
        return;
    }

    mcp_mutex_lock(data->client_mutex);

    tcp_client_connection_t* client = &data->clients[client_index];

    if (client->state != CLIENT_STATE_INACTIVE) {
        client->state = CLIENT_STATE_CLOSING;
        client->should_stop = true;

        if (client->socket != MCP_INVALID_SOCKET) {
            mcp_log_info("Closing client connection from %s:%d (index: %d)",
                        client->client_ip, client->client_port, client_index);

#ifdef _WIN32
            shutdown(client->socket, SD_BOTH);
#else
            shutdown(client->socket, SHUT_RDWR);
#endif

            mcp_socket_close(client->socket);
            client->socket = MCP_INVALID_SOCKET;
        }

        client->state = CLIENT_STATE_INACTIVE;
        tcp_stats_update_connection_closed(&data->stats);

        mcp_log_debug("Client connection closed and slot freed (index: %d)", client_index);
    } else {
        mcp_log_debug("Client already inactive, nothing to close (index: %d)", client_index);
    }

    mcp_mutex_unlock(data->client_mutex);
}

/**
 * @brief Cleanup thread function for removing idle connections.
 *
 * This thread periodically checks for idle client connections and marks them
 * for closing if they have been inactive for too long. It uses a configurable
 * idle timeout and check interval to balance responsiveness and CPU usage.
 *
 * @param arg Pointer to the TCP transport data
 * @return void* Always returns NULL
 */
void* tcp_cleanup_thread_func(void* arg) {
    mcp_tcp_transport_data_t* data = (mcp_tcp_transport_data_t*)arg;
    if (!data) {
        mcp_log_error("Cleanup thread started with NULL data");
        return NULL;
    }

    mcp_log_info("Cleanup thread started");

    const uint32_t SLEEP_INTERVAL_MS = 1000;  // 1 second sleep interval
    const uint32_t DEFAULT_CHECK_INTERVAL_MS = 30000;  // 30 seconds default check interval

    uint32_t counter = 0;
    uint32_t check_interval = data->idle_timeout_ms > 0 ?
                             data->idle_timeout_ms / 2 :
                             DEFAULT_CHECK_INTERVAL_MS;

    if (check_interval < SLEEP_INTERVAL_MS) {
        check_interval = SLEEP_INTERVAL_MS;
    }

    mcp_log_debug("Cleanup thread using check interval of %u ms", check_interval);

    while (data->cleanup_running) {
        mcp_sleep_ms(SLEEP_INTERVAL_MS);

        if (!data->cleanup_running) {
            mcp_log_debug("Cleanup thread received stop signal");
            break;
        }

        counter += SLEEP_INTERVAL_MS;
        if (counter < check_interval) {
            continue;
        }

        counter = 0;
        mcp_log_debug("Checking for idle connections (timeout: %u ms)", data->idle_timeout_ms);

        time_t current_time = time(NULL);
        mcp_mutex_lock(data->client_mutex);

        int idle_count = 0;
        for (int i = 0; i < data->max_clients; i++) {
            tcp_client_connection_t* client = &data->clients[i];

            if (client->state != CLIENT_STATE_ACTIVE) {
                continue;
            }

            uint32_t idle_time_ms = 0;
            if (current_time > client->last_activity_time) {
                idle_time_ms = (uint32_t)((current_time - client->last_activity_time) * 1000);
            }

            if (data->idle_timeout_ms > 0 && idle_time_ms >= data->idle_timeout_ms) {
                mcp_log_info("Client %s:%d idle for %u ms (timeout: %u ms), marking for close",
                            client->client_ip, client->client_port,
                            idle_time_ms, data->idle_timeout_ms);

                client->should_stop = true;
                idle_count++;
            }
        }

        mcp_mutex_unlock(data->client_mutex);

        if (idle_count > 0) {
            mcp_log_info("Marked %d idle connection(s) for closing", idle_count);
        }
    }

    mcp_log_info("Cleanup thread exiting");
    return NULL;
}

/**
 * @brief Initializes the server statistics.
 *
 * This function initializes all statistics counters to zero and sets the
 * start time to the current time.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_init(tcp_server_stats_t* stats) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_init");
        return;
    }

    memset(stats, 0, sizeof(tcp_server_stats_t));
    stats->start_time = time(NULL);

    mcp_log_debug("Server statistics initialized");
}

/**
 * @brief Updates statistics when a connection is accepted.
 *
 * This function increments the total and active connection counters.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_connection_accepted(tcp_server_stats_t* stats) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_update_connection_accepted");
        return;
    }

    stats->total_connections++;
    stats->active_connections++;

    mcp_log_debug("Connection accepted: total=%llu, active=%llu",
                 stats->total_connections, stats->active_connections);
}

/**
 * @brief Updates statistics when a connection is rejected.
 *
 * This function increments the rejected connection counter.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_connection_rejected(tcp_server_stats_t* stats) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_update_connection_rejected");
        return;
    }

    stats->rejected_connections++;

    mcp_log_debug("Connection rejected: total_rejected=%llu",
                 stats->rejected_connections);
}

/**
 * @brief Updates statistics when a connection is closed.
 *
 * This function decrements the active connection counter, ensuring it
 * never goes below zero.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_connection_closed(tcp_server_stats_t* stats) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_update_connection_closed");
        return;
    }

    if (stats->active_connections > 0) {
        stats->active_connections--;
        mcp_log_debug("Connection closed: active=%llu", stats->active_connections);
    } else {
        mcp_log_warn("Active connection counter already at zero");
    }
}

/**
 * @brief Updates statistics when a message is received.
 *
 * This function increments the received message counter and adds to the
 * total bytes received.
 *
 * @param stats Pointer to the statistics structure
 * @param bytes Number of bytes received
 */
void tcp_stats_update_message_received(tcp_server_stats_t* stats, size_t bytes) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_update_message_received");
        return;
    }

    stats->messages_received++;
    stats->bytes_received += bytes;

    if (stats->messages_received % 100 == 0) {
        mcp_log_debug("Messages received: %llu (total bytes: %llu)",
                     stats->messages_received, stats->bytes_received);
    }
}

/**
 * @brief Updates statistics when a message is sent.
 *
 * This function increments the sent message counter and adds to the
 * total bytes sent.
 *
 * @param stats Pointer to the statistics structure
 * @param bytes Number of bytes sent
 */
void tcp_stats_update_message_sent(tcp_server_stats_t* stats, size_t bytes) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_update_message_sent");
        return;
    }

    stats->messages_sent++;
    stats->bytes_sent += bytes;

    if (stats->messages_sent % 100 == 0) {
        mcp_log_debug("Messages sent: %llu (total bytes: %llu)",
                     stats->messages_sent, stats->bytes_sent);
    }
}

/**
 * @brief Updates statistics when an error occurs.
 *
 * This function increments the error counter.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_error(tcp_server_stats_t* stats) {
    if (!stats) {
        mcp_log_error("NULL stats parameter in tcp_stats_update_error");
        return;
    }

    stats->errors++;

    mcp_log_debug("Error counter incremented: total_errors=%llu",
                 stats->errors);
}
