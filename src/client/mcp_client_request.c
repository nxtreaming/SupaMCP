#include "internal/client_internal.h"
#include <mcp_log.h>
#include <mcp_json_rpc.h>
#include <mcp_string_utils.h>
#include "mcp_socket_utils.h"
#include <mcp_memory_pool.h>
#include <mcp_thread_cache.h>
#include <mcp_arena.h>
#include <mcp_cache_aligned.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Performance optimization constants
#define REQUEST_BUFFER_INITIAL_SIZE 1024
#define MAX_RESPONSE_SIZE (10 * 1024 * 1024) // 10MB max response size

/**
 * @brief Internal function to send a request and wait for a response.
 *
 * This optimized function efficiently handles the core logic of sending a formatted request,
 * managing the pending request state, waiting for the response via condition variables,
 * and handling timeouts or errors.
 *
 * @param client The MCP client instance
 * @param request_json The JSON-RPC request string
 * @param request_id The request ID
 * @param result Pointer to store the result string
 * @param error_code Pointer to store the error code
 * @param error_message Pointer to store the error message
 * @return 0 on success, -1 on failure, -2 on timeout
 */
int mcp_client_send_and_wait(
    mcp_client_t* client,
    const char* request_json,
    uint64_t request_id,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || request_json == NULL || result == NULL ||
        error_code == NULL || error_message == NULL) {
        mcp_log_error("Invalid parameters for send_and_wait");
        return -1;
    }

    if (client->transport == NULL) {
        mcp_log_error("Client transport is NULL");
        return -1;
    }

    // Initialize output parameters
    *result = NULL;
    *error_message = NULL;
    *error_code = MCP_ERROR_NONE;

    // Calculate JSON length - excluding null terminator, as required by server
    size_t json_len = strlen(request_json);

    // Check for reasonable request size
    if (json_len == 0 || json_len > MAX_RESPONSE_SIZE) {
        mcp_log_error("Invalid request JSON size: %zu bytes", json_len);
        return -1;
    }

    // Convert length to network byte order
    uint32_t net_len = htonl((uint32_t)json_len);

    // Prepare buffers for vectored send using cache-aligned data when possible
    MCP_CACHE_ALIGNED mcp_buffer_t send_buffers[2];
    send_buffers[0].data = &net_len;
    send_buffers[0].size = sizeof(net_len);
    send_buffers[1].data = request_json;
    send_buffers[1].size = json_len;

    // Get transport protocol once for efficiency
    mcp_transport_protocol_t transport_protocol = mcp_transport_get_protocol(client->transport);
    bool is_http = (transport_protocol == MCP_TRANSPORT_PROTOCOL_HTTP);
    bool is_websocket = (transport_protocol == MCP_TRANSPORT_PROTOCOL_WEBSOCKET);
    bool is_synchronous = (is_http || is_websocket);

    // For transports that need asynchronous request handling
    pending_request_t pending_req;
    pending_request_entry_t* req_entry_wrapper = NULL;

    // Only set up asynchronous handling for non-synchronous transports
    if (!is_synchronous) {
        // --- Asynchronous Receive Logic ---
        // Initialize pending request structure
        memset(&pending_req, 0, sizeof(pending_req));
        pending_req.id = request_id;
        pending_req.status = PENDING_REQUEST_WAITING;
        pending_req.result_ptr = result;
        pending_req.error_code_ptr = error_code;
        pending_req.error_message_ptr = error_message;

        // Create condition variable for synchronization
        pending_req.cv = mcp_cond_create();
        if (pending_req.cv == NULL) {
            mcp_log_error("Failed to create condition variable for request %llu",
                         (unsigned long long)request_id);
            return -1;
        }

        // Add to pending requests map (protected by mutex)
        mcp_mutex_lock(client->pending_requests_mutex);

        // Add the request to the hash table
        int add_status = mcp_client_add_pending_request_entry(client, pending_req.id, &pending_req);
        if (add_status != 0) {
            mcp_mutex_unlock(client->pending_requests_mutex);
            mcp_cond_destroy(pending_req.cv);
            mcp_log_error("Failed to add request %llu to hash table",
                         (unsigned long long)request_id);
            return -1;
        }

        mcp_mutex_unlock(client->pending_requests_mutex);
        mcp_log_debug("Added request %llu to pending requests table",
                     (unsigned long long)request_id);
    }

    // Send the request using vectored I/O
    mcp_log_debug("Sending request %llu (%zu bytes)",
                 (unsigned long long)request_id, json_len);
    int send_status = mcp_transport_sendv(client->transport, send_buffers, 2);

    // Handle send errors
    if (send_status != 0) {
        mcp_log_error("Failed to send request %llu (status: %d)",
                     (unsigned long long)request_id, send_status);

        // Clean up the pending request if it was created
        if (!is_synchronous) {
            mcp_mutex_lock(client->pending_requests_mutex);
            mcp_client_remove_pending_request_entry(client, request_id);
            mcp_mutex_unlock(client->pending_requests_mutex);
        }

        return -1;
    }

    mcp_log_debug("Request %llu sent successfully", (unsigned long long)request_id);

    // For HTTP or WebSocket transport, handle the response synchronously
    if (is_synchronous) {
        // Create a buffer to receive the response
        char* response_data = NULL;
        size_t response_size = 0;
        const char* transport_name = is_http ? "HTTP" : "WebSocket";

        // Try to receive the response - this is a synchronous operation for HTTP/WebSocket
        mcp_log_debug("%s transport: Waiting for response to request %llu (timeout: %d ms)",
                     transport_name, (unsigned long long)request_id,
                     client->config.request_timeout_ms);

        int status = mcp_transport_receive(
            client->transport,
            &response_data,
            &response_size,
            client->config.request_timeout_ms
        );

        // Process the response if received successfully
        if (status == 0 && response_data != NULL && response_size > 0) {
            // Log response data at debug level only (avoid expensive string formatting)
            if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
                // Truncate long responses in logs
                if (response_size > 1024) {
                    mcp_log_debug("%s transport: Received large response (%zu bytes) for request %llu",
                                 transport_name, response_size, (unsigned long long)request_id);
                } else {
                    mcp_log_debug("%s transport: Received response: %s",
                                 transport_name, response_data);
                }
            }

            // Extract the result from the response
            uint64_t response_id;
            mcp_error_code_t response_error_code;
            char* response_error_message = NULL;
            char* response_result = NULL;

            // Parse the JSON response
            int parse_result = mcp_json_parse_response(
                response_data,
                &response_id,
                &response_error_code,
                &response_error_message,
                &response_result
            );

            if (parse_result == 0) {
                // Check if the response ID matches the request ID
                if (response_id == request_id) {
                    // Set the output parameters
                    *error_code = response_error_code;
                    *error_message = response_error_message; // Transfer ownership
                    *result = response_result;               // Transfer ownership

                    mcp_log_debug("%s transport: Successfully processed response for request %llu",
                                 transport_name, (unsigned long long)request_id);
                } else {
                    // Response ID doesn't match request ID - this is an error
                    mcp_log_error("%s transport: Response ID %llu doesn't match request ID %llu",
                                 transport_name, (unsigned long long)response_id,
                                 (unsigned long long)request_id);

                    *error_code = MCP_ERROR_INTERNAL_ERROR;
                    *error_message = mcp_strdup("Response ID doesn't match request ID");

                    // Free parsed data since we're not using it
                    free(response_error_message);
                    free(response_result);
                }
            } else {
                // Failed to parse response JSON
                mcp_log_error("%s transport: Failed to parse response JSON (error: %d)",
                             transport_name, parse_result);

                *error_code = MCP_ERROR_PARSE_ERROR;
                *error_message = mcp_strdup("Failed to parse response JSON");
            }

            // Free the raw response data
            free(response_data);
        } else {
            // No response received or error occurred
            mcp_log_error("%s transport: Failed to receive response for request %llu (status: %d)",
                         transport_name, (unsigned long long)request_id, status);

            *error_code = MCP_ERROR_TRANSPORT_ERROR;

            // Create appropriate error message based on transport type
            char error_buffer[128];
            snprintf(error_buffer, sizeof(error_buffer),
                    "Failed to receive %s response (status: %d)",
                    transport_name, status);
            *error_message = mcp_strdup(error_buffer);
        }

        // Return success (0) only if no error occurred
        return (*error_code == MCP_ERROR_NONE) ? 0 : -1;
    }

    // For transports that use asynchronous response handling (not HTTP or WebSocket)
    mcp_log_debug("Waiting for asynchronous response to request %llu",
                 (unsigned long long)request_id);

    int wait_result = 0;
    int final_status = -1;

    // Lock the mutex to safely access the pending requests table
    mcp_mutex_lock(client->pending_requests_mutex);

    // Find the pending request entry in the hash table
    req_entry_wrapper = mcp_client_find_pending_request_entry(client, request_id, false);

    // Process the request if found and still waiting
    if (req_entry_wrapper && req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
        // Wait for the response with appropriate timeout handling
        if (client->config.request_timeout_ms > 0) {
            // Use timed wait with the configured timeout
            mcp_log_debug("Waiting for response to request %llu with timeout %d ms",
                         (unsigned long long)request_id, client->config.request_timeout_ms);

            wait_result = mcp_cond_timedwait(
                req_entry_wrapper->request.cv,
                client->pending_requests_mutex,
                client->config.request_timeout_ms
            );
        } else {
            // Wait indefinitely
            mcp_log_debug("Waiting indefinitely for response to request %llu",
                         (unsigned long long)request_id);

            wait_result = mcp_cond_wait(
                req_entry_wrapper->request.cv,
                client->pending_requests_mutex
            );
        }

        // Handle timeout and other wait errors with platform-specific code
#ifdef ETIMEDOUT
        // POSIX systems
        if (wait_result == ETIMEDOUT) {
            mcp_log_error("Request %llu timed out after %d ms",
                         (unsigned long long)request_id, client->config.request_timeout_ms);
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
            mcp_log_error("Wait failed for request %llu with code: %d (%s)",
                         (unsigned long long)request_id, wait_result, strerror(wait_result));
        }
#else
        // Windows and other systems
        if (wait_result == 1) { // Windows timeout code
            mcp_log_error("Request %llu timed out after %d ms",
                         (unsigned long long)request_id, client->config.request_timeout_ms);
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
            mcp_log_error("Wait failed for request %llu with code: %d",
                         (unsigned long long)request_id, wait_result);
        }
#endif
    } else if (req_entry_wrapper) {
        // Request found but not in waiting state
        mcp_log_debug("Request %llu found but not in waiting state (status: %d)",
                     (unsigned long long)request_id, req_entry_wrapper->request.status);
    } else {
        // Request not found
        mcp_log_error("Request %llu not found in pending requests table",
                     (unsigned long long)request_id);
    }

    // Determine the final outcome based on request status
    if (req_entry_wrapper) {
        // Log the request status
        mcp_log_debug("Request %llu final status: %d",
                     (unsigned long long)request_id, req_entry_wrapper->request.status);

        // Set final status based on request status
        switch (req_entry_wrapper->request.status) {
            case PENDING_REQUEST_COMPLETED:
                mcp_log_debug("Request %llu completed successfully",
                             (unsigned long long)request_id);
                final_status = 0;
                break;

            case PENDING_REQUEST_TIMEOUT:
                mcp_log_error("Request %llu timed out", (unsigned long long)request_id);
                final_status = -2;
                break;

            case PENDING_REQUEST_ERROR:
                mcp_log_error("Request %llu failed with error",
                             (unsigned long long)request_id);
                final_status = -1;
                break;

            default:
                mcp_log_error("Request %llu has unexpected status: %d",
                             (unsigned long long)request_id,
                             req_entry_wrapper->request.status);
                final_status = -1;
                break;
        }

        // Remove entry from hash table
        mcp_client_remove_pending_request_entry(client, request_id);
    } else {
        // Entry not found - check if callback set any output parameters
        mcp_log_error("Failed to find pending request entry for ID %llu",
                     (unsigned long long)request_id);

        // Determine status based on output parameters
        if (*error_code != MCP_ERROR_NONE) {
            // Error code set - request failed
            final_status = -1;
        } else if (*result != NULL) {
            // Result set - request succeeded
            final_status = 0;
        } else {
            // No result or error - unknown state
            mcp_log_error("Request %llu not found and no result/error set",
                         (unsigned long long)request_id);
            final_status = -1;
        }
    }

    // Unlock the mutex
    mcp_mutex_unlock(client->pending_requests_mutex);

    // Set appropriate error information based on the final status
    if (final_status == -2) {
        // Timeout case
        mcp_log_error("Request %llu timed out", (unsigned long long)request_id);
        *error_code = MCP_ERROR_TRANSPORT_ERROR;
        *error_message = mcp_strdup("Request timed out");
        return -1;
    } else if (final_status != 0) {
        // Other error cases
        mcp_log_error("Error processing response for request %llu",
                     (unsigned long long)request_id);

        // Set appropriate error message if not already set
        if (*error_code != MCP_ERROR_NONE && *error_message == NULL) {
            *error_message = mcp_strdup("Unknown internal error occurred");
        } else if (*error_code == MCP_ERROR_NONE) {
            *error_code = MCP_ERROR_INTERNAL_ERROR;
            *error_message = mcp_strdup("Internal error processing response");
        }
        return -1;
    }

    // Success case
    mcp_log_debug("Request %llu completed successfully", (unsigned long long)request_id);
    return 0;
}

