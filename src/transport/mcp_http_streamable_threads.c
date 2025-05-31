#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#include "win_socket_compat.h"
#endif

#include "internal/http_streamable_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/**
 * @brief Thread function for HTTP event processing
 */
void* http_streamable_event_thread_func(void* arg) {
    if (arg == NULL) {
        mcp_log_error("Invalid argument for HTTP streamable event thread");
        return NULL;
    }

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;

    if (data == NULL) {
        mcp_log_error("Invalid transport data for HTTP streamable event thread");
        return NULL;
    }

    mcp_log_info("HTTP streamable event thread started");

    time_t last_heartbeat = time(NULL);

    while (data->running) {
        // Service libwebsockets
        if (data->context) {
            int service_result = lws_service(data->context, HTTP_STREAMABLE_LWS_SERVICE_TIMEOUT_MS);
            if (service_result < 0) {
                mcp_log_error("lws_service failed: %d", service_result);
                break;
            }
        }

        // Send heartbeats if enabled
        if (data->send_heartbeats) {
            time_t current_time = time(NULL);
            if ((current_time - last_heartbeat) * 1000 >= (time_t)data->heartbeat_interval_ms) {
                mcp_mutex_lock(data->sse_mutex);

                // Send heartbeat to all connected SSE clients
                for (size_t i = 0; i < data->sse_client_count; i++) {
                    if (data->sse_clients[i] != NULL) {
                        send_sse_heartbeat_to_wsi(data->sse_clients[i]);
                    }
                }

                data->last_heartbeat_time = current_time;
                data->heartbeat_counter++;

                mcp_mutex_unlock(data->sse_mutex);

                last_heartbeat = current_time;
                mcp_log_debug("Sent heartbeat to %zu SSE clients", data->sse_client_count);
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
void* http_streamable_cleanup_thread_func(void* arg) {
    if (arg == NULL) {
        mcp_log_error("Invalid argument for HTTP streamable cleanup thread");
        return NULL;
    }

    mcp_transport_t* transport = (mcp_transport_t*)arg;
    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)transport->transport_data;

    if (data == NULL) {
        mcp_log_error("Invalid transport data for HTTP streamable cleanup thread");
        return NULL;
    }

    mcp_log_info("HTTP streamable cleanup thread started");

    while (data->running) {
        // Sleep for the cleanup interval
        for (int i = 0; i < HTTP_STREAMABLE_CLEANUP_INTERVAL_SECONDS && data->running; i++) {
            mcp_sleep_ms(1000);
        }

        if (!data->running) {
            break;
        }

        // Clean up expired sessions
        if (data->session_manager) {
            size_t cleaned_count = mcp_session_manager_cleanup_expired(data->session_manager);
            if (cleaned_count > 0) {
                mcp_log_info("Cleanup thread removed %zu expired sessions", cleaned_count);
            }
        }

        // Clean up disconnected SSE clients
        mcp_mutex_lock(data->sse_mutex);

        size_t active_clients = 0;
        for (size_t i = 0; i < data->sse_client_count; i++) {
            if (data->sse_clients[i] != NULL) {
                // Check if client is still connected
                // This is a simplified check - in a real implementation,
                // we would need to track client state more carefully
                active_clients++;
            }
        }

        if (active_clients != data->sse_client_count) {
            mcp_log_debug("Cleanup thread found %zu active SSE clients (was %zu)", 
                         active_clients, data->sse_client_count);
            data->sse_client_count = active_clients;
        }

        mcp_mutex_unlock(data->sse_mutex);
    }

    mcp_log_info("HTTP streamable cleanup thread stopped");
    return NULL;
}

/**
 * @brief Process JSON-RPC request and generate response
 */
char* process_jsonrpc_request(http_streamable_transport_data_t* data, const char* request_json, const char* session_id) {
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
int handle_mcp_endpoint_request(struct lws* wsi, http_streamable_transport_data_t* data, http_streamable_session_data_t* session_data) {
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
int handle_mcp_post_request(struct lws* wsi, http_streamable_transport_data_t* data, http_streamable_session_data_t* session_data) {
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
    if (session_data->request_body == NULL || session_data->request_body_size == 0) {
        mcp_log_error("No request body for POST request");
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
    
    bool wants_sse = (accept_len > 0 && strstr(accept_header, "text/event-stream") != NULL);

    if (wants_sse) {
        // Start SSE stream
        mcp_log_info("Starting SSE stream for POST request");
        
        // TODO: Implement SSE stream response
        // For now, send JSON response
        int result = send_http_json_response(wsi, response, 
                                           session_data->has_session ? session_data->session_id : NULL);
        free(response);
        return result;
    } else {
        // Send JSON response
        int result = send_http_json_response(wsi, response, 
                                           session_data->has_session ? session_data->session_id : NULL);
        free(response);
        return result;
    }
}

/**
 * @brief Handle MCP endpoint GET request (SSE stream)
 */
int handle_mcp_get_request(struct lws* wsi, http_streamable_transport_data_t* data, http_streamable_session_data_t* session_data) {
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
    unsigned char headers[512];
    unsigned char* p = headers;
    unsigned char* end = headers + sizeof(headers);

    if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end)) {
        return -1;
    }

    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                    (unsigned char*)"text/event-stream", 18, &p, end)) {
        return -1;
    }

    if (lws_add_http_header_by_name(wsi, (unsigned char*)"Cache-Control",
                                   (unsigned char*)"no-cache", 8, &p, end)) {
        return -1;
    }

    if (lws_add_http_header_by_name(wsi, (unsigned char*)"Connection",
                                   (unsigned char*)"keep-alive", 10, &p, end)) {
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

    // Add to SSE clients list
    mcp_mutex_lock(data->sse_mutex);

    // Find a free slot
    bool added = false;
    for (size_t i = 0; i < data->max_sse_clients; i++) {
        if (data->sse_clients[i] == NULL) {
            data->sse_clients[i] = wsi;
            data->sse_client_count++;
            added = true;
            break;
        }
    }

    mcp_mutex_unlock(data->sse_mutex);

    if (!added) {
        mcp_log_warn("SSE client limit reached (%zu)", data->max_sse_clients);
        return send_http_error_response(wsi, HTTP_STATUS_SERVICE_UNAVAILABLE, "SSE client limit reached");
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

    send_sse_event(wsi, NULL, "connection", connection_data);

    mcp_log_info("SSE stream initialized for %s",
                session_data->has_session ? session_data->session_id : "anonymous client");

    return 0;
}

/**
 * @brief Handle MCP endpoint DELETE request (session termination)
 */
int handle_mcp_delete_request(struct lws* wsi, http_streamable_transport_data_t* data, http_streamable_session_data_t* session_data) {
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
int handle_options_request(struct lws* wsi, http_streamable_transport_data_t* data) {
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

    // Add CORS headers if enabled
    if (data->enable_cors) {
        if (lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Allow-Origin",
                                       (unsigned char*)data->cors_allow_origin, 
                                       (int)strlen(data->cors_allow_origin), &p, end)) {
            return -1;
        }

        if (lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Allow-Methods",
                                       (unsigned char*)data->cors_allow_methods, 
                                       (int)strlen(data->cors_allow_methods), &p, end)) {
            return -1;
        }

        if (lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Allow-Headers",
                                       (unsigned char*)data->cors_allow_headers, 
                                       (int)strlen(data->cors_allow_headers), &p, end)) {
            return -1;
        }

        char max_age_str[32];
        snprintf(max_age_str, sizeof(max_age_str), "%d", data->cors_max_age);
        if (lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Max-Age",
                                       (unsigned char*)max_age_str, 
                                       (int)strlen(max_age_str), &p, end)) {
            return -1;
        }
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
