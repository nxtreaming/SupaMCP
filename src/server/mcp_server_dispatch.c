#include "internal/server_internal.h"
#include "mcp_gateway_routing.h"
#include "mcp_json_rpc.h"
#include "mcp_auth.h"
#include "mcp_arena.h"
#include "mcp_thread_local.h"
#include "mcp_json.h"
#include "mcp_json_message.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_performance_collector.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @internal
 * @brief Handles the 'ping' request.
 * Simple handler that returns a pong response to confirm server is live.
 * This is primarily used as an initial handshake for connection testing.
 * 
 * SHOULD BE STATIC, but the unity test framework needs to access it.
 */
char* handle_ping_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request,
    const mcp_auth_context_t* auth_context, int* error_code) {
    // No params needed, arena unused, auth_context may be unused for ping
    (void)arena;
    (void)auth_context;

    if (server == NULL || request == NULL || error_code == NULL) {
        if (error_code)
            *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot proceed without basic parameters
    }

    // For ping requests, we allow a NULL auth_context as ping is often used for initial connection testing
    // This makes the ping handler more lenient than other handlers
    *error_code = MCP_ERROR_NONE;

    mcp_log_debug("Received ping request (ID: %llu, params: %s)",
        (unsigned long long)request->id,
        request->params ? request->params : "NULL");

    // Log auth context info
    if (auth_context) {
        mcp_log_debug("Auth context: type=%d, identifier=%s",
            auth_context->type,
            auth_context->identifier ? auth_context->identifier : "NULL");
    }
    else {
        mcp_log_debug("Auth context is NULL");
    }

    // Create a direct response without using the thread-local arena
    mcp_log_debug("Creating direct ping response");

    // Use mcp_json_format_response instead of mcp_json_create_response
    // This doesn't use the thread-local arena
    const char* pong_result = "{\"message\":\"pong\"}";
    char* response = mcp_json_format_response(request->id, pong_result);

    if (!response) {
        mcp_log_error("Failed to create ping response");
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        return create_error_response(request->id, *error_code, "Failed to create ping response");
    }

    mcp_log_debug("Created ping response (ID: %llu): '%s'",
        (unsigned long long)request->id, response);

    return response;
}

/**
 * @internal
 * @brief Parses and handles a single incoming message.
 *
 * Uses an arena for temporary allocations during parsing. Determines message type
 * and dispatches to the appropriate handler (handle_request or handles notifications/responses).
 *
 * @param server The server instance.
 * @param data Raw message data (expected to be null-terminated JSON string).
 * @param size Size of the data.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code on failure (e.g., parse error).
 * @brief Parses and handles a single incoming message or a batch of messages.
 *
 * Uses an arena for temporary allocations during parsing. Determines message type
 * and dispatches to the appropriate handler. Handles batch requests/notifications.
 *
 * @param server The server instance.
 * @param data Raw message data (expected to be null-terminated JSON string).
 * @param size Size of the data.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code on failure (e.g., parse error).
 *                        For batches, this reflects the overall processing status.
 * @return A malloc'd JSON string response. For single requests, it's the response object.
 *         For batches containing requests, it's a JSON array of response objects.
 *         Returns NULL for notifications, single responses, or errors during parsing/allocation.
 */
char* handle_message(mcp_server_t* server, const void* data, size_t size, int* error_code) {
#ifdef MCP_ENABLE_PROFILING
    PROFILE_BEGIN("handle_message");
#endif

    // Start performance metrics collection
    mcp_performance_timer_t perf_timer = mcp_performance_timer_create();
    mcp_performance_collect_request_start(&perf_timer);

    if (server == NULL || data == NULL || size == 0 || error_code == NULL) {
        if (error_code)
            *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL;
    }
    *error_code = MCP_ERROR_NONE; // Default to success

    // Use the thread-local arena instead of creating a new one
    mcp_log_debug("Using thread-local arena for message processing");
    mcp_arena_t* arena = mcp_arena_get_current();
    if (!arena) {
        mcp_log_error("Thread-local arena not initialized, creating one");
        if (mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE) != 0) {
            mcp_log_error("Failed to initialize thread-local arena");
            *error_code = MCP_ERROR_INTERNAL_ERROR;
            return create_error_response(0, MCP_ERROR_INTERNAL_ERROR, "Internal server error");
        }
        arena = mcp_arena_get_current();
        if (!arena) {
            mcp_log_error("Failed to get thread-local arena after initialization");
            *error_code = MCP_ERROR_INTERNAL_ERROR;
            return create_error_response(0, MCP_ERROR_INTERNAL_ERROR, "Internal server error");
        }
    }

    // We'll reset the arena after we're done with it

