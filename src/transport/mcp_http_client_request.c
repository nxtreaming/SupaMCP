#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/http_client_request.h"
#include "internal/http_client_internal.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_socket_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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

// Maximum buffer size for HTTP headers
#define HTTP_HEADER_BUFFER_SIZE 4096
// Maximum buffer size for reading from socket
#define HTTP_READ_BUFFER_SIZE 4096
// Default select timeout in milliseconds
#define HTTP_SELECT_TIMEOUT_MS 100

/**
 * @brief Creates an HTTP response structure.
 *
 * @param data Response body data (will be owned by the response)
 * @param size Size of the response data
 * @param status_code HTTP status code
 * @param content_type Content type of the response (will be copied)
 * @return http_response_t* Newly created response or NULL on failure
 */
static http_response_t* http_response_create(char* data, size_t size, int status_code, const char* content_type) {
    http_response_t* response = (http_response_t*)malloc(sizeof(http_response_t));
    if (response == NULL) {
        mcp_log_error("Failed to allocate memory for HTTP response");
        return NULL;
    }

    // Initialize all fields
    response->data = data;
    response->size = size;
    response->status_code = status_code;
    response->content_type = NULL;

    // Copy content type if provided
    if (content_type) {
        response->content_type = mcp_strdup(content_type);
        if (response->content_type == NULL) {
            mcp_log_error("Failed to allocate memory for content type");
            free(response);
            return NULL;
        }
    }

    return response;
}

/**
 * @brief Frees an HTTP response structure and all associated memory.
 *
 * @param response The response to free
 */
void http_response_free(http_response_t* response) {
    if (response == NULL) {
        return;
    }

    // Free all allocated memory
    if (response->data) {
        free(response->data);
        response->data = NULL;
    }

    if (response->content_type) {
        free(response->content_type);
        response->content_type = NULL;
    }

    // Free the response structure itself
    free(response);
}

/**
 * @brief Parse a URL into host, port, path, and SSL flag
 *
 * @param url The URL to parse
 * @param host_out Pointer to store the allocated host string
 * @param port_out Pointer to store the port number
 * @param path_out Pointer to store the path within the host string
 * @param use_ssl_out Pointer to store the SSL flag
 * @return true if parsing was successful, false otherwise
 */
static bool parse_url(const char* url, char** host_out, int* port_out, char** path_out, bool* use_ssl_out) {
    if (url == NULL || host_out == NULL || port_out == NULL || path_out == NULL || use_ssl_out == NULL) {
        return false;
    }

    // Make a copy of the URL for parsing
    char* url_copy = mcp_strdup(url);
    if (url_copy == NULL) {
        mcp_log_error("Failed to allocate memory for URL copy");
        return false;
    }

    // Default values
    *use_ssl_out = false;
    *port_out = 80;

    // Skip http:// or https:// prefix
    char* host_start = url_copy;

    if (strncmp(url_copy, "http://", 7) == 0) {
        host_start = url_copy + 7;
    } else if (strncmp(url_copy, "https://", 8) == 0) {
        host_start = url_copy + 8;
        *use_ssl_out = true;
        *port_out = 443;
    }

    // Find path
    char* path = strchr(host_start, '/');
    if (path == NULL) {
        *path_out = mcp_strdup("/");
        if (*path_out == NULL) {
            free(url_copy);
            return false;
        }
    } else {
        *path = '\0';  // Terminate host part
        *path_out = mcp_strdup(path + 1);
        if (*path_out == NULL) {
            free(url_copy);
            return false;
        }
    }

    // Find port
    char* port_str = strchr(host_start, ':');
    if (port_str != NULL) {
        *port_str = '\0';  // Terminate host part
        port_str++;
        *port_out = atoi(port_str);
    }

    // Copy host
    *host_out = mcp_strdup(host_start);
    if (*host_out == NULL) {
        free(*path_out);
        free(url_copy);
        return false;
    }

    free(url_copy);
    return true;
}

/**
 * @brief Set up a socket with proper timeouts
 *
 * @param timeout_ms Timeout in milliseconds
 * @return socket_t The created socket or MCP_INVALID_SOCKET on failure
 */
