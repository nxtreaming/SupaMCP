/**
 * @file mcp_sthttp_client_core.c
 * @brief Core HTTP client functionality for Streamable HTTP transport
 *
 * This file implements the core HTTP client functionality including
 * socket management, HTTP request/response handling, and SSE stream processing.
 */
#include "internal/sthttp_client_internal.h"

/**
 * @brief Receive data with timeout
 */
static ssize_t socket_recv_with_timeout(socket_t socket_fd, void* buffer, size_t size, uint32_t timeout_ms) {
    if (socket_fd == MCP_INVALID_SOCKET || buffer == NULL || size == 0) {
        return -1;
    }

    // Wait for socket to become readable
    int wait_result = mcp_socket_wait_readable(socket_fd, timeout_ms, NULL);
    if (wait_result <= 0) {
        return -1; // Timeout or error
    }

    ssize_t result = recv(socket_fd, (char*)buffer, (int)size, 0);
    if (result < 0) {
        mcp_log_debug("recv failed with error: %d", errno);
    }
    return result;
}

/**
 * @brief Create socket connection
 */
socket_t http_client_create_socket(const char* host, uint16_t port, uint32_t timeout_ms) {
    if (host == NULL) {
        return MCP_INVALID_SOCKET;
    }

    socket_t socket_fd = mcp_socket_connect(host, port, timeout_ms);
    if (socket_fd == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to %s:%d", host, port);
        return MCP_INVALID_SOCKET;
    }

    mcp_log_debug("Connected to %s:%d", host, port);
    return socket_fd;
}

/**
 * @brief Build HTTP request string
 */
char* http_client_build_request(sthttp_client_data_t* data, const char* method, const char* json_data) {
    if (data == NULL || method == NULL) {
        return NULL;
    }

    const char* endpoint = data->config.mcp_endpoint ? data->config.mcp_endpoint : "/mcp";
    const char* host = data->config.host ? data->config.host : "localhost";
    const char* user_agent = data->config.user_agent ? data->config.user_agent : "SupaMCP-Client/1.0";
    
    size_t content_length = json_data ? strlen(json_data) : 0;
    
    // Calculate buffer size
    size_t buffer_size = 1024 + content_length;
    if (data->config.custom_headers) {
        buffer_size += strlen(data->config.custom_headers);
    }
    if (data->config.api_key) {
        buffer_size += strlen(data->config.api_key) + 50;
    }
    
    char* request = (char*)malloc(buffer_size);
    if (request == NULL) {
        mcp_log_error("Failed to allocate request buffer");
        return NULL;
    }

    // Build request line and basic headers
    int offset = snprintf(request, buffer_size,
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: %s\r\n"
        "Connection: keep-alive\r\n",
        method, endpoint, host, data->config.port, user_agent);

    // Add Content-Type and Content-Length for POST requests
    if (strcmp(method, "POST") == 0 && json_data) {
        offset += snprintf(request + offset, buffer_size - offset,
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n",
            content_length);
    }

    // Add Accept header for GET requests (SSE)
    if (strcmp(method, "GET") == 0) {
        offset += snprintf(request + offset, buffer_size - offset,
            "Accept: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n");
    }

    // Add session ID header if available
    if (data->has_session && data->session_id) {
        offset += snprintf(request + offset, buffer_size - offset,
            "Mcp-Session-Id: %s\r\n", data->session_id);
    }

    // Add API key header if configured
    if (data->config.api_key) {
        offset += snprintf(request + offset, buffer_size - offset,
            "Authorization: Bearer %s\r\n", data->config.api_key);
    }

    // Add custom headers if configured
    if (data->config.custom_headers) {
        offset += snprintf(request + offset, buffer_size - offset,
            "%s\r\n", data->config.custom_headers);
    }

    // End headers
    offset += snprintf(request + offset, buffer_size - offset, "\r\n");

    // Add body for POST requests
    if (strcmp(method, "POST") == 0 && json_data) {
        offset += snprintf(request + offset, buffer_size - offset, "%s", json_data);
    }

    return request;
}

/**
 * @brief Send HTTP request over socket
 */
