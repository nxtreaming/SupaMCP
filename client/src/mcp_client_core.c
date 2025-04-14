#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_client_internal.h"
#include <mcp_log.h>
#include <mcp_json_rpc.h>
#include <mcp_string_utils.h>
#include <mcp_memory_pool.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <stdlib.h>
#include <string.h>

// Platform specific includes for socket types
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#endif

/**
 * @brief Create an MCP client instance.
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport) {
    if (config == NULL || transport == NULL) {
        return NULL; // Config and transport are required
    }

    // Initialize the memory pool system if not already initialized
    static int memory_system_initialized = 0;
    if (!memory_system_initialized) {
        if (!mcp_memory_pool_system_init(64, 32, 16)) {
            mcp_log_error("Failed to initialize memory pool system.");
            mcp_transport_destroy(transport);
            return NULL;
        }

        // Initialize the thread cache
        if (!mcp_thread_cache_init()) {
            mcp_log_error("Failed to initialize thread cache.");
            mcp_transport_destroy(transport);
            return NULL;
        }

        // Initialize thread-local arena
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            mcp_transport_destroy(transport);
            return NULL;
        }

        memory_system_initialized = 1;
    }

    // Allocate client structure using malloc to avoid potential circular dependencies
    mcp_client_t* client = (mcp_client_t*)malloc(sizeof(mcp_client_t));
    if (client == NULL) {
        mcp_transport_destroy(transport);
        return NULL;
    }

    // Store config and transport
    client->config = *config; // Copy config struct
    client->transport = transport;
    client->next_id = 1;

    // Initialize synchronization primitives using the abstraction layer
    client->pending_requests_mutex = mcp_mutex_create();
    if (client->pending_requests_mutex == NULL) {
        mcp_log_error("Failed to create pending requests mutex.");
        mcp_transport_destroy(transport);
        free(client);
        return NULL;
    }

    // Initialize hash table
    client->pending_requests_capacity = INITIAL_PENDING_REQUESTS_CAPACITY;
    client->pending_requests_count = 0;
    client->pending_requests_table = (pending_request_entry_t*)calloc(client->pending_requests_capacity, sizeof(pending_request_entry_t));
    if (client->pending_requests_table == NULL) {
        mcp_mutex_destroy(client->pending_requests_mutex); // Use abstracted destroy
        mcp_transport_destroy(transport);
        free(client);
        return NULL;
    }
    // Initialize status and CV pointers for all allocated entries
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        client->pending_requests_table[i].request.status = PENDING_REQUEST_INVALID;
        client->pending_requests_table[i].request.cv = NULL; // Initialize CV pointer
    }

    // Start the transport's receive mechanism with our internal callbacks
    if (mcp_transport_start(
            client->transport,
            mcp_client_receive_callback,
            client,
            mcp_client_transport_error_callback
        ) != 0)
    {
        mcp_client_destroy(client); // Will destroy transport and mutex/CS
        return NULL;
    }

    return client;
}

/**
 * Destroy an MCP client
 */
void mcp_client_destroy(mcp_client_t* client) {
    if (client == NULL) {
        return;
    }

    // Transport is stopped and destroyed here
    if (client->transport != NULL) {
        mcp_transport_stop(client->transport); // Ensure it's stopped
        mcp_transport_destroy(client->transport);
        client->transport = NULL;
    }

    // Clean up synchronization primitives and pending requests map
    mcp_mutex_destroy(client->pending_requests_mutex);
    client->pending_requests_mutex = NULL; // Avoid double free

    // Free any remaining pending requests (and their condition variables) in the hash table
    if (client->pending_requests_table != NULL) {
        for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
            if (client->pending_requests_table[i].id != 0 && client->pending_requests_table[i].request.status != PENDING_REQUEST_INVALID) {
                // Destroy the condition variable using the abstraction
                mcp_cond_destroy(client->pending_requests_table[i].request.cv);
            }
        }
        free(client->pending_requests_table);
    }

    // Free the client structure
    free(client);

    // Note: We don't clean up the thread cache and memory pool system here
    // because they might be used by other clients
}

/**
 * @brief Callback invoked by the transport layer when a fatal error occurs (e.g., disconnection).
 *
 * This function iterates through all waiting requests, marks them as errored,
 * and signals their condition variables to wake up the waiting threads.
 */
void mcp_client_transport_error_callback(void* user_data, int transport_error_code) {
    mcp_client_t* client = (mcp_client_t*)user_data;
    if (client == NULL) return;

    mcp_log_info("Transport error detected (code: %d). Notifying waiting requests.", transport_error_code);

    // Lock the mutex to safely access the pending requests table
    mcp_mutex_lock(client->pending_requests_mutex);

    // Iterate through the hash table
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        pending_request_entry_t* entry = &client->pending_requests_table[i];
        // Check if the slot is active and the request is currently waiting
        if (entry->id != 0 && entry->request.status == PENDING_REQUEST_WAITING) {
            // Set error details for the waiting request
            *(entry->request.error_code_ptr) = MCP_ERROR_TRANSPORT_ERROR; // Use a generic transport error
            // Avoid overwriting existing error message if one was somehow set
            if (*(entry->request.error_message_ptr) == NULL) {
                // Allocate error message using our helper
                *(entry->request.error_message_ptr) = mcp_strdup("Transport connection error");
                // If mcp_strdup fails, the message pointer remains NULL.
            }

            // Update status to ERROR
            entry->request.status = PENDING_REQUEST_ERROR;

            // Signal the condition variable to wake up the waiting thread
            if (entry->request.cv) {
                mcp_cond_signal(entry->request.cv);
            }
            // Note: The waiting thread is responsible for removing the entry from the table
        }
    }

    // Unlock the mutex
    mcp_mutex_unlock(client->pending_requests_mutex);
}