static socket_t setup_socket(uint32_t timeout_ms) {
    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to create socket: %d", mcp_socket_get_lasterror());
        return MCP_INVALID_SOCKET;
    }

    // Set timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        mcp_log_error("Failed to set socket receive timeout: %d", mcp_socket_get_lasterror());
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        mcp_log_error("Failed to set socket send timeout: %d", mcp_socket_get_lasterror());
    }

    return sock;
}

/**
 * @brief Connect to a server using the given host and port
 *
 * @param sock The socket to use
 * @param host The host to connect to
 * @param port The port to connect to
 * @return true if connection was successful, false otherwise
 */
static bool connect_to_server(socket_t sock, const char* host, int port) {
    if (sock == MCP_INVALID_SOCKET || host == NULL) {
        return false;
    }

    // Resolve host
    struct hostent* server = gethostbyname(host);
    if (server == NULL) {
        mcp_log_error("Failed to resolve host: %s (error: %d)", host, mcp_socket_get_lasterror());
        return false;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == MCP_SOCKET_ERROR) {
        mcp_log_error("Failed to connect to server: %s:%d (error: %d)",
                     host, port, mcp_socket_get_lasterror());
        return false;
    }

    return true;
}

/**
 * @brief Build an HTTP request string
 *
 * @param buffer Buffer to store the request
 * @param buffer_size Size of the buffer
 * @param path Path part of the URL
 * @param host Host to connect to
 * @param content_type Content type of the request
 * @param data_size Size of the request body
 * @param api_key Optional API key for authentication
 * @return int Length of the request or -1 on failure
 */
static int build_http_request(char* buffer, size_t buffer_size, const char* path,
                             const char* host, const char* content_type,
                             size_t data_size, const char* api_key) {
    if (buffer == NULL || path == NULL || host == NULL || content_type == NULL) {
        return -1;
    }

    int request_len = snprintf(buffer, buffer_size,
                              "POST /%s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n",
                              path, host, content_type, data_size);

    // Check for buffer overflow
    if (request_len < 0 || (size_t)request_len >= buffer_size) {
        mcp_log_error("HTTP request buffer too small");
        return -1;
    }

    // Add API key if provided
    if (api_key != NULL) {
        int api_key_len = snprintf(buffer + request_len, buffer_size - request_len,
                                  "Authorization: Bearer %s\r\n", api_key);

        // Check for buffer overflow
        if (api_key_len < 0 || (size_t)(request_len + api_key_len) >= buffer_size) {
            mcp_log_error("HTTP request buffer too small for API key");
            return -1;
        }

        request_len += api_key_len;
    }

    // End headers
    int end_len = snprintf(buffer + request_len, buffer_size - request_len, "\r\n");

    // Check for buffer overflow
    if (end_len < 0 || (size_t)(request_len + end_len) >= buffer_size) {
        mcp_log_error("HTTP request buffer too small for end headers");
        return -1;
    }

    return request_len + end_len;
}

/**
 * @brief Wait for socket to be readable with timeout
 *
 * @param sock Socket to wait for
 * @param timeout_ms Timeout in milliseconds
 * @return int 1 if socket is readable, 0 on timeout, -1 on error
 */
static int wait_for_socket(socket_t sock, uint32_t timeout_ms) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int result = select((int)sock + 1, &readfds, NULL, NULL, &tv);

    if (result == -1) {
        mcp_log_error("Select failed with error: %d", mcp_socket_get_lasterror());
    }

    return result;
}

/**
 * @brief Parse HTTP headers from a buffer
 *
 * @param header_buffer Buffer containing HTTP headers
 * @param status_code_out Pointer to store the status code
 * @param content_type_out Pointer to store the allocated content type string
 * @param content_length_out Pointer to store the content length
 * @param chunked_out Pointer to store the chunked encoding flag
 * @param body_start_out Pointer to store the pointer to the body start in the buffer
 * @return true if parsing was successful, false otherwise
 */
