#include "internal/server_internal.h"
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
    
    // Verify if message has null terminator
    bool has_terminator = (size > 0 && ((const char*)data)[size-1] == '\0');
    if (!has_terminator) {
        log_message(LOG_LEVEL_WARN, "Task data missing terminator, this may cause JSON parsing errors");
    }
    
    // Call handle_message from mcp_server_dispatch.c (declared in internal header)
    char* response_json = handle_message(server, data, size, &error_code);

    // Log and free the response
    if (response_json != NULL) {
        // Free the response - actual response should be returned by transport_message_callback
        free(response_json);
    } else if (error_code != MCP_ERROR_NONE) {
        // Log processing error
        log_message(LOG_LEVEL_ERROR, "Error processing message (code: %d), no response generated.", error_code);
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

    // --- Rate Limiting Check (optional) ---
    // TODO: Implement rate limiting if needed
    
    // --- Input validation ---
    size_t max_size = server->config.max_message_size > 0 ? 
                      server->config.max_message_size : DEFAULT_MAX_MESSAGE_SIZE;
    
    if (size > max_size) {
        log_message(LOG_LEVEL_ERROR, "Received message size (%zu) exceeds limit (%zu)", size, max_size);
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
            log_message(LOG_LEVEL_DEBUG, "Added NULL terminator to message data");
        }
    }
    
    if (!message_copy) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for message copy");
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("transport_message_callback");
        return NULL;
    }
    
    // --- Direct message processing ---
    int handler_error_code = MCP_ERROR_NONE;
    char* response_json = handle_message(server, message_copy, size, &handler_error_code);
    
    // Free message copy
    free(message_copy);
    
    // --- Handle response ---
    if (response_json != NULL) {
        // Return response string to be sent via client socket
        PROFILE_END("transport_message_callback");
        return response_json; // Caller is responsible for freeing
    } else if (handler_error_code != MCP_ERROR_NONE) {
        // Message processing failed but no error response was generated
        log_message(LOG_LEVEL_ERROR, "Failed to process message (error code: %d), no response generated", handler_error_code);
    }
    
    PROFILE_END("transport_message_callback");
    return NULL; // No response
}
