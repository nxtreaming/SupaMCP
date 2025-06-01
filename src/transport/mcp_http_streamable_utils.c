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
#include <time.h>

/**
 * @brief Create SSE stream context
 */
sse_stream_context_t* sse_stream_context_create(size_t max_stored_events) {
    sse_stream_context_t* context = (sse_stream_context_t*)calloc(1, sizeof(sse_stream_context_t));
    if (context == NULL) {
        mcp_log_error("Failed to allocate memory for SSE stream context");
        return NULL;
    }

    context->max_stored_events = max_stored_events;
    context->next_event_id = 1;

    // Allocate events array
    if (max_stored_events > 0) {
        context->stored_events = (sse_event_t*)calloc(max_stored_events, sizeof(sse_event_t));
        if (context->stored_events == NULL) {
            mcp_log_error("Failed to allocate memory for stored events");
            free(context);
            return NULL;
        }
    }

    // Create mutex
    context->mutex = mcp_mutex_create();
    if (context->mutex == NULL) {
        mcp_log_error("Failed to create SSE stream context mutex");
        if (context->stored_events) {
            free(context->stored_events);
        }
        free(context);
        return NULL;
    }

    mcp_log_debug("Created SSE stream context with max %zu events", max_stored_events);
    return context;
}

/**
 * @brief Destroy SSE stream context
 */
void sse_stream_context_destroy(sse_stream_context_t* context) {
    if (context == NULL) {
        return;
    }

    mcp_mutex_lock(context->mutex);

    // Free stored events
    if (context->stored_events) {
        for (size_t i = 0; i < context->stored_event_count; i++) {
            size_t index = (context->event_head + i) % context->max_stored_events;
            sse_event_clear(&context->stored_events[index]);
        }
        free(context->stored_events);
    }

    // Free stream ID and last event ID
    if (context->stream_id) {
        free(context->stream_id);
    }
    if (context->last_event_id) {
        free(context->last_event_id);
    }

    mcp_mutex_unlock(context->mutex);
    mcp_mutex_destroy(context->mutex);
    free(context);
}

/**
 * @brief Store an event in SSE stream context
 */
void sse_stream_context_store_event(sse_stream_context_t* context, const char* event_id, const char* event_type, const char* data) {
    if (context == NULL || context->stored_events == NULL || context->max_stored_events == 0) {
        return;
    }

    mcp_mutex_lock(context->mutex);

    // Generate event ID if not provided
    char generated_id[32];
    if (event_id == NULL) {
        snprintf(generated_id, sizeof(generated_id), "%llu", (unsigned long long)context->next_event_id++);
        event_id = generated_id;
    }

    // Get the index for the new event
    size_t index = context->event_tail;

    // Clear the existing event at this index if buffer is full
    if (context->stored_event_count == context->max_stored_events) {
        sse_event_clear(&context->stored_events[index]);
        context->event_head = (context->event_head + 1) % context->max_stored_events;
    } else {
        context->stored_event_count++;
    }

    // Store the new event
    sse_event_t* event = &context->stored_events[index];
    event->id = event_id ? mcp_strdup(event_id) : NULL;
    event->event = event_type ? mcp_strdup(event_type) : NULL;
    event->data = data ? mcp_strdup(data) : NULL;
    event->timestamp = time(NULL);

    // Update tail pointer
    context->event_tail = (context->event_tail + 1) % context->max_stored_events;

    // Update last event ID
    if (context->last_event_id) {
        free(context->last_event_id);
    }
    context->last_event_id = event_id ? mcp_strdup(event_id) : NULL;

    mcp_mutex_unlock(context->mutex);

    mcp_log_debug("Stored SSE event: id=%s, type=%s", event_id ? event_id : "null", event_type ? event_type : "null");
}

/**
 * @brief Replay events from a given last event ID
 */
