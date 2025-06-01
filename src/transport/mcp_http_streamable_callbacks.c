#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/http_streamable_transport_internal.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Forward declarations
static int lws_callback_http_streamable(struct lws* wsi, enum lws_callback_reasons reason,
                                       void* user, void* in, size_t len);
static int handle_wsi_create(struct lws* wsi, http_streamable_session_data_t* session);
static int handle_http_request(struct lws* wsi, http_streamable_transport_data_t* data, 
                              http_streamable_session_data_t* session, const char* uri);
static int handle_http_body(struct lws* wsi, http_streamable_session_data_t* session, void* in, size_t len);
static int handle_http_body_completion(struct lws* wsi, http_streamable_transport_data_t* data, 
                                      http_streamable_session_data_t* session);
static int handle_closed_http(struct lws* wsi, http_streamable_transport_data_t* data, 
                             http_streamable_session_data_t* session);
static void extract_origin_header(struct lws* wsi, http_streamable_session_data_t* session);
static void extract_session_header(struct lws* wsi, http_streamable_session_data_t* session);

/**
 * @brief Main HTTP callback function for libwebsockets
 */
static int lws_callback_http_streamable(struct lws* wsi, enum lws_callback_reasons reason,
                                       void* user, void* in, size_t len) {
    if (wsi == NULL) {
        mcp_log_error("Invalid WebSocket instance (NULL)");
        return -1;
    }

    http_streamable_session_data_t* session = (http_streamable_session_data_t*)user;
    http_streamable_transport_data_t* data = (http_streamable_transport_data_t*)lws_context_user(lws_get_context(wsi));

    switch (reason) {
        case LWS_CALLBACK_HTTP_BIND_PROTOCOL:
            // Initialize session data
            return handle_wsi_create(wsi, session);

        case LWS_CALLBACK_HTTP:
            {
                // Main HTTP request handler
                if (in == NULL) {
                    mcp_log_error("Invalid HTTP request (NULL URI)");
                    return -1;
                }

                char* uri = (char*)in;
                mcp_log_info("HTTP streamable request: %s", uri);

                // Extract headers
                extract_origin_header(wsi, session);
                extract_session_header(wsi, session);

                return handle_http_request(wsi, data, session, uri);
            }

        case LWS_CALLBACK_HTTP_BODY:
            // Handle HTTP request body data
            return handle_http_body(wsi, session, in, len);

        case LWS_CALLBACK_HTTP_BODY_COMPLETION:
            // Handle HTTP request body completion
            handle_http_body_completion(wsi, data, session);

            // Now process the POST request with complete body
            mcp_log_debug("Processing completed POST request for URI: %s", session->request_uri);
            if (strcmp(session->request_uri, data->mcp_endpoint) == 0) {
                mcp_log_debug("Calling handle_mcp_endpoint_request for POST with body");
                int result = handle_mcp_endpoint_request(wsi, data, session);
                mcp_log_debug("handle_mcp_endpoint_request returned: %d", result);
                return result;
            }
            mcp_log_debug("URI does not match MCP endpoint: %s", data->mcp_endpoint);
            return 0;

        case LWS_CALLBACK_CLOSED_HTTP:
            // Handle HTTP connection closure
            return handle_closed_http(wsi, data, session);

        default:
            // For other callbacks, use the default dummy handler
            return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
}

/**
 * @brief Handle WebSocket instance creation
 */
static int handle_wsi_create(struct lws* wsi, http_streamable_session_data_t* session) {
    (void)wsi;
    if (session == NULL) {
        mcp_log_error("Session data is NULL");
        return -1;
    }

    // Initialize session data
    memset(session, 0, sizeof(http_streamable_session_data_t));
    session->has_session = false;
    session->is_sse_stream = false;
    session->sse_context = NULL;
    session->request_body = NULL;
    session->request_body_size = 0;
    session->request_body_capacity = 0;
    session->request_uri[0] = '\0';
    session->origin_validated = false;

    mcp_log_debug("Initialized session data for new connection");
    return 0;
}

/**
 * @brief Handle HTTP request
 */
static int handle_http_request(struct lws* wsi, http_streamable_transport_data_t* data,
                              http_streamable_session_data_t* session, const char* uri) {
    if (wsi == NULL || data == NULL || session == NULL || uri == NULL) {
        mcp_log_error("handle_http_request: Invalid parameters");
        return -1;
    }

    // Check if this is the MCP endpoint
    if (strcmp(uri, data->mcp_endpoint) == 0) {
        // For POST requests, we need to wait for the body
        char method[16] = {0};

        // Check for POST method using multiple approaches
        int post_uri_len = lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI);
        int get_uri_len = lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI);

        if (post_uri_len > 0) {
            strncpy(method, "POST", sizeof(method) - 1);
        } else if (get_uri_len > 0) {
            strncpy(method, "GET", sizeof(method) - 1);
        } else {
            // Default to GET if we can't determine the method
            strncpy(method, "GET", sizeof(method) - 1);
        }

        // If this is a POST request, save URI and return 0 to wait for body
        if (strcmp(method, "POST") == 0) {
            strncpy(session->request_uri, uri, sizeof(session->request_uri) - 1);
            session->request_uri[sizeof(session->request_uri) - 1] = '\0';
            return 0; // Tell libwebsockets to expect a body
        }

        // For non-POST requests, handle immediately
        return handle_mcp_endpoint_request(wsi, data, session);
    }

    // Check for legacy endpoints if enabled
    if (data->config.enable_legacy_endpoints) {
        // Handle legacy /call_tool endpoint
        if (strcmp(uri, "/call_tool") == 0) {
            mcp_log_info("Legacy /call_tool endpoint accessed");
            // Redirect to MCP endpoint or handle directly
            return handle_mcp_endpoint_request(wsi, data, session);
        }

        // Handle legacy /events endpoint
        if (strcmp(uri, "/events") == 0) {
            mcp_log_info("Legacy /events endpoint accessed");
            // Handle as SSE stream
            session->is_sse_stream = true;
            return handle_mcp_get_request(wsi, data, session);
        }

        // Handle legacy /tools endpoint
        if (strcmp(uri, "/tools") == 0) {
            mcp_log_info("Legacy /tools endpoint accessed");
            // Return tools discovery response
            const char* tools_json = "{\"tools\":[]}"; // Placeholder
            return send_http_json_response(wsi, tools_json, NULL);
        }
    }

    // Handle static files if doc_root is configured
    if (data->config.doc_root != NULL) {
        // TODO: Implement static file serving
        mcp_log_debug("Static file request: %s", uri);
    }

    // Default: 404 Not Found
    mcp_log_warn("No handler found for URI: %s", uri);
    return send_http_error_response(wsi, HTTP_STATUS_NOT_FOUND, "Not Found");
}

