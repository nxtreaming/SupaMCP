#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/http_transport_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// HTTP content types
#define HTTP_CONTENT_TYPE_HTML "text/html"
#define HTTP_CONTENT_TYPE_EVENT_STREAM "text/event-stream"

// HTTP headers
#define HTTP_HEADER_CACHE_CONTROL "Cache-Control"
#define HTTP_HEADER_CONNECTION "Connection"
#define HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN "Access-Control-Allow-Origin"
#define HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS "Access-Control-Allow-Methods"
#define HTTP_HEADER_ACCESS_CONTROL_ALLOW_HEADERS "Access-Control-Allow-Headers"
#define HTTP_HEADER_ACCESS_CONTROL_MAX_AGE "Access-Control-Max-Age"
#define HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS "Access-Control-Allow-Credentials"

// HTTP header values
#define HTTP_HEADER_VALUE_NO_CACHE "no-cache"
#define HTTP_HEADER_VALUE_KEEP_ALIVE "keep-alive"
#define HTTP_HEADER_VALUE_TRUE "true"

// SSE event fields
#define SSE_FIELD_EVENT "event: "
#define SSE_FIELD_ID "id: "
#define SSE_FIELD_DATA "data: "

// URL query parameters
#define URL_PARAM_LAST_EVENT_ID "lastEventId="
#define URL_PARAM_FILTER "filter="
#define URL_PARAM_SESSION_ID "session_id="

// Buffer sizes
#define HTTP_BUFFER_SIZE 1024
#define HTTP_QUERY_BUFFER_SIZE 256
#define HTTP_SESSION_ID_BUFFER_SIZE 256
#define HTTP_MAX_AGE_BUFFER_SIZE 16

// Forward declarations
static char* url_decode(const char* src);
static char* extract_query_param(const char* query, const char* param_name);
static void replay_stored_events(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session);

/**
 * @brief Root path handler for HTTP server
 *
 * This function handles requests to the root path ("/") of the HTTP server.
 * It serves a simple HTML page with information about the server and available tools.
 *
 * @param wsi WebSocket instance
 * @param reason Callback reason
 * @param user User data
 * @param in Input data
 * @param len Length of input data
 * @return int 0 on success, non-zero on failure
 */
int lws_root_handler(struct lws* wsi, enum lws_callback_reasons reason,
                    void* user, void* in, size_t len) {
    (void)user;
    (void)len;

    mcp_log_debug("Root handler: reason=%d", reason);

    if (reason == LWS_CALLBACK_PROTOCOL_INIT) {
        mcp_log_info("Root handler: Protocol initialized");
        return 0;
    }

    if (reason == LWS_CALLBACK_HTTP) {
        if (wsi == NULL || in == NULL) {
            mcp_log_error("Root handler: Invalid parameters");
            return -1;
        }

        char* uri = (char*)in;
        mcp_log_info("Root handler: HTTP request: %s", uri);

        // Only handle the root path ("/")
        if (strcmp(uri, "/") != 0) {
            mcp_log_debug("Root handler: Not root path, passing to next handler");
            return -1; // Return -1 to let libwebsockets try the next protocol handler
        }

        mcp_log_info("Root handler: Serving root page");

        // Prepare response headers
        unsigned char buffer[LWS_PRE + HTTP_BUFFER_SIZE];
        unsigned char* p = &buffer[LWS_PRE];
        unsigned char* end = &buffer[sizeof(buffer) - 1];

        // Add headers
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                       HTTP_CONTENT_TYPE_HTML,
                                       LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
            mcp_log_error("Root handler: Failed to add HTTP headers");
            return -1;
        }

        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
            mcp_log_error("Root handler: Failed to finalize HTTP headers");
            return -1;
        }

        // Create a simple HTML page
        const char* html =
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>MCP HTTP Server</title>\n"
            "    <style>\n"
            "        body { font-family: Arial, sans-serif; margin: 20px; }\n"
            "        h1 { color: #333; }\n"
            "        pre { background-color: #f5f5f5; padding: 10px; border-radius: 5px; }\n"
            "    </style>\n"
            "</head>\n"
            "<body>\n"
            "    <h1>MCP HTTP Server</h1>\n"
            "    <p>This is a test page created by the MCP HTTP server.</p>\n"
            "    <h2>Available Tools:</h2>\n"
            "    <ul>\n"
            "        <li><strong>echo</strong> - Echoes back the input text</li>\n"
            "        <li><strong>reverse</strong> - Reverses the input text</li>\n"
            "    </ul>\n"
            "    <h2>Tool Call Example:</h2>\n"
            "    <pre>curl -X POST http://127.0.0.1:8180/call_tool -H \"Content-Type: application/json\" -d \"{\\\"name\\\":\\\"echo\\\",\\\"params\\\":{\\\"text\\\":\\\"Hello, MCP Server!\\\"}}\"</pre>\n"
            "</body>\n"
            "</html>\n";

        // Write response body
        int bytes_written = lws_write(wsi, (unsigned char*)html, strlen(html), LWS_WRITE_HTTP);
        if (bytes_written < 0) {
            mcp_log_error("Root handler: Failed to write HTTP response body");
            return -1;
        }

        mcp_log_debug("Root handler: Wrote %d bytes", bytes_written);

        // Complete HTTP transaction
        int should_close = lws_http_transaction_completed(wsi);
        if (should_close) {
            mcp_log_debug("HTTP handler: Transaction completed, connection will close");
        }

        return 0;
    }

    // For all other callbacks, return 0 to indicate success
    return 0;
}