static bool parse_http_headers(char* header_buffer, int* status_code_out,
                              char** content_type_out, int* content_length_out,
                              bool* chunked_out, char** body_start_out) {
    if (header_buffer == NULL || status_code_out == NULL || content_type_out == NULL ||
        content_length_out == NULL || chunked_out == NULL || body_start_out == NULL) {
        return false;
    }

    // Initialize output parameters
    *status_code_out = 0;
    *content_type_out = NULL;
    *content_length_out = -1;
    *chunked_out = false;
    *body_start_out = NULL;

    // Find body start
    *body_start_out = strstr(header_buffer, "\r\n\r\n");
    if (*body_start_out == NULL) {
        mcp_log_error("Invalid HTTP response: no end of headers found");
        return false;
    }

    // Move pointer past the header delimiter
    *body_start_out += 4;

    // Make a copy of the header buffer for parsing
    char* header_copy = mcp_strdup(header_buffer);
    if (header_copy == NULL) {
        mcp_log_error("Failed to allocate memory for header parsing");
        return false;
    }

    // Parse status code
    char* status_line = strtok(header_copy, "\r\n");
    if (status_line != NULL && strncmp(status_line, "HTTP/1.", 7) == 0) {
        *status_code_out = atoi(status_line + 9);
    } else {
        mcp_log_error("Invalid HTTP response: no status line found");
        free(header_copy);
        return false;
    }

    // Parse headers
    char* header_line = strtok(NULL, "\r\n");
    while (header_line != NULL) {
        // Parse Content-Type
        if (strncasecmp(header_line, "Content-Type:", 13) == 0) {
            char* value = header_line + 13;
            // Trim leading spaces
            while (*value == ' ') {
                value++;
            }
            *content_type_out = mcp_strdup(value);
        }
        // Parse Content-Length
        else if (strncasecmp(header_line, "Content-Length:", 15) == 0) {
            *content_length_out = atoi(header_line + 15);
            mcp_log_debug("Content-Length: %d", *content_length_out);
        }
        // Parse Transfer-Encoding
        else if (strncasecmp(header_line, "Transfer-Encoding:", 18) == 0) {
            if (strstr(header_line + 18, "chunked") != NULL) {
                *chunked_out = true;
                mcp_log_debug("Transfer-Encoding: chunked");
            }
        }

        header_line = strtok(NULL, "\r\n");
    }

    free(header_copy);
    return true;
}

/**
 * @brief Read HTTP response headers from a socket
 *
 * @param sock Socket to read from
 * @param timeout_ms Timeout in milliseconds
 * @param status_code_out Pointer to store the status code
 * @param content_type_out Pointer to store the allocated content type string
 * @param content_length_out Pointer to store the content length
 * @param chunked_out Pointer to store the chunked encoding flag
 * @param response_data_out Pointer to store the allocated initial response data
 * @param response_size_out Pointer to store the initial response data size
 * @return true if reading was successful, false otherwise
 */
