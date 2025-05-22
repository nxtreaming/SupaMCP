/**
 * @file mcp_tcp_client_reconnect.c
 * @brief Implementation of TCP client reconnection functionality.
 *
 * This file implements the reconnection logic for TCP client connections,
 * including exponential backoff with jitter, connection state management,
 * and automatic reconnection attempts.
 */
#include "internal/tcp_client_transport_internal.h"
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdarg.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

// Global flag to indicate reconnection is in progress
// This is accessed by other files, so it must remain a non-static global
bool reconnection_in_progress = false;

// Global mutex for thread-safe access to reconnection_in_progress
static mcp_mutex_t* g_reconnect_mutex = NULL;
static bool g_mutex_initialized = false;

// Initialize global mutex if not already initialized
static void init_global_mutex(void) {
    if (!g_mutex_initialized) {
        g_reconnect_mutex = mcp_mutex_create();
        if (!g_reconnect_mutex) {
            mcp_log_error("Failed to create global reconnect mutex");
            return;
        }
        g_mutex_initialized = true;
    }
}

// Thread-safe check if reconnection is in progress
bool is_reconnection_in_progress(void) {
    if (!g_mutex_initialized) {
        return false;
    }
    
    mcp_mutex_lock(g_reconnect_mutex);
    bool in_progress = reconnection_in_progress;
    mcp_mutex_unlock(g_reconnect_mutex);
    return in_progress;
}

// Thread-safe set reconnection in progress
static void set_reconnection_in_progress(bool in_progress) {
    if (!g_mutex_initialized) {
        return;
    }
    
    mcp_mutex_lock(g_reconnect_mutex);
    reconnection_in_progress = in_progress;
    mcp_mutex_unlock(g_reconnect_mutex);
}

/**
 * @brief String formatting helper function with static buffer
 * 
 * @note This function is not thread-safe. For thread safety, use a mutex.
 */
static const char* mcp_string_format(const char* format, ...) {
    static char buffer[256];
    
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Ensure null termination if truncation occurred
    if (len >= (int)sizeof(buffer)) {
        buffer[sizeof(buffer) - 1] = '\0';
    }
    
    return buffer;
}

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
 * This function updates the connection state of a TCP client transport and
 * calls the state callback if one is registered. The callback is only called
 * when the state actually changes.
 *
 * @param data The TCP client transport data
 * @param new_state The new connection state
 */