/**
 * @brief Add CORS headers to HTTP response
 *
 * This function adds Cross-Origin Resource Sharing (CORS) headers to an HTTP response
 * based on the transport configuration.
 *
 * @param wsi WebSocket instance
 * @param data Transport data containing CORS configuration
 * @param p Pointer to the current position in the header buffer
 * @param end Pointer to the end of the header buffer
 * @return int 0 on success, -1 on failure
 */
int add_cors_headers(struct lws* wsi, http_transport_data_t* data,
                    unsigned char** p, unsigned char* end) {
    if (wsi == NULL || data == NULL || p == NULL || *p == NULL || end == NULL) {
        mcp_log_error("Invalid parameters for add_cors_headers");
        return -1;
    }

    // Check if CORS is enabled
    if (!data->enable_cors) {
        mcp_log_debug("CORS is disabled, no headers added");
        return 0;
    }

    mcp_log_debug("Adding CORS headers");

    // Add Access-Control-Allow-Origin header
    if (data->cors_allow_origin != NULL) {
        if (lws_add_http_header_by_name(wsi,
                                       (unsigned char*)HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN,
                                       (unsigned char*)data->cors_allow_origin,
                                       (int)strlen(data->cors_allow_origin),
                                       p, end)) {
            mcp_log_error("Failed to add Access-Control-Allow-Origin header");
            return -1;
        }
    }

    // Add Access-Control-Allow-Methods header
    if (data->cors_allow_methods != NULL) {
        if (lws_add_http_header_by_name(wsi,
                                       (unsigned char*)HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS,
                                       (unsigned char*)data->cors_allow_methods,
                                       (int)strlen(data->cors_allow_methods),
                                       p, end)) {
            mcp_log_error("Failed to add Access-Control-Allow-Methods header");
            return -1;
        }
    }

    // Add Access-Control-Allow-Headers header
    if (data->cors_allow_headers != NULL) {
        if (lws_add_http_header_by_name(wsi,
                                       (unsigned char*)HTTP_HEADER_ACCESS_CONTROL_ALLOW_HEADERS,
                                       (unsigned char*)data->cors_allow_headers,
                                       (int)strlen(data->cors_allow_headers),
                                       p, end)) {
            mcp_log_error("Failed to add Access-Control-Allow-Headers header");
            return -1;
        }
    }

    // Add Access-Control-Max-Age header
    char max_age_str[HTTP_MAX_AGE_BUFFER_SIZE];
    int max_age_len = snprintf(max_age_str, sizeof(max_age_str), "%d", data->cors_max_age);
    if (max_age_len < 0 || max_age_len >= (int)sizeof(max_age_str)) {
        mcp_log_error("Failed to format Access-Control-Max-Age value");
        return -1;
    }

    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)HTTP_HEADER_ACCESS_CONTROL_MAX_AGE,
                                   (unsigned char*)max_age_str,
                                   max_age_len,
                                   p, end)) {
        mcp_log_error("Failed to add Access-Control-Max-Age header");
        return -1;
    }

    // Add Access-Control-Allow-Credentials header
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS,
                                   (unsigned char*)HTTP_HEADER_VALUE_TRUE,
                                   (int)strlen(HTTP_HEADER_VALUE_TRUE),
                                   p, end)) {
        mcp_log_error("Failed to add Access-Control-Allow-Credentials header");
        return -1;
    }

    mcp_log_debug("CORS headers added successfully");
    return 0;
}

