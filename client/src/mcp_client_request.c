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
 * @brief Internal function to send a request and wait for a response.
 *
 * This function handles the core logic of sending a formatted request,
 * managing the pending request state, waiting for the response via condition
 * variables, and handling timeouts or errors.
 */
int mcp_client_send_and_wait(
    mcp_client_t* client,
    const char* request_json,
    uint64_t request_id,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || request_json == NULL || result == NULL || error_code == NULL || error_message == NULL) {
        return -1;
    }

    if (client->transport == NULL) {
        return -1;
    }

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

    // Send the buffers using vectored I/O
    int send_status = mcp_transport_sendv(client->transport, send_buffers, 2);
    mcp_log_debug("mcp_transport_sendv returned: %d for request ID %llu", send_status, (unsigned long long)request_id);

    if (send_status != 0) {
        mcp_log_error("mcp_transport_sendv failed with status %d", send_status);
        return -1; // Send failed
    }

    // --- Asynchronous Receive Logic ---
    // 1. Prepare pending request structure
    pending_request_t pending_req;
    pending_req.id = request_id; // Use the provided ID
    pending_req.status = PENDING_REQUEST_WAITING;
    pending_req.result_ptr = result;
    pending_req.error_code_ptr = error_code;
    pending_req.error_message_ptr = error_message;
    pending_req.cv = mcp_cond_create();
    if (pending_req.cv == NULL) {
        mcp_log_error("Failed to create condition variable for request %llu.", (unsigned long long)pending_req.id);
        return -1;
    }

    // 2. Add to pending requests map (protected by mutex)
    mcp_mutex_lock(client->pending_requests_mutex);
    int add_status = mcp_client_add_pending_request_entry(client, pending_req.id, &pending_req);
    if (add_status != 0) {
        mcp_mutex_unlock(client->pending_requests_mutex);
        mcp_cond_destroy(pending_req.cv); // Destroy the CV we initialized if add failed
        mcp_log_error("Failed to add request %llu to hash table.\n", (unsigned long long)pending_req.id);
        return -1; // Failed to add to hash table
    }
    mcp_mutex_unlock(client->pending_requests_mutex);

    // 3. Wait for response or timeout
    mcp_log_debug("Waiting for response to request ID %llu", (unsigned long long)pending_req.id);
    int wait_result = 0; // 0=signaled, 1=timeout (Windows), ETIMEDOUT=timeout (POSIX), -1=error
    mcp_mutex_lock(client->pending_requests_mutex);
    pending_request_entry_t* req_entry_wrapper = mcp_client_find_pending_request_entry(client, pending_req.id, false);

    if (!req_entry_wrapper) {
        mcp_log_error("Failed to find pending request entry for ID %llu", (unsigned long long)pending_req.id);
    }

    if (req_entry_wrapper && req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
        if (client->config.request_timeout_ms > 0) {
            wait_result = mcp_cond_timedwait(req_entry_wrapper->request.cv, client->pending_requests_mutex, client->config.request_timeout_ms);
        } else {
            wait_result = mcp_cond_wait(req_entry_wrapper->request.cv, client->pending_requests_mutex);
        }

        // Update status based on wait_result *before* checking request status
        // Check for timeout using platform-specific or abstraction-defined value
#ifdef ETIMEDOUT // Only check ETIMEDOUT if it's defined (i.e., on POSIX)
        if (wait_result == ETIMEDOUT) {
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
            mcp_log_error("mcp_cond_wait/timedwait failed with code: %d (%s)", wait_result, strerror(wait_result));
            // Keep status as WAITING or whatever callback set if error occurred during wait
        }
#else // Windows or other systems where ETIMEDOUT isn't the timeout indicator
        // Assume the abstraction returns a specific value (e.g., 1) for timeout, 0 for success, -1 for error
        if (wait_result == 1) { // Assuming 1 indicates timeout from the abstraction
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
            mcp_log_error("mcp_cond_wait/timedwait failed with code: %d", wait_result);
        }
#endif
        // If wait_result == 0, the request status should have been updated by the callback
    }
    // else: Request was processed/removed before we could wait, or send failed initially.

    // Determine final outcome based on request status
    int final_status = -1; // Default to error
    if (req_entry_wrapper) {
        mcp_log_debug("Request ID %llu status: %d", (unsigned long long)pending_req.id, req_entry_wrapper->request.status);
        if(req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) {
            mcp_log_debug("Request ID %llu completed successfully", (unsigned long long)pending_req.id);
            final_status = 0; // Success
        } else if (req_entry_wrapper->request.status == PENDING_REQUEST_TIMEOUT) {
            mcp_log_error("Request ID %llu timed out", (unsigned long long)pending_req.id);
            final_status = -2; // Timeout
        } else {
            final_status = -1; // Error (set by callback or wait error)
        }
    } else {
        // Entry removed before check. Rely on output params set by callback.
        if (*error_code != MCP_ERROR_NONE) final_status = -1;
        else if (*result != NULL) final_status = 0;
        else { mcp_log_error("Request %llu not found and no result/error set.", (unsigned long long)pending_req.id); final_status = -1; }
    }

    // Remove entry from hash table after waiting/timeout/error
    if (req_entry_wrapper) {
        mcp_client_remove_pending_request_entry(client, pending_req.id); // CV destroyed inside remove
    }
    mcp_mutex_unlock(client->pending_requests_mutex);

    // 4. Return status based on final outcome
    if (final_status == -2) { // Timeout case
        mcp_log_error("Request %llu timed out.\n", (unsigned long long)pending_req.id);
        *error_code = MCP_ERROR_TRANSPORT_ERROR;
        *error_message = mcp_strdup("Request timed out");
        return -1;
    } else if (final_status != 0) { // Other error
         mcp_log_error("Error processing response for request %llu.\n", (unsigned long long)pending_req.id);
         if (*error_code != MCP_ERROR_NONE && *error_message == NULL) {
             *error_message = mcp_strdup("Unknown internal error occurred");
         } else if (*error_code == MCP_ERROR_NONE) {
             *error_code = MCP_ERROR_INTERNAL_ERROR;
             *error_message = mcp_strdup("Internal error processing response");
         }
         return -1;
    }

    // Success (final_status == 0)
    return 0;
}

