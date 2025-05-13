#include "internal/client_internal.h"
#include <mcp_log.h>
#include <mcp_json.h>
#include <mcp_json_rpc.h>
#include <mcp_string_utils.h>
#include <mcp_socket_utils.h>
#include <mcp_transport.h>
#include "../src/transport/internal/transport_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
 * @brief Frees an array of batch responses and their contents.
 *
 * @param responses Array of batch response structures.
 * @param count Number of responses in the array.
 */
void mcp_client_free_batch_responses(mcp_batch_response_t* responses, size_t count) {
    if (responses == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(responses[i].result);
        free(responses[i].error_message);
    }

    free(responses);
}

/**
 * @brief Sends a batch of requests to the MCP server and receives responses.
 *
 * @param client The client instance.
 * @param requests Array of batch request structures.
 * @param request_count Number of requests in the array.
 * @param[out] responses Pointer to receive an array of batch response structures.
 * @param[out] response_count Pointer to receive the number of responses in the array.
 * @return 0 on successful communication (check individual response error_codes),
 *         -1 on failure (e.g., transport error, timeout, parse error).
 */
int mcp_client_send_batch_request(
    mcp_client_t* client,
    const mcp_batch_request_t* requests,
    size_t request_count,
    mcp_batch_response_t** responses,
    size_t* response_count
) {
    if (client == NULL || requests == NULL || request_count == 0 ||
        responses == NULL || response_count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *responses = NULL;
    *response_count = 0;

    // Validate requests
    for (size_t i = 0; i < request_count; i++) {
        if (requests[i].method == NULL) {
            mcp_log_error("Request %zu has NULL method", i);
            return -1;
        }
    }

    // Allocate the response array
    mcp_batch_response_t* response_array = (mcp_batch_response_t*)calloc(request_count, sizeof(mcp_batch_response_t));
    if (response_array == NULL) {
        mcp_log_error("Failed to allocate memory for batch response array");
        return -1;
    }

    // Send each request individually and collect responses
    for (size_t i = 0; i < request_count; i++) {
        // Set the ID from the request
        response_array[i].id = requests[i].id;

        // Set default values
        response_array[i].result = NULL;
        response_array[i].error_code = MCP_ERROR_NONE;
        response_array[i].error_message = NULL;

        // Send the individual request
        char* result = NULL;
        mcp_error_code_t error_code = MCP_ERROR_NONE;
        char* error_message = NULL;

        int send_result = mcp_client_send_request(
            client,
            requests[i].method,
            requests[i].params ? requests[i].params : "{}",
            &result,
            &error_code,
            &error_message
        );

        if (send_result != 0 || error_code != MCP_ERROR_NONE) {
            // Request failed
            response_array[i].error_code = error_code != MCP_ERROR_NONE ? error_code : MCP_ERROR_INTERNAL_ERROR;
            response_array[i].error_message = error_message; // Transfer ownership
            free(result); // Free the result if any
        } else {
            // Request succeeded
            response_array[i].result = result; // Transfer ownership
        }
    }

    // Set output parameters
    *responses = response_array;
    *response_count = request_count;

    return 0;
}
