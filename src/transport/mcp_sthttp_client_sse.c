/**
 * @file mcp_sthttp_client_sse.c
 * @brief SSE (Server-Sent Events) client implementation for Streamable HTTP transport
 *
 * This file implements SSE stream handling, event parsing, and reconnection logic
 * for the HTTP Streamable client transport.
 */
#include "internal/sthttp_client_internal.h"
#include "internal/sthttp_transport_internal.h"

/**
 * @brief Parse HTTP response
 */
int http_client_parse_response(const char* raw_response, size_t response_length, http_response_t* response) {
    if (raw_response == NULL || response == NULL || response_length == 0) {
        return -1;
    }

    // Initialize response structure
    memset(response, 0, sizeof(http_response_t));

    // Find end of status line
    const char* status_line_end = strstr(raw_response, "\r\n");
    if (status_line_end == NULL) {
        return -1;
    }

    // Parse status code
    if (sscanf(raw_response, "HTTP/1.%*d %d", &response->status_code) != 1) {
        return -1;
    }

    // Find end of headers
    const char* headers_end = strstr(raw_response, "\r\n\r\n");
    if (headers_end == NULL) {
        return -1;
    }

    // Extract headers
    size_t headers_length = headers_end - raw_response + 2; // Include final \r\n
    response->headers = (char*)malloc(headers_length + 1);
    if (response->headers == NULL) {
        return -1;
    }
    memcpy(response->headers, raw_response, headers_length);
    response->headers[headers_length] = '\0';

    // Extract Content-Type (case-insensitive)
    const char* content_type_start = NULL;

    // Try different case variations
    content_type_start = strstr(response->headers, "Content-Type:");
    if (!content_type_start) {
        content_type_start = strstr(response->headers, "content-type:");
    }
    if (!content_type_start) {
        content_type_start = strstr(response->headers, "Content-type:");
    }
    if (!content_type_start) {
        content_type_start = strstr(response->headers, "CONTENT-TYPE:");
    }

    if (content_type_start) {
        // Find the colon and skip it
        const char* colon = strchr(content_type_start, ':');
        if (colon) {
            content_type_start = colon + 1;
            while (*content_type_start == ' ')
                content_type_start++; // Skip spaces

            const char* content_type_end = strstr(content_type_start, "\r\n");
            if (content_type_end) {
                size_t content_type_length = content_type_end - content_type_start;
                response->content_type = (char*)malloc(content_type_length + 1);
                if (response->content_type) {
                    memcpy(response->content_type, content_type_start, content_type_length);
                    response->content_type[content_type_length] = '\0';
                }
            }
        }
    }

    // Extract body
    const char* body_start = headers_end + 4; // Skip \r\n\r\n
    size_t body_length = response_length - (body_start - raw_response);
    if (body_length > 0) {
        response->body = (char*)malloc(body_length + 1);
        if (response->body == NULL) {
            free(response->headers);
            free(response->content_type);
            return -1;
        }
        memcpy(response->body, body_start, body_length);
        response->body[body_length] = '\0';
        response->body_length = body_length;
    }

    return 0;
}

/**
 * @brief Free HTTP response
 */
void http_client_free_response(http_response_t* response) {
    if (response == NULL) {
        return;
    }

    free(response->headers);
    free(response->body);
    free(response->content_type);
    free(response->session_id);
    
    memset(response, 0, sizeof(http_response_t));
}

/**
 * @brief Extract session ID from response headers
 */
char* http_client_extract_session_id(const char* headers) {
    if (headers == NULL) {
        return NULL;
    }

    // Try different case variations for session header
    const char* session_header = strstr(headers, "Mcp-Session-Id:");
    if (!session_header) {
        session_header = strstr(headers, "mcp-session-id:");
    }
    if (!session_header) {
        session_header = strstr(headers, "MCP-SESSION-ID:");
    }
    if (!session_header) {
        session_header = strstr(headers, "Mcp-session-id:");
    }

    if (session_header == NULL) {
        return NULL;
    }

    // Find the colon and skip it
    const char* colon = strchr(session_header, ':');
    if (colon) {
        session_header = colon + 1;
    } else {
        return NULL;
    }
    while (*session_header == ' ')
        session_header++; // Skip spaces

    const char* session_end = strstr(session_header, "\r\n");
    if (session_end == NULL) {
        return NULL;
    }

    size_t session_length = session_end - session_header;
    if (session_length == 0 || session_length >= HTTP_CLIENT_SESSION_ID_BUFFER_SIZE) {
        return NULL;
    }

    char* session_id = (char*)malloc(session_length + 1);
    if (session_id == NULL) {
        return NULL;
    }

    memcpy(session_id, session_header, session_length);
    session_id[session_length] = '\0';

    return session_id;
}