static bool read_http_headers(socket_t sock, uint32_t timeout_ms,
                             int* status_code_out, char** content_type_out,
                             int* content_length_out, bool* chunked_out,
                             char** response_data_out, size_t* response_size_out) {
    if (sock == MCP_INVALID_SOCKET || status_code_out == NULL || content_type_out == NULL ||
        content_length_out == NULL || chunked_out == NULL ||
        response_data_out == NULL || response_size_out == NULL) {
        return false;
    }

    // Initialize output parameters
    *status_code_out = 0;
    *content_type_out = NULL;
    *content_length_out = -1;
    *chunked_out = false;
    *response_data_out = NULL;
    *response_size_out = 0;

    char buffer[HTTP_READ_BUFFER_SIZE];
    char header_buffer[HTTP_HEADER_BUFFER_SIZE] = {0};
    int header_pos = 0;
    bool headers_done = false;
    int bytes_read;

    // Set up timeout tracking
    time_t start_time = time(NULL);
    time_t current_time;

    mcp_log_info("Waiting for server response with timeout: %u ms", timeout_ms);

    while (!headers_done) {
        // Check for timeout
        current_time = time(NULL);
        unsigned long elapsed_ms = (unsigned long)((current_time - start_time) * 1000);
        if (elapsed_ms >= timeout_ms) {
            mcp_log_error("Timeout waiting for HTTP response headers");
            return false;
        }

        // Calculate remaining timeout
        unsigned long remaining_ms = timeout_ms - elapsed_ms;

        // Wait for socket to be readable
        int select_result = wait_for_socket(sock, remaining_ms);

        if (select_result <= 0) {
            // Error or timeout
            return false;
        }

        // Socket is readable, receive data
        bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            mcp_log_error("recv returned %d, error: %d", bytes_read, mcp_socket_get_lasterror());
            return false;
        }
        buffer[bytes_read] = '\0';

        // Copy to header buffer
        if (header_pos + bytes_read < (int)sizeof(header_buffer)) {
            memcpy(header_buffer + header_pos, buffer, bytes_read);
            header_pos += bytes_read;
            header_buffer[header_pos] = '\0';
        } else {
            mcp_log_error("HTTP header buffer overflow");
            return false;
        }

        // Check if headers are complete
        if (strstr(header_buffer, "\r\n\r\n") != NULL) {
            headers_done = true;

            // Parse headers
            char* body_start;
            if (!parse_http_headers(header_buffer, status_code_out, content_type_out,
                                   content_length_out, chunked_out, &body_start)) {
                return false;
            }

            // Calculate body bytes already received
            int body_bytes = header_pos - (int)(body_start - header_buffer);

            if (body_bytes > 0) {
                *response_data_out = (char*)malloc(body_bytes);
                if (*response_data_out == NULL) {
                    mcp_log_error("Failed to allocate memory for response data");
                    return false;
                }

                memcpy(*response_data_out, body_start, body_bytes);
                *response_size_out = body_bytes;
            }
        }
    }

    return true;
}

/**
 * @brief Read HTTP response body with Content-Length
 *
 * @param sock Socket to read from
 * @param timeout_ms Timeout in milliseconds
 * @param content_length Expected content length
 * @param response_data Pointer to the response data buffer (will be reallocated)
 * @param response_size Pointer to the current response data size
 * @return true if reading was successful, false otherwise
 */
static bool read_http_body_with_length(socket_t sock, uint32_t timeout_ms,
                                      int content_length, char** response_data,
                                      size_t* response_size) {
    if (sock == MCP_INVALID_SOCKET || response_data == NULL || response_size == NULL) {
        return false;
    }

    // If we already have all the data, return success
    if (*response_size >= (size_t)content_length) {
        mcp_log_debug("Already received complete response body (%zu bytes)", *response_size);
        return true;
    }

    char buffer[HTTP_READ_BUFFER_SIZE];
    int bytes_read;

    // Set up timeout tracking
    time_t start_time = time(NULL);
    time_t current_time;

    mcp_log_debug("Reading body with Content-Length: %d (already read: %zu bytes)",
                 content_length, *response_size);

    // Continue reading until we have the full content or timeout
    while (*response_size < (size_t)content_length) {
        // Check for timeout
        current_time = time(NULL);
        unsigned long elapsed_ms = (unsigned long)((current_time - start_time) * 1000);
        if (elapsed_ms >= timeout_ms) {
            mcp_log_warn("Timeout reading HTTP response body, returning partial response (%zu/%d bytes)",
                        *response_size, content_length);
            return true;  // Return true with partial data
        }

        // Calculate remaining timeout for select
        unsigned long remaining_ms = timeout_ms - elapsed_ms;
        if (remaining_ms > HTTP_SELECT_TIMEOUT_MS) {
            remaining_ms = HTTP_SELECT_TIMEOUT_MS;  // Use shorter timeout for select
        }

        // Wait for socket to be readable
        int select_result = wait_for_socket(sock, remaining_ms);

        if (select_result < 0) {
            // Error
            return false;
        } else if (select_result == 0) {
            // Short timeout, just continue the loop
            continue;
        }

        // Socket is readable, receive data
        size_t remaining_bytes = content_length - *response_size;
        size_t to_read = sizeof(buffer);
        if (to_read > remaining_bytes) {
            to_read = remaining_bytes;
        }

        bytes_read = recv(sock, buffer, (int)to_read, 0);
        if (bytes_read <= 0) {
            // End of data or error
            if (bytes_read < 0) {
                mcp_log_error("recv failed during body read with error: %d", mcp_socket_get_lasterror());
                return false;
            } else {
                mcp_log_debug("Connection closed by server after reading %zu/%d bytes",
                             *response_size, content_length);
                return true;  // Return true with partial data
            }
        }

        // Allocate or expand buffer for response data
        char* new_data = (char*)realloc(*response_data, *response_size + bytes_read);
        if (new_data == NULL) {
            mcp_log_error("Failed to allocate memory for HTTP response body");
            return false;
        }

        *response_data = new_data;
        memcpy(*response_data + *response_size, buffer, bytes_read);
        *response_size += bytes_read;

        mcp_log_debug("Read %d bytes, total: %zu/%d", bytes_read, *response_size, content_length);
    }

    mcp_log_debug("Received complete response body (%zu bytes)", *response_size);
    return true;
}

