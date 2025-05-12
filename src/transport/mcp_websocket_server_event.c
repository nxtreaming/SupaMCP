#include "internal/websocket_server_internal.h"

// Helper function to check for client timeouts and send pings
void ws_server_check_timeouts(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only check every PING_INTERVAL_MS milliseconds
    if (difftime(now, data->last_ping_time) * 1000 < WS_PING_INTERVAL_MS) {
        return;
    }

    data->last_ping_time = now;

    mcp_mutex_lock(data->clients_mutex);

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        ws_client_t* client = &data->clients[i];

        if (client->state == WS_CLIENT_STATE_ACTIVE && client->wsi) {
            // Check if client has been inactive for too long
            if (difftime(now, client->last_activity) * 1000 > WS_PING_TIMEOUT_MS) {
                // If we've sent too many pings without response, close the connection
                if (client->ping_sent >= WS_MAX_PING_FAILURES) {
                    mcp_log_warn("Client %d timed out after %d ping failures, closing connection",
                                client->client_id, client->ping_sent);
                    client->state = WS_CLIENT_STATE_CLOSING;

                    // Set a short timeout to force connection closure
                    lws_set_timeout(client->wsi, PENDING_TIMEOUT_CLOSE_SEND, 1);
                } else {
                    // Send a ping to check if client is still alive
                    mcp_log_debug("Sending ping to client %d (attempt %d/%d)",
                                client->client_id, client->ping_sent + 1, WS_MAX_PING_FAILURES);
                    ws_server_client_send_ping(client);
                }
            }
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

// Helper function to clean up inactive clients
void ws_server_cleanup_inactive_clients(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only clean up every CLEANUP_INTERVAL_MS milliseconds
    if (difftime(now, data->last_cleanup_time) * 1000 < WS_CLEANUP_INTERVAL_MS) {
        return;
    }

    data->last_cleanup_time = now;

    mcp_mutex_lock(data->clients_mutex);

    // Use bitmap to quickly skip inactive clients
    for (int i = 0; i < (MAX_WEBSOCKET_CLIENTS / 32 + 1); i++) {
        uint32_t word = data->client_bitmap[i];

        // Skip words with no active clients
        if (word == 0) {
            continue;
        }

        // Process only active bits in this word
        for (int j = 0; j < 32; j++) {
            if ((word & (1 << j)) == 0) {
                continue; // Skip inactive slots
            }

            int client_index = i * 32 + j;
            if (client_index >= MAX_WEBSOCKET_CLIENTS) {
                break; // Don't go beyond array bounds
            }

            ws_client_t* client = &data->clients[client_index];

            // Clean up clients in error state or closing state without a valid wsi
            if ((client->state == WS_CLIENT_STATE_ERROR ||
                 (client->state == WS_CLIENT_STATE_CLOSING && !client->wsi)) &&
                difftime(now, client->last_activity) > 5) { // 5 seconds grace period (reduced from 10s)

                mcp_log_info("Cleaning up inactive client %d (state: %d, last activity: %.1f seconds ago)",
                           client->client_id, client->state, difftime(now, client->last_activity));
                ws_server_client_cleanup(client, data);
            }
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

// Server event loop thread function with adaptive timeout
void* ws_server_event_thread(void* arg) {
    ws_server_data_t* data = (ws_server_data_t*)arg;

    // Use adaptive service timeout based on activity level
    int service_timeout_ms = 20; // Start with 20ms timeout

    // Track last service time for performance monitoring
    time_t last_service_time = time(NULL);
    time_t last_activity_check = time(NULL);
    time_t last_ping_check = time(NULL);
    time_t last_cleanup_check = time(NULL);
    unsigned long service_count = 0;

    // Constants for periodic operations
    const int ACTIVITY_CHECK_INTERVAL = 1;  // Check activity level every 1 second
    const int PING_CHECK_INTERVAL = 5;      // Check for client timeouts every 5 seconds
    const int CLEANUP_CHECK_INTERVAL = 10;  // Clean up inactive clients every 10 seconds

    // Initialize thread-local arena for this thread
    mcp_log_debug("Initializing thread-local arena for WebSocket server event thread");
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) { // 1MB arena
        mcp_log_error("Failed to initialize thread-local arena in WebSocket server event thread");
    }

    mcp_log_info("WebSocket server event thread started");

    while (data->running) {
        // Service libwebsockets with current timeout
        int service_result = lws_service(data->context, service_timeout_ms);
        if (service_result < 0) {
            mcp_log_warn("lws_service returned error: %d", service_result);
            // Don't exit the loop, just continue and try again
            // Add a small delay to avoid spinning on persistent errors
            if (service_result == -1) {
                mcp_sleep_ms(100);
            }
        }

        // Increment service counter
        service_count++;

        // Get current time once per loop iteration
        time_t now = time(NULL);

        // Adjust service timeout based on activity level (every 1 second)
        if (difftime(now, last_activity_check) >= ACTIVITY_CHECK_INTERVAL) {
            last_activity_check = now;

            // Use shorter timeout when there are active clients
            if (data->active_clients > 0) {
                // More clients = shorter timeout for better responsiveness
                if (data->active_clients > 10) {
                    service_timeout_ms = 10; // Very responsive for many clients
                } else {
                    service_timeout_ms = 20; // Balanced for few clients
                }
            } else {
                // No clients = longer timeout to reduce CPU usage
                service_timeout_ms = 50;
            }
        }

        // Log performance stats every ~60 seconds
        if (difftime(now, last_service_time) >= 60) {
            double elapsed = difftime(now, last_service_time);
            double rate = service_count / elapsed;
            mcp_log_debug("WebSocket server performance: %.1f service calls/sec, %lu active clients, timeout: %d ms",
                         rate, data->active_clients, service_timeout_ms);

            // Reset counters
            last_service_time = now;
            service_count = 0;
        }

        // Check for client timeouts and send pings (every 5 seconds)
        if (difftime(now, last_ping_check) >= PING_CHECK_INTERVAL) {
            last_ping_check = now;
            ws_server_check_timeouts(data);
        }

        // Clean up inactive clients (every 10 seconds)
        if (difftime(now, last_cleanup_check) >= CLEANUP_CHECK_INTERVAL) {
            last_cleanup_check = now;
            ws_server_cleanup_inactive_clients(data);
        }
    }

    mcp_log_info("WebSocket server event thread exiting");

    // Destroy thread-local arena before thread exits
    mcp_log_debug("Destroying thread-local arena for WebSocket server event thread");
    mcp_arena_destroy_current_thread();

    return NULL;
}