/**
 * @brief Parse SSE event from buffer
 */
int sse_parse_event(const char* buffer, size_t length, sse_event_t* event) {
    if (buffer == NULL || event == NULL || length == 0) {
        return -1;
    }

    // Initialize event structure
    memset(event, 0, sizeof(sse_event_t));

    const char* ptr = buffer;
    const char* end = buffer + length;
    
    while (ptr < end) {
        // Find end of line
        const char* line_end = ptr;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }
        
        if (line_end >= end) {
            break; // Incomplete line
        }
        
        // Skip \r if present
        const char* line_content_end = line_end;
        if (line_content_end > ptr && *(line_content_end - 1) == '\r') {
            line_content_end--;
        }
        
        size_t line_length = line_content_end - ptr;
        // Empty line indicates end of event
        if (line_length == 0) {
            return 0; // Event complete
        }
        
        // Parse field
        if (strncmp(ptr, "id:", 3) == 0) {
            const char* value_start = ptr + 3;
            while (value_start < line_content_end && *value_start == ' ') value_start++;
            
            size_t value_length = line_content_end - value_start;
            if (value_length > 0) {
                event->id = (char*)malloc(value_length + 1);
                if (event->id) {
                    memcpy(event->id, value_start, value_length);
                    event->id[value_length] = '\0';
                }
            }
        } else if (strncmp(ptr, "event:", 6) == 0) {
            const char* value_start = ptr + 6;
            while (value_start < line_content_end && *value_start == ' ') value_start++;
            
            size_t value_length = line_content_end - value_start;
            if (value_length > 0) {
                event->event = (char*)malloc(value_length + 1);
                if (event->event) {
                    memcpy(event->event, value_start, value_length);
                    event->event[value_length] = '\0';
                }
            }
        } else if (strncmp(ptr, "data:", 5) == 0) {
            const char* value_start = ptr + 5;
            while (value_start < line_content_end && *value_start == ' ') value_start++;
            
            size_t value_length = line_content_end - value_start;
            if (value_length > 0) {
                if (event->data == NULL) {
                    // First data line
                    event->data = (char*)malloc(value_length + 1);
                    if (event->data) {
                        memcpy(event->data, value_start, value_length);
                        event->data[value_length] = '\0';
                    }
                } else {
                    // Append to existing data with newline
                    size_t existing_length = strlen(event->data);
                    char* new_data = (char*)realloc(event->data, existing_length + 1 + value_length + 1);
                    if (new_data) {
                        event->data = new_data;
                        event->data[existing_length] = '\n';
                        memcpy(event->data + existing_length + 1, value_start, value_length);
                        event->data[existing_length + 1 + value_length] = '\0';
                    }
                }
            }
        }
        
        // Move to next line
        ptr = line_end + 1;
    }
    
    return -1; // Incomplete event
}

/**
 * @brief Free SSE event
 */
void sse_free_event(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    free(event->id);
    free(event->event);
    free(event->data);
    
    memset(event, 0, sizeof(sse_event_t));
}

/**
 * @brief Connect SSE stream
 */
