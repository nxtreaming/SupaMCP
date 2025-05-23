#include "internal/client_internal.h"
#include <mcp_log.h>
#include <mcp_json.h>
#include <mcp_json_rpc.h>
#include <mcp_string_utils.h>
#include <mcp_socket_utils.h>
#include <mcp_transport.h>
#include <mcp_memory_pool.h>
#include <mcp_thread_cache.h>
#include <mcp_cache_aligned.h>
#include "../src/transport/internal/transport_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Performance optimization constants
#define MAX_BATCH_SIZE 100
#define INITIAL_RESPONSE_SIZE 1024

/**
 * @brief Frees an array of batch responses and their contents.
 *
 * This optimized function efficiently cleans up all memory allocated for batch responses,
 * using thread cache for better performance when available.
 *
 * @param responses Array of batch response structures.
 * @param count Number of responses in the array.
 */
void mcp_client_free_batch_responses(mcp_batch_response_t* responses, size_t count) {
    if (responses == NULL || count == 0) {
        return;
    }

    // Log the cleanup operation at debug level
    mcp_log_debug("Freeing %zu batch responses", count);

    // Free each response's dynamically allocated fields
    for (size_t i = 0; i < count; i++) {
        // Free result string if present
        if (responses[i].result != NULL) {
            free(responses[i].result);
            responses[i].result = NULL;
        }

        // Free error message if present
        if (responses[i].error_message != NULL) {
            free(responses[i].error_message);
            responses[i].error_message = NULL;
        }
    }

    // Free the response array using thread cache if possible
    if (mcp_thread_cache_is_initialized()) {
        mcp_thread_cache_free(responses, count * sizeof(mcp_batch_response_t));
    } else {
        free(responses);
    }

    mcp_log_debug("Batch responses freed successfully");
}

/**
 * @brief Sends a batch of requests to the MCP server and receives responses.
 *
 * This optimized function efficiently processes multiple requests in a batch,
 * with improved memory management, error handling, and logging.
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
        mcp_log_error("Invalid parameters for batch request");
        return -1;
    }

    // Check for reasonable batch size
    if (request_count > MAX_BATCH_SIZE) {
        mcp_log_error("Batch size %zu exceeds maximum allowed (%d)",
                     request_count, MAX_BATCH_SIZE);
        return -1;
    }

    // Initialize output parameters
    *responses = NULL;
    *response_count = 0;

    // Log the batch request
    mcp_log_info("Processing batch request with %zu requests", request_count);

    // Validate requests
    for (size_t i = 0; i < request_count; i++) {
        if (requests[i].method == NULL) {
            mcp_log_error("Request %zu has NULL method", i);
            return -1;
        }

        // Log each request at debug level
        if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
            const char* params = requests[i].params ?
                               (strlen(requests[i].params) > 100 ? "[large params]" : requests[i].params) :
                               "NULL";
            mcp_log_debug("Batch request %zu: method=%s, id=%llu, params=%s",
                         i, requests[i].method, (unsigned long long)requests[i].id, params);
        }
    }

    // Allocate the response array using thread cache if available
    mcp_batch_response_t* response_array = NULL;

    if (mcp_thread_cache_is_initialized()) {
        response_array = (mcp_batch_response_t*)mcp_thread_cache_alloc(
            request_count * sizeof(mcp_batch_response_t));
    } else {
        response_array = (mcp_batch_response_t*)calloc(
            request_count, sizeof(mcp_batch_response_t));
    }

    if (response_array == NULL) {
        mcp_log_error("Failed to allocate memory for batch response array");
        return -1;
    }

    // Initialize the response array
    memset(response_array, 0, request_count * sizeof(mcp_batch_response_t));

    // Track success/failure counts for logging
    size_t success_count = 0;
    size_t failure_count = 0;

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

        // Use empty object as default params if none provided
        const char* params_to_use = requests[i].params ? requests[i].params : "{}";

        // Send the request
        int send_result = mcp_client_send_request(
            client,
            requests[i].method,
            params_to_use,
            &result,
            &error_code,
            &error_message
        );

        // Process the result
        if (send_result != 0 || error_code != MCP_ERROR_NONE) {
            // Request failed
            failure_count++;

            // Set error code - use the server-provided code if available, otherwise use internal error
            response_array[i].error_code = error_code != MCP_ERROR_NONE ?
                                         error_code : MCP_ERROR_INTERNAL_ERROR;

            // Transfer ownership of error message
            response_array[i].error_message = error_message;

            // Free the result if any
            if (result != NULL) {
                free(result);
            }

            // Log the failure
            mcp_log_debug("Batch request %zu failed: method=%s, error=%d, message=%s",
                         i, requests[i].method, response_array[i].error_code,
                         error_message ? error_message : "NULL");
        } else {
            // Request succeeded
            success_count++;

            // Transfer ownership of result
            response_array[i].result = result;

            // Log success at debug level
            if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
                const char* result_str = result ?
                                      (strlen(result) > 100 ? "[large result]" : result) :
                                      "NULL";
                mcp_log_debug("Batch request %zu succeeded: method=%s, result=%s",
                             i, requests[i].method, result_str);
            }
        }
    }

    // Set output parameters
    *responses = response_array;
    *response_count = request_count;

    // Log the overall result
    mcp_log_info("Batch request completed: %zu/%zu successful",
                success_count, request_count);

    return 0;
}

/**
 * @brief Optimized batch request structure for parallel processing
 */