void mcp_tcp_client_update_connection_state(mcp_tcp_client_transport_data_t* data, mcp_connection_state_t new_state) {
    if (!data) {
        mcp_log_error("NULL data parameter in update_connection_state");
        return;
    }

    // Only update and notify if the state actually changes
    if (data->connection_state != new_state) {
        // Log the state change
        const char* state_names[] = {
            "Disconnected",
            "Connecting",
            "Connected",
            "Reconnecting",
            "Failed"
        };

        mcp_log_info("Connection state changed: %s -> %s",
                    state_names[data->connection_state],
                    state_names[new_state]);

        // Update the state
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
 * @brief Calculates the delay for the next reconnection attempt using exponential backoff with jitter.
 *
 * This function implements an improved exponential backoff algorithm with jitter to prevent
 * the "thundering herd" problem where multiple clients reconnect at the same time.
 *
 * The algorithm works as follows:
 * 1. Calculate base delay: initial_delay * (backoff_factor ^ (attempt - 1))
 * 2. Cap the base delay at the maximum configured delay
 * 3. If jitter is enabled, add randomness to the delay
 * 4. Ensure the final delay is at least 10% of the base delay
 *
 * @param config The reconnection configuration
 * @param attempt The current reconnection attempt number
 * @return The delay in milliseconds
 */
static uint32_t calculate_reconnect_delay(const mcp_reconnect_config_t* config, int attempt) {
    if (!config) {
        mcp_log_error("NULL config parameter in calculate_reconnect_delay");
        return 1000; // Return a reasonable default (1 second)
    }

    // Ensure attempt is at least 1
    if (attempt < 1) {
        attempt = 1;
    }

    // Calculate base delay with exponential backoff
    // initial_delay * (backoff_factor ^ (attempt - 1))
    float base_delay = (float)config->initial_reconnect_delay_ms *
                       powf(config->backoff_factor, (float)(attempt - 1));

    // Cap at maximum delay
    if (base_delay > config->max_reconnect_delay_ms) {
        base_delay = (float)config->max_reconnect_delay_ms;
    }

    // Start with base delay
    float final_delay = base_delay;

    // Add jitter if enabled (full jitter algorithm)
    if (config->randomize_delay) {
        // Use thread-safe random number generation with mutex
        static mcp_mutex_t* rand_mutex = NULL;
        static bool seeded = false;

        // Initialize random seed if not already done
        if (!seeded) {
            if (!rand_mutex) {
                rand_mutex = mcp_mutex_create();
                if (!rand_mutex) {
                    mcp_log_error("Failed to create random number mutex");
                    return (uint32_t)base_delay; // Fall back to base delay
                }
            }

            mcp_mutex_lock(rand_mutex);
            
            if (!seeded) {
                // Use time and thread ID for seed
#ifdef _WIN32
                unsigned int seed = (unsigned int)(GetTickCount() ^ GetCurrentThreadId());
#else
                unsigned int seed = (unsigned int)(time(NULL) ^ (uintptr_t)pthread_self());
#endif
                srand(seed);
                seeded = true;
            }
            
            mcp_mutex_unlock(rand_mutex);
        }

        // Generate random value between 0 and 1
        float random = (float)rand() / (float)RAND_MAX;
        
        // Apply jitter: random value between 0 and base_delay
        final_delay = random * base_delay;

        // Ensure minimum delay is at least 10% of base delay
        // This prevents very short delays that could lead to rapid reconnection attempts
        if (final_delay < (base_delay * 0.1f)) {
            final_delay = base_delay * 0.1f;
        }
    }

    // Convert to uint32_t with bounds checking
    uint32_t delay_ms = (final_delay > (float)UINT32_MAX) ? UINT32_MAX : (uint32_t)final_delay;

    // Log the calculated delay
    mcp_log_debug("Reconnect delay: base=%u ms, with jitter=%u ms (attempt %d)",
                 (uint32_t)base_delay, delay_ms, attempt);

    return delay_ms;
}

/**
 * @brief Attempts to reconnect to the server.
 *
 * This function tries to establish a new connection to the server and
 * starts the receiver thread if successful. It handles cleanup of any
 * existing socket and updates the connection state accordingly.
 *
 * @param data The TCP client transport data
 * @return 0 on success, non-zero on failure
 */
static int attempt_reconnect(mcp_tcp_client_transport_data_t* data) {
    if (!data) {
        mcp_log_error("NULL data parameter in attempt_reconnect");
        return -1;
    }

    if (!data->host) {
        mcp_log_error("NULL host in attempt_reconnect");
        return -1;
    }

    // Log reconnection attempt
    int max_attempts = data->reconnect_config.max_reconnect_attempts;
    mcp_log_info("Attempting to reconnect to %s:%u (attempt %d/%s)",
                data->host, data->port, data->reconnect_attempt,
                max_attempts > 0 ? mcp_string_format("%d", max_attempts) : "unlimited");

    // Close existing socket if any
    if (data->sock != MCP_INVALID_SOCKET) {
        mcp_log_debug("Closing existing socket before reconnection attempt");
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
    }

    // Attempt to connect with a timeout (5 seconds)
    const int CONNECT_TIMEOUT_MS = 5000;
    data->sock = mcp_socket_connect(data->host, data->port, CONNECT_TIMEOUT_MS);
    if (data->sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Reconnection attempt %d failed", data->reconnect_attempt);
        return -1;
    }

    // Connection successful
    data->connected = true;
    mcp_log_info("Reconnected successfully to %s:%u", data->host, data->port);

    // Set global reconnection in progress flag
    set_reconnection_in_progress(true);

    // Start receiver thread
    if (mcp_thread_create(&data->receive_thread, tcp_client_receive_thread_func, data->transport_handle) != 0) {
        mcp_log_error("Failed to create receiver thread after reconnection");

        // Clean up on failure
        mcp_socket_close(data->sock);
        data->sock = MCP_INVALID_SOCKET;
        data->connected = false;
        set_reconnection_in_progress(false);

        return -1;
    }

    // Update connection state to connected
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_CONNECTED);

    // Reset reconnection attempt counter on successful reconnection
    data->reconnect_attempt = 0;

    return 0;
}

/**
 * @brief Thread function for handling reconnection attempts with exponential backoff.
 *
 * This function implements an efficient reconnection algorithm with proper
 * thread synchronization and improved sleep logic. It uses a condition variable
 * to allow for early wakeup if the thread needs to be stopped.
 *
 * The reconnection process follows these steps:
 * 1. Calculate delay based on the current attempt number
 * 2. Wait for the calculated delay (can be interrupted)
 * 3. Attempt to reconnect to the server
 * 4. If successful, exit the thread
 * 5. If unsuccessful, increment attempt counter and try again
 * 6. If maximum attempts reached, mark as failed and exit
 *
 * @param arg Pointer to the mcp_transport_t handle
 * @return NULL on exit
 */
void* tcp_client_reconnect_thread_func(void* arg) {
    if (!arg) {
        mcp_log_error("NULL argument to reconnect thread function");
        return NULL;
    }

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (!transport->transport_data) {
        mcp_log_error("Invalid transport data in reconnect thread");
        return NULL;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    mcp_log_info("Reconnect thread started");

    // Create a condition variable for efficient waiting with interrupt capability
    mcp_cond_t* wait_cond = mcp_cond_create();
    if (!wait_cond) {
        mcp_log_error("Failed to create condition variable for reconnect thread");
        return NULL;
    }

    // Main reconnection loop
    while (data->reconnect_thread_running) {
        // Lock mutex to access shared reconnection state
        mcp_mutex_lock(data->reconnect_mutex);

        // Check if we should continue reconnecting
        bool should_stop = false;

        // Stop if reconnection is disabled
        if (!data->reconnect_enabled) {
            mcp_log_debug("Reconnection disabled, stopping reconnect thread");
            should_stop = true;
        }
        // Stop if already connected
        else if (data->connected) {
            mcp_log_debug("Already connected, stopping reconnect thread");
            should_stop = true;
        }
        // Stop if maximum attempts reached
        else if (data->reconnect_config.max_reconnect_attempts > 0 &&
                 data->reconnect_attempt > data->reconnect_config.max_reconnect_attempts) {
            mcp_log_debug("Maximum reconnection attempts reached, stopping reconnect thread");
            should_stop = true;
        }

        if (should_stop) {
            mcp_mutex_unlock(data->reconnect_mutex);
            break;
        }

        // Calculate delay for this attempt using exponential backoff with jitter
        uint32_t delay_ms = calculate_reconnect_delay(&data->reconnect_config, data->reconnect_attempt);

        // Log the wait time
        mcp_log_info("Waiting %u ms before reconnection attempt %d", delay_ms, data->reconnect_attempt);

        // Wait using condition variable with timeout
        // This allows the thread to be woken up early if needed (e.g., when stopping)
        mcp_cond_timedwait(wait_cond, data->reconnect_mutex, delay_ms);

        // Check if we were interrupted or if the thread should exit
        if (!data->reconnect_thread_running) {
            mcp_log_debug("Reconnect thread interrupted during wait");
            mcp_mutex_unlock(data->reconnect_mutex);
            break;
        }

        // Update state to reconnecting
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_RECONNECTING);

        // Format max attempts string for logging
        const char* max_attempts_str;
        if (data->reconnect_config.max_reconnect_attempts > 0) {
            max_attempts_str = mcp_string_format("%d", data->reconnect_config.max_reconnect_attempts);
        } else {
            max_attempts_str = "unlimited";
        }

        mcp_log_info("Attempting reconnection (attempt %d/%s)",
                    data->reconnect_attempt, max_attempts_str);

        // Attempt to reconnect
        if (attempt_reconnect(data) == 0) {
            // Reconnection successful
            mcp_log_info("Reconnection successful, exiting reconnect thread");
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

            // Update connection state to failed
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);

            mcp_mutex_unlock(data->reconnect_mutex);
            break;
        }

        mcp_mutex_unlock(data->reconnect_mutex);
    }

    // Clean up condition variable
    if (wait_cond) {
        mcp_cond_destroy(wait_cond);
    }

    mcp_log_info("Reconnect thread exiting");
    return NULL;
}

