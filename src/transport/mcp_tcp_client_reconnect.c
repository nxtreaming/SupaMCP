#include "internal/tcp_client_transport_internal.h"
#include <stdlib.h>
#include <time.h>
#include <math.h>

// Define the default reconnection configuration
const mcp_reconnect_config_t MCP_DEFAULT_RECONNECT_CONFIG = {
    .enable_reconnect = true,
    .max_reconnect_attempts = 10,
    .initial_reconnect_delay_ms = 1000,  // 1 second
    .max_reconnect_delay_ms = 30000,     // 30 seconds
    .backoff_factor = 2.0f,              // Double the delay each time
    .randomize_delay = true              // Add randomness to delay
};

/**
 * @brief Updates the connection state and notifies the callback if set.
 *
 * @param data The TCP client transport data.
 * @param new_state The new connection state.
 */
void mcp_tcp_client_update_connection_state(mcp_tcp_client_transport_data_t* data, mcp_connection_state_t new_state) {
    if (data->connection_state != new_state) {
        data->connection_state = new_state;

        // Call the state callback if set
        if (data->state_callback) {
            data->state_callback(
                data->state_callback_user_data,
                new_state,
                data->reconnect_attempt
            );
        }
    }
}

/**
 * @brief Calculates the delay for the next reconnection attempt using exponential backoff.
 *
 * @param config The reconnection configuration.
 * @param attempt The current reconnection attempt number.
 * @return The delay in milliseconds.
 */
static uint32_t calculate_reconnect_delay(const mcp_reconnect_config_t* config, int attempt) {
    // Calculate base delay with exponential backoff
    float delay = (float)config->initial_reconnect_delay_ms * powf(config->backoff_factor, (float)(attempt - 1));

    // Cap at maximum delay
    if (delay > config->max_reconnect_delay_ms) {
        delay = (float)config->max_reconnect_delay_ms;
    }

    // Add randomness if enabled (±20% variation)
    if (config->randomize_delay) {
        // Ensure random number generator is seeded
        static bool seeded = false;
        if (!seeded) {
            srand((unsigned int)time(NULL));
            seeded = true;
        }

        // Generate random factor between 0.8 and 1.2
        float random_factor = 0.8f + ((float)rand() / RAND_MAX) * 0.4f;
        delay *= random_factor;
    }

    return (uint32_t)delay;
}

/**
 * @brief Attempts to reconnect to the server.
 *
 * @param data The TCP client transport data.
 * @return 0 on success, non-zero on failure.
 */
static int attempt_reconnect(mcp_tcp_client_transport_data_t* data) {
    mcp_log_info("Attempting to reconnect to %s:%u (attempt %d/%d)",
                data->host, data->port, data->reconnect_attempt,
                data->reconnect_config.max_reconnect_attempts);

    // Close existing socket if any
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
    }

    // Attempt to connect with a timeout
    data->sock = mcp_socket_connect(data->host, data->port, 5000);
    if (data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Reconnection attempt %d failed", data->reconnect_attempt);
        return -1;
    }

    // Connection successful
    data->connected = true;
    mcp_log_info("Reconnected successfully to %s:%u", data->host, data->port);

    // Start receiver thread
    reconnection_in_progress = true;
    if (mcp_thread_create(&data->receive_thread, tcp_client_receive_thread_func, data->transport_handle) != 0) {
        mcp_log_error("Failed to create receiver thread after reconnection");
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
        data->connected = false;
        reconnection_in_progress = false;
        return -1;
    }

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTED);

    return 0;
}

/**
 * @brief Thread function for handling reconnection attempts with exponential backoff.
 *
 * @param arg Pointer to the mcp_transport_t handle.
 * @return NULL on exit.
 */
void* tcp_client_reconnect_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    mcp_log_info("Reconnect thread started");

    while (data->reconnect_thread_running) {
        // Lock mutex to access reconnection state
        mcp_mutex_lock(data->reconnect_mutex);

        // Check if we should continue reconnecting
        if (!data->reconnect_enabled || data->connected ||
            (data->reconnect_config.max_reconnect_attempts > 0 &&
             data->reconnect_attempt > data->reconnect_config.max_reconnect_attempts)) {
            // Stop reconnection attempts
            mcp_mutex_unlock(data->reconnect_mutex);
            break;
        }

        // Calculate delay for this attempt
        uint32_t delay_ms = calculate_reconnect_delay(&data->reconnect_config, data->reconnect_attempt);

        // Unlock mutex while waiting
        mcp_mutex_unlock(data->reconnect_mutex);

        // Wait for the calculated delay
        mcp_log_info("Waiting %u ms before reconnection attempt %d", delay_ms, data->reconnect_attempt);

        // Sleep with periodic checks to allow early termination
        uint32_t sleep_chunk = 100; // Check every 100ms
        uint32_t elapsed = 0;
        while (elapsed < delay_ms && data->reconnect_thread_running) {
            uint32_t chunk = (delay_ms - elapsed < sleep_chunk) ? (delay_ms - elapsed) : sleep_chunk;
#ifdef _WIN32
            Sleep(chunk);
#else
            usleep(chunk * 1000);
#endif
            elapsed += chunk;
        }

        // Check if thread should still be running
        if (!data->reconnect_thread_running) {
            break;
        }

        // Lock mutex again to attempt reconnection
        mcp_mutex_lock(data->reconnect_mutex);

        // Update state to reconnecting
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_RECONNECTING);

        // Attempt to reconnect
        if (attempt_reconnect(data) == 0) {
            // Reconnection successful
            mcp_mutex_unlock(data->reconnect_mutex);
            break;
        }

        // Increment attempt counter
        data->reconnect_attempt++;

        // Check if we've reached the maximum number of attempts
        if (data->reconnect_config.max_reconnect_attempts > 0 &&
            data->reconnect_attempt > data->reconnect_config.max_reconnect_attempts) {
            mcp_log_error("Maximum reconnection attempts (%d) reached",
                         data->reconnect_config.max_reconnect_attempts);
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
            mcp_mutex_unlock(data->reconnect_mutex);
            break;
        }

        mcp_mutex_unlock(data->reconnect_mutex);
    }

    mcp_log_info("Reconnect thread exiting");
    return NULL;
}

