#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include <win_socket_compat.h>

// On Windows, strcasecmp is _stricmp
#define strcasecmp _stricmp
#endif

#include "internal/mcp_http_transport_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Root path handler
int lws_root_handler(struct lws* wsi, enum lws_callback_reasons reason,
               void* user, void* in, size_t len) {
    (void)user; // Unused parameter
    (void)len;  // Unused parameter
    mcp_log_debug("Root handler: reason=%d", reason);

    // Handle protocol initialization
    if (reason == LWS_CALLBACK_PROTOCOL_INIT) {
        mcp_log_info("Root handler: Protocol init");
        return 0; // Return 0 to indicate success
    }

    // Handle HTTP requests
    if (reason == LWS_CALLBACK_HTTP) {
        char* uri = (char*)in;
        mcp_log_info("Root handler: HTTP request: %s", uri);

        // Only handle the root path ("/")
        if (strcmp(uri, "/") != 0) {
            mcp_log_info("Root handler: Not root path, passing to next handler");
            return -1; // Return -1 to let libwebsockets try the next protocol handler
        }

        mcp_log_info("Root handler: Serving root page");

        // Prepare response headers
        unsigned char buffer[LWS_PRE + 1024];
        unsigned char* p = &buffer[LWS_PRE];
        unsigned char* end = &buffer[sizeof(buffer) - 1];

        // Add headers
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                       "text/html",
                                       LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
            return -1;
        }

        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
            return -1;
        }

        // Create a simple HTML page
        const char* html =
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>MCP HTTP Server</title>\n"
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
        lws_write(wsi, (unsigned char*)html, strlen(html), LWS_WRITE_HTTP);

        // Complete HTTP transaction
        lws_http_transaction_completed(wsi);
        return 0;
    }

    // For all other callbacks, return 0 to indicate success
    return 0;
}

// Add CORS headers to HTTP response
int add_cors_headers(struct lws* wsi, http_transport_data_t* data,
                   unsigned char** p, unsigned char* end) {
    if (!data->enable_cors) {
        return 0; // CORS is disabled, no headers to add
    }

    // Add Access-Control-Allow-Origin header
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Access-Control-Allow-Origin",
                                   (unsigned char*)data->cors_allow_origin,
                                   (int)strlen(data->cors_allow_origin),
                                   p, end)) {
        return -1;
    }

    // Add Access-Control-Allow-Methods header
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Access-Control-Allow-Methods",
                                   (unsigned char*)data->cors_allow_methods,
                                   (int)strlen(data->cors_allow_methods),
                                   p, end)) {
        return -1;
    }

    // Add Access-Control-Allow-Headers header
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Access-Control-Allow-Headers",
                                   (unsigned char*)data->cors_allow_headers,
                                   (int)strlen(data->cors_allow_headers),
                                   p, end)) {
        return -1;
    }

    // Add Access-Control-Max-Age header
    char max_age_str[16];
    snprintf(max_age_str, sizeof(max_age_str), "%d", data->cors_max_age);
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Access-Control-Max-Age",
                                   (unsigned char*)max_age_str,
                                   (int)strlen(max_age_str),
                                   p, end)) {
        return -1;
    }

    // Add Access-Control-Allow-Credentials header
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Access-Control-Allow-Credentials",
                                   (unsigned char*)"true", 4,
                                   p, end)) {
        return -1;
    }

    return 0;
}

