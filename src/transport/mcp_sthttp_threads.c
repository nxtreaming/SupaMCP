#include "internal/sthttp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief Thread function for HTTP event processing
 */
void* sthttp_event_thread_func(void* arg) {
    if (arg == NULL) {
        mcp_log_error("Invalid argument for HTTP streamable event thread");
        return NULL;
    }

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_error("Invalid transport data for HTTP streamable event thread");
        return NULL;
    }

    mcp_log_info("HTTP streamable event thread started");

    time_t last_heartbeat = time(NULL);

    while (data->running) {
        // Service libwebsockets
        if (data->context) {
            int service_result = lws_service(data->context, STHTTP_LWS_SERVICE_TIMEOUT_MS);
            if (service_result < 0) {
                mcp_log_error("lws_service failed: %d", service_result);
                break;
            }
        }

        // Send heartbeats if enabled
        if (data->send_heartbeats && data->sse_clients) {
            time_t current_time = time(NULL);
            if ((current_time - last_heartbeat) * 1000 >= (time_t)data->heartbeat_interval_ms) {
                // Send heartbeat to all connected SSE clients using dynamic array
                int heartbeat_sent = dynamic_sse_clients_broadcast_heartbeat(data->sse_clients);

                data->last_heartbeat_time = current_time;
                data->heartbeat_counter++;

                last_heartbeat = current_time;
                size_t active_clients = dynamic_sse_clients_count(data->sse_clients);
                mcp_log_debug("Sent heartbeat to %d SSE clients (active: %zu)", heartbeat_sent, active_clients);
            }
        }

        // Small sleep to prevent busy waiting
        mcp_sleep_ms(10);
    }

    mcp_log_info("HTTP streamable event thread stopped");
    return NULL;
}

/**
 * @brief Thread function for periodic cleanup
 */
void* sthttp_cleanup_thread_func(void* arg) {
    if (arg == NULL) {
        mcp_log_error("Invalid argument for HTTP streamable cleanup thread");
        return NULL;
    }

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    sthttp_transport_data_t* data = (sthttp_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_error("Invalid transport data for HTTP streamable cleanup thread");
        return NULL;
    }

    mcp_log_info("HTTP streamable cleanup thread started");

    while (data->running) {
        // Use condition variable for efficient timed wait
        mcp_mutex_lock(data->cleanup_mutex);

        // Wait for cleanup interval or shutdown signal
        int wait_result = mcp_cond_timedwait(data->cleanup_condition,
                                            data->cleanup_mutex,
                                            STHTTP_CLEANUP_INTERVAL_SECONDS * 1000);

        bool should_exit = data->cleanup_shutdown;
        mcp_mutex_unlock(data->cleanup_mutex);

        if (should_exit || !data->running) {
            break;
        }

        // Log if there was an error (but continue operation)
        if (wait_result != 0 && wait_result != -2) { // -2 is timeout, which is expected
            mcp_log_debug("Cleanup thread condition wait returned: %d", wait_result);
        }

        // Clean up expired sessions
        if (data->session_manager) {
            size_t cleaned_count = mcp_session_manager_cleanup_expired(data->session_manager);
            if (cleaned_count > 0) {
                mcp_log_info("Cleanup thread removed %zu expired sessions", cleaned_count);
            }
        }

        // Clean up disconnected SSE clients using dynamic array
        if (data->sse_clients) {
            size_t cleaned_clients = dynamic_sse_clients_cleanup(data->sse_clients);
            if (cleaned_clients > 0) {
                size_t active_clients = dynamic_sse_clients_count(data->sse_clients);
                mcp_log_debug("Cleanup thread removed %zu disconnected SSE clients (active: %zu)",
                             cleaned_clients, active_clients);
            }
        }
    }

    mcp_log_info("HTTP streamable cleanup thread stopped");
    return NULL;
}

/**
 * @brief Process JSON-RPC request and generate response
 */