#ifdef MCP_ENABLE_PROFILING
    PROFILE_START("handle_message");
#endif

    const char* json_str = (const char*)data;
    mcp_message_t* messages = NULL;
    size_t message_count = 0;

    // Parse message or batch
    int parse_result = mcp_json_parse_message_or_batch(json_str, &messages, &message_count);
    if (parse_result != 0 || messages == NULL) {
        mcp_log_error("JSON parsing failed (Code: %d)", parse_result);
        // No need to destroy the thread-local arena, just reset it
        mcp_arena_reset_current_thread();
        *error_code = parse_result != 0 ? parse_result : MCP_ERROR_PARSE_ERROR;
        // Generate JSON-RPC Parse Error response (ID is unknown)
        return create_error_response(0, MCP_ERROR_PARSE_ERROR, "Parse error");
    }

    // --- Authentication (Simplified for Batch - Authenticate once based on first request?) ---
    // TODO: Implement more granular auth per request in batch if needed.
    mcp_auth_context_t* auth_context = NULL;
    mcp_auth_type_t required_auth_type = (server->config.api_key != NULL && strlen(server->config.api_key) > 0)
                                           ? MCP_AUTH_API_KEY
                                           : MCP_AUTH_NONE;
    const char* credentials = NULL;
    uint64_t first_request_id = 0; // For potential batch auth error response
    bool is_ping_request = false;

    if (message_count > 0 && messages[0].type == MCP_MESSAGE_TYPE_REQUEST) {
        first_request_id = messages[0].request.id;

        // Check if this is a ping request (special case for authentication)
        if (messages[0].request.method != NULL && strcmp(messages[0].request.method, "ping") == 0) {
            is_ping_request = true;
            mcp_log_debug("Detected ping request, using relaxed authentication");
        }

        if (required_auth_type == MCP_AUTH_API_KEY) {
            mcp_json_t* params_json = mcp_json_parse(messages[0].request.params);
            if (params_json && mcp_json_get_type(params_json) == MCP_JSON_OBJECT) {
                mcp_json_t* key_node = mcp_json_object_get_property(params_json, "apiKey");
                if (key_node && mcp_json_get_type(key_node) == MCP_JSON_STRING) {
                    mcp_json_get_string(key_node, &credentials);
                }
            }
            // Arena handles params_json cleanup
        }
    }

    // For ping requests, we'll create a special anonymous auth context if authentication fails
    if (mcp_auth_verify(server, required_auth_type, credentials, &auth_context) != 0) {
        if (is_ping_request) {
            // For ping requests, create a special anonymous auth context
            mcp_log_debug("Creating anonymous auth context for ping request");
            auth_context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
            if (auth_context) {
                auth_context->type = MCP_AUTH_NONE;
                auth_context->identifier = mcp_strdup("ping_anonymous");

                // Add minimal permissions for ping
                auth_context->allowed_resources = (char**)malloc(sizeof(char*));
                if (auth_context->allowed_resources) {
                    auth_context->allowed_resources_count = 1;
                    auth_context->allowed_resources[0] = mcp_strdup("*");
                }

                auth_context->allowed_tools = (char**)malloc(sizeof(char*));
                if (auth_context->allowed_tools) {
                    auth_context->allowed_tools_count = 1;
                    auth_context->allowed_tools[0] = mcp_strdup("*");
                }
            }
        } else {
            // For non-ping requests, fail with authentication error
            mcp_log_warn("Authentication failed for incoming message/batch.");
            *error_code = MCP_ERROR_INVALID_REQUEST; // Generic auth failure
            // For batch, JSON-RPC spec is unclear on error response format.
            // Sending a single error response might be appropriate.
            char* error_response = create_error_response(first_request_id, *error_code, "Authentication failed");
            mcp_json_free_message_array(messages, message_count); // Free parsed messages
            // No need to destroy the thread-local arena, just reset it
            mcp_arena_reset_current_thread();
#ifdef MCP_ENABLE_PROFILING
            PROFILE_END("handle_message");
#endif
            return error_response;
        }
    }
    mcp_log_debug("Authentication successful (Identifier: %s)", auth_context ? auth_context->identifier : "N/A");
    // --- End Authentication Check ---

    char* final_response_str = NULL;
    dyn_buf_t response_batch_buf; // Use dynamic buffer for batch response array
    bool is_batch_response = false;
    bool batch_buffer_initialized = false;

    if (message_count > 1) {
        is_batch_response = true;
        if (dyn_buf_init(&response_batch_buf, 512) != 0) { // Initial capacity
            mcp_log_error("Failed to init batch response buffer.");
            *error_code = MCP_ERROR_INTERNAL_ERROR;
            // Fall through to cleanup
        } else {
            batch_buffer_initialized = true;
            dyn_buf_append(&response_batch_buf, "[");
        }
    }

    bool first_response_in_batch = true;

    // Pre-allocate response buffer for batch processing
    // This avoids multiple small allocations and string copies
    if (is_batch_response && batch_buffer_initialized) {
        // Estimate initial buffer size based on message count and average response size
        size_t estimated_size = message_count * 256; // Assume average response size of 256 bytes
        if (dyn_buf_ensure_capacity(&response_batch_buf, estimated_size) != 0) {
            mcp_log_warn("Failed to pre-allocate batch response buffer, will grow dynamically");
        }
    }

    // Process each message
    for (size_t i = 0; i < message_count && *error_code == MCP_ERROR_NONE; ++i) {
        mcp_message_t* current_msg = &messages[i];
        char* single_response_str = NULL;
        int current_msg_error = MCP_ERROR_NONE;

        // Only log at debug level to reduce overhead
        if (mcp_log_get_level() <= MCP_LOG_LEVEL_DEBUG) {
            mcp_log_debug("Processing message %zu of %zu, type: %d", i+1, message_count, current_msg->type);
        }

        // Fast path for common message types
        if (current_msg->type == MCP_MESSAGE_TYPE_REQUEST) {
            // Process request message
            if (mcp_log_get_level() <= MCP_LOG_LEVEL_DEBUG) {
                mcp_log_debug("Request message: method=%s, id=%llu",
                             current_msg->request.method ? current_msg->request.method : "NULL",
                             (unsigned long long)current_msg->request.id);
            }

            // Handle the request
            single_response_str = handle_request(server, arena, &current_msg->request, auth_context, &current_msg_error);
            // Process the response
            if (single_response_str != NULL) {
                if (is_batch_response && batch_buffer_initialized) {
                    // Append to batch response buffer
                    if (!first_response_in_batch) {
                        dyn_buf_append(&response_batch_buf, ",");
                    }
                    dyn_buf_append(&response_batch_buf, single_response_str);
                    first_response_in_batch = false;
                    free(single_response_str); // Free individual response string after appending
                } else if (!is_batch_response) {
                    // This was the only message, store its response directly
                    final_response_str = single_response_str;
                    single_response_str = NULL; // Avoid double free
                } else {
                    // Batch buffer init failed, discard individual response
                    free(single_response_str);
                }
            } else if (current_msg_error != MCP_ERROR_NONE && is_batch_response && batch_buffer_initialized) {
                // Generate error response for failed request in batch
                char* error_response = create_error_response(current_msg->request.id, current_msg_error,
                                                           "Request processing failed");
                if (error_response) {
                    if (!first_response_in_batch) {
                        dyn_buf_append(&response_batch_buf, ",");
                    }
                    dyn_buf_append(&response_batch_buf, error_response);
                    first_response_in_batch = false;
                    free(error_response);
                }
            }
        } else if (current_msg->type == MCP_MESSAGE_TYPE_NOTIFICATION) {
            // Process notification (currently does nothing)
            // No response generated for notifications.
        } else if (current_msg->type == MCP_MESSAGE_TYPE_RESPONSE) {
            // Server received a response - typically ignore.
        } else {
            // Invalid message type - skip
        }

        // Reset arena for the next message in the batch (if any)
        mcp_arena_reset_current_thread();
    }

    // Finalize batch response if needed
    if (is_batch_response && batch_buffer_initialized) {
        dyn_buf_append(&response_batch_buf, "]");
        final_response_str = dyn_buf_finalize(&response_batch_buf);
        if (final_response_str == NULL) {
            *error_code = MCP_ERROR_INTERNAL_ERROR; // Finalization/allocation failed
        }
        // Check if the batch response is just "[]" (only notifications or errors occurred)
        if (final_response_str && strcmp(final_response_str, "[]") == 0) {
            free(final_response_str);
            final_response_str = NULL; // No actual responses to send
        }
    } else if (is_batch_response && !batch_buffer_initialized) {
        // Error occurred during buffer init, ensure no response is sent
        final_response_str = NULL;
    }

    // Free the parsed message array and its contents
    mcp_json_free_message_array(messages, message_count);

    // Free the authentication context
    mcp_auth_context_free(auth_context);

    // No need to destroy the thread-local arena, just reset it
    mcp_arena_reset_current_thread();