int sse_stream_context_replay_events(sse_stream_context_t* context, struct lws* wsi, const char* last_event_id) {
    if (context == NULL || wsi == NULL || context->stored_events == NULL) {
        return 0;
    }

    mcp_mutex_lock(context->mutex);

    int replayed_count = 0;
    bool found_start = (last_event_id == NULL); // If no last event ID, replay all

    // Iterate through stored events
    for (size_t i = 0; i < context->stored_event_count; i++) {
        size_t index = (context->event_head + i) % context->max_stored_events;
        sse_event_t* event = &context->stored_events[index];

        // If we haven't found the starting point yet, look for the last event ID
        if (!found_start) {
            if (event->id && last_event_id && strcmp(event->id, last_event_id) == 0) {
                found_start = true;
            }
            continue; // Skip this event
        }

        // Send the event
        if (send_sse_event(wsi, event->id, event->event, event->data) == 0) {
            replayed_count++;
        } else {
            mcp_log_error("Failed to send replayed SSE event");
            break;
        }
    }

    mcp_mutex_unlock(context->mutex);

    mcp_log_debug("Replayed %d SSE events from last_event_id=%s", replayed_count, last_event_id ? last_event_id : "null");
    return replayed_count;
}

/**
 * @brief Validate origin against allowed origins list
 */
bool validate_origin(http_streamable_transport_data_t* data, const char* origin) {
    if (data == NULL || !data->validate_origin || origin == NULL) {
        return true; // No validation required
    }

    if (data->allowed_origins == NULL || data->allowed_origins_count == 0) {
        return true; // No restrictions
    }

    for (size_t i = 0; i < data->allowed_origins_count; i++) {
        const char* allowed = data->allowed_origins[i];
        
        // Check for exact match
        if (strcmp(origin, allowed) == 0) {
            return true;
        }

        // Check for wildcard match (e.g., "http://localhost:*")
        size_t allowed_len = strlen(allowed);
        if (allowed_len > 0 && allowed[allowed_len - 1] == '*') {
            if (strncmp(origin, allowed, allowed_len - 1) == 0) {
                return true;
            }
        }
    }

    mcp_log_warn("Origin validation failed for: %s", origin);
    return false;
}

/**
 * @brief Parse allowed origins string into array
 */
bool parse_allowed_origins(const char* origins_str, char*** origins_out, size_t* count_out) {
    if (origins_str == NULL || origins_out == NULL || count_out == NULL) {
        return false;
    }

    *origins_out = NULL;
    *count_out = 0;

    // Count commas to estimate array size
    size_t comma_count = 0;
    for (const char* p = origins_str; *p; p++) {
        if (*p == ',') {
            comma_count++;
        }
    }

    size_t max_origins = comma_count + 1;
    char** origins = (char**)calloc(max_origins, sizeof(char*));
    if (origins == NULL) {
        mcp_log_error("Failed to allocate memory for origins array");
        return false;
    }

    // Parse the string
    char* str_copy = mcp_strdup(origins_str);
    if (str_copy == NULL) {
        free(origins);
        return false;
    }

    size_t count = 0;
    char* token = strtok(str_copy, ",");
    while (token != NULL && count < max_origins) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') {
            token++;
        }
        
        size_t len = strlen(token);
        while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t')) {
            token[--len] = '\0';
        }

        if (len > 0) {
            origins[count] = mcp_strdup(token);
            if (origins[count] == NULL) {
                // Cleanup on failure
                for (size_t i = 0; i < count; i++) {
                    free(origins[i]);
                }
                free(origins);
                free(str_copy);
                return false;
            }
            count++;
        }

        token = strtok(NULL, ",");
    }

    free(str_copy);

    *origins_out = origins;
    *count_out = count;

    mcp_log_debug("Parsed %zu allowed origins", count);
    return true;
}

/**
 * @brief Free allowed origins array
 */
void free_allowed_origins(char** origins, size_t count) {
    if (origins == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (origins[i]) {
            free(origins[i]);
        }
    }
    free(origins);
}

/**
 * @brief Send HTTP error response
 */
int send_http_error_response(struct lws* wsi, int status_code, const char* message) {
    if (wsi == NULL) {
        return -1;
    }

    const char* status_text;
    switch (status_code) {
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "Error"; break;
    }

    // Prepare response headers
    unsigned char headers[512];
    unsigned char* p = headers;
    unsigned char* end = headers + sizeof(headers);

    if (lws_add_http_header_status(wsi, status_code, &p, end)) {
        return -1;
    }

    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, 
                                    (unsigned char*)"text/plain", 10, &p, end)) {
        return -1;
    }

    if (lws_finalize_http_header(wsi, &p, end)) {
        return -1;
    }

    // Write headers
    if (lws_write(wsi, headers, p - headers, LWS_WRITE_HTTP_HEADERS) < 0) {
        return -1;
    }

    // Write body
    const char* body = message ? message : status_text;
    if (lws_write(wsi, (unsigned char*)body, strlen(body), LWS_WRITE_HTTP) < 0) {
        return -1;
    }

    // Complete transaction
    if (lws_http_transaction_completed(wsi)) {
        return -1;
    }

    return 0;
}