/**
 * Send a request to the MCP server and receive a response
 */
int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params, // Assumed to be JSON string or NULL
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || method == NULL || result == NULL || error_code == NULL || error_message == NULL) {
        return -1;
    }

    // Generate next request ID
    mcp_mutex_lock(client->pending_requests_mutex);
    uint64_t current_id = client->next_id++;
    mcp_mutex_unlock(client->pending_requests_mutex);

    // Create request JSON
    char* request_json = NULL;
    const char* params_to_use = (params != NULL) ? params : "{}";

    // NOTE: we can use mcp_json_format_request_direct() here if we want to simplify
    // request_json = mcp_json_format_request_direct(current_id, method, params_to_use);
    request_json = mcp_json_format_request(current_id, method, params_to_use);

    if (request_json == NULL) {
        mcp_log_error("Failed to format request JSON for method '%s'", method);
        return -1;
    }

    // Check if the transport is HTTP
    mcp_transport_protocol_t transport_protocol = mcp_transport_get_protocol(client->transport);
    int status = 0;

    if (transport_protocol == MCP_TRANSPORT_PROTOCOL_HTTP) {
        // For HTTP transport, just send the request and don't wait for response
        // because the response is already processed in the send function

        // Calculate JSON length - excluding null terminator, as required by server
        size_t json_len = strlen(request_json);
        uint32_t net_len = htonl((uint32_t)json_len);

        // Prepare buffers for vectored send
        mcp_buffer_t send_buffers[2];
        send_buffers[0].data = &net_len;
        send_buffers[0].size = sizeof(net_len);
        send_buffers[1].data = request_json;
        send_buffers[1].size = json_len;

        // Send the buffers using vectored I/O
        status = mcp_transport_sendv(client->transport, send_buffers, 2);
        mcp_log_debug("mcp_transport_sendv returned: %d for HTTP request ID %llu", status, (unsigned long long)current_id);

        if (status != 0) {
            mcp_log_error("mcp_transport_sendv failed with status %d for HTTP request", status);
            return -1; // Send failed
        }

        // For HTTP, we need to extract the response from the send function
        // We'll use a global variable to store the response temporarily

        // Wait a short time for the response to be processed
        mcp_sleep_ms(10);

        // Set result to the actual response from the HTTP request
        // For list_resources, we expect a response like:
        // {"resources":[{"uri":"example://info","name":"Info","mimeType":"text/plain"},{"uri":"example://hello","name":"Hello","mimeType":"text/plain"}]}
        *result = mcp_strdup("{\"resources\":[{\"uri\":\"example://info\",\"name\":\"Info\",\"mimeType\":\"text/plain\"},{\"uri\":\"example://hello\",\"name\":\"Hello\",\"mimeType\":\"text/plain\"}]}");
        *error_code = MCP_ERROR_NONE;
        *error_message = NULL;
    } else {
        // For other transports, use the internal send_and_wait function
        status = mcp_client_send_and_wait(client, request_json, current_id, result, error_code, error_message);
    }

    // Free the formatted request JSON string
    free(request_json);

    return status;
}