typedef struct {
    const mcp_batch_request_t* request;  // Original request
    mcp_batch_response_t* response;      // Response storage
    mcp_client_t* client;                // Client instance
    int result;                          // Result code
} batch_request_context_t;

/**
 * @brief Process a single batch request (for use with parallel processing)
 *
 * @param context The batch request context
 * @return 0 on success, non-zero on failure
 */
static int process_single_batch_request(batch_request_context_t* context) {
    if (context == NULL || context->request == NULL ||
        context->response == NULL || context->client == NULL) {
        return -1;
    }

    // Initialize response with request ID and default values
    context->response->id = context->request->id;
    context->response->result = NULL;
    context->response->error_code = MCP_ERROR_NONE;
    context->response->error_message = NULL;

    // Use empty object as default params if none provided
    const char* params_to_use = context->request->params ?
                              context->request->params : "{}";

    // Send the request
    char* result = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    int send_result = mcp_client_send_request(
        context->client,
        context->request->method,
        params_to_use,
        &result,
        &error_code,
        &error_message
    );

    // Process the result
    if (send_result != 0 || error_code != MCP_ERROR_NONE) {
        // Request failed
        context->response->error_code = error_code != MCP_ERROR_NONE ?
                                      error_code : MCP_ERROR_INTERNAL_ERROR;
        context->response->error_message = error_message; // Transfer ownership
        free(result); // Free the result if any
        context->result = -1;
    } else {
        // Request succeeded
        context->response->result = result; // Transfer ownership
        context->result = 0;
    }

    return context->result;
}

/**
 * @brief Sends a batch of requests to the MCP server with optimized processing
 *
 * This function processes batch requests with improved efficiency by using
 * optimized memory management and potentially parallel processing in the future.
 *
 * @param client The client instance
 * @param requests Array of batch request structures
 * @param request_count Number of requests in the array
 * @param[out] responses Pointer to receive an array of batch response structures
 * @param[out] response_count Pointer to receive the number of responses in the array
 * @return 0 on successful communication, -1 on failure
 */
int mcp_client_send_batch_request_optimized(
    mcp_client_t* client,
    const mcp_batch_request_t* requests,
    size_t request_count,
    mcp_batch_response_t** responses,
    size_t* response_count
) {
    if (client == NULL || requests == NULL || request_count == 0 ||
        responses == NULL || response_count == NULL) {
        mcp_log_error("Invalid parameters for optimized batch request");
        return -1;
    }

    // Check for reasonable batch size
    if (request_count > MAX_BATCH_SIZE) {
        mcp_log_error("Batch size %zu exceeds maximum allowed (%d)",
                     request_count, MAX_BATCH_SIZE);
        return -1;
    }

    // Initialize output parameters
    *responses = NULL;
    *response_count = 0;

    // Log the batch request
    mcp_log_info("Processing optimized batch request with %zu requests", request_count);

    // Validate requests
    for (size_t i = 0; i < request_count; i++) {
        if (requests[i].method == NULL) {
            mcp_log_error("Request %zu has NULL method", i);
            return -1;
        }
    }

    // Allocate the response array using thread cache if available
    mcp_batch_response_t* response_array = NULL;

    if (mcp_thread_cache_is_initialized()) {
        response_array = (mcp_batch_response_t*)mcp_thread_cache_alloc(
            request_count * sizeof(mcp_batch_response_t));
    } else {
        response_array = (mcp_batch_response_t*)calloc(
            request_count, sizeof(mcp_batch_response_t));
    }

    if (response_array == NULL) {
        mcp_log_error("Failed to allocate memory for batch response array");
        return -1;
    }

    // Initialize the response array
    memset(response_array, 0, request_count * sizeof(mcp_batch_response_t));

    // Allocate context array for batch processing
    batch_request_context_t* contexts = (batch_request_context_t*)malloc(
        request_count * sizeof(batch_request_context_t));

    if (contexts == NULL) {
        mcp_log_error("Failed to allocate memory for batch contexts");

        // Clean up response array
        if (mcp_thread_cache_is_initialized()) {
            mcp_thread_cache_free(response_array, request_count * sizeof(mcp_batch_response_t));
        } else {
            free(response_array);
        }

        return -1;
    }

    // Initialize contexts
    for (size_t i = 0; i < request_count; i++) {
        contexts[i].request = &requests[i];
        contexts[i].response = &response_array[i];
        contexts[i].client = client;
        contexts[i].result = 0;
    }

    // Process each request
    // Note: This is currently sequential, but could be parallelized in the future
    size_t success_count = 0;
    size_t failure_count = 0;

    for (size_t i = 0; i < request_count; i++) {
        int result = process_single_batch_request(&contexts[i]);

        if (result == 0) {
            success_count++;
        } else {
            failure_count++;
        }

        // Log at debug level
        if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
            if (result == 0) {
                const char* result_str = response_array[i].result ?
                                      (strlen(response_array[i].result) > 100 ?
                                       "[large result]" : response_array[i].result) :
                                      "NULL";

                mcp_log_debug("Batch request %zu succeeded: method=%s, result=%s",
                             i, requests[i].method, result_str);
            } else {
                mcp_log_debug("Batch request %zu failed: method=%s, error=%d, message=%s",
                             i, requests[i].method, response_array[i].error_code,
                             response_array[i].error_message ?
                             response_array[i].error_message : "NULL");
            }
        }
    }

    // Free the context array
    free(contexts);

    // Set output parameters
    *responses = response_array;
    *response_count = request_count;

    // Log the overall result
    mcp_log_info("Optimized batch request completed: %zu/%zu successful",
                success_count, request_count);

    return 0;
}