/**
 * @brief Starts the reconnection process.
 *
 * This function initiates the reconnection process by creating a thread
 * that will attempt to reconnect to the server with exponential backoff.
 * It ensures that only one reconnection thread is running at a time.
 *
 * @param transport The transport handle
 * @return 0 on success, non-zero on error
 */
int start_reconnection_process(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in start_reconnection_process");
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to access shared reconnection state
    mcp_mutex_lock(data->reconnect_mutex);

    // Check if reconnection is enabled
    if (!data->reconnect_enabled) {
        mcp_log_debug("Reconnection is disabled, not starting reconnection process");
        mcp_mutex_unlock(data->reconnect_mutex);
        return 0;
    }

    // Check if reconnection thread is already running
    if (data->reconnect_thread_running) {
        mcp_log_debug("Reconnection thread already running, not starting another");
        mcp_mutex_unlock(data->reconnect_mutex);
        return 0;
    }

    mcp_log_info("Starting reconnection process");

    // Reset reconnection attempt counter
    data->reconnect_attempt = 1;

    // Update connection state to reconnecting
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_RECONNECTING);

    // Set flag and update global state before creating thread
    data->reconnect_thread_running = true;
    set_reconnection_in_progress(true);

    // Create reconnection thread
    int thread_result = mcp_thread_create(
        &data->reconnect_thread,
        tcp_client_reconnect_thread_func,
        transport
    );
    if (thread_result == 0) {
        mcp_log_debug("Reconnection thread created successfully");
    }

    if (thread_result != 0) {
        mcp_log_error("Failed to create reconnection thread (error: %d)", thread_result);

        // Reset flags on failure
        data->reconnect_thread_running = false;
        set_reconnection_in_progress(false);

        // Update connection state to failed
        mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);
        
        mcp_log_error("Failed to start reconnection thread (error: %d)", thread_result);

        mcp_mutex_unlock(data->reconnect_mutex);
        return -1;
    }

    mcp_mutex_unlock(data->reconnect_mutex);
    return 0;
}

