#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/mcp_http_client_request.h"
#include "internal/mcp_http_client_internal.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_socket_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Include socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// On Windows, strncasecmp is _strnicmp
#define strncasecmp _strnicmp
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

/**
 * @brief Creates an HTTP response structure.
 */
static http_response_t* http_response_create(char* data, size_t size, int status_code, const char* content_type) {
    http_response_t* response = (http_response_t*)malloc(sizeof(http_response_t));
    if (response == NULL) {
        return NULL;
    }

    response->data = data;
    response->size = size;
    response->status_code = status_code;
    response->content_type = content_type ? mcp_strdup(content_type) : NULL;

    return response;
}

/**
 * @brief Frees an HTTP response structure.
 */
void http_response_free(http_response_t* response) {
    if (response == NULL) {
        return;
    }

    if (response->data) {
        free(response->data);
    }

    if (response->content_type) {
        free(response->content_type);
    }

    free(response);
}

/**
 * @brief Sends an HTTP POST request.
 *
 * This is a simplified implementation that uses sockets directly.
 * In a production environment, you might want to use a more robust HTTP client library.
 */
http_response_t* http_post_request(const char* url, const char* content_type,
                                  const void* data, size_t size,
                                  const char* api_key, uint32_t timeout_ms) {
    if (url == NULL || data == NULL || size == 0) {
        return NULL;
    }

    // Parse URL to get host, port, and path
    char* url_copy = mcp_strdup(url);
    if (url_copy == NULL) {
        return NULL;
    }

    // Skip http:// or https:// prefix
    char* host_start = url_copy;
    bool use_ssl = false;

    if (strncmp(url_copy, "http://", 7) == 0) {
        host_start = url_copy + 7;
    } else if (strncmp(url_copy, "https://", 8) == 0) {
        host_start = url_copy + 8;
        use_ssl = true;
    }

    // Find path
    char* path = strchr(host_start, '/');
    if (path == NULL) {
        path = "/";
    } else {
        *path = '\0';
        path++;
    }

    // Find port
    char* port_str = strchr(host_start, ':');
    int port = use_ssl ? 443 : 80;

    if (port_str != NULL) {
        *port_str = '\0';
        port_str++;
        port = atoi(port_str);
    }

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to create socket");
        free(url_copy);
        return NULL;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        mcp_log_error("Failed to set socket receive timeout");
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        mcp_log_error("Failed to set socket send timeout");
    }

    // Resolve host
    struct hostent* server = gethostbyname(host_start);
    if (server == NULL) {
        mcp_log_error("Failed to resolve host: %s", host_start);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == MCP_SOCKET_ERROR) {
        mcp_log_error("Failed to connect to server: %s:%d", host_start, port);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // TODO: Implement SSL support if needed
    if (use_ssl) {
        mcp_log_error("SSL not implemented yet");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Build HTTP request
    char request[4096];
    int request_len = snprintf(request, sizeof(request),
                              "POST /%s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n",
                              path, host_start, content_type, size);

    // Add API key if provided
    if (api_key != NULL) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Authorization: Bearer %s\r\n", api_key);
    }

    // End headers
    request_len += snprintf(request + request_len, sizeof(request) - request_len, "\r\n");

    // Send request headers
    int sent_len = send(sock, request, request_len, 0);
    if (sent_len != request_len) {
        mcp_log_error("Failed to send HTTP request headers: actual: %d, expected: %d", sent_len, request_len);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Send request body
    sent_len = send(sock, data, (int)size, 0);
    if (sent_len != (int)size) {
        mcp_log_error("Failed to send HTTP request body: actual: %d, expected: %d", sent_len, size);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Read response using select for proper timeout handling
    char buffer[4096];
    int bytes_read = 0;
    char* response_data = NULL;
    size_t response_size = 0;
    int status_code = 0;
    char* content_type_value = NULL;

    // Read headers
    bool headers_done = false;
    char header_buffer[4096] = {0};
    int header_pos = 0;
    int content_length = -1;  // -1 means not specified
    bool chunked_encoding = false;

    // Set up select timeout
    fd_set readfds;
    int select_result;
    time_t start_time = time(NULL);
    time_t current_time;

    mcp_log_info("Waiting for server response with timeout: %u ms", timeout_ms);

    while (!headers_done) {
        // Calculate remaining timeout
        current_time = time(NULL);
        unsigned long elapsed_ms = (unsigned long)((current_time - start_time) * 1000);
        if (elapsed_ms >= timeout_ms) {
            mcp_log_error("Timeout waiting for HTTP response headers");
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        }

        // Set up select parameters
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // Calculate remaining timeout
        unsigned long remaining_ms = timeout_ms - elapsed_ms;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        // Wait for socket to be readable
        select_result = select((int)sock + 1, &readfds, NULL, NULL, &tv);

        if (select_result == -1) {
            // Select error
            mcp_log_error("Select failed with error: %d", mcp_socket_get_last_error());
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        } else if (select_result == 0) {
            // Select timeout
            mcp_log_error("Select timed out waiting for HTTP response");
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        }

        // Socket is readable, receive data
        bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            mcp_log_error("recv returned %d, error:%d", bytes_read, mcp_socket_get_last_error());
            break;
        }
        buffer[bytes_read] = '\0';

        // Copy to header buffer
        if (header_pos + bytes_read < sizeof(header_buffer)) {
            memcpy(header_buffer + header_pos, buffer, bytes_read);
            header_pos += bytes_read;
            header_buffer[header_pos] = '\0';
        }

        // Check if headers are complete
        if (strstr(header_buffer, "\r\n\r\n") != NULL) {
            headers_done = true;

            // Make a copy of the header buffer for parsing
            char* header_copy = mcp_strdup(header_buffer);
            if (header_copy != NULL) {
                // Parse status code
                char* status_line = strtok(header_copy, "\r\n");
                if (status_line != NULL && strncmp(status_line, "HTTP/1.", 7) == 0) {
                    status_code = atoi(status_line + 9);
                }

                // Parse headers
                char* header_line = strtok(NULL, "\r\n");
                while (header_line != NULL) {
                    // Parse Content-Type
                    if (strncasecmp(header_line, "Content-Type:", 13) == 0) {
                        content_type_value = mcp_strdup(header_line + 14);
                        // Trim leading spaces
                        while (*content_type_value == ' ') {
                            content_type_value++;
                        }
                    }
                    // Parse Content-Length
                    else if (strncasecmp(header_line, "Content-Length:", 15) == 0) {
                        content_length = atoi(header_line + 15);
                        mcp_log_debug("Content-Length: %d", content_length);
                    }
                    // Parse Transfer-Encoding
                    else if (strncasecmp(header_line, "Transfer-Encoding:", 18) == 0) {
                        if (strstr(header_line + 18, "chunked") != NULL) {
                            chunked_encoding = true;
                            mcp_log_debug("Transfer-Encoding: chunked");
                        }
                    }

                    header_line = strtok(NULL, "\r\n");
                }

                // Free the copy
                free(header_copy);
            }

            // Find body start
            char* body_start = strstr(header_buffer, "\r\n\r\n") + 4;
            int body_bytes = header_pos - (int)(body_start - header_buffer);

            if (body_bytes > 0) {
                response_data = (char*)malloc(body_bytes);
                if (response_data != NULL) {
                    memcpy(response_data, body_start, body_bytes);
                    response_size = body_bytes;
                }
            }
        }
    }

    // Read body with proper timeout handling
    if (headers_done) {
        // Reset start time for body reading
        start_time = time(NULL);

        // If we have Content-Length, we know exactly how much data to read
        if (content_length > 0) {
            mcp_log_debug("Reading body with Content-Length: %d (already read: %zu bytes)",
                         content_length, response_size);

            // Continue reading until we have the full content or timeout
            while (response_size < (size_t)content_length) {
                // Calculate remaining timeout
                current_time = time(NULL);
                unsigned long elapsed_ms = (unsigned long)((current_time - start_time) * 1000);
                if (elapsed_ms >= timeout_ms) {
                    mcp_log_warn("Timeout reading HTTP response body, returning partial response (%zu/%d bytes)",
                                response_size, content_length);
                    break;
                }

                // Check if we need to read more data
                size_t remaining_bytes = content_length - response_size;
                if (remaining_bytes == 0) {
                    mcp_log_debug("Received complete response body (%zu bytes)", response_size);
                    break;
                }

                // Set up select with a short timeout (100ms)
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                // Use a short timeout for select to avoid blocking too long
                struct timeval short_tv;
                short_tv.tv_sec = 0;
                short_tv.tv_usec = 100000;  // 100ms

                select_result = select((int)sock + 1, &readfds, NULL, NULL, &short_tv);

                if (select_result == -1) {
                    // Select error
                    mcp_log_error("Select failed during body read with error: %d", mcp_socket_get_last_error());
                    break;
                } else if (select_result == 0) {
                    // Short timeout, just continue the loop
                    continue;
                }

                // Socket is readable, receive data
                size_t to_read = sizeof(buffer);
                if (to_read > remaining_bytes) {
                    to_read = remaining_bytes;
                }

                bytes_read = recv(sock, buffer, (int)to_read, 0);
                if (bytes_read <= 0) {
                    // End of data or error
                    if (bytes_read < 0) {
                        mcp_log_error("recv failed during body read with error: %d", mcp_socket_get_last_error());
                    } else {
                        mcp_log_debug("Connection closed by server after reading %zu/%d bytes",
                                     response_size, content_length);
                    }
                    break;
                }

                // Allocate or expand buffer for response data
                char* new_data = (char*)realloc(response_data, response_size + bytes_read);
                if (new_data == NULL) {
                    free(response_data);
                    free(url_copy);
                    mcp_log_error("Failed to allocate memory for HTTP response body");
                    mcp_socket_close(sock);
                    return NULL;
                }

                response_data = new_data;
                memcpy(response_data + response_size, buffer, bytes_read);
                response_size += bytes_read;

                mcp_log_debug("Read %d bytes, total: %zu/%d", bytes_read, response_size, content_length);

                // If we've read all the data, we're done
                if (response_size >= (size_t)content_length) {
                    mcp_log_debug("Received complete response body (%zu bytes)", response_size);
                    break;
                }
            }
        }
        // For chunked encoding or unknown length, read until connection closed or timeout
        else {
            mcp_log_debug("Reading body with %s encoding",
                         chunked_encoding ? "chunked" : "unknown length");

            // For simplicity, we'll just read until the connection is closed or timeout
            // A proper implementation would parse chunked encoding
            while (1) {
                // Calculate remaining timeout
                current_time = time(NULL);
                unsigned long elapsed_ms = (unsigned long)((current_time - start_time) * 1000);
                if (elapsed_ms >= timeout_ms) {
                    mcp_log_warn("Timeout reading HTTP response body, returning partial response");
                    break;
                }

                // Set up select with a short timeout (100ms)
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                // Use a short timeout for select to avoid blocking too long
                struct timeval short_tv;
                short_tv.tv_sec = 0;
                short_tv.tv_usec = 100000;  // 100ms

                select_result = select((int)sock + 1, &readfds, NULL, NULL, &short_tv);

                if (select_result == -1) {
                    // Select error
                    mcp_log_error("Select failed during body read with error: %d", mcp_socket_get_last_error());
                    break;
                } else if (select_result == 0) {
                    // Short timeout, just continue the loop
                    continue;
                }

                // Socket is readable, receive data
                bytes_read = recv(sock, buffer, sizeof(buffer), 0);
                if (bytes_read <= 0) {
                    // End of data or error
                    if (bytes_read < 0) {
                        mcp_log_error("recv failed during body read with error: %d", mcp_socket_get_last_error());
                    } else {
                        mcp_log_debug("Connection closed by server after reading %zu bytes", response_size);
                    }
                    break;
                }

                // Allocate or expand buffer for response data
                char* new_data = (char*)realloc(response_data, response_size + bytes_read);
                if (new_data == NULL) {
                    free(response_data);
                    free(url_copy);
                    mcp_log_error("Failed to allocate memory for HTTP response body");
                    mcp_socket_close(sock);
                    return NULL;
                }

                response_data = new_data;
                memcpy(response_data + response_size, buffer, bytes_read);
                response_size += bytes_read;

                mcp_log_debug("Read %d bytes, total: %zu", bytes_read, response_size);
            }
        }
    }

    // Clean up
    free(url_copy);
    mcp_socket_close(sock);

    // Create response
    http_response_t* response = http_response_create(response_data, response_size, status_code, content_type_value);

    // Free content type value (it's copied in http_response_create)
    if (content_type_value != NULL) {
        free(content_type_value);
    }

    return response;
}