char* process_jsonrpc_request(sthttp_transport_data_t* data, const char* request_json, const char* session_id) {
    if (data == NULL || request_json == NULL) {
        return NULL;
    }

    if (data->message_callback == NULL) {
        mcp_log_error("No message callback configured");
        return NULL;
    }

    // Call the message callback to process the request
    int error_code = 0;
    char* response = data->message_callback(data->callback_user_data, 
                                          request_json, 
                                          strlen(request_json), 
                                          &error_code);
    if (error_code != 0) {
        mcp_log_error("Message callback returned error: %d", error_code);
        if (response) {
            free(response);
            response = NULL;
        }
    }

    // Log the session context if provided
    if (session_id) {
        mcp_log_debug("Processed JSON-RPC request for session: %s", session_id);
    } else {
        mcp_log_debug("Processed JSON-RPC request (no session)");
    }

    return response;
}

/**
 * @brief Handle MCP endpoint request
 */
int handle_mcp_endpoint_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data) {
    if (wsi == NULL || data == NULL || session_data == NULL) {
        return -1;
    }

    // Determine HTTP method by checking URI tokens
    char method[16] = {0};
    if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0) {
        strncpy(method, "POST", sizeof(method) - 1);
    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) > 0) {
        strncpy(method, "GET", sizeof(method) - 1);
    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_OPTIONS_URI) > 0) {
        strncpy(method, "OPTIONS", sizeof(method) - 1);
    } else {
        // Try to determine method from other headers
        int method_idx = lws_http_get_uri_and_method(wsi, NULL, NULL);
        switch (method_idx) {
            case LWSHUMETH_GET:
                strncpy(method, "GET", sizeof(method) - 1);
                break;
            case LWSHUMETH_POST:
                strncpy(method, "POST", sizeof(method) - 1);
                break;
            case LWSHUMETH_OPTIONS:
                strncpy(method, "OPTIONS", sizeof(method) - 1);
                break;
            case LWSHUMETH_DELETE:
                strncpy(method, "DELETE", sizeof(method) - 1);
                break;
            default:
                mcp_log_error("Unknown HTTP method index: %d", method_idx);
                return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Invalid HTTP method");
        }
    }

    mcp_log_info("MCP endpoint request: %s", method);

    // Route based on HTTP method
    if (strcmp(method, "POST") == 0) {
        return handle_mcp_post_request(wsi, data, session_data);
    } else if (strcmp(method, "GET") == 0) {
        return handle_mcp_get_request(wsi, data, session_data);
    } else if (strcmp(method, "DELETE") == 0) {
        return handle_mcp_delete_request(wsi, data, session_data);
    } else if (strcmp(method, "OPTIONS") == 0) {
        return handle_options_request(wsi, data);
    } else {
        mcp_log_warn("Unsupported HTTP method: %s", method);
        return send_http_error_response(wsi, HTTP_STATUS_METHOD_NOT_ALLOWED, "Method not allowed");
    }
}

/**
 * @brief Handle MCP endpoint POST request
 */