/**
 * @brief Stops the reconnection process.
 *
 * This function stops any ongoing reconnection process by signaling
 * the reconnection thread to exit and waiting for it to complete.
 * It properly handles thread synchronization to ensure clean shutdown.
 *
 * @param transport The transport handle
 */
void stop_reconnection_process(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        mcp_log_debug("Invalid transport handle in stop_reconnection_process");
        return;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to access shared reconnection state
    mcp_mutex_lock(data->reconnect_mutex);

    // Check if reconnection thread is running
    if (data->reconnect_thread_running) {
        mcp_log_info("Stopping reconnection process");

        // Signal thread to stop
        data->reconnect_thread_running = false;

        // Unlock mutex while waiting for thread to exit
        // This is important to avoid deadlock, as the thread needs to
        // acquire the mutex to check the running flag
        mcp_mutex_unlock(data->reconnect_mutex);

        // Wait for thread to exit with timeout
        if (data->reconnect_thread) {
            const int THREAD_JOIN_TIMEOUT_MS = 5000; // 5 second timeout
            mcp_log_debug("Waiting for reconnection thread to exit (timeout: %d ms)", THREAD_JOIN_TIMEOUT_MS);
        
            // Try to join the thread
            // Note: Using mcp_thread_join without timeout for now
            // In the future, consider implementing a proper timed join if needed
            int join_result = mcp_thread_join(data->reconnect_thread, NULL);
            if (join_result != 0) {
                mcp_log_warn("Reconnection thread did not exit cleanly (error: %d)", join_result);
            }
        
            data->reconnect_thread = 0;
            set_reconnection_in_progress(false);
            mcp_log_debug("Reconnection thread has exited");
        }
    } else {
        mcp_log_debug("No reconnection process running");
        mcp_mutex_unlock(data->reconnect_mutex);
    }
}

/**
 * @brief Sets the connection state callback for a TCP client transport.
 *
 * This function registers a callback that will be called whenever the
 * connection state changes. The callback is immediately called with the
 * current state when registered.
 *
 * @param transport The transport handle
 * @param callback The callback function to register
 * @param user_data User data to pass to the callback
 * @return 0 on success, non-zero on error
 */