/**
 * @brief Send a request to the MCP server and receive a response
 *
 * This optimized function efficiently formats a JSON-RPC request with the given method
 * and parameters, sends it to the server, and waits for a response.
 *
 * @param client The MCP client instance
 * @param method The JSON-RPC method to call
 * @param params The JSON-RPC parameters (can be NULL)
 * @param result Pointer to store the result string
 * @param error_code Pointer to store the error code
 * @param error_message Pointer to store the error message
 * @return 0 on success, -1 on failure
 */
int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params, // Assumed to be JSON string or NULL
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || method == NULL || result == NULL ||
        error_code == NULL || error_message == NULL) {
        mcp_log_error("Invalid parameters for send_request");
        return -1;
    }

    // Initialize output parameters
    *result = NULL;
    *error_message = NULL;
    *error_code = MCP_ERROR_NONE;

    // Generate next request ID with atomic operation
    mcp_mutex_lock(client->pending_requests_mutex);
    uint64_t current_id = client->next_id++;
    mcp_mutex_unlock(client->pending_requests_mutex);

    // Use empty object as default params if none provided
    const char* params_to_use = (params != NULL) ? params : "{}";

    // Format the request JSON using the more efficient direct formatter when available
    char* request_json = mcp_json_format_request_direct(current_id, method, params_to_use);

    // Fall back to standard formatter if direct formatter fails
    if (request_json == NULL) {
        request_json = mcp_json_format_request(current_id, method, params_to_use);
        if (request_json == NULL) {
            mcp_log_error("Failed to format request JSON for method '%s'", method);
            return -1;
        }
    }

    mcp_log_debug("Sending request for method '%s' with ID %llu",
                 method, (unsigned long long)current_id);

    // Use the unified send_and_wait function for all transport types
    int status = mcp_client_send_and_wait(
        client,
        request_json,
        current_id,
        result,
        error_code,
        error_message
    );

    // Free the formatted request JSON string
    free(request_json);

    // Log the result of the request
    if (status == 0) {
        mcp_log_debug("Request for method '%s' (ID: %llu) completed successfully",
                     method, (unsigned long long)current_id);
    } else {
        mcp_log_error("Request for method '%s' (ID: %llu) failed with status %d",
                     method, (unsigned long long)current_id, status);
    }

    return status;
}