int handle_mcp_post_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data) {
    if (wsi == NULL || data == NULL || session_data == NULL) {
        return -1;
    }

    // Validate origin if required
    if (data->validate_origin && session_data->origin[0] != '\0') {
        if (!validate_origin(data, session_data->origin)) {
            return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Origin not allowed");
        }
    }

    // Check if we have a complete request body
    mcp_log_debug("POST request body check: body=%p, size=%zu",
                  session_data->request_body, session_data->request_body_size);

    if (session_data->request_body == NULL || session_data->request_body_size == 0) {
        mcp_log_error("No request body for POST request (body=%p, size=%zu)",
                      session_data->request_body, session_data->request_body_size);
        return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Request body required");
    }

    // Process the JSON-RPC request
    char* response = process_jsonrpc_request(data, session_data->request_body, 
                                           session_data->has_session ? session_data->session_id : NULL);
    if (response == NULL) {
        return send_http_error_response(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to process request");
    }

    // Check Accept header to determine response type
    char accept_header[256];
    int accept_len = lws_hdr_copy(wsi, accept_header, sizeof(accept_header), WSI_TOKEN_HTTP_ACCEPT);

    mcp_log_debug("Accept header: '%s' (length: %d)", accept_len > 0 ? accept_header : "(none)", accept_len);

    bool wants_sse = (accept_len > 0 && strstr(accept_header, "text/event-stream") != NULL);
    mcp_log_debug("Client wants SSE: %s", wants_sse ? "yes" : "no");
    if (wants_sse) {
        // Start SSE stream
        mcp_log_info("Starting SSE stream for POST request");

        // TODO: Implement SSE stream response
        // For now, send JSON response
        mcp_log_debug("About to call send_http_json_response (SSE path)");
        int result = send_http_json_response(wsi, response,
                                           session_data->has_session ? session_data->session_id : NULL);
        mcp_log_debug("send_http_json_response returned: %d (SSE path)", result);
        free(response);
        return result;
    } else {
        // Send JSON response
        mcp_log_debug("About to call send_http_json_response (normal path)");
        int result = send_http_json_response(wsi, response,
                                           session_data->has_session ? session_data->session_id : NULL);
        mcp_log_debug("send_http_json_response returned: %d (normal path)", result);
        free(response);
        return result;
    }
}

/**
 * @brief Handle MCP endpoint GET request (SSE stream)
 */
int handle_mcp_get_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data) {
    if (wsi == NULL || data == NULL || session_data == NULL) {
        return -1;
    }

    // Validate origin if required
    if (data->validate_origin && session_data->origin[0] != '\0') {
        if (!validate_origin(data, session_data->origin)) {
            return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Origin not allowed");
        }
    }

    // Check Accept header
    char accept_header[256];
    int accept_len = lws_hdr_copy(wsi, accept_header, sizeof(accept_header), WSI_TOKEN_HTTP_ACCEPT);
    if (accept_len <= 0 || strstr(accept_header, "text/event-stream") == NULL) {
        return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "SSE stream requires Accept: text/event-stream");
    }

    // Initialize SSE stream
    session_data->is_sse_stream = true;

    // Get session if available
    mcp_http_session_t* session = NULL;
    if (session_data->has_session && data->session_manager) {
        session = mcp_session_manager_get_session(data->session_manager, session_data->session_id);
        if (session) {
            session_data->session = session;
            mcp_session_touch(session);
        }
    }

    // Create SSE context for this stream
    size_t max_events = data->config.max_stored_events > 0 ? data->config.max_stored_events : MAX_SSE_STORED_EVENTS_DEFAULT;
    session_data->sse_context = sse_stream_context_create(max_events);
    if (session_data->sse_context == NULL) {
        mcp_log_error("Failed to create SSE context");
        return send_http_error_response(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to initialize SSE stream");
    }

    // Check for Last-Event-ID header for stream resumability
    char last_event_id[HTTP_LAST_EVENT_ID_BUFFER_SIZE];
    if (extract_last_event_id(wsi, last_event_id)) {
        mcp_log_info("SSE stream resuming from event ID: %s", last_event_id);

        // Use session-specific or global SSE context for replay
        sse_stream_context_t* replay_context = session_data->sse_context;
        if (session == NULL && data->global_sse_context) {
            replay_context = data->global_sse_context;
        }

        // We'll replay events after sending headers
    }

    // Prepare SSE response headers
    mcp_log_debug("handle_mcp_get_request: Preparing SSE response headers");
    unsigned char headers[1024];  // Increased buffer size
    unsigned char* p = headers;
    unsigned char* end = headers + sizeof(headers);

    mcp_log_debug("handle_mcp_get_request: Adding HTTP status header (buffer size: %zu)", sizeof(headers));
    int status_result = lws_add_http_header_status(wsi, 200, &p, end);  // Use 200 instead of HTTP_STATUS_OK
    mcp_log_debug("handle_mcp_get_request: lws_add_http_header_status returned: %d", status_result);
    if (status_result) {
        mcp_log_error("handle_mcp_get_request: Failed to add HTTP status header (result: %d)", status_result);
        return -1;
    }
    mcp_log_debug("handle_mcp_get_request: HTTP status header added successfully");

    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                    (unsigned char*)"text/event-stream", 17, &p, end)) {
        mcp_log_error("handle_mcp_get_request: Failed to add Content-Type header");
        return -1;
    }
    mcp_log_debug("handle_mcp_get_request: Content-Type header added successfully");

    if (lws_add_http_header_by_name(wsi, (unsigned char*)"Cache-Control", (unsigned char*)"no-cache", 8, &p, end)) {
        return -1;
    }

    if (lws_add_http_header_by_name(wsi, (unsigned char*)"Connection", (unsigned char*)"keep-alive", 10, &p, end)) {
        return -1;
    }

    // Add session ID header if available
    if (session_data->has_session) {
        if (lws_add_http_header_by_name(wsi, (unsigned char*)MCP_SESSION_HEADER_NAME,
                                       (unsigned char*)session_data->session_id,
                                       (int)strlen(session_data->session_id), &p, end)) {
            return -1;
        }
    }

    // Add CORS headers if enabled
    if (data->enable_cors) {
        add_streamable_cors_headers(wsi, data, &p, end);
    }

    if (lws_finalize_http_header(wsi, &p, end)) {
        return -1;
    }

    // Write headers
    if (lws_write(wsi, headers, p - headers, LWS_WRITE_HTTP_HEADERS) < 0) {
        return -1;
    }

    // Add to dynamic SSE clients list
    if (dynamic_sse_clients_add(data->sse_clients, wsi) != 0) {
        mcp_log_error("Failed to add SSE client to dynamic array");
        return send_http_error_response(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to register SSE client");
    }

    // Replay events if Last-Event-ID was provided
    if (last_event_id[0] != '\0') {
        sse_stream_context_t* replay_context = session_data->sse_context;
        if (session == NULL && data->global_sse_context) {
            replay_context = data->global_sse_context;
        }

        if (replay_context) {
            int replayed = sse_stream_context_replay_events(replay_context, wsi, last_event_id);
            mcp_log_info("Replayed %d events for SSE stream", replayed);
        }
    }

    // Send initial connection event
    char connection_data[256];
    snprintf(connection_data, sizeof(connection_data),
             "{\"type\":\"connection\",\"session_id\":\"%s\",\"timestamp\":%lld}",
             session_data->has_session ? session_data->session_id : "null",
             (long long)time(NULL));

    mcp_log_debug("handle_mcp_get_request: Sending initial connection event: %s", connection_data);
    int sse_result = send_sse_event(wsi, NULL, "connection", connection_data);
    mcp_log_debug("handle_mcp_get_request: send_sse_event returned: %d", sse_result);

    mcp_log_info("SSE stream initialized for %s",
                session_data->has_session ? session_data->session_id : "anonymous client");

    mcp_log_debug("handle_mcp_get_request: Returning 0 (success)");
    return 0;
}

