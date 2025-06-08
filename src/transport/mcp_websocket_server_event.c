#include "internal/websocket_server_internal.h"

// Check for client timeouts and send pings
static void ws_server_check_timeouts(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only check every PING_INTERVAL_MS milliseconds
    if (difftime(now, data->last_ping_time) * 1000 < WS_PING_INTERVAL_MS) {
        return;
    }

    data->last_ping_time = now;

    // Use read lock for checking client timeouts (read-only operation)
    ws_server_read_lock_clients(data);

    for (uint32_t i = 0; i < data->max_clients; i++) {
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

    ws_server_read_unlock_clients(data);
}

// Clean up inactive clients
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

    // Use read lock for cleanup operations (mostly read-only)
    ws_server_read_lock_clients(data);

    // Use bitmap to quickly skip inactive clients with atomic reads
    for (uint32_t i = 0; i < data->bitmap_size; i++) {
        // Atomic read of bitmap word
#ifdef _WIN32
        uint32_t word = InterlockedOr((volatile LONG*)&data->client_bitmap[i], 0);
#else
        uint32_t word = __sync_or_and_fetch(&data->client_bitmap[i], 0);
#endif

        // Skip words with no active clients
        if (word == 0) {
            continue;
        }

        // Process only active bits in this word
        for (int j = 0; j < 32; j++) {
            if ((word & (1 << j)) == 0) {
                continue; // Skip inactive slots
            }

            uint32_t client_index = i * 32 + j;
            if (client_index >= data->max_clients) {
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

    ws_server_read_unlock_clients(data);
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
        // Check for forced service requirements and adjust timeout
        int adjusted_timeout = lws_service_adjust_timeout(data->context, service_timeout_ms, 0);

        // Service libwebsockets with adjusted timeout
        int service_result = lws_service(data->context, adjusted_timeout);
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

            // Use atomic load to read active client count without locking
            uint32_t current_active_clients = MCP_ATOMIC_LOAD(data->active_clients);

            // Use shorter timeout when there are active clients
            if (current_active_clients > 0) {
                // More clients = shorter timeout for better responsiveness
                if (current_active_clients > 100) {
                    service_timeout_ms = 10; // Very responsive for many clients
                } else {
                    service_timeout_ms = 20; // Balanced for few clients
                }
            } else {
                // No clients = longer timeout to reduce CPU usage
                service_timeout_ms = 50;
            }
        }

        // Log performance stats every ~60 seconds (only when performance logging enabled)
        if (difftime(now, last_service_time) >= 60) {
            double elapsed = difftime(now, last_service_time);
#if MCP_ENABLE_PERF_LOGS
            double rate = service_count / elapsed;
            uint32_t current_active_clients = MCP_ATOMIC_LOAD(data->active_clients);
            mcp_log_perf("[WS] performance: %.1f service calls/sec, %u active clients, timeout: %d ms",
                         rate, current_active_clients, service_timeout_ms);
#else
            // Avoid unused variable warning when performance logging is disabled
            (void)elapsed;
#endif

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