int sse_client_connect(sthttp_client_data_t* data) {
    if (data == NULL) {
        return -1;
    }

    mcp_mutex_lock(data->sse_mutex);
    
    // Check if already connected
    if (data->sse_conn && data->sse_conn->connected) {
        mcp_mutex_unlock(data->sse_mutex);
        return 0;
    }

    // Create SSE connection structure
    if (data->sse_conn == NULL) {
        data->sse_conn = (sse_connection_t*)malloc(sizeof(sse_connection_t));
        if (data->sse_conn == NULL) {
            mcp_mutex_unlock(data->sse_mutex);
            return -1;
        }
        memset(data->sse_conn, 0, sizeof(sse_connection_t));
        
        // Allocate receive buffer
        data->sse_conn->buffer_size = HTTP_CLIENT_BUFFER_SIZE;
        data->sse_conn->buffer = (char*)malloc(data->sse_conn->buffer_size);
        if (data->sse_conn->buffer == NULL) {
            free(data->sse_conn);
            data->sse_conn = NULL;
            mcp_mutex_unlock(data->sse_mutex);
            return -1;
        }
    }

    // Create socket connection
    data->sse_conn->socket_fd = http_client_create_socket(data->config.host, data->config.port, data->config.connect_timeout_ms);
    if (data->sse_conn->socket_fd == MCP_INVALID_SOCKET) {
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    // Build SSE request
    char* request = http_client_build_request(data, "GET", NULL);
    if (request == NULL) {
        mcp_socket_close(data->sse_conn->socket_fd);
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    // Send SSE request
    int result = http_client_send_raw_request(data->sse_conn->socket_fd, request, data->config.request_timeout_ms);
    free(request);
    if (result != 0) {
        mcp_socket_close(data->sse_conn->socket_fd);
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    // Use optimized HTTP response receiver for headers
    http_response_t response;
    memset(&response, 0, sizeof(response)); // Initialize response structure
    result = http_client_receive_response_optimized(data->sse_conn->socket_fd, &response, data->config.request_timeout_ms);
    if (result != 0) {
        mcp_socket_close(data->sse_conn->socket_fd);
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    // Check HTTP status code
    if (response.status_code != 200) {
        mcp_log_error("SSE connection failed with status %d", response.status_code);
        http_client_free_response(&response);
        mcp_socket_close(data->sse_conn->socket_fd);
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    // Debug: Log the full response for troubleshooting
    mcp_log_debug("Parsed content type: '%s'", response.content_type ? response.content_type : "NULL");

    // Verify content type
    if (response.content_type == NULL || strstr(response.content_type, "text/event-stream") == NULL) {
        mcp_log_error("Invalid SSE content type: %s", response.content_type ? response.content_type : "none");
        mcp_log_error("Expected: text/event-stream");
        http_client_free_response(&response);
        mcp_socket_close(data->sse_conn->socket_fd);
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    http_client_free_response(&response);

    // Mark as connected
    data->sse_conn->connected = true;
    data->sse_conn->buffer_used = 0;
    data->sse_conn->parse_state = HTTP_PARSE_STATE_COMPLETE; // Skip headers, go straight to body
    
    // Start SSE receive thread
    data->sse_conn->sse_thread_running = true;
    if (mcp_thread_create(&data->sse_conn->sse_thread, sse_client_thread_func, data) != 0) {
        mcp_log_error("Failed to create SSE receive thread");
        data->sse_conn->connected = false;
        data->sse_conn->sse_thread_running = false;
        mcp_socket_close(data->sse_conn->socket_fd);
        mcp_mutex_unlock(data->sse_mutex);
        return -1;
    }

    mcp_mutex_unlock(data->sse_mutex);
    
    http_client_set_state(data, MCP_CLIENT_STATE_SSE_CONNECTED);
    mcp_log_info("SSE stream connected");

    return 0;
}

/**
 * @brief Disconnect SSE stream
 */
void sse_client_disconnect(sthttp_client_data_t* data) {
    if (data == NULL) {
        return;
    }

    mcp_mutex_lock(data->sse_mutex);

    if (data->sse_conn == NULL) {
        mcp_mutex_unlock(data->sse_mutex);
        return;
    }

    // Stop SSE thread
    if (data->sse_conn->sse_thread_running) {
        data->sse_conn->sse_thread_running = false;

        // Close socket to interrupt recv()
        if (data->sse_conn->socket_fd != MCP_INVALID_SOCKET) {
            mcp_socket_close(data->sse_conn->socket_fd);
            data->sse_conn->socket_fd = MCP_INVALID_SOCKET;
        }

        mcp_mutex_unlock(data->sse_mutex);

        // Wait for thread to finish
        mcp_thread_join(data->sse_conn->sse_thread, NULL);

        mcp_mutex_lock(data->sse_mutex);
    }

    // Mark as disconnected
    data->sse_conn->connected = false;

    // Free resources
    free(data->sse_conn->buffer);
    free(data->sse_conn->last_event_id);
    free(data->sse_conn);
    data->sse_conn = NULL;

    mcp_mutex_unlock(data->sse_mutex);

    mcp_log_info("SSE stream disconnected");
}

/**
 * @brief SSE receive thread function with optimized parser
 */
void* sse_client_thread_func(void* arg) {
    sthttp_client_data_t* data = (sthttp_client_data_t*)arg;
    if (data == NULL) {
        return NULL;
    }

    mcp_log_debug("SSE receive thread started");

    char temp_buffer[1024];
    sse_parser_context_t* parser = sse_parser_create();
    if (parser == NULL) {
        mcp_log_error("Failed to create SSE parser");
        return NULL;
    }

    while (data->sse_conn && data->sse_conn->sse_thread_running && !data->shutdown_requested) {
        // Receive data using socket utility function
        int wait_result = mcp_socket_wait_readable(data->sse_conn->socket_fd, 1000, NULL);
        if (wait_result <= 0) {
            continue; // Timeout or error, continue loop
        }

        ssize_t bytes_received = recv(data->sse_conn->socket_fd, temp_buffer, sizeof(temp_buffer) - 1, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                mcp_log_info("SSE connection closed by server");
            } else {
                mcp_log_error("SSE receive error or timeout");
            }
            break;
        }
        temp_buffer[bytes_received] = '\0';

        // Add to buffer
        mcp_mutex_lock(data->sse_mutex);

        if (data->sse_conn->buffer_used + bytes_received >= data->sse_conn->buffer_size) {
            // Expand buffer
            size_t new_size = data->sse_conn->buffer_size * 2;
            char* new_buffer = (char*)realloc(data->sse_conn->buffer, new_size);
            if (new_buffer == NULL) {
                mcp_log_error("Failed to expand SSE buffer");
                mcp_mutex_unlock(data->sse_mutex);
                break;
            }
            data->sse_conn->buffer = new_buffer;
            data->sse_conn->buffer_size = new_size;
        }

        memcpy(data->sse_conn->buffer + data->sse_conn->buffer_used, temp_buffer, bytes_received);
        data->sse_conn->buffer_used += bytes_received;
        data->sse_conn->buffer[data->sse_conn->buffer_used] = '\0';

        mcp_mutex_unlock(data->sse_mutex);

        // Process events using optimized parser
        mcp_mutex_lock(data->sse_mutex);
        char* buffer_data = data->sse_conn->buffer;
        size_t buffer_length = data->sse_conn->buffer_used;
        mcp_mutex_unlock(data->sse_mutex);

        sse_event_t event;
        int parse_result = sse_parser_process(parser, buffer_data, buffer_length, &event);

        if (parse_result > 0) {
            // Event parsed successfully
            // Update last event ID
            if (event.id) {
                mcp_mutex_lock(data->sse_mutex);
                free(data->sse_conn->last_event_id);
                data->sse_conn->last_event_id = mcp_strdup(event.id);
                mcp_mutex_unlock(data->sse_mutex);
            }

            // Update statistics
            http_client_update_stats(data, "sse_event_received");

            // Call SSE callback
            if (data->sse_callback) {
                // Create temporary transport for callback
                mcp_transport_t temp_transport = {
                    .type = MCP_TRANSPORT_TYPE_CLIENT,
                    .protocol_type = MCP_TRANSPORT_PROTOCOL_HTTP,
                    .transport_data = data
                };
                data->sse_callback(&temp_transport, event.id, event.event, event.data, data->sse_callback_user_data);
            }

            // Free event
            sse_free_event(&event);

            // Clear processed data from buffer
            mcp_mutex_lock(data->sse_mutex);
            data->sse_conn->buffer_used = 0;
            mcp_mutex_unlock(data->sse_mutex);
        } else if (parse_result < 0) {
            // Parsing error
            mcp_log_error("SSE parsing error");
            break;
        }
        // parse_result == 0 means need more data, continue loop

    }

    // Cleanup
    sse_parser_destroy(parser);

    // Update state
    if (data->sse_conn) {
        data->sse_conn->connected = false;
        data->sse_conn->sse_thread_running = false;
    }

    http_client_set_state(data, MCP_CLIENT_STATE_CONNECTED);

    mcp_log_debug("SSE receive thread finished");
    return NULL;
}
