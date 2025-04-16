#include "internal/server_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

// Structure to hold data for a message processing task
typedef struct message_task_data_t {
    mcp_server_t* server;
    mcp_transport_t* transport; // Transport to send response on
    void* message_data;         // Copied message data (owned by this struct)
    size_t message_size;
} message_task_data_t;

// Worker function executed by the thread pool
void process_message_task(void* arg) {
    PROFILE_START("process_message_task"); // Profile task execution
    message_task_data_t* task_data = (message_task_data_t*)arg;
    if (!task_data || !task_data->server || !task_data->transport || !task_data->message_data) {
        mcp_log_error("Error: Invalid task data in process_message_task.");
        // Attempt cleanup even with invalid data
        if (task_data) free(task_data->message_data);
        free(task_data);
        PROFILE_END("process_message_task"); // End profile on error
        return;
    }

    mcp_server_t* server = task_data->server;
    void* data = task_data->message_data;
    size_t size = task_data->message_size;
    size_t max_size = server->config.max_message_size > 0 ? server->config.max_message_size : DEFAULT_MAX_MESSAGE_SIZE;

    // --- Input Validation: Check message size ---
    if (size > max_size) {
        mcp_log_error("Error: Received message size (%zu) exceeds limit (%zu).", size, max_size);
        // We cannot generate a JSON-RPC error here as we haven't parsed the ID yet.
        // The connection will likely be closed by the transport layer after this task finishes.
        free(task_data->message_data); // Free copied data
        free(task_data);
        PROFILE_END("process_message_task"); // End profile on error
        return;
    }
    // --- End Input Validation ---

    int error_code = 0;

    // Verify if message has null terminator
    bool has_terminator = (size > 0 && ((const char*)data)[size-1] == '\0');
    if (!has_terminator) {
        mcp_log_warn("Task data missing terminator, this may cause JSON parsing errors");
    }

    // Call handle_message from mcp_server_dispatch.c (declared in internal header)
    mcp_log_debug("Calling handle_message with data: '%.*s'", (int)size, (const char*)data);
    char* response_json = NULL;

    // Use try-catch to catch any exceptions on Windows
#ifdef _WIN32
    __try {
#endif
        response_json = handle_message(server, data, size, &error_code);
        mcp_log_debug("handle_message returned: error_code=%d, response=%s",
                     error_code,
                     response_json ? "non-NULL" : "NULL");
#ifdef _WIN32
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        mcp_log_error("EXCEPTION in handle_message: %d", GetExceptionCode());
        error_code = MCP_ERROR_INTERNAL_ERROR;
        response_json = NULL;
    }
#endif

    // Log and free the response
    if (response_json != NULL) {
        // Free the response - actual response should be returned by transport_message_callback
        free(response_json);
    } else if (error_code != MCP_ERROR_NONE) {
        // Log processing error
        mcp_log_error("Error processing message (code: %d), no response generated.", error_code);
    }

    // Clean up task data
    free(task_data->message_data);
    free(task_data);
    PROFILE_END("process_message_task");
}

/**
 * @internal
 * @brief Callback function passed to the transport layer.
 *
 * This function is invoked by the transport when a complete message is received.
 * It directly processes the message and returns a response string.
 *
 * @param user_data Pointer to the mcp_server_t instance.
 * @param data Pointer to the received raw message data.
 * @param size Size of the received data.
 * @param[out] error_code Pointer to store potential errors during callback processing itself (not application errors).
 * @return A dynamically allocated string (malloc'd) containing the response to send, or NULL if no response should be sent.
 */
