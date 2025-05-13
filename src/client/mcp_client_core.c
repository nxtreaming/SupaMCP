#include "internal/client_internal.h"
#include <mcp_log.h>
#include <mcp_json_rpc.h>
#include <mcp_string_utils.h>
#include <mcp_memory_pool.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <mcp_websocket_transport.h>
#include <mcp_cache_aligned.h>
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

// Memory pool initialization parameters
#define CLIENT_SMALL_POOL_BLOCKS 64
#define CLIENT_MEDIUM_POOL_BLOCKS 32
#define CLIENT_LARGE_POOL_BLOCKS 16

/**
 * @brief Create an MCP client instance.
 *
 * This function initializes memory pools, thread cache, and arena for efficient
 * memory management. It creates a client instance with the provided configuration
 * and transport.
 *
 * @param config Client configuration settings
 * @param transport Transport handle (owned by client after creation)
 * @return Pointer to the created client instance, or NULL on failure
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport) {
    if (config == NULL || transport == NULL)
        return NULL;

    // Initialize the memory pool system if not already initialized
    static MCP_CACHE_ALIGNED int memory_system_initialized = 0;
    if (!memory_system_initialized) {
        // Use defined constants for pool sizes
        if (!mcp_memory_pool_system_init(
                CLIENT_SMALL_POOL_BLOCKS,
                CLIENT_MEDIUM_POOL_BLOCKS,
                CLIENT_LARGE_POOL_BLOCKS)) {
            mcp_log_error("Failed to initialize memory pool system.");
            mcp_transport_destroy(transport);
            return NULL;
        }

        // Initialize the thread cache with optimized settings
        mcp_thread_cache_config_t cache_config = {
            .small_cache_size = 16,   // Optimized for client operations
            .medium_cache_size = 8,   // Fewer medium objects needed
            .large_cache_size = 4,    // Even fewer large objects
            .adaptive_sizing = true,  // Enable adaptive sizing for better performance
            .growth_threshold = 0.75, // Grow cache when hit ratio is above 75%
            .shrink_threshold = 0.25, // Shrink cache when hit ratio is below 25%
            .min_cache_size = 4,      // Minimum cache size
            .max_cache_size = 32      // Maximum cache size
        };

        if (!mcp_thread_cache_init_with_config(&cache_config)) {
            mcp_log_error("Failed to initialize thread cache.");
            mcp_memory_pool_system_cleanup();
            mcp_transport_destroy(transport);
            return NULL;
        }

        // Initialize thread-local arena with optimized size
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            mcp_thread_cache_cleanup();
            mcp_memory_pool_system_cleanup();
            mcp_transport_destroy(transport);
            return NULL;
        }

        memory_system_initialized = 1;
    }

    // Allocate client structure using thread cache for better performance
    mcp_client_t* client = (mcp_client_t*)mcp_thread_cache_alloc(sizeof(mcp_client_t));
    if (client == NULL) {
        mcp_log_error("Failed to allocate client structure");
        mcp_transport_destroy(transport);
        return NULL;
    }

    // Initialize client structure
    memset(client, 0, sizeof(mcp_client_t)); // Clear all fields

    // Store config and transport
    client->config = *config; // Copy config struct
    client->transport = transport;
    client->next_id = 1;

    // Initialize synchronization primitives using the abstraction layer
    client->pending_requests_mutex = mcp_mutex_create();
    if (client->pending_requests_mutex == NULL) {
        mcp_log_error("Failed to create pending requests mutex.");
        mcp_transport_destroy(transport);
        mcp_thread_cache_free(client, sizeof(mcp_client_t));
        return NULL;
    }

    // Initialize hash table with power-of-2 size for efficient hashing
    client->pending_requests_capacity = INITIAL_PENDING_REQUESTS_CAPACITY;
    client->pending_requests_count = 0;

    // Use thread cache for hash table allocation
    client->pending_requests_table = (pending_request_entry_t*)mcp_thread_cache_alloc(
        client->pending_requests_capacity * sizeof(pending_request_entry_t));

    if (client->pending_requests_table == NULL) {
        mcp_log_error("Failed to allocate pending requests table");
        mcp_mutex_destroy(client->pending_requests_mutex);
        mcp_transport_destroy(transport);
        mcp_thread_cache_free(client, sizeof(mcp_client_t));
        return NULL;
    }

    // Initialize status and CV pointers for all allocated entries
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        client->pending_requests_table[i].id = 0; // Mark as empty
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
        mcp_log_error("Failed to start transport");
        mcp_client_destroy(client); // Will destroy transport and mutex/CS
        return NULL;
    }

    mcp_log_info("MCP client created successfully");
    return client;
}

/**
 * @brief Destroy an MCP client and free all associated resources
 *
 * This function stops and destroys the transport, cleans up synchronization primitives,
 * and frees all memory allocated for the client.
 *
 * @param client The client instance to destroy
 */
