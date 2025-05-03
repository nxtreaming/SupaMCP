#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_client_internal.h"
#include <mcp_log.h>
#include <mcp_json_rpc.h>
#include <mcp_string_utils.h>
#include "mcp_socket_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
 * @brief Internal function to send a request and wait for a response.
 *
 * This function handles the core logic of sending a formatted request,
 * managing the pending request state, waiting for the response via condition
 * variables, and handling timeouts or errors.
 *
 * @param client The MCP client instance
 * @param request_json The JSON-RPC request string
 * @param request_id The request ID
 * @param result Pointer to store the result string
 * @param error_code Pointer to store the error code
 * @param error_message Pointer to store the error message
 * @param is_http Flag indicating if this is an HTTP transport request
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
        error_code == NULL || error_message == NULL)
        return -1;

    if (client->transport == NULL)
        return -1;

    // Initialize result and error message
    *result = NULL;
    *error_message = NULL;
    *error_code = MCP_ERROR_NONE;

    // Calculate JSON length - excluding null terminator, as required by server
    size_t json_len = strlen(request_json);
    uint32_t net_len = htonl((uint32_t)json_len);

    // Prepare buffers for vectored send
    mcp_buffer_t send_buffers[2];
    send_buffers[0].data = &net_len;
    send_buffers[0].size = sizeof(net_len);
    send_buffers[1].data = request_json;
    send_buffers[1].size = json_len;

    // Check if this is an HTTP transport
    mcp_transport_protocol_t transport_protocol = mcp_transport_get_protocol(client->transport);
    bool is_http = (transport_protocol == MCP_TRANSPORT_PROTOCOL_HTTP);

    // For non-HTTP transports, set up the asynchronous request handling
    pending_request_t pending_req;
    pending_request_entry_t* req_entry_wrapper = NULL;

    if (!is_http) {
        // --- Asynchronous Receive Logic for non-HTTP transports ---
        // 1. Prepare pending request structure
        pending_req.id = request_id;
        pending_req.status = PENDING_REQUEST_WAITING;
        pending_req.result_ptr = result;
        pending_req.error_code_ptr = error_code;
        pending_req.error_message_ptr = error_message;
        pending_req.cv = mcp_cond_create();
        if (pending_req.cv == NULL) {
            mcp_log_error("Failed to create condition variable for request %llu.", (unsigned long long)request_id);
            return -1;
        }

        // 2. Add to pending requests map (protected by mutex)
        mcp_mutex_lock(client->pending_requests_mutex);
        int add_status = mcp_client_add_pending_request_entry(client, pending_req.id, &pending_req);
        if (add_status != 0) {
            mcp_mutex_unlock(client->pending_requests_mutex);
            mcp_cond_destroy(pending_req.cv);
            mcp_log_error("Failed to add request %llu to hash table.", (unsigned long long)request_id);
            return -1;
        }
        mcp_mutex_unlock(client->pending_requests_mutex);
    }

    // Send the buffers using vectored I/O
    int send_status = mcp_transport_sendv(client->transport, send_buffers, 2);
    if (send_status != 0) {
        mcp_log_error("mcp_transport_sendv failed with status %d", send_status);

        // Clean up the pending request if it was created
        if (!is_http) {
            mcp_mutex_lock(client->pending_requests_mutex);
            mcp_client_remove_pending_request_entry(client, request_id);
            mcp_mutex_unlock(client->pending_requests_mutex);
        }

        return -1;
    }

    // For HTTP transport, handle the response synchronously
    if (is_http) {
        // Create a buffer to receive the response
        char* response_data = NULL;
        size_t response_size = 0;

        // Try to receive the response, This is a synchronous operation for HTTP transport
        int status = mcp_transport_receive(client->transport, &response_data, &response_size, client->config.request_timeout_ms);
        if (status == 0 && response_data != NULL) {
            // Parse the response JSON
            mcp_log_debug("HTTP transport: Received response data: %s", response_data);

            // Extract the result from the response
            uint64_t response_id;
            mcp_error_code_t response_error_code;
            char* response_error_message = NULL;
            char* response_result = NULL;

            int parse_result = mcp_json_parse_response(response_data, &response_id, &response_error_code,
                                                      &response_error_message, &response_result);
            if (parse_result == 0) {
                // Check if the response ID matches the request ID
                if (response_id == request_id) {
                    // Set the output parameters
                    *error_code = response_error_code;
                    *error_message = response_error_message;
                    *result = response_result;
                } else {
                    // Response ID doesn't match request ID
                    mcp_log_error("HTTP transport: Response ID %llu doesn't match request ID %llu",
                                 (unsigned long long)response_id, (unsigned long long)request_id);
                    *error_code = MCP_ERROR_INTERNAL_ERROR;
                    *error_message = mcp_strdup("Response ID doesn't match request ID");
                    free(response_error_message);
                    free(response_result);
                }
            } else {
                // Failed to parse response
                mcp_log_error("HTTP transport: Failed to parse response: %s", response_data);
                *error_code = MCP_ERROR_PARSE_ERROR;
                *error_message = mcp_strdup("Failed to parse response");
            }

            // Free the response data
            free(response_data);
        } else {
            // No response received or error occurred
            mcp_log_error("HTTP transport: Failed to receive response for request ID %llu", (unsigned long long)request_id);
            *error_code = MCP_ERROR_TRANSPORT_ERROR;
            *error_message = mcp_strdup("Failed to receive HTTP response");
        }

        return (*error_code == MCP_ERROR_NONE) ? 0 : -1;
    }

    // For non-HTTP transports, wait for the response asynchronously
    mcp_log_debug("Waiting for response to request ID %llu", (unsigned long long)request_id);
    int wait_result = 0;
    int final_status = -1;

    mcp_mutex_lock(client->pending_requests_mutex);
    req_entry_wrapper = mcp_client_find_pending_request_entry(client, request_id, false);
    if (req_entry_wrapper && req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
        // Wait for the response with or without timeout
        if (client->config.request_timeout_ms > 0) {
            wait_result = mcp_cond_timedwait(req_entry_wrapper->request.cv, client->pending_requests_mutex, client->config.request_timeout_ms);
        } else {
            wait_result = mcp_cond_wait(req_entry_wrapper->request.cv, client->pending_requests_mutex);
        }

        // Handle timeout and other wait errors
#ifdef ETIMEDOUT
        if (wait_result == ETIMEDOUT) {
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
            mcp_log_error("mcp_cond_wait/timedwait failed with code: %d (%s)", wait_result, strerror(wait_result));
        }
#else
        if (wait_result == 1) {
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
            mcp_log_error("mcp_cond_wait/timedwait failed with code: %d", wait_result);
        }
#endif
    }

    // Determine the final outcome based on request status
    if (req_entry_wrapper) {
        mcp_log_debug("Request ID %llu status: %d", (unsigned long long)request_id, req_entry_wrapper->request.status);
        if (req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) {
            mcp_log_debug("Request ID %llu completed successfully", (unsigned long long)request_id);
            final_status = 0;
        } else if (req_entry_wrapper->request.status == PENDING_REQUEST_TIMEOUT) {
            mcp_log_error("Request ID %llu timed out", (unsigned long long)request_id);
            final_status = -2;
        } else {
            final_status = -1;
        }

        // Remove entry from hash table
        mcp_client_remove_pending_request_entry(client, request_id);
    } else {
        mcp_log_error("Failed to find pending request entry for ID %llu", (unsigned long long)request_id);
        // Entry removed before check. Rely on output params set by callback.
        if (*error_code != MCP_ERROR_NONE)
            final_status = -1;
        else if (*result != NULL)
            final_status = 0;
        else {
            mcp_log_error("Request %llu not found and no result/error set.", (unsigned long long)request_id);
            final_status = -1;
        }
    }
    mcp_mutex_unlock(client->pending_requests_mutex);

    // Set appropriate error information based on the final status
    if (final_status == -2) {
        // Timeout case
        mcp_log_error("Request %llu timed out.", (unsigned long long)request_id);
        *error_code = MCP_ERROR_TRANSPORT_ERROR;
        *error_message = mcp_strdup("Request timed out");
        return -1;
    } else if (final_status != 0) {
        // Other error
        mcp_log_error("Error processing response for request %llu.", (unsigned long long)request_id);
        if (*error_code != MCP_ERROR_NONE && *error_message == NULL) {
            *error_message = mcp_strdup("Unknown internal error occurred");
        } else if (*error_code == MCP_ERROR_NONE) {
            *error_code = MCP_ERROR_INTERNAL_ERROR;
            *error_message = mcp_strdup("Internal error processing response");
        }
        return -1;
    }

    return 0;
}

/**
 * @brief Send a request to the MCP server and receive a response
 *
 * This function formats a JSON-RPC request with the given method and parameters,
 * sends it to the server, and waits for a response.
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
        error_code == NULL || error_message == NULL)
        return -1;

    // Generate next request ID
    mcp_mutex_lock(client->pending_requests_mutex);
    uint64_t current_id = client->next_id++;
    mcp_mutex_unlock(client->pending_requests_mutex);

    // Create request JSON
    char* request_json = NULL;
    const char* params_to_use = (params != NULL) ? params : "{}";

    // Format the request JSON
    request_json = mcp_json_format_request(current_id, method, params_to_use);
    if (request_json == NULL) {
        mcp_log_error("Failed to format request JSON for method '%s'", method);
        return -1;
    }

    // Use the unified send_and_wait function for all transport types
    int status = mcp_client_send_and_wait(client, request_json, current_id, result, error_code, error_message);

    // Free the formatted request JSON string
    free(request_json);

    return status;
}