char* transport_message_callback(void* user_data, const void* data, size_t size, int* error_code) {
    PROFILE_START("transport_message_callback");
    mcp_server_t* server = (mcp_server_t*)user_data;
    if (server == NULL || data == NULL || size == 0 || error_code == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot process
    }
    *error_code = MCP_ERROR_NONE;

    // Check if server is shutting down
    if (server->shutting_down) {
        mcp_log_warn("Server is shutting down, rejecting new request");
        *error_code = MCP_ERROR_SERVER_SHUTTING_DOWN;
        return NULL;
    }

    // Increment active request counter
    #ifdef _WIN32
    InterlockedIncrement((LONG*)&server->active_requests);
    #else
    __sync_fetch_and_add(&server->active_requests, 1);
    #endif

    // --- Rate Limiting Check ---
    // Get client IP address from transport (or use default if not available)
    const char* client_ip = mcp_transport_get_client_ip(server->transport);
    if (!client_ip) {
        client_ip = "unknown";
    }

    // Check if rate limiting is enabled
    if (server->config.rate_limit_window_seconds > 0 && server->config.rate_limit_max_requests > 0) {
        bool allowed = true;

        if (server->config.use_advanced_rate_limiter && server->advanced_rate_limiter) {
            // Use advanced rate limiter
            allowed = mcp_advanced_rate_limiter_check(server->advanced_rate_limiter, client_ip, NULL, NULL, NULL);
            if (!allowed) {
                mcp_log_warn("Advanced rate limit exceeded for client IP: %s", client_ip ? client_ip : "unknown");
                *error_code = MCP_ERROR_TOO_MANY_REQUESTS;

                // Decrement active request counter since we're rejecting this request
                #ifdef _WIN32
                InterlockedDecrement((LONG*)&server->active_requests);
                #else
                __sync_fetch_and_sub(&server->active_requests, 1);
                #endif

                PROFILE_END("transport_message_callback");
                return NULL;
            }
        } else if (server->rate_limiter) {
            // Use basic rate limiter
            allowed = mcp_rate_limiter_check(server->rate_limiter, client_ip ? client_ip : "unknown");
            if (!allowed) {
                mcp_log_warn("Rate limit exceeded for client IP: %s", client_ip ? client_ip : "unknown");
                *error_code = MCP_ERROR_TOO_MANY_REQUESTS;

                // Decrement active request counter since we're rejecting this request
                #ifdef _WIN32
                InterlockedDecrement((LONG*)&server->active_requests);
                #else
                __sync_fetch_and_sub(&server->active_requests, 1);
                #endif

                PROFILE_END("transport_message_callback");
                return NULL;
            }
        }
    }

    // --- Input validation ---
    size_t max_size = server->config.max_message_size > 0 ?
                      server->config.max_message_size : DEFAULT_MAX_MESSAGE_SIZE;

    if (size > max_size) {
        mcp_log_error("Received message size (%zu) exceeds limit (%zu)", size, max_size);
        *error_code = MCP_ERROR_INVALID_REQUEST;
        return NULL;
    }

    // --- Prepare message data ---
    // Check if message has terminator
    bool has_terminator = (size > 0 && ((const char*)data)[size-1] == '\0');
    void* message_copy;

    if (has_terminator) {
        // If already has terminator, just copy
        message_copy = malloc(size);
        if (message_copy) {
            memcpy(message_copy, data, size);
        }
    } else {
        // If no terminator, add one
        message_copy = malloc(size + 1);
        if (message_copy) {
            memcpy(message_copy, data, size);
            ((char*)message_copy)[size] = '\0';
            mcp_log_debug("Added NULL terminator to message data");
        }
    }

    if (!message_copy) {
        mcp_log_error("Failed to allocate memory for message copy");
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("transport_message_callback");
        return NULL;
    }

    // --- Direct message processing ---
    int handler_error_code = MCP_ERROR_NONE;
    char* response_json = NULL;

    mcp_log_debug("Transport callback: calling handle_message with data: '%.*s'", (int)size, (const char*)message_copy);

    // Use try-catch to catch any exceptions on Windows
#ifdef _WIN32
    __try {
#endif
        response_json = handle_message(server, message_copy, size, &handler_error_code);
        mcp_log_debug("Transport callback: handle_message returned: error_code=%d, response=%s",
                     handler_error_code,
                     response_json ? "non-NULL" : "NULL");
#ifdef _WIN32
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        mcp_log_error("EXCEPTION in transport callback handle_message: %d", GetExceptionCode());
        handler_error_code = MCP_ERROR_INTERNAL_ERROR;
        response_json = NULL;
    }
#endif

    // Free message copy
    free(message_copy);

    // --- Handle response ---
    char* result = NULL;
    if (response_json != NULL) {
        // Return response string to be sent via client socket
        result = response_json; // Caller is responsible for freeing
    } else if (handler_error_code != MCP_ERROR_NONE) {
        // Message processing failed but no error response was generated
        mcp_log_error("Failed to process message (error code: %d), no response generated", handler_error_code);
    }

    // Decrement active request counter
    #ifdef _WIN32
    LONG prev_count = InterlockedDecrement((LONG*)&server->active_requests);
    #else
    int prev_count = __sync_fetch_and_sub(&server->active_requests, 1);
    #endif

    // If this was the last request and server is shutting down, signal the condition variable
    if (prev_count == 1 && server->shutting_down) {
        if (mcp_mutex_lock(server->shutdown_mutex) == 0) {
            mcp_cond_signal(server->shutdown_cond);
            mcp_mutex_unlock(server->shutdown_mutex);
            mcp_log_info("Last request completed, signaling shutdown condition");
        }
    }

    PROFILE_END("transport_message_callback");
    return result; // Return the response or NULL
}