#ifdef MCP_ENABLE_PROFILING
    PROFILE_END("handle_message");
#endif

    // End performance metrics collection
    bool success = (*error_code == MCP_ERROR_NONE);
    size_t response_size = final_response_str ? strlen(final_response_str) : 0;
    mcp_performance_collect_request_end(&perf_timer, success, response_size, size);

    return final_response_str; // Return malloc'd response string (single object or array, or NULL)
}

/**
 * @internal
 * @brief Handles a parsed request message by dispatching to the correct method handler.
 *
 * @param server The server instance.
 * @param arena Arena used for parsing the request (can be used by handlers for param parsing).
 * @param request Pointer to the parsed request structure.
 * @param auth_context The authentication context for the client making the request.
 * @param[out] error_code Set to MCP_ERROR_NONE on success, or an error code if the method is not found or access denied.
 * @return A malloc'd JSON string response (success or error response).
 */
char* handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code) {
    if (server == NULL || request == NULL || error_code == NULL) {
        if(error_code)
            *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot proceed without basic parameters
    }

    // If arena is NULL, use the thread-local arena
    if (arena == NULL) {
        mcp_log_debug("Using thread-local arena for request handling");
        arena = mcp_arena_get_current();
        if (!arena) {
            mcp_log_error("Thread-local arena not initialized");
            if(error_code)
                *error_code = MCP_ERROR_INTERNAL_ERROR;
            return NULL;
        }
    }

    // Special case for ping requests - they can proceed without auth_context
    if (request->method != NULL && strcmp(request->method, "ping") == 0) {
        // Ping requests can proceed without auth_context
    } else if (auth_context == NULL) {
        // For non-ping requests, auth_context is required
        if(error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // Cannot proceed without auth context for non-ping requests
    }
    *error_code = MCP_ERROR_NONE; // Default to success

    // --- Gateway Routing Check ---
    // Only attempt routing if gateway mode is enabled and backends are configured
    if (server->is_gateway_mode && server->backends != NULL && server->backend_count > 0) {
        // Check if this request should be routed to a backend
        const mcp_backend_info_t* target_backend = find_backend_for_request(request, server->backends, server->backend_count);

        if (target_backend) {
            // Found a backend to route to. Call the forwarding function.
            // Pass the pool manager from the server struct.
            if (server->pool_manager == NULL) {
                mcp_log_error("Gateway mode enabled but pool manager is NULL.");
                *error_code = MCP_ERROR_INTERNAL_ERROR;
                return create_error_response(request->id, *error_code, "Gateway configuration error.");
            }
            return gateway_forward_request(server->pool_manager, target_backend, request, error_code);
        }
        // If target_backend is NULL, fall through to local handling.
    }
    // --- End Gateway Routing Check ---

    // --- Local Handling (Gateway mode disabled OR no backend route found) ---
    if (mcp_log_get_level() <= MCP_LOG_LEVEL_DEBUG) {
        mcp_log_debug("Handling request locally (method: %s).", request->method);
    }

    // Handle the request locally based on its method
    // Note: Pass auth_context down to specific handlers

    // Use method length for faster comparison
    const char* method = request->method;
    if (!method) {
        *error_code = MCP_ERROR_INVALID_REQUEST;
        return create_error_response(request->id, *error_code, "Missing method");
    }

    // Get method length for faster comparison
    size_t method_len = strlen(method);

    // Fast path for common methods using length check first
    if (method_len == 4 && memcmp(method, "ping", 4) == 0) {
        // Special handling for ping requests, all servers should support this connection health check
        return handle_ping_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 9 && memcmp(method, "call_tool", 9) == 0) {
        return handle_call_tool_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 13 && memcmp(method, "read_resource", 13) == 0) {
        return handle_read_resource_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 14 && memcmp(method, "list_resources", 14) == 0) {
        return handle_list_resources_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 10 && memcmp(method, "list_tools", 10) == 0) {
        return handle_list_tools_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 23 && memcmp(method, "list_resource_templates", 23) == 0) {
        return handle_list_resource_templates_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 22 && memcmp(method, "get_performance_metrics", 22) == 0) {
        return handle_get_performance_metrics_request(server, arena, request, auth_context, error_code);
    }
    else if (method_len == 24 && memcmp(method, "reset_performance_metrics", 24) == 0) {
        return handle_reset_performance_metrics_request(server, arena, request, auth_context, error_code);
    }
    else {
        // Unknown method - Create and return error response string
        *error_code = MCP_ERROR_METHOD_NOT_FOUND;
        // Use the helper function from mcp_server_response.c (declared in internal header)
        return create_error_response(request->id, *error_code, "Method not found");
    }
}