/**
 * @brief URL decode a string
 *
 * This function decodes URL-encoded strings, handling %xx escapes and + as space.
 *
 * @param src The URL-encoded string to decode
 * @return char* The decoded string (caller must free) or NULL on failure
 */
static char* url_decode(const char* src) {
    if (src == NULL) {
        return NULL;
    }

    // Allocate memory for the decoded string (will be at most as long as the source)
    size_t src_len = strlen(src);
    char* decoded = (char*)malloc(src_len + 1);
    if (decoded == NULL) {
        mcp_log_error("Failed to allocate memory for URL decoding");
        return NULL;
    }

    // Decode the string
    const char* src_ptr = src;
    char* dst_ptr = decoded;

    while (*src_ptr) {
        if (*src_ptr == '%' && src_ptr[1] && src_ptr[2]) {
            // Handle %xx escape
            char hex[3] = {src_ptr[1], src_ptr[2], 0};
            *dst_ptr = (char)strtol(hex, NULL, 16);
            src_ptr += 3;
        } else if (*src_ptr == '+') {
            // Handle + as space
            *dst_ptr = ' ';
            src_ptr++;
        } else {
            // Copy character as-is
            *dst_ptr = *src_ptr;
            src_ptr++;
        }
        dst_ptr++;
    }

    // Null-terminate the decoded string
    *dst_ptr = '\0';

    return decoded;
}

/**
 * @brief Extract a parameter value from a query string
 *
 * This function extracts the value of a parameter from a query string.
 *
 * @param query The query string to parse
 * @param param_name The name of the parameter to extract
 * @return char* The parameter value (caller must free) or NULL if not found
 */
static char* extract_query_param(const char* query, const char* param_name) {
    if (query == NULL || param_name == NULL || *query == '\0' || *param_name == '\0') {
        return NULL;
    }

    // Build the parameter prefix (param_name=)
    size_t prefix_len = strlen(param_name);
    char* prefix = (char*)malloc(prefix_len + 2); // +2 for '=' and '\0'
    if (prefix == NULL) {
        mcp_log_error("Failed to allocate memory for parameter prefix");
        return NULL;
    }

    sprintf(prefix, "%s=", param_name);

    // Find the parameter in the query string
    const char* param_start = strstr(query, prefix);
    free(prefix); // Free the prefix buffer

    if (param_start == NULL) {
        return NULL; // Parameter not found
    }

    // Skip the parameter name and '='
    param_start += prefix_len + 1;

    // Find the end of the parameter value
    const char* param_end = strchr(param_start, '&');

    // Extract the parameter value
    if (param_end != NULL) {
        // Parameter is not the last one in the query string
        size_t value_len = param_end - param_start;
        char* value = (char*)malloc(value_len + 1);
        if (value == NULL) {
            mcp_log_error("Failed to allocate memory for parameter value");
            return NULL;
        }

        strncpy(value, param_start, value_len);
        value[value_len] = '\0';
        return value;
    } else {
        // Parameter is the last one in the query string
        return mcp_strdup(param_start);
    }
}

/**
 * @brief Replay stored SSE events to a client
 *
 * This function replays stored SSE events to a client that has reconnected with a Last-Event-ID.
 *
 * @param wsi WebSocket instance
 * @param data Transport data
 * @param session Session data
 */