void mcp_client_destroy(mcp_client_t* client) {
    if (client == NULL)
        return;

    mcp_log_debug("Destroying MCP client");

    // Transport is stopped and destroyed here
    if (client->transport != NULL) {
        // Ensure transport is stopped before destroying
        mcp_transport_stop(client->transport);
        mcp_transport_destroy(client->transport);
        client->transport = NULL;
    }

    // Clean up synchronization primitives
    if (client->pending_requests_mutex != NULL) {
        mcp_mutex_destroy(client->pending_requests_mutex);
        client->pending_requests_mutex = NULL;
    }

    // Free any remaining pending requests (and their condition variables) in the hash table
    if (client->pending_requests_table != NULL) {
        // Clean up condition variables for active requests
        for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
            if (client->pending_requests_table[i].id != 0 &&
                client->pending_requests_table[i].request.status != PENDING_REQUEST_INVALID) {
                // Destroy the condition variable
                if (client->pending_requests_table[i].request.cv != NULL) {
                    mcp_cond_destroy(client->pending_requests_table[i].request.cv);
                    client->pending_requests_table[i].request.cv = NULL;
                }
            }
        }

        // Free the hash table using thread cache
        mcp_thread_cache_free(client->pending_requests_table,
                             client->pending_requests_capacity * sizeof(pending_request_entry_t));
        client->pending_requests_table = NULL;
    }

    // Free the client structure using thread cache
    mcp_thread_cache_free(client, sizeof(mcp_client_t));

    mcp_log_debug("MCP client destroyed successfully");

    // Note: We don't clean up the thread cache and memory pool system here
    // because they might be used by other clients or components
}

/**
 * @brief Callback invoked by the transport layer when a fatal error occurs (e.g., disconnection).
 *
 * This optimized function efficiently iterates through all waiting requests, marks them as errored,
 * and signals their condition variables to wake up the waiting threads.
 *
 * @param user_data Client instance passed as user data
 * @param transport_error_code Error code from the transport layer
 */
void mcp_client_transport_error_callback(void* user_data, int transport_error_code) {
    mcp_client_t* client = (mcp_client_t*)user_data;
    if (client == NULL)
        return;

    mcp_log_info("Transport error detected (code: %d). Notifying waiting requests.", transport_error_code);

    // Lock the mutex to safely access the pending requests table
    mcp_mutex_lock(client->pending_requests_mutex);

    // Count of notified requests for logging
    size_t notified_count = 0;

    // Pre-allocate error message for all waiting requests
    const char* error_msg = "Transport connection error";

    // Iterate through the hash table
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        pending_request_entry_t* entry = &client->pending_requests_table[i];

        // Fast path: Skip empty or non-waiting entries
        if (entry->id == 0 || entry->request.status != PENDING_REQUEST_WAITING) {
            continue;
        }

        // Set error details for the waiting request
        *(entry->request.error_code_ptr) = MCP_ERROR_TRANSPORT_ERROR;

        // Avoid overwriting existing error message if one was somehow set
        if (*(entry->request.error_message_ptr) == NULL) {
            // Allocate error message using our helper
            *(entry->request.error_message_ptr) = mcp_strdup(error_msg);
        }

        // Update status to ERROR
        entry->request.status = PENDING_REQUEST_ERROR;

        // Signal the condition variable to wake up the waiting thread
        if (entry->request.cv) {
            mcp_cond_signal(entry->request.cv);
            notified_count++;
        }
    }

    // Unlock the mutex
    mcp_mutex_unlock(client->pending_requests_mutex);

    // Log the number of requests that were notified
    if (notified_count > 0) {
        mcp_log_info("Notified %zu waiting requests about transport error", notified_count);
    } else {
        mcp_log_debug("No waiting requests to notify about transport error");
    }
}

