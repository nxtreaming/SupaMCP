#include "mcp_server_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// --- Thread Pool Task Data and Worker ---

// Structure to hold data for a message processing task
// Definition moved here from mcp_server.c
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
        fprintf(stderr, "Error: Invalid task data in process_message_task.\n");
        // Attempt cleanup even with invalid data
        if (task_data) free(task_data->message_data);
        free(task_data);
        PROFILE_END("process_message_task"); // End profile on error
        return;
    }

    mcp_server_t* server = task_data->server;
    mcp_transport_t* transport = task_data->transport;
    void* data = task_data->message_data;
    size_t size = task_data->message_size;
    size_t max_size = server->config.max_message_size > 0 ? server->config.max_message_size : DEFAULT_MAX_MESSAGE_SIZE;

    // --- Input Validation: Check message size ---
    if (size > max_size) {
        fprintf(stderr, "Error: Received message size (%zu) exceeds limit (%zu).\n", size, max_size);
        // We cannot generate a JSON-RPC error here as we haven't parsed the ID yet.
        // The connection will likely be closed by the transport layer after this task finishes.
        free(task_data->message_data); // Free copied data
        free(task_data);
        PROFILE_END("process_message_task"); // End profile on error
        return;
    }
    // --- End Input Validation ---


    int error_code = 0;
    // Call handle_message from mcp_server_dispatch.c (declared in internal header)
    char* response_json = handle_message(server, data, size, &error_code);

    // If handle_message produced a response, send it back via the transport
    if (response_json != NULL) {
        // Framing (length prefix) should be handled by the transport layer itself.
        // Server core just sends the JSON payload.
        size_t json_len = strlen(response_json);
        int send_status = mcp_transport_send(transport, response_json, json_len);

        if (send_status != 0) {
            // Log send error
            log_message(LOG_LEVEL_ERROR, "Failed to send response via transport (status: %d)", send_status);
            // Depending on the error, might want to close connection, but transport might handle that.
        }

        free(response_json); // Free the response from handle_message
    } else if (error_code != MCP_ERROR_NONE) {
        // Log if handle_message failed but didn't produce an error response string
        // (e.g., parse error before ID was known)
        fprintf(stderr, "Error processing message (code: %d), no response generated.\n", error_code);
    }

    // Clean up task data
    free(task_data->message_data);
    free(task_data);
    PROFILE_END("process_message_task");
}


// --- Transport Callback ---

/**
 * @internal
 * @brief Callback function passed to the transport layer.
 *
 * This function is invoked by the transport when a complete message is received.
 * It copies the message data and dispatches it to the thread pool for processing.
 *
 * @param user_data Pointer to the mcp_server_t instance.
 * @param data Pointer to the received raw message data.
 * @param size Size of the received data.
 * @param[out] error_code Pointer to store potential errors during callback processing itself (not application errors).
 * @return NULL. Responses are sent asynchronously by the worker thread.
 */
char* transport_message_callback(void* user_data, const void* data, size_t size, int* error_code) {
    mcp_server_t* server = (mcp_server_t*)user_data;
    if (server == NULL || data == NULL || size == 0 || error_code == NULL || server->thread_pool == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot process
    }
    *error_code = MCP_ERROR_NONE;

    // --- Rate Limiting Check ---
    // TODO: Need a client identifier from the transport layer (e.g., IP address)
    // const char* client_id = get_client_identifier_from_transport(transport); // Placeholder
    // if (server->rate_limiter && client_id && !mcp_rate_limiter_check(server->rate_limiter, client_id)) {
    //     fprintf(stderr, "Rate limit exceeded for client: %s\n", client_id);
    //     *error_code = MCP_ERROR_INTERNAL_ERROR; // Or a specific rate limit error code?
    //     // Don't dispatch task, potentially close connection?
    //     return NULL;
    // }
    // --- End Rate Limiting Check ---


    // Create task data - must copy message data as the original buffer might be reused/freed
    message_task_data_t* task_data = (message_task_data_t*)malloc(sizeof(message_task_data_t));
    if (!task_data) {
        fprintf(stderr, "Error: Failed to allocate task data.\n");
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        return NULL;
    }

    task_data->message_data = malloc(size);
    if (!task_data->message_data) {
        fprintf(stderr, "Error: Failed to allocate buffer for message copy.\n");
        free(task_data);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        return NULL;
    }

    memcpy(task_data->message_data, data, size);
    task_data->server = server;
    // Use the transport stored in the server struct (set during mcp_server_start)
    task_data->transport = server->transport;
    task_data->message_size = size;

    // Add task to the thread pool
    if (mcp_thread_pool_add_task(server->thread_pool, process_message_task, task_data) != 0) {
        fprintf(stderr, "Error: Failed to add message processing task to thread pool.\n");
        // Cleanup allocated data if task add failed
        free(task_data->message_data);
        free(task_data);
        *error_code = MCP_ERROR_INTERNAL_ERROR; // Indicate failure to queue
    }

    // Callback itself doesn't return the response string anymore
    return NULL;
}