int http_client_send_raw_request(socket_t socket_fd, const char* request, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (socket_fd == MCP_INVALID_SOCKET || request == NULL) {
        return -1;
    }

    size_t request_len = strlen(request);
    int result = mcp_socket_send_exact(socket_fd, request, request_len, NULL);
    if (result != 0) {
        mcp_log_error("Failed to send request");
        return -1;
    }
    return 0;
}

/**
 * @brief Receive HTTP response over socket
 */
int http_client_receive_response(socket_t socket_fd, char* buffer, size_t buffer_size, uint32_t timeout_ms) {
    if (socket_fd == MCP_INVALID_SOCKET || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    size_t received = 0;
    bool headers_complete = false;
    size_t content_length = 0;
    size_t headers_end = 0;

    while (received < buffer_size - 1) {
        ssize_t bytes_received = socket_recv_with_timeout(socket_fd, buffer + received, buffer_size - received - 1, timeout_ms);
        if (bytes_received < 0) {
            if (received > 0) {
                break; // We have some data, might be complete
            }
            mcp_log_error("Receive timeout or error");
            return -1;
        }

        if (bytes_received == 0) {
            break; // Connection closed
        }

        received += bytes_received;
        buffer[received] = '\0';

        // Check if headers are complete
        if (!headers_complete) {
            char* headers_end_ptr = strstr(buffer, "\r\n\r\n");
            if (headers_end_ptr) {
                headers_complete = true;
                headers_end = headers_end_ptr - buffer + 4;

                // Extract Content-Length
                char* content_length_header = strstr(buffer, "Content-Length:");
                if (content_length_header) {
                    content_length = strtoul(content_length_header + 15, NULL, 10);
                } else {
                    // Try case-insensitive search
                    content_length_header = strstr(buffer, "content-length:");
                    if (content_length_header) {
                        content_length = strtoul(content_length_header + 15, NULL, 10);
                    }
                }
            }
        }

        // Check if we have received the complete response
        if (headers_complete) {
            size_t expected_total = headers_end + content_length;
            if (content_length == 0 || received >= expected_total) {
                break;
            }
        }
    }

    mcp_log_debug("Received %zu bytes", received);
    return (int)received;
}

/**
 * @brief Send HTTP POST request
 */
int http_client_send_request(sthttp_client_data_t* data, const char* json_data, http_response_t* response) {
    if (data == NULL || json_data == NULL || response == NULL) {
        return -1;
    }

    // Create socket connection
    socket_t socket_fd = http_client_create_socket(data->config.host, data->config.port, data->config.connect_timeout_ms);
    if (socket_fd == MCP_INVALID_SOCKET) {
        return -1;
    }

    // Build HTTP request
    char* request = http_client_build_request(data, "POST", json_data);
    if (request == NULL) {
        mcp_socket_close(socket_fd);
        return -1;
    }

    // Send request
    int result = http_client_send_raw_request(socket_fd, request, data->config.request_timeout_ms);
    free(request);

    if (result != 0) {
        mcp_socket_close(socket_fd);
        return -1;
    }

    // Receive response
    char* response_buffer = (char*)malloc(HTTP_CLIENT_BUFFER_SIZE);
    if (response_buffer == NULL) {
        mcp_socket_close(socket_fd);
        return -1;
    }

    int response_length = http_client_receive_response(socket_fd, response_buffer, HTTP_CLIENT_BUFFER_SIZE, data->config.request_timeout_ms);
    mcp_socket_close(socket_fd);
    
    if (response_length <= 0) {
        free(response_buffer);
        return -1;
    }

    // Parse response
    result = http_client_parse_response(response_buffer, response_length, response);
    free(response_buffer);
    
    // Extract session ID if sessions are enabled
    if (result == 0 && data->config.enable_sessions && response->headers) {
        char* session_id = http_client_extract_session_id(response->headers);
        if (session_id) {
            free(data->session_id);
            data->session_id = session_id;
            data->has_session = true;
            mcp_log_debug("Session ID updated: %s", session_id);
        }
    }

    return result;
}