/**
 * @brief Send HTTP JSON response
 */
int send_http_json_response(struct lws* wsi, const char* json_data, const char* session_id) {
    if (wsi == NULL || json_data == NULL) {
        mcp_log_error("send_http_json_response: invalid parameters (wsi=%p, json_data=%p)", wsi, json_data);
        return -1;
    }

    mcp_log_debug("send_http_json_response: sending %zu bytes of JSON data", strlen(json_data));

    // Prepare response headers
    unsigned char headers[512];
    unsigned char* p = headers;
    unsigned char* end = headers + sizeof(headers);

    if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end)) {
        return -1;
    }

    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
                                    (unsigned char*)"application/json", 16, &p, end)) {
        return -1;
    }

    // Add Content-Length header
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%zu", strlen(json_data));
    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH,
                                    (unsigned char*)content_length_str, (int)strlen(content_length_str), &p, end)) {
        mcp_log_error("send_http_json_response: failed to add Content-Length header");
        return -1;
    }
    // Add session ID header if provided
    if (session_id != NULL) {
        if (lws_add_http_header_by_name(wsi, (unsigned char*)MCP_SESSION_HEADER_NAME,
                                       (unsigned char*)session_id, (int)strlen(session_id), &p, end)) {
            return -1;
        }
    }

    if (lws_finalize_http_header(wsi, &p, end)) {
        return -1;
    }

    // Write headers
    size_t header_len = p - headers;
    int header_bytes = lws_write(wsi, headers, header_len, LWS_WRITE_HTTP_HEADERS);
    if (header_bytes < 0) {
        mcp_log_error("send_http_json_response: failed to write headers");
        return -1;
    }

    // Write JSON body
    size_t json_len = strlen(json_data);
    int body_bytes = lws_write(wsi, (unsigned char*)json_data, json_len, LWS_WRITE_HTTP);
    if (body_bytes < 0) {
        mcp_log_error("send_http_json_response: failed to write body");
        return -1;
    }

    // Complete transaction
    if (lws_http_transaction_completed(wsi)) {
        mcp_log_error("send_http_json_response: failed to complete transaction");
        return -1;
    }
    return 0;
}

/**
 * @brief Send SSE event
 */