int mcp_tcp_client_set_connection_state_callback(
    mcp_transport_t* transport,
    mcp_connection_state_callback_t callback,
    void* user_data
) {
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in set_connection_state_callback");
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to safely update callback
    mcp_mutex_lock(data->reconnect_mutex);

    // Set callback and user data
    data->state_callback = callback;
    data->state_callback_user_data = user_data;

    // Log callback registration
    if (callback) {
        mcp_log_debug("Connection state callback registered");

        // Immediately call the callback with current state
        // This ensures the caller gets the current state right away
        callback(user_data, data->connection_state, data->reconnect_attempt);
    } else {
        mcp_log_debug("Connection state callback cleared");
    }

    mcp_mutex_unlock(data->reconnect_mutex);
    return 0;
}

/**
 * @brief Gets the current connection state of a TCP client transport.
 *
 * This function returns the current connection state of a TCP client transport.
 * If the transport handle is invalid, it returns MCP_CONNECTION_STATE_DISCONNECTED.
 *
 * @param transport The transport handle
 * @return The current connection state
 */
mcp_connection_state_t mcp_tcp_client_get_connection_state(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        mcp_log_debug("Invalid transport handle in get_connection_state");
        return MCP_CONNECTION_STATE_DISCONNECTED;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // No need to lock mutex here as reading an enum is atomic
    mcp_connection_state_t state = data->connection_state;

    return state;
}

/**
 * @brief Manually triggers a reconnection attempt.
 *
 * This function allows the application to manually trigger a reconnection
 * attempt. It first tries an immediate reconnection, and if that fails,
 * it starts the reconnection process with exponential backoff.
 *
 * @param transport The transport handle
 * @return 0 on success, non-zero on error
 */
int mcp_tcp_client_reconnect(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        mcp_log_error("Invalid transport handle in manual reconnect");
        return -1;
    }

    mcp_tcp_client_transport_data_t* data = (mcp_tcp_client_transport_data_t*)transport->transport_data;

    // Lock mutex to access shared reconnection state
    mcp_mutex_lock(data->reconnect_mutex);

    // Check if already connected
    if (data->connected) {
        mcp_log_info("Already connected, ignoring manual reconnect request");
        mcp_mutex_unlock(data->reconnect_mutex);
        return 0;
    }

    // Check if reconnection is already in progress
    if (data->reconnect_thread_running) {
        mcp_log_info("Reconnection already in progress, ignoring manual reconnect request");
        mcp_mutex_unlock(data->reconnect_mutex);
        return 0;
    }

    mcp_log_info("Manual reconnection requested");

    // Enable reconnection if not already enabled
    if (!data->reconnect_enabled) {
        mcp_log_debug("Enabling reconnection for manual reconnect");
        data->reconnect_enabled = true;
    }

    // Reset reconnection attempt counter
    data->reconnect_attempt = 1;

    // Update connection state to reconnecting
    mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_RECONNECTING);

    // Try immediate reconnection first
    mcp_log_info("Attempting immediate reconnection");
    int result = attempt_reconnect(data);
    // If immediate reconnection failed, start reconnection process with backoff
    if (result != 0) {
        mcp_log_info("Immediate reconnection failed, starting reconnection process");

        // Set flag before creating thread
        data->reconnect_thread_running = true;

        // Create reconnection thread
        int thread_result = mcp_thread_create(
            &data->reconnect_thread,
            tcp_client_reconnect_thread_func,
            transport
        );

        if (thread_result != 0) {
            mcp_log_error("Failed to create reconnection thread (error: %d)", thread_result);

            // Reset flag on failure
            data->reconnect_thread_running = false;

            // Update connection state to failed
            mcp_tcp_client_update_connection_state(data, MCP_CONNECTION_STATE_FAILED);

            mcp_mutex_unlock(data->reconnect_mutex);
            return -1;
        }

        mcp_log_debug("Reconnection thread created for manual reconnect");
    } else {
        mcp_log_info("Immediate reconnection successful");
    }

    mcp_mutex_unlock(data->reconnect_mutex);
    return 0;
}