// Handle SSE request
void handle_sse_request(struct lws* wsi, http_transport_data_t* data) {
    // Get session data
    http_session_data_t* session_data = (http_session_data_t*)lws_wsi_user(wsi);
    if (!session_data) {
        mcp_log_error("No session data for SSE request");
        return;
    }

    // Log session data
    mcp_log_debug("SSE request - session data: is_sse_client=%d, session_id=%s",
                 session_data->is_sse_client,
                 session_data->session_id ? session_data->session_id : "NULL");

    // Prepare response headers
    unsigned char buffer[LWS_PRE + 1024];
    unsigned char* p = &buffer[LWS_PRE];
    unsigned char* end = &buffer[sizeof(buffer) - 1];

    // Add headers
    if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                   "text/event-stream",
                                   LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
        return;
    }

    // Add SSE specific headers
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Cache-Control",
                                   (unsigned char*)"no-cache", 8, &p, end)) {
        return;
    }

    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Connection",
                                   (unsigned char*)"keep-alive", 10, &p, end)) {
        return;
    }

    // Add CORS headers
    if (add_cors_headers(wsi, data, &p, end) != 0) {
        mcp_log_error("Failed to add CORS headers for SSE");
        return;
    }

    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
        return;
    }

    // Mark as SSE connection
    lws_http_mark_sse(wsi);

    // Mark as SSE client
    session_data->is_sse_client = true;
    session_data->last_event_id = 0;

    // Check for Last-Event-ID header
    // libwebsockets doesn't have a direct token for Last-Event-ID, so we need to use a custom header
    char last_event_id[32] = {0};

    // Try to get the Last-Event-ID from the query string first
    char query[256] = {0};
    int len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
    if (len > 0 && len < (int)sizeof(query)) {
        lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS);

        // Parse query string for 'lastEventId' parameter
        char* id_param = strstr(query, "lastEventId=");
        if (id_param) {
            id_param += 12; // Skip "lastEventId="

            // Find the end of the parameter value
            char* end_param = strchr(id_param, '&');
            if (end_param) {
                *end_param = '\0';
            }

            // Copy the ID
            strncpy(last_event_id, id_param, sizeof(last_event_id) - 1);
            session_data->last_event_id = atoi(last_event_id);
            mcp_log_info("SSE client reconnected with Last-Event-ID: %d", session_data->last_event_id);
        }
    }

    // Check for event filter in query string (reuse the query string we already parsed)
    if (len == 0) {
        // If we didn't get the query string above, get it now
        len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
        if (len > 0 && len < (int)sizeof(query)) {
            lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS);
        }
    }

    if (len > 0) {
        // Parse query string for 'filter' parameter
        char* filter_param = strstr(query, "filter=");
        if (filter_param) {
            filter_param += 7; // Skip "filter="

            // Find the end of the parameter value
            char* end_param = strchr(filter_param, '&');
            if (end_param) {
                *end_param = '\0';
            }

            // Store the filter
            session_data->event_filter = mcp_strdup(filter_param);
            mcp_log_info("SSE client connected with event filter: %s", session_data->event_filter);
        }

        // Parse query string for 'session_id' parameter
        mcp_log_info("Parsing query string for session_id: '%s'", query);
        char* session_id_param = strstr(query, "session_id=");
        if (session_id_param) {
            mcp_log_info("Found session_id parameter at position %d", (int)(session_id_param - query));
            session_id_param += 11; // Skip "session_id="
            mcp_log_info("After skipping prefix, session_id_param = '%s'", session_id_param);

            // Find the end of the parameter value
            char* end_param = strchr(session_id_param, '&');
            if (end_param) {
                mcp_log_info("Found end of session_id at position %d", (int)(end_param - session_id_param));
                *end_param = '\0';
                mcp_log_info("After null-terminating, session_id_param = '%s'", session_id_param);
            } else {
                mcp_log_info("No '&' found, session_id is the last parameter");
            }

            // Log the raw session_id parameter
            mcp_log_info("SSE client connecting with raw session_id parameter: '%s'", session_id_param);

            // URL decode the session_id parameter if needed
            // (This is a simple implementation that handles %xx escapes)
            char decoded_session_id[256] = {0};
            const char* src = session_id_param;
            char* dst = decoded_session_id;
            while (*src) {
                if (*src == '%' && src[1] && src[2]) {
                    // Handle %xx escape
                    char hex[3] = {src[1], src[2], 0};
                    *dst = (char)strtol(hex, NULL, 16);
                    src += 3;
                } else if (*src == '+') {
                    // Handle + as space
                    *dst = ' ';
                    src++;
                } else {
                    // Copy character as-is
                    *dst = *src;
                    src++;
                }
                dst++;
            }
            *dst = '\0';

            // TEMPORARY FIX: Store the session ID directly from the query string
            // Extract the session_id directly from the query string
            char* direct_session_id = NULL;
            char* session_id_start = strstr(query, "session_id=");
            if (session_id_start) {
                session_id_start += 11; // Skip "session_id="
                char* session_id_end = strchr(session_id_start, '&');
                if (session_id_end) {
                    // Allocate memory for the session ID
                    size_t id_len = session_id_end - session_id_start;
                    direct_session_id = (char*)malloc(id_len + 1);
                    if (direct_session_id) {
                        strncpy(direct_session_id, session_id_start, id_len);
                        direct_session_id[id_len] = '\0';
                    }
                } else {
                    // Session ID is the last parameter
                    direct_session_id = mcp_strdup(session_id_start);
                }
            }

            mcp_log_info("Direct session_id extraction: '%s'", direct_session_id ? direct_session_id : "NULL");

            // Store the session ID (use the direct extraction if available)
            if (direct_session_id) {
                session_data->session_id = direct_session_id;
            } else if (strcmp(session_id_param, decoded_session_id) != 0) {
                mcp_log_info("URL decoded session_id: '%s' -> '%s'", session_id_param, decoded_session_id);
                session_data->session_id = mcp_strdup(decoded_session_id);
            } else {
                session_data->session_id = mcp_strdup(session_id_param);
            }

            mcp_log_info("SSE client connected with session ID: '%s'", session_data->session_id);

            // Add more detailed logging
            mcp_log_info("SSE connection details - session_id: '%s', query: '%s'",
                         session_data->session_id, query);

            // Verify that the session_id was stored correctly
            if (session_data->session_id == NULL) {
                mcp_log_error("Failed to store session_id: '%s'", session_id_param);
            } else if (strcmp(session_data->session_id, session_id_param) != 0 &&
                       strcmp(session_data->session_id, decoded_session_id) != 0) {
                mcp_log_error("Session ID mismatch: stored='%s', param='%s', decoded='%s'",
                             session_data->session_id, session_id_param, decoded_session_id);
            } else {
                mcp_log_info("Session ID stored correctly: '%s'", session_data->session_id);
            }
        } else {
            mcp_log_debug("SSE client connected without session ID - query: %s", query);
        }
    }

    // Add to SSE clients list
    mcp_mutex_lock(data->sse_mutex);
    if (data->sse_client_count < MAX_SSE_CLIENTS) {
        data->sse_clients[data->sse_client_count++] = wsi;

        // Log the client connection with session details
        if (session_data) {
            mcp_log_info("Added SSE client #%d - session_id: %s, filter: %s",
                        data->sse_client_count,
                        session_data->session_id ? session_data->session_id : "NULL",
                        session_data->event_filter ? session_data->event_filter : "ALL");
        } else {
            mcp_log_info("Added SSE client #%d - no session data", data->sse_client_count);
        }
    } else {
        mcp_log_error("Maximum number of SSE clients (%d) reached, rejecting connection", MAX_SSE_CLIENTS);
    }
    mcp_mutex_unlock(data->sse_mutex);

    // Send initial SSE message
    const char* initial_msg = "data: connected\n\n";
    lws_write(wsi, (unsigned char*)initial_msg, strlen(initial_msg), LWS_WRITE_HTTP);

    // If client reconnected with Last-Event-ID, replay missed events
    if (session_data && session_data->last_event_id > 0) {
        mcp_mutex_lock(data->event_mutex);

        // Iterate through the circular buffer to find events with ID greater than last_event_id
        if (data->stored_event_count > 0) {
            int current = data->event_head;
            int count = 0;

            // Process all events in the buffer
            while (count < data->stored_event_count) {
                int event_id = atoi(data->stored_events[current].id);

                // Only send events with ID greater than last_event_id
                if (event_id > session_data->last_event_id) {
                    // Skip events that don't match the filter (if any)
                    if (!(session_data->event_filter && data->stored_events[current].event_type &&
                        strcmp(session_data->event_filter, data->stored_events[current].event_type) != 0)) {

                        // Replay the event
                        if (data->stored_events[current].event_type) {
                            // Write event type
                            lws_write_http(wsi, "event: ", 7);
                            lws_write_http(wsi, data->stored_events[current].event_type,
                                          strlen(data->stored_events[current].event_type));
                            lws_write_http(wsi, "\n", 1);
                        }

                        // Write event ID
                        lws_write_http(wsi, "id: ", 4);
                        lws_write_http(wsi, data->stored_events[current].id,
                                      strlen(data->stored_events[current].id));
                        lws_write_http(wsi, "\n", 1);

                        // Write event data
                        lws_write_http(wsi, "data: ", 6);
                        lws_write_http(wsi, data->stored_events[current].data,
                                      strlen(data->stored_events[current].data));
                        lws_write_http(wsi, "\n\n", 2);

                        // Request a callback when the socket is writable again
                        lws_callback_on_writable(wsi);
                    }
                }

                // Move to the next event in the circular buffer
                current = (current + 1) % MAX_SSE_STORED_EVENTS;
                count++;
            }
        }

        mcp_mutex_unlock(data->event_mutex);
    }
}