int send_sse_event(struct lws* wsi, const char* event_id, const char* event_type, const char* data) {
    if (wsi == NULL) {
        return -1;
    }

    char buffer[4096];
    size_t offset = 0;

    // Add event ID if provided
    if (event_id != NULL) {
        int written = snprintf(buffer + offset, sizeof(buffer) - offset, "id: %s\n", event_id);
        if (written < 0 || (size_t)written >= sizeof(buffer) - offset) {
            mcp_log_error("SSE event buffer overflow (id)");
            return -1;
        }
        offset += written;
    }

    // Add event type if provided
    if (event_type != NULL) {
        int written = snprintf(buffer + offset, sizeof(buffer) - offset, "event: %s\n", event_type);
        if (written < 0 || (size_t)written >= sizeof(buffer) - offset) {
            mcp_log_error("SSE event buffer overflow (event)");
            return -1;
        }
        offset += written;
    }

    // Add data (required)
    if (data != NULL) {
        int written = snprintf(buffer + offset, sizeof(buffer) - offset, "data: %s\n", data);
        if (written < 0 || (size_t)written >= sizeof(buffer) - offset) {
            mcp_log_error("SSE event buffer overflow (data)");
            return -1;
        }
        offset += written;
    }

    // Add final newline
    if (offset < sizeof(buffer) - 1) {
        buffer[offset++] = '\n';
        buffer[offset] = '\0';
    } else {
        mcp_log_error("SSE event buffer overflow (final newline)");
        return -1;
    }

    // Send the event
    if (lws_write(wsi, (unsigned char*)buffer, offset, LWS_WRITE_HTTP) < 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Send SSE heartbeat to a specific WebSocket instance
 */
int send_sse_heartbeat_to_wsi(struct lws* wsi) {
    if (wsi == NULL) {
        return -1;
    }

    const char* heartbeat = ": heartbeat\n\n";
    if (lws_write(wsi, (unsigned char*)heartbeat, strlen(heartbeat), LWS_WRITE_HTTP) < 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Extract session ID from headers
 */
bool extract_session_id(struct lws* wsi, char* session_id_out) {
    if (wsi == NULL || session_id_out == NULL) {
        return false;
    }

    // Use libwebsockets custom header API to extract Mcp-Session-Id
    const char* header_name = "mcp-session-id:";
    int header_name_len = (int)strlen(header_name);

    // Get the length of the header value
    int value_len = lws_hdr_custom_length(wsi, header_name, header_name_len);
    if (value_len <= 0 || value_len >= MCP_SESSION_ID_MAX_LENGTH) {
        session_id_out[0] = '\0';
        return false;
    }

    // Copy the header value
    int copied = lws_hdr_custom_copy(wsi, session_id_out, MCP_SESSION_ID_MAX_LENGTH,
                                    header_name, header_name_len);
    if (copied <= 0) {
        session_id_out[0] = '\0';
        return false;
    }

    // Validate the session ID format
    if (!mcp_session_id_is_valid(session_id_out)) {
        mcp_log_warn("Invalid session ID format: %s", session_id_out);
        session_id_out[0] = '\0';
        return false;
    }

    mcp_log_debug("Extracted session ID: %s", session_id_out);
    return true;
}

/**
 * @brief Extract last event ID from headers
 */
bool extract_last_event_id(struct lws* wsi, char* last_event_id_out) {
    if (wsi == NULL || last_event_id_out == NULL) {
        return false;
    }

    // Use libwebsockets custom header API to extract Last-Event-ID
    const char* header_name = "last-event-id:";
    int header_name_len = (int)strlen(header_name);

    // Get the length of the header value
    int value_len = lws_hdr_custom_length(wsi, header_name, header_name_len);
    if (value_len <= 0 || value_len >= HTTP_LAST_EVENT_ID_BUFFER_SIZE) {
        last_event_id_out[0] = '\0';
        return false;
    }

    // Copy the header value
    int copied = lws_hdr_custom_copy(wsi, last_event_id_out, HTTP_LAST_EVENT_ID_BUFFER_SIZE,
                                    header_name, header_name_len);
    if (copied <= 0) {
        last_event_id_out[0] = '\0';
        return false;
    }

    // Basic validation - event ID should be alphanumeric
    for (int i = 0; last_event_id_out[i] != '\0'; i++) {
        char c = last_event_id_out[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) {
            mcp_log_warn("Invalid Last-Event-ID format: %s", last_event_id_out);
            last_event_id_out[0] = '\0';
            return false;
        }
    }

    mcp_log_debug("Extracted Last-Event-ID: %s", last_event_id_out);
    return true;
}

/**
 * @brief Validate SSE text input for control characters
 */
bool validate_sse_text_input(const char* text) {
    if (text == NULL) {
        return false;
    }

    for (const char* p = text; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;

        // Allow newlines, carriage returns, and tabs
        if (c == '\n' || c == '\r' || c == '\t') {
            continue;
        }

        // Reject other control characters (below 0x20)
        if (c < 0x20) {
            mcp_log_warn("Invalid control character in SSE text: 0x%02x", c);
            return false;
        }
    }

    return true;
}

/**
 * @brief Add CORS headers to response for streamable transport
 */
void add_streamable_cors_headers(struct lws* wsi, http_streamable_transport_data_t* data,
                                unsigned char** p, unsigned char* end) {
    if (wsi == NULL || data == NULL || !data->enable_cors || p == NULL || *p == NULL || end == NULL) {
        return;
    }

    // Add CORS headers using the same pattern as the existing implementation
    if (data->cors_allow_origin) {
        lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Allow-Origin",
                                   (unsigned char*)data->cors_allow_origin,
                                   (int)strlen(data->cors_allow_origin), p, end);
    }

    if (data->cors_allow_methods) {
        lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Allow-Methods",
                                   (unsigned char*)data->cors_allow_methods,
                                   (int)strlen(data->cors_allow_methods), p, end);
    }

    if (data->cors_allow_headers) {
        lws_add_http_header_by_name(wsi, (unsigned char*)"Access-Control-Allow-Headers",
                                   (unsigned char*)data->cors_allow_headers,
                                   (int)strlen(data->cors_allow_headers), p, end);
    }
}