static void replay_stored_events(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
    if (wsi == NULL || data == NULL || session == NULL || session->last_event_id <= 0) {
        return;
    }

    mcp_log_info("Replaying missed events for client with Last-Event-ID: %d", session->last_event_id);

    // Lock the event mutex to safely access the stored events
    mcp_mutex_lock(data->event_mutex);

    // Check if there are any stored events
    if (data->stored_event_count <= 0) {
        mcp_log_debug("No stored events to replay");
        mcp_mutex_unlock(data->event_mutex);
        return;
    }

    // Iterate through the circular buffer to find events with ID greater than last_event_id
    int current = data->event_head;
    int count = 0;
    int replayed_count = 0;

    // Process all events in the buffer
    while (count < data->stored_event_count) {
        // Get the event ID
        int event_id = 0;
        if (data->stored_events[current].id != NULL) {
            event_id = atoi(data->stored_events[current].id);
        }

        // Only send events with ID greater than last_event_id
        if (event_id > session->last_event_id) {
            // Check if the event matches the filter (if any)
            bool should_send = true;

            if (session->event_filter != NULL && data->stored_events[current].event != NULL) {
                if (strcmp(session->event_filter, data->stored_events[current].event) != 0) {
                    should_send = false;
                }
            }

            // Send the event if it passes the filter
            if (should_send) {
                // Replay the event
                if (data->stored_events[current].event != NULL) {
                    // Write event type
                    lws_write_http(wsi, SSE_FIELD_EVENT, strlen(SSE_FIELD_EVENT));
                    lws_write_http(wsi, data->stored_events[current].event,
                                  strlen(data->stored_events[current].event));
                    lws_write_http(wsi, "\n", 1);
                }

                // Write event ID
                lws_write_http(wsi, SSE_FIELD_ID, strlen(SSE_FIELD_ID));
                lws_write_http(wsi, data->stored_events[current].id,
                              strlen(data->stored_events[current].id));
                lws_write_http(wsi, "\n", 1);

                // Write event data
                lws_write_http(wsi, SSE_FIELD_DATA, strlen(SSE_FIELD_DATA));
                lws_write_http(wsi, data->stored_events[current].data,
                              strlen(data->stored_events[current].data));
                lws_write_http(wsi, "\n\n", 2);

                // Request a callback when the socket is writable again
                lws_callback_on_writable(wsi);

                replayed_count++;
            }
        }

        // Move to the next event in the circular buffer
        current = (current + 1) % MAX_SSE_STORED_EVENTS;
        count++;
    }

    mcp_mutex_unlock(data->event_mutex);

    mcp_log_info("Replayed %d events to client", replayed_count);
}

/**
 * @brief Handle SSE (Server-Sent Events) request
 *
 * This function processes an SSE request, sets up the SSE connection,
 * and handles event replay for reconnecting clients.
 *
 * @param wsi WebSocket instance
 * @param data Transport data
 */