/**
 * @brief Read HTTP response body with unknown length or chunked encoding
 *
 * @param sock Socket to read from
 * @param timeout_ms Timeout in milliseconds
 * @param chunked Whether chunked encoding is used
 * @param response_data Pointer to the response data buffer (will be reallocated)
 * @param response_size Pointer to the current response data size
 * @return true if reading was successful, false otherwise
 */
static bool read_http_body_unknown_length(socket_t sock, uint32_t timeout_ms,
                                         bool chunked, char** response_data,
                                         size_t* response_size) {
    if (sock == MCP_INVALID_SOCKET || response_data == NULL || response_size == NULL) {
        return false;
    }

    char buffer[HTTP_READ_BUFFER_SIZE];
    int bytes_read;

    // Set up timeout tracking
    time_t start_time = time(NULL);
    time_t current_time;

    mcp_log_debug("Reading body with %s encoding",
                 chunked ? "chunked" : "unknown length");

    // For simplicity, we'll just read until the connection is closed or timeout
    // A proper implementation would parse chunked encoding
    while (1) {
        // Check for timeout
        current_time = time(NULL);
        unsigned long elapsed_ms = (unsigned long)((current_time - start_time) * 1000);
        if (elapsed_ms >= timeout_ms) {
            mcp_log_warn("Timeout reading HTTP response body, returning partial response");
            return true;  // Return true with partial data
        }

        // Calculate remaining timeout for select
        unsigned long remaining_ms = timeout_ms - elapsed_ms;
        if (remaining_ms > HTTP_SELECT_TIMEOUT_MS) {
            remaining_ms = HTTP_SELECT_TIMEOUT_MS;  // Use shorter timeout for select
        }

        // Wait for socket to be readable
        int select_result = wait_for_socket(sock, remaining_ms);

        if (select_result < 0) {
            // Error
            return false;
        } else if (select_result == 0) {
            // Short timeout, just continue the loop
            continue;
        }

        // Socket is readable, receive data
        bytes_read = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            // End of data or error
            if (bytes_read < 0) {
                mcp_log_error("recv failed during body read with error: %d", mcp_socket_get_lasterror());
                return false;
            } else {
                mcp_log_debug("Connection closed by server after reading %zu bytes", *response_size);
                return true;  // Connection closed normally
            }
        }

        // Allocate or expand buffer for response data
        char* new_data = (char*)realloc(*response_data, *response_size + bytes_read);
        if (new_data == NULL) {
            mcp_log_error("Failed to allocate memory for HTTP response body");
            return false;
        }

        *response_data = new_data;
        memcpy(*response_data + *response_size, buffer, bytes_read);
        *response_size += bytes_read;

        mcp_log_debug("Read %d bytes, total: %zu", bytes_read, *response_size);
    }
}

/**
 * @brief Sends an HTTP POST request.
 *
 * This is a simplified implementation that uses sockets directly.
 * In a production environment, you might want to use a more robust HTTP client library.
 *
 * @param url The URL to send the request to
 * @param content_type Content type of the request
 * @param data Request body data
 * @param size Size of the request body
 * @param api_key Optional API key for authentication
 * @param timeout_ms Timeout in milliseconds
 * @return http_response_t* Response structure or NULL on failure
 */