/**
 * @brief Handle MCP endpoint DELETE request (session termination)
 */
int handle_mcp_delete_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data) {
    if (wsi == NULL || data == NULL || session_data == NULL) {
        return -1;
    }

    // Session termination requires a session ID
    if (!session_data->has_session || data->session_manager == NULL) {
        return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Session termination requires a session");
    }

    // Terminate the session
    bool terminated = mcp_session_manager_terminate_session(data->session_manager, session_data->session_id);
    if (terminated) {
        // Send 204 No Content response
        unsigned char headers[256];
        unsigned char* p = headers;
        unsigned char* end = headers + sizeof(headers);

        if (lws_add_http_header_status(wsi, 204, &p, end) ||
            lws_finalize_http_header(wsi, &p, end) ||
            lws_write(wsi, headers, p - headers, LWS_WRITE_HTTP_HEADERS) < 0 ||
            lws_http_transaction_completed(wsi)) {
            return -1;
        }

        mcp_log_info("Session terminated: %s", session_data->session_id);
        return 0;
    } else {
        return send_http_error_response(wsi, HTTP_STATUS_NOT_FOUND, "Session not found");
    }
}

/**
 * @brief Handle OPTIONS request (CORS preflight)
 */
int handle_options_request(struct lws* wsi, sthttp_transport_data_t* data) {
    if (wsi == NULL || data == NULL) {
        return -1;
    }

    // Prepare CORS response headers
    unsigned char headers[512];
    unsigned char* p = headers;
    unsigned char* end = headers + sizeof(headers);

    if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end)) {
        return -1;
    }

    // Add CORS headers if enabled (using optimized approach)
    if (add_optimized_cors_headers(wsi, data, &p, end) != 0) {
        return -1;
    }

    if (lws_finalize_http_header(wsi, &p, end)) {
        return -1;
    }

    // Write headers
    if (lws_write(wsi, headers, p - headers, LWS_WRITE_HTTP_HEADERS) < 0) {
        return -1;
    }

    // Complete transaction
    if (lws_http_transaction_completed(wsi)) {
        return -1;
    }

    return 0;
}
