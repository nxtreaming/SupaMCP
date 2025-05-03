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

/**
 * @brief Client handler wrapper function for thread pool.
 *
 * This function is called by the thread pool when a client connection is ready to be processed.
 * It calls the actual client handler function with the client connection as argument.
 *
 * @param arg Pointer to the client connection
 */
void tcp_client_handler_wrapper(void* arg) {
    tcp_client_connection_t* client = (tcp_client_connection_t*)arg;
    if (!client) {
        mcp_log_error("Client handler wrapper called with NULL client");
        return;
    }

    // Call the actual client handler function
    tcp_client_handler_thread_func(client);
}

/**
 * @brief Updates the last activity time for a client connection.
 *
 * @param client Pointer to the client connection
 */
void tcp_update_client_activity(tcp_client_connection_t* client) {
    if (client) {
        client->last_activity_time = time(NULL);
    }
}

/**
 * @brief Finds a free slot in the clients array.
 *
 * @param data Pointer to the TCP transport data
 * @return int Index of the free slot, or -1 if no free slot is available
 */
int tcp_find_free_client_slot(mcp_tcp_transport_data_t* data) {
    if (!data || !data->clients) {
        return -1;
    }

    for (int i = 0; i < data->max_clients; i++) {
        if (data->clients[i].state == CLIENT_STATE_INACTIVE) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Closes a client connection and releases associated resources.
 *
 * @param data Pointer to the TCP transport data
 * @param client_index Index of the client in the clients array
 */
void tcp_close_client_connection(mcp_tcp_transport_data_t* data, int client_index) {
    if (!data || client_index < 0 || client_index >= data->max_clients) {
        return;
    }

    mcp_mutex_lock(data->client_mutex);

    tcp_client_connection_t* client = &data->clients[client_index];

    // Only close if not already inactive
    if (client->state != CLIENT_STATE_INACTIVE) {
        // Mark as closing to prevent further processing
        client->state = CLIENT_STATE_CLOSING;

        // Signal the client to stop
        client->should_stop = true;

        // Close the socket
        if (client->socket != MCP_INVALID_SOCKET) {
            mcp_log_info("Closing client connection from %s:%d",
                        client->client_ip, client->client_port);

            // Shutdown the socket
#ifdef _WIN32
            shutdown(client->socket, SD_BOTH);
#else
            shutdown(client->socket, SHUT_RDWR);
#endif

            // Close the socket
            mcp_socket_close(client->socket);
            client->socket = MCP_INVALID_SOCKET;
        }

        // Mark as inactive
        client->state = CLIENT_STATE_INACTIVE;

        // Update statistics
        tcp_stats_update_connection_closed(&data->stats);
    }

    mcp_mutex_unlock(data->client_mutex);
}

/**
 * @brief Cleanup thread function for removing idle connections.
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

    while (data->cleanup_running) {
        // Use a much shorter sleep interval to respond quickly to shutdown requests
        // 1 second is a good balance between responsiveness and CPU usage
        uint32_t sleep_interval = 1000; // 1 second

        // For idle connection checking, calculate the actual check interval
        uint32_t check_interval = data->idle_timeout_ms / 2;
        if (check_interval == 0) {
            check_interval = 30000; // Default to 30 seconds if no idle timeout
        }

        // Use a static counter to track when to check for idle connections
        static uint32_t counter = 0;
        bool check_idle_connections = false;

        // Only check for idle connections periodically
        if (counter >= check_interval) {
            check_idle_connections = true;
            counter = 0;
        } else {
            counter += sleep_interval;
        }

#ifdef _WIN32
        Sleep(sleep_interval);
#else
        usleep(sleep_interval * 1000);
#endif

        // Check if we should exit
        if (!data->cleanup_running) {
            mcp_log_debug("Cleanup thread received stop signal");
            break;
        }

        // Skip idle connection check if it's not time yet
        if (!check_idle_connections) {
            continue;
        }

        // Get current time
        time_t current_time = time(NULL);

        // Check for idle connections
        mcp_mutex_lock(data->client_mutex);
        for (int i = 0; i < data->max_clients; i++) {
            tcp_client_connection_t* client = &data->clients[i];

            // Skip inactive connections
            if (client->state != CLIENT_STATE_ACTIVE) {
                continue;
            }

            // Check if the connection has been idle for too long
            if (data->idle_timeout_ms > 0 &&
                (current_time - client->last_activity_time) * 1000 >= data->idle_timeout_ms) {

                mcp_log_info("Client connection from %s:%d idle for %ld seconds, closing",
                            client->client_ip, client->client_port,
                            current_time - client->last_activity_time);

                // Mark for closing (will be handled outside the lock)
                client->should_stop = true;
            }
        }
        mcp_mutex_unlock(data->client_mutex);
    }

    mcp_log_info("Cleanup thread exiting");
    return NULL;
}

/**
 * @brief Initializes the server statistics.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_init(tcp_server_stats_t* stats) {
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(tcp_server_stats_t));
    stats->start_time = time(NULL);
}

/**
 * @brief Updates statistics when a connection is accepted.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_connection_accepted(tcp_server_stats_t* stats) {
    if (!stats) {
        return;
    }

    stats->total_connections++;
    stats->active_connections++;
}

/**
 * @brief Updates statistics when a connection is rejected.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_connection_rejected(tcp_server_stats_t* stats) {
    if (!stats) {
        return;
    }

    stats->rejected_connections++;
}

/**
 * @brief Updates statistics when a connection is closed.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_connection_closed(tcp_server_stats_t* stats) {
    if (!stats) {
        return;
    }

    if (stats->active_connections > 0) {
        stats->active_connections--;
    }
}

/**
 * @brief Updates statistics when a message is received.
 *
 * @param stats Pointer to the statistics structure
 * @param bytes Number of bytes received
 */
void tcp_stats_update_message_received(tcp_server_stats_t* stats, size_t bytes) {
    if (!stats) {
        return;
    }

    stats->messages_received++;
    stats->bytes_received += bytes;
}

/**
 * @brief Updates statistics when a message is sent.
 *
 * @param stats Pointer to the statistics structure
 * @param bytes Number of bytes sent
 */
void tcp_stats_update_message_sent(tcp_server_stats_t* stats, size_t bytes) {
    if (!stats) {
        return;
    }

    stats->messages_sent++;
    stats->bytes_sent += bytes;
}

/**
 * @brief Updates statistics when an error occurs.
 *
 * @param stats Pointer to the statistics structure
 */
void tcp_stats_update_error(tcp_server_stats_t* stats) {
    if (!stats) {
        return;
    }

    stats->errors++;
}