/**
 * @brief Callback invoked by the transport layer when a message is received.
 *
 * This optimized function efficiently parses the received JSON message, finds the
 * corresponding pending request, updates its status and result, and signals the waiting thread.
 *
 * @param user_data Client instance passed as user data
 * @param data Received message data
 * @param size Size of the received data
 * @param error_code Pointer to store error code
 * @return NULL (client callback never sends a response back)
 */
char* mcp_client_receive_callback(void* user_data, const void* data, size_t size, int* error_code) {
    // Fast validation of input parameters
    if (user_data == NULL || data == NULL || size == 0 || error_code == NULL) {
        if (error_code) {
            *error_code = MCP_ERROR_INVALID_PARAMS;
        }
        return NULL;
    }

    // Initialize error code to success
    *error_code = 0;

    mcp_client_t* client = (mcp_client_t*)user_data;

    // We expect data to be a null-terminated JSON string from the transport receive thread
    const char* response_json = (const char*)data;

    // Variables for parsed response
    uint64_t id;
    mcp_error_code_t resp_error_code = MCP_ERROR_NONE;
    char* resp_error_message = NULL;
    char* resp_result = NULL;

    // Parse the response with optimized logging
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        // Only format and log the JSON if debug logging is enabled
        mcp_log_debug("Parsing response JSON: %s", response_json);
    }

    // Parse the response
    int parse_result = mcp_json_parse_response(response_json, &id, &resp_error_code,
                                              &resp_error_message, &resp_result);

    // Handle parse errors
    if (parse_result != 0) {
        mcp_log_error("Failed to parse response JSON (error: %d)", parse_result);
        *error_code = MCP_ERROR_PARSE_ERROR;
        return NULL;
    }

    // Log parsed response details at debug level
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        mcp_log_debug("Parsed response: ID=%llu, error_code=%d, result=%s",
                     (unsigned long long)id, resp_error_code,
                     resp_result ? (strlen(resp_result) > 100 ? "[large result]" : resp_result) : "NULL");
    }

    // Fast path for ID 0 (Initial Ping/Pong)
    if (id == 0) {
        // This is likely the response to the initial ping sent by the receive thread
        mcp_log_debug("Received response for initial ping (ID: 0), ignoring");
        free(resp_error_message);
        free(resp_result);
        return NULL;
    }

    // Find the pending request and signal it
    mcp_mutex_lock(client->pending_requests_mutex);

    // Find the pending request entry in the hash table
    pending_request_entry_t* req_entry = mcp_client_find_pending_request_entry(client, id, false);

    // Process the request if found and valid
    if (req_entry != NULL && req_entry->request.status != PENDING_REQUEST_INVALID) {
        // Fast path for waiting requests
        if (req_entry->request.status == PENDING_REQUEST_WAITING) {
            // Store results via pointers
            *(req_entry->request.error_code_ptr) = resp_error_code;
            *(req_entry->request.error_message_ptr) = resp_error_message; // Transfer ownership
            *(req_entry->request.result_ptr) = resp_result;               // Transfer ownership

            // Update status based on error code
            req_entry->request.status = (resp_error_code == MCP_ERROR_NONE) ?
                                       PENDING_REQUEST_COMPLETED : PENDING_REQUEST_ERROR;

            // Signal the waiting thread if condition variable exists
            if (req_entry->request.cv) {
                mcp_cond_signal(req_entry->request.cv);
                mcp_log_debug("Signaled waiting thread for request ID %llu", (unsigned long long)id);
            } else {
                mcp_log_error("No condition variable for request ID %llu", (unsigned long long)id);
            }
        } else {
            // Request already completed or timed out, discard response
            mcp_log_warn("Received response for already completed request %llu (status: %d)",
                        (unsigned long long)id, req_entry->request.status);
            free(resp_error_message);
            free(resp_result);
        }
    } else {
        // Response received for an unknown/unexpected ID
        mcp_log_warn("Received response with unexpected ID: %llu", (unsigned long long)id);
        free(resp_error_message);
        free(resp_result);
        *error_code = MCP_ERROR_INVALID_REQUEST;
    }

    mcp_mutex_unlock(client->pending_requests_mutex);

    return NULL; // Client callback never sends a response back
}

