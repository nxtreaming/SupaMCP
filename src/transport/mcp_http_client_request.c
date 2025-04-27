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
    if (send(sock, request, request_len, 0) != request_len) {
        mcp_log_error("Failed to send HTTP request headers");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Send request body
    if (send(sock, data, (int)size, 0) != (int)size) {
        mcp_log_error("Failed to send HTTP request body");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Read response
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

    while (!headers_done && (bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
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

                // Parse content type
                char* header_line = strtok(NULL, "\r\n");
                while (header_line != NULL) {
                    if (strncasecmp(header_line, "Content-Type:", 13) == 0) {
                        content_type_value = mcp_strdup(header_line + 14);
                        // Trim leading spaces
                        while (*content_type_value == ' ') {
                            content_type_value++;
                        }
                        break;
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

    // Read body
    if (headers_done) {
        while ((bytes_read = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
            char* new_data = (char*)realloc(response_data, response_size + bytes_read);
            if (new_data == NULL) {
                free(response_data);
                free(url_copy);
                mcp_socket_close(sock);
                return NULL;
            }

            response_data = new_data;
            memcpy(response_data + response_size, buffer, bytes_read);
            response_size += bytes_read;
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