/**
 * @brief Starts the reconnection process.
 *
 * @param transport The transport handle.
 * @return 0 on success, non-zero on error.
 */
int start_reconnection_process(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to access reconnection state
    mcp_mutex_lock(data->reconnect_mutex);

    // Check if reconnection is enabled and not already in progress
    if (!data->reconnect_enabled || data->reconnect_thread_running) {
        mcp_mutex_unlock(data->reconnect_mutex);
        return 0; // Not an error, just no action needed
    }

    // Reset reconnection attempt counter
    data->reconnect_attempt = 1;

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_RECONNECTING);

    // Start reconnection thread
    data->reconnect_thread_running = true;
    if (mcp_thread_create(&data->reconnect_thread, tcp_client_reconnect_thread_func, transport) != 0) {
        mcp_log_error("Failed to create reconnection thread");
        data->reconnect_thread_running = false;
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
        mcp_mutex_unlock(data->reconnect_mutex);
        return -1;
    }

    mcp_mutex_unlock(data->reconnect_mutex);
    return 0;
}

/**
 * @brief Stops the reconnection process.
 *
 * @param transport The transport handle.
 */
void stop_reconnection_process(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to access reconnection state
    mcp_mutex_lock(data->reconnect_mutex);

    // Check if reconnection thread is running
    if (data->reconnect_thread_running) {
        // Signal thread to stop
        data->reconnect_thread_running = false;

        // Unlock mutex while waiting for thread to exit
        mcp_mutex_unlock(data->reconnect_mutex);

        // Wait for thread to exit
        if (data->reconnect_thread) {
            mcp_thread_join(data->reconnect_thread, NULL);
            data->reconnect_thread = 0;
        }
    } else {
        mcp_mutex_unlock(data->reconnect_mutex);
    }
}

/**
 * @brief Sets the connection state callback for a TCP client transport.
 *
 * @param transport The transport handle.
 * @param callback The callback function.
 * @param user_data User data to pass to the callback.
 * @return 0 on success, non-zero on error.
 */
int mcp_tcp_client_set_connection_state_callback(
    mcp_transport_t* transport,
    mcp_connection_state_callback_t callback,
    void* user_data
) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to safely update callback
    mcp_mutex_lock(data->reconnect_mutex);

    // Set callback and user data
    data->state_callback = callback;
    data->state_callback_user_data = user_data;

    // If callback is set, immediately call it with current state
    if (callback) {
        callback(user_data, data->connection_state, data->reconnect_attempt);
    }

    mcp_mutex_unlock(data->reconnect_mutex);
    return 0;
}

/**
 * @brief Gets the current connection state of a TCP client transport.
 *
 * @param transport The transport handle.
 * @return The current connection state.
 */
mcp_connection_state_t mcp_tcp_client_get_connection_state(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return MCP_CONNECTION_STATE_DISCONNECTED;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;
    return data->connection_state;
}

/**
 * @brief Manually triggers a reconnection attempt.
 *
 * @param transport The transport handle.
 * @return 0 on success, non-zero on error.
 */
int mcp_tcp_client_reconnect(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to access reconnection state
    mcp_mutex_lock(data->reconnect_mutex);

    // Check if already connected or reconnection in progress
    if (data->connected || data->reconnect_thread_running) {
        mcp_mutex_unlock(data->reconnect_mutex);
        return 0; // Not an error, just no action needed
    }

    // Enable reconnection if not already enabled
    data->reconnect_enabled = true;

    // Reset reconnection attempt counter
    data->reconnect_attempt = 1;

    // Update connection state
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_RECONNECTING);

    // Try immediate reconnection
    int result = attempt_reconnect(data);

    // If immediate reconnection failed, start reconnection process
    if (result != 0) {
        // Start reconnection thread
        data->reconnect_thread_running = true;
        if (mcp_thread_create(&data->reconnect_thread, tcp_client_reconnect_thread_func, transport) != 0) {
            mcp_log_error("Failed to create reconnection thread");
            data->reconnect_thread_running = false;
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
            mcp_mutex_unlock(data->reconnect_mutex);
            return -1;
        }
    }

    mcp_mutex_unlock(data->reconnect_mutex);
    return 0;
}