/**
 * @brief Check if the client is connected to the server.
 *
 * This optimized function efficiently checks the underlying transport connection status
 * based on the transport protocol type.
 *
 * @param client The client instance.
 * @return 1 if connected, 0 if not connected, -1 on error.
 */
int mcp_client_is_connected(mcp_client_t* client) {
    // Fast validation of client and transport
    if (client == NULL || client->transport == NULL) {
        return -1;
    }

    // Get the transport protocol once
    mcp_transport_protocol_t protocol = mcp_transport_get_protocol(client->transport);

    // Handle different transport protocols
    switch (protocol) {
        case MCP_TRANSPORT_PROTOCOL_WEBSOCKET:
            // WebSocket has a specific connection check function
            return mcp_transport_websocket_client_is_connected(client->transport);

        case MCP_TRANSPORT_PROTOCOL_HTTP:
            // HTTP is stateless, so we assume it's connected if the transport exists
            // In the future, we could implement a ping/pong mechanism
            return 1;

        case MCP_TRANSPORT_PROTOCOL_TCP:
            // For TCP, we could check if the socket is valid and connected
            // For now, assume connected if transport exists
            return 1;

        case MCP_TRANSPORT_PROTOCOL_STDIO:
            // STDIO is always "connected" if the transport exists
            return 1;

        default:
            // Unknown protocol, assume connected if transport exists
            mcp_log_debug("Unknown transport protocol: %d", protocol);
            return 1;
    }
}

/**
 * @brief Sends a pre-formatted request and receives the raw response.
 *
 * This optimized function formats a JSON-RPC request with the provided method, parameters,
 * and ID, sends it to the server, and waits for a response.
 *
 * @param client The client instance
 * @param method The method name
 * @param params_json The JSON parameters string
 * @param id The request ID
 * @param response_json_out Pointer to store the response JSON
 * @param error_code_out Pointer to store the error code
 * @param error_message_out Pointer to store the error message
 * @return 0 on success, -1 on failure
 */
int mcp_client_send_raw_request(
    mcp_client_t* client,
    const char* method,
    const char* params_json,
    uint64_t id,
    char** response_json_out,
    mcp_error_code_t* error_code_out,
    char** error_message_out
) {
    // Validate input parameters
    if (client == NULL || method == NULL || params_json == NULL ||
        response_json_out == NULL || error_code_out == NULL ||
        error_message_out == NULL) {
        mcp_log_error("Invalid parameters for send_raw_request");
        return -1;
    }

    // Initialize output parameters
    *response_json_out = NULL;
    *error_code_out = MCP_ERROR_NONE;
    *error_message_out = NULL;

    // Create the full request JSON string using the provided components
    // Use the more efficient direct formatter when available
    char* request_json = mcp_json_format_request_direct(id, method, params_json);

    // Fall back to standard formatter if direct formatter fails
    if (request_json == NULL) {
        mcp_log_debug("Direct JSON formatter failed, using standard formatter");
        request_json = mcp_json_format_request(id, method, params_json);

        if (request_json == NULL) {
            mcp_log_error("Failed to format request JSON for method '%s'", method);
            return -1;
        }
    }

    // Use the internal send_and_wait function
    int status = mcp_client_send_and_wait(
        client,
        request_json,
        id,
        response_json_out,
        error_code_out,
        error_message_out
    );

    // Free the formatted request JSON string
    free(request_json);

    // Ensure response_json_out is NULL on failure
    if (status != 0 && *response_json_out != NULL) {
        free(*response_json_out);
        *response_json_out = NULL;
    }

    return status;
}