http_response_t* http_post_request(const char* url, const char* content_type,
                                  const void* data, size_t size,
                                  const char* api_key, uint32_t timeout_ms) {
    // Validate input parameters
    if (url == NULL || data == NULL || size == 0 || content_type == NULL) {
        mcp_log_error("Invalid parameters for HTTP POST request");
        return NULL;
    }

    // Parse URL
    char* host = NULL;
    char* path = NULL;
    int port = 80;
    bool use_ssl = false;

    if (!parse_url(url, &host, &port, &path, &use_ssl)) {
        mcp_log_error("Failed to parse URL: %s", url);
        return NULL;
    }

    // Check for SSL (not implemented)
    if (use_ssl) {
        mcp_log_error("SSL not implemented yet");
        free(host);
        free(path);
        return NULL;
    }

    // Set up socket
    socket_t sock = setup_socket(timeout_ms);
    if (sock == MCP_INVALID_SOCKET) {
        free(host);
        free(path);
        return NULL;
    }

    // Connect to server
    if (!connect_to_server(sock, host, port)) {
        mcp_socket_close(sock);
        free(host);
        free(path);
        return NULL;
    }

    // Build HTTP request
    char request[HTTP_HEADER_BUFFER_SIZE];
    int request_len = build_http_request(request, sizeof(request), path, host,
                                        content_type, size, api_key);

    if (request_len < 0) {
        mcp_socket_close(sock);
        free(host);
        free(path);
        return NULL;
    }

    // Send request headers
    int sent_len = send(sock, request, request_len, 0);
    if (sent_len != request_len) {
        mcp_log_error("Failed to send HTTP request headers: actual: %d, expected: %d",
                     sent_len, request_len);
        mcp_socket_close(sock);
        free(host);
        free(path);
        return NULL;
    }

    // Send request body
    sent_len = send(sock, data, (int)size, 0);
    if (sent_len != (int)size) {
        mcp_log_error("Failed to send HTTP request body: actual: %d, expected: %d",
                     sent_len, (int)size);
        mcp_socket_close(sock);
        free(host);
        free(path);
        return NULL;
    }

    // Read response headers
    int status_code = 0;
    char* content_type_value = NULL;
    int content_length = -1;
    bool chunked_encoding = false;
    char* response_data = NULL;
    size_t response_size = 0;

    if (!read_http_headers(sock, timeout_ms, &status_code, &content_type_value,
                          &content_length, &chunked_encoding,
                          &response_data, &response_size)) {
        mcp_log_error("Failed to read HTTP response headers");
        if (response_data) {
            free(response_data);
        }
        if (content_type_value) {
            free(content_type_value);
        }
        mcp_socket_close(sock);
        free(host);
        free(path);
        return NULL;
    }

    // Read response body
    if (content_length > 0) {
        // Known content length
        if (!read_http_body_with_length(sock, timeout_ms, content_length,
                                       &response_data, &response_size)) {
            mcp_log_error("Failed to read HTTP response body with content length");
            if (response_data) {
                free(response_data);
            }
            if (content_type_value) {
                free(content_type_value);
            }
            mcp_socket_close(sock);
            free(host);
            free(path);
            return NULL;
        }
    } else {
        // Unknown content length or chunked encoding
        if (!read_http_body_unknown_length(sock, timeout_ms, chunked_encoding,
                                          &response_data, &response_size)) {
            mcp_log_error("Failed to read HTTP response body with unknown length");
            if (response_data) {
                free(response_data);
            }
            if (content_type_value) {
                free(content_type_value);
            }
            mcp_socket_close(sock);
            free(host);
            free(path);
            return NULL;
        }
    }

    // Clean up
    mcp_socket_close(sock);
    free(host);
    free(path);

    // Create response
    http_response_t* response = http_response_create(response_data, response_size,
                                                   status_code, content_type_value);

    // Free content type value (it's copied in http_response_create)
    if (content_type_value) {
        free(content_type_value);
    }

    // If response creation failed, free the response data
    if (response == NULL && response_data) {
        free(response_data);
    }

    return response;
}