/**
 * @brief Callback invoked by the transport layer when a message is received.
 *
 * This function parses the received JSON message, finds the corresponding pending request,
 * updates its status and result, and signals the waiting thread.
 */
char* mcp_client_receive_callback(void* user_data, const void* data, size_t size, int* error_code) {
    mcp_client_t* client = (mcp_client_t*)user_data;
    if (client == NULL || data == NULL || size == 0 || error_code == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // No response generated by client callback
    }
    *error_code = 0; // Default success for callback processing itself

    // We expect data to be a null-terminated JSON string from the transport receive thread
    const char* response_json = (const char*)data;
    uint64_t id;
    mcp_error_code_t resp_error_code = MCP_ERROR_NONE;
    char* resp_error_message = NULL;
    char* resp_result = NULL;

    // Parse the response
    mcp_log_debug("Parsing response JSON: %s", response_json);
    int parse_result = mcp_json_parse_response(response_json, &id, &resp_error_code, &resp_error_message, &resp_result);

    if (parse_result != 0) {
        mcp_log_error("Client failed to parse response JSON (error: %d): %s", parse_result, response_json);
        *error_code = MCP_ERROR_PARSE_ERROR;
        // Cannot signal specific request on parse error, maybe log?
        return NULL;
    }

    mcp_log_debug("Parsed response: ID=%llu, error_code=%d, result=%s",
                 (unsigned long long)id, resp_error_code, resp_result ? resp_result : "NULL");

    // --- Special Handling for ID 0 (Initial Ping/Pong) ---
    if (id == 0) {
        // This is likely the response to the initial ping sent by the receive thread.
        // Ignore it, as it's not tied to a user request.
        mcp_log_debug("Received response for initial ping (ID: 0), ignoring.");
        free(resp_error_message); // Free parsed fields even if ignored
        free(resp_result);
        return NULL; // Don't process further
    }
    // --- End Special Handling ---

    // Find the pending request and signal it
    mcp_mutex_lock(client->pending_requests_mutex);

    // Find the pending request entry in the hash table
    pending_request_entry_t* req_entry_wrapper = mcp_client_find_pending_request_entry(client, id, false);

    if (req_entry_wrapper != NULL && req_entry_wrapper->request.status != PENDING_REQUEST_INVALID) {
        // Found the pending request
        mcp_log_debug("Found pending request for ID %llu, status: %d",
                     (unsigned long long)id, req_entry_wrapper->request.status);

        if (req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
            mcp_log_debug("Updating pending request ID %llu with response", (unsigned long long)id);

            // Store results via pointers
            *(req_entry_wrapper->request.error_code_ptr) = resp_error_code;
            *(req_entry_wrapper->request.error_message_ptr) = resp_error_message; // Transfer ownership
            *(req_entry_wrapper->request.result_ptr) = resp_result;             // Transfer ownership

            // Update status
            req_entry_wrapper->request.status = (resp_error_code == MCP_ERROR_NONE) ? PENDING_REQUEST_COMPLETED : PENDING_REQUEST_ERROR;
            mcp_log_debug("Updated request ID %llu status to %d",
                         (unsigned long long)id, req_entry_wrapper->request.status);

            // Signal the waiting thread
            if (req_entry_wrapper->request.cv) {
                mcp_log_debug("Signaling condition variable for request ID %llu", (unsigned long long)id);
                mcp_cond_signal(req_entry_wrapper->request.cv);
            } else {
                mcp_log_error("No condition variable for request ID %llu", (unsigned long long)id);
            }
            // Note: We don't remove the entry here. The waiting thread will remove it after waking up.
        } else {
            // Request already timed out or errored, discard response
            mcp_log_error("Received response for already completed/timed out request %llu\n", (unsigned long long)id);
            free(resp_error_message);
            free(resp_result);
        }
    } else {
        // Response received for an unknown/unexpected ID (and ID is not 0)
        mcp_log_warn("Received response with unexpected ID: %llu", (unsigned long long)id);
        free(resp_error_message);
        free(resp_result);
        *error_code = MCP_ERROR_INVALID_REQUEST; // Set error for the callback itself
    }

    mcp_mutex_unlock(client->pending_requests_mutex);

    return NULL; // Client callback never sends a response back
}

/**
 * Sends a pre-formatted request and receives the raw response.
 */
int mcp_client_send_raw_request(
    mcp_client_t* client,
    const char* method, // Still needed for logging/context? Or remove? Keep for now.
    const char* params_json, // The raw JSON params string
    uint64_t id, // Use the provided ID
    char** response_json_out, // Changed name for clarity
    mcp_error_code_t* error_code_out, // Changed name
    char** error_message_out // Changed name
) {
    if (client == NULL || method == NULL || params_json == NULL || response_json_out == NULL || error_code_out == NULL || error_message_out == NULL) {
        return -1;
    }

    // Create the full request JSON string using the provided components
    char* request_json = NULL;

    // NOTE: we can use mcp_json_format_request_direct here if we want to simplify
    //request_json = mcp_json_format_request_direct(id, method, params_json);
    request_json = mcp_json_format_request(id, method, params_json);

    if (request_json == NULL) {
        mcp_log_error("Failed to format raw request JSON for method '%s'", method);
        return -1;
    }

    // Use the internal send_and_wait function
    int status = mcp_client_send_and_wait(client, request_json, id, response_json_out, error_code_out, error_message_out);

    // Free the formatted request JSON string
    free(request_json);

    // send_and_wait already populates the output parameters correctly based on success/error/timeout.
    // Ensure response_json_out is NULL on failure.
    if (status != 0) {
        *response_json_out = NULL;
    }

    return status;
}