/**
 * @brief Handle HTTP request body data
 */
static int handle_http_body(struct lws* wsi, http_streamable_session_data_t* session, void* in, size_t len) {
    (void)wsi;
    if (session == NULL || in == NULL || len == 0) {
        return 0; // Not an error, just no data
    }

    // Allocate or expand request body buffer
    size_t new_size = session->request_body_size + len;
    if (new_size > session->request_body_capacity) {
        size_t new_capacity = new_size * 2; // Double the capacity
        if (new_capacity < 1024) {
            new_capacity = 1024; // Minimum capacity
        }

        char* new_buffer = (char*)realloc(session->request_body, new_capacity);
        if (new_buffer == NULL) {
            mcp_log_error("Failed to allocate memory for request body");
            return -1;
        }

        session->request_body = new_buffer;
        session->request_body_capacity = new_capacity;
    }

    // Append the new data
    memcpy(session->request_body + session->request_body_size, in, len);
    session->request_body_size += len;

    mcp_log_debug("Received %zu bytes of request body (total: %zu)", len, session->request_body_size);
    return 0;
}

/**
 * @brief Handle HTTP request body completion
 */
static int handle_http_body_completion(struct lws* wsi, http_streamable_transport_data_t* data, 
                                      http_streamable_session_data_t* session) {
    if (session == NULL) {
        return -1;
    }

    // Null-terminate the request body if we have one
    if (session->request_body != NULL && session->request_body_size > 0) {
        // Ensure we have space for null terminator
        if (session->request_body_size >= session->request_body_capacity) {
            char* new_buffer = (char*)realloc(session->request_body, session->request_body_size + 1);
            if (new_buffer == NULL) {
                mcp_log_error("Failed to allocate memory for null terminator");
                return -1;
            }
            session->request_body = new_buffer;
            session->request_body_capacity = session->request_body_size + 1;
        }

        session->request_body[session->request_body_size] = '\0';
        mcp_log_debug("Request body completed: %zu bytes", session->request_body_size);
    }

    // The actual request processing will be handled by the HTTP callback
    // This is just notification that the body is complete
    (void)wsi;
    (void)data;
    return 0;
}

/**
 * @brief Handle HTTP connection closure
 */
static int handle_closed_http(struct lws* wsi, http_streamable_transport_data_t* data, 
                             http_streamable_session_data_t* session) {
    if (session == NULL) {
        return 0;
    }

    // Clean up session data
    if (session->request_body) {
        free(session->request_body);
        session->request_body = NULL;
        session->request_body_size = 0;
        session->request_body_capacity = 0;
    }

    if (session->sse_context) {
        sse_stream_context_destroy(session->sse_context);
        session->sse_context = NULL;
    }

    // Remove from SSE clients list if this was an SSE connection
    if (session->is_sse_stream && data != NULL) {
        mcp_mutex_lock(data->sse_mutex);

        for (size_t i = 0; i < data->sse_client_count; i++) {
            if (data->sse_clients[i] == wsi) {
                // Remove this client by shifting the array
                for (size_t j = i; j < data->sse_client_count - 1; j++) {
                    data->sse_clients[j] = data->sse_clients[j + 1];
                }
                data->sse_client_count--;
                break;
            }
        }

        mcp_mutex_unlock(data->sse_mutex);
        mcp_log_debug("Removed SSE client from list");
    }

    mcp_log_debug("HTTP connection closed and cleaned up");
    return 0;
}

/**
 * @brief Extract Origin header
 */
static void extract_origin_header(struct lws* wsi, http_streamable_session_data_t* session) {
    if (wsi == NULL || session == NULL) {
        return;
    }

    int origin_len = lws_hdr_copy(wsi, session->origin, sizeof(session->origin), WSI_TOKEN_ORIGIN);
    if (origin_len > 0) {
        mcp_log_debug("Origin header: %s", session->origin);
    } else {
        session->origin[0] = '\0';
    }
}

/**
 * @brief Extract session header
 */
static void extract_session_header(struct lws* wsi, http_streamable_session_data_t* session) {
    if (wsi == NULL || session == NULL) {
        return;
    }

    // Extract session ID from Mcp-Session-Id header
    session->has_session = extract_session_id(wsi, session->session_id);

    if (session->has_session) {
        mcp_log_info("Request has session ID: %s", session->session_id);
    } else {
        mcp_log_debug("Request has no session ID");
    }
}

// LWS protocols for streamable HTTP transport
struct lws_protocols http_streamable_protocols[] = {
    {
        "http-streamable",
        lws_callback_http_streamable,
        sizeof(http_streamable_session_data_t),
        4096,  // rx buffer size - increased for POST body handling
    },
    { NULL, NULL, 0, 0 } // terminator
};