void handle_sse_request(struct lws* wsi, http_transport_data_t* data) {
    if (wsi == NULL || data == NULL) {
        mcp_log_error("Invalid parameters for handle_sse_request");
        return;
    }

    // Get session data
    http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);
    if (session == NULL) {
        mcp_log_error("No session data for SSE request");
        return;
    }

    mcp_log_info("Handling SSE request");

    // Log session data
    mcp_log_debug("SSE request - session data: is_sse_client=%d, session_id=%s",
                 session->is_sse_client,
                 session->session_id ? session->session_id : "NULL");

    // Prepare response headers
    unsigned char buffer[LWS_PRE + HTTP_BUFFER_SIZE];
    unsigned char* p = &buffer[LWS_PRE];
    unsigned char* end = &buffer[sizeof(buffer) - 1];

    // Add common headers
    if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                   HTTP_CONTENT_TYPE_EVENT_STREAM,
                                   LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
        mcp_log_error("Failed to add common HTTP headers for SSE");
        return;
    }

    // Add SSE specific headers
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)HTTP_HEADER_CACHE_CONTROL,
                                   (unsigned char*)HTTP_HEADER_VALUE_NO_CACHE,
                                   (int)strlen(HTTP_HEADER_VALUE_NO_CACHE),
                                   &p, end)) {
        mcp_log_error("Failed to add Cache-Control header for SSE");
        return;
    }

    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)HTTP_HEADER_CONNECTION,
                                   (unsigned char*)HTTP_HEADER_VALUE_KEEP_ALIVE,
                                   (int)strlen(HTTP_HEADER_VALUE_KEEP_ALIVE),
                                   &p, end)) {
        mcp_log_error("Failed to add Connection header for SSE");
        return;
    }

    // Add CORS headers
    if (add_cors_headers(wsi, data, &p, end) != 0) {
        mcp_log_error("Failed to add CORS headers for SSE");
        return;
    }

    // Finalize headers
    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
        mcp_log_error("Failed to finalize HTTP headers for SSE");
        return;
    }

    // Mark as SSE connection
    lws_http_mark_sse(wsi);

    // Mark as SSE client
    session->is_sse_client = true;
    session->last_event_id = 0;

    // Get query string
    char query[HTTP_QUERY_BUFFER_SIZE] = {0};
    int query_len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);

    if (query_len > 0 && query_len < (int)sizeof(query)) {
        if (lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS) < 0) {
            mcp_log_error("Failed to copy query string for SSE request");
            return;
        }

        mcp_log_debug("SSE request query string: '%s'", query);

        // Extract Last-Event-ID from query string
        char* last_event_id = extract_query_param(query, "lastEventId");
        if (last_event_id != NULL) {
            session->last_event_id = atoi(last_event_id);
            mcp_log_info("SSE client reconnected with Last-Event-ID: %d", session->last_event_id);
            free(last_event_id);
        }

        // Extract event filter from query string
        char* filter = extract_query_param(query, "filter");
        if (filter != NULL) {
            // Free any existing filter
            if (session->event_filter != NULL) {
                free(session->event_filter);
            }

            session->event_filter = filter; // Transfer ownership
            mcp_log_info("SSE client connected with event filter: %s", session->event_filter);
        }

        // Extract session ID from query string
        char* session_id = extract_query_param(query, "session_id");
        if (session_id != NULL) {
            // Free any existing session ID
            if (session->session_id != NULL) {
                free(session->session_id);
            }

            // URL decode the session ID if needed
            char* decoded_session_id = url_decode(session_id);
            if (decoded_session_id != NULL && strcmp(session_id, decoded_session_id) != 0) {
                mcp_log_debug("URL decoded session_id: '%s' -> '%s'", session_id, decoded_session_id);
                session->session_id = decoded_session_id; // Transfer ownership
                free(session_id);
            } else {
                // Use the original session ID
                session->session_id = session_id; // Transfer ownership
                free(decoded_session_id); // Free the decoded version if it was created
            }

            mcp_log_info("SSE client connected with session ID: '%s'", session->session_id);
        } else {
            mcp_log_debug("SSE client connected without session ID");
        }
    } else {
        mcp_log_debug("SSE request has no query string (len=%d)", query_len);
    }

    // Add to SSE clients list
    mcp_mutex_lock(data->sse_mutex);

    if (data->sse_client_count < MAX_SSE_CLIENTS) {
        data->sse_clients[data->sse_client_count++] = wsi;

        // Log the client connection with session details
        mcp_log_info("Added SSE client #%d - session_id: %s, filter: %s",
                    data->sse_client_count,
                    session->session_id ? session->session_id : "NULL",
                    session->event_filter ? session->event_filter : "ALL");
    } else {
        mcp_log_error("Maximum number of SSE clients (%d) reached, rejecting connection", MAX_SSE_CLIENTS);
        mcp_mutex_unlock(data->sse_mutex);
        return;
    }

    mcp_mutex_unlock(data->sse_mutex);

    // Send initial SSE message
    const char* initial_msg = "data: connected\n\n";
    int bytes_written = lws_write(wsi, (unsigned char*)initial_msg, strlen(initial_msg), LWS_WRITE_HTTP);
    if (bytes_written < 0) {
        mcp_log_error("Failed to write initial SSE message");
        return;
    }

    // If client reconnected with Last-Event-ID, replay missed events
    if (session->last_event_id > 0) {
        replay_stored_events(wsi, data, session);
    }
}
