#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "kmcp_http_client.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

/**
 * @brief Complete definition of HTTP client structure
 */
struct kmcp_http_client {
    char* base_url;        // Base URL
    char* host;            // Host name
    char* path;            // Path
    int port;              // Port
    bool use_ssl;          // Whether to use SSL
    char* api_key;         // API key
};

/**
 * @brief Validate URL for security
 *
 * @param url URL to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if URL is valid, or an error code otherwise
 */
static kmcp_error_t validate_url(const char* url) {
    if (!url) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum URL length
    size_t url_len = strlen(url);
    if (url_len < 8) { // At minimum "http://a"
        mcp_log_error("URL too short: %s", url);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for valid protocol
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        mcp_log_error("Invalid URL protocol: %s", url);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for invalid characters in URL
    const char* invalid_chars = "<>\"{}|\\^`\0";
    if (strpbrk(url, invalid_chars) != NULL) {
        mcp_log_error("URL contains invalid characters: %s", url);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Parse URL
 *
 * @param url URL
 * @param host Pointer to host name, memory allocated by function, caller responsible for freeing
 * @param path Pointer to path, memory allocated by function, caller responsible for freeing
 * @param port Pointer to port
 * @param use_ssl Pointer to whether to use SSL
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
static kmcp_error_t parse_url(const char* url, char** host, char** path, int* port, bool* use_ssl) {
    if (!url || !host || !path || !port || !use_ssl) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Validate URL first
    kmcp_error_t result = validate_url(url);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Initialize output parameters
    *host = NULL;
    *path = NULL;
    *port = 80;
    *use_ssl = false;

    // Check if URL starts with http:// or https://
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;
        *use_ssl = false;
        *port = 80;
    } else if (strncmp(url, "https://", 8) == 0) {
        url += 8;
        *use_ssl = true;
        *port = 443;
    } else {
        // Not a valid URL
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Find separator between host name and path
    const char* path_start = strchr(url, '/');
    if (!path_start) {
        // No path, use root path
        *host = mcp_strdup(url);
        *path = mcp_strdup("/");
    } else {
        // Has path
        size_t host_len = path_start - url;
        *host = (char*)malloc(host_len + 1);
        if (!*host) {
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }
        strncpy(*host, url, host_len);
        (*host)[host_len] = '\0';

        *path = mcp_strdup(path_start);
    }

    // Check if host name contains port
    char* port_start = strchr(*host, ':');
    if (port_start) {
        // Has port
        *port_start = '\0';
        *port = atoi(port_start + 1);
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Create an HTTP client
 */
kmcp_http_client_t* kmcp_http_client_create(const char* base_url, const char* api_key) {
    if (!base_url) {
        mcp_log_error("Invalid parameter: base_url is NULL");
        return NULL;
    }

    // Validate the base URL
    if (validate_url(base_url) != KMCP_SUCCESS) {
        mcp_log_error("Invalid base URL: %s", base_url);
        return NULL;
    }

    // Allocate memory
    kmcp_http_client_t* client = (kmcp_http_client_t*)malloc(sizeof(kmcp_http_client_t));
    if (!client) {
        mcp_log_error("Failed to allocate memory for HTTP client");
        return NULL;
    }

    // Initialize fields
    memset(client, 0, sizeof(kmcp_http_client_t));
    client->base_url = mcp_strdup(base_url);
    client->api_key = api_key ? mcp_strdup(api_key) : NULL;

    // Parse URL
    if (parse_url(base_url, &client->host, &client->path, &client->port, &client->use_ssl) != 0) {
        mcp_log_error("Failed to parse URL: %s", base_url);
        free(client->base_url);
        free(client->api_key);
        free(client);
        return NULL;
    }

    return client;
}

/**
 * @brief Send an HTTP request
 *
 * Note: This is a simple HTTP client implementation with basic SSL support.
 * For production use, it is recommended to use a mature HTTP client library such as libcurl.
 *
 * @warning The SSL implementation is basic and does not validate certificates properly.
 * Do not use for sensitive data in production environments.
 */
kmcp_error_t kmcp_http_client_send(
    kmcp_http_client_t* client,
    const char* method,
    const char* path,
    const char* content_type,
    const char* body,
    char** response,
    int* status
) {
    if (!client || !method || !path || !response || !status) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *response = NULL;
    *status = 0;

    // Build complete path
    char* full_path = NULL;
    if (path[0] == '/') {
        // Absolute path
        full_path = mcp_strdup(path);
    } else {
        // Relative path
        size_t base_path_len = strlen(client->path);
        size_t path_len = strlen(path);
        full_path = (char*)malloc(base_path_len + path_len + 2); // +2 for '/' and '\0'
        if (!full_path) {
            mcp_log_error("Failed to allocate memory for full path");
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        strcpy(full_path, client->path);
        if (client->path[base_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, path);
    }

    // Calculate the required request buffer size
    size_t request_size = 0;

    // Request line
    request_size += strlen(method) + strlen(full_path) + 20; // Method + path + HTTP/1.1 + CRLF + some extra

    // Host header
    request_size += strlen(client->host) + 10; // Host: + host + CRLF + some extra

    // Content-Type header
    if (content_type) {
        request_size += strlen(content_type) + 20; // Content-Type: + content_type + CRLF + some extra
    }

    // Content-Length header
    if (body) {
        request_size += 30; // Content-Length: + length + CRLF + some extra
    }

    // API key header
    if (client->api_key) {
        request_size += strlen(client->api_key) + 30; // Authorization: Bearer + api_key + CRLF + some extra
    }

    // Connection header
    request_size += 20; // Connection: close + CRLF + some extra

    // Empty line
    request_size += 3; // CRLF + some extra

    // Request body
    if (body) {
        request_size += strlen(body);
    }

    // Allocate request buffer with some extra space for safety
    char* request = (char*)malloc(request_size + 100); // Add 100 bytes extra for safety
    if (!request) {
        mcp_log_error("Failed to allocate memory for HTTP request");
        free(full_path);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    int request_len = 0;
    size_t remaining = request_size + 100;

    // Request line
    int written = snprintf(request + request_len, remaining, "%s %s HTTP/1.1\r\n", method, full_path);
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for request line");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Host header
    written = snprintf(request + request_len, remaining, "Host: %s\r\n", client->host);
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for host header");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Content-Type header
    if (content_type) {
        written = snprintf(request + request_len, remaining, "Content-Type: %s\r\n", content_type);
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for content-type header");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
        remaining -= written;
    }

    // Content-Length header
    if (body) {
        written = snprintf(request + request_len, remaining, "Content-Length: %zu\r\n", strlen(body));
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for content-length header");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
        remaining -= written;
    }

    // API key header
    if (client->api_key) {
        written = snprintf(request + request_len, remaining, "Authorization: Bearer %s\r\n", client->api_key);
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for authorization header");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
        remaining -= written;
    }

    // Connection header
    written = snprintf(request + request_len, remaining, "Connection: close\r\n");
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for connection header");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Empty line
    written = snprintf(request + request_len, remaining, "\r\n");
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for empty line");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Request body
    if (body) {
        written = snprintf(request + request_len, remaining, "%s", body);
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for request body");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
    }

    // Create socket
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        mcp_log_error("Failed to initialize Winsock");
        free(full_path);
        return KMCP_ERROR_CONNECTION_FAILED;
    }
#endif

#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    // Store socket for cleanup in case of error
    if (sock < 0) {
        mcp_log_error("Failed to create socket");
        free(full_path);
#ifdef _WIN32
        WSACleanup();
#endif
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Resolve host name
    struct hostent* host = gethostbyname(client->host);
    if (!host) {
        mcp_log_error("Failed to resolve host: %s", client->host);
        free(full_path);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)client->port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        mcp_log_error("Failed to connect to server: %s:%d", client->host, client->port);
        free(full_path);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // For HTTPS connections, we should establish SSL/TLS connection
    // This is a placeholder for SSL/TLS implementation
    // In a real implementation, you would use OpenSSL or another SSL library
    if (client->use_ssl) {
        mcp_log_warn("HTTPS connections are not fully supported in this implementation");
        mcp_log_warn("For secure connections, use a mature HTTP client library such as libcurl");
        // Here you would initialize SSL context, create SSL connection, etc.
        // For example with OpenSSL:
        // SSL_CTX* ctx = SSL_CTX_new(SSLv23_client_method());
        // SSL* ssl = SSL_new(ctx);
        // SSL_set_fd(ssl, sock);
        // SSL_connect(ssl);
    }

    // Send request
    int send_result;
    if (client->use_ssl) {
        // For SSL connections, you would use SSL_write instead of send
        // For example: send_result = SSL_write(ssl, request, request_len);
        // This is a placeholder for SSL/TLS implementation
        mcp_log_warn("HTTPS send not fully implemented, falling back to unencrypted send");
        send_result = send(sock, request, request_len, 0);
    } else {
        send_result = send(sock, request, request_len, 0);
    }

    if (send_result != request_len) {
        mcp_log_error("Failed to send request");
        free(request);
        free(full_path);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Free the request buffer
    free(request);

    // Receive response with dynamic buffer
    size_t buffer_size = 4096; // Initial buffer size
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        mcp_log_error("Failed to allocate memory for response buffer");
        free(full_path);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    int total_received = 0;
    int bytes_received = 0;

    while (1) {
        if (client->use_ssl) {
            // For SSL connections, you would use SSL_read instead of recv
            // For example: bytes_received = SSL_read(ssl, buffer + total_received, buffer_size - total_received - 1);
            // This is a placeholder for SSL/TLS implementation
            bytes_received = recv(sock, buffer + total_received, (int)buffer_size - total_received - 1, 0);
        } else {
            bytes_received = recv(sock, buffer + total_received, (int)buffer_size - total_received - 1, 0);
        }

        if (bytes_received <= 0) {
            break;
        }
        total_received += bytes_received;

        // Check if buffer is almost full, resize if needed
        if (total_received >= (int)buffer_size - 1024) { // Leave 1KB margin
            size_t new_size = buffer_size * 2;
            char* new_buffer = (char*)realloc(buffer, new_size);

            if (!new_buffer) {
                mcp_log_error("Failed to resize response buffer");
                free(buffer);
                free(full_path);
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }

            buffer = new_buffer;
            buffer_size = new_size;
        }
    }

    buffer[total_received] = '\0';

    // Close SSL connection if used
    if (client->use_ssl) {
        // For SSL connections, you would clean up SSL resources
        // For example:
        // SSL_shutdown(ssl);
        // SSL_free(ssl);
        // SSL_CTX_free(ctx);
    }

    // Close socket
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    // Parse response
    if (total_received == 0) {
        mcp_log_error("No response received");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Parse status code
    char* status_line = buffer;
    char* status_code_str = strchr(status_line, ' ');
    if (!status_code_str) {
        mcp_log_error("Invalid response format");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }

    *status = atoi(status_code_str + 1);

    // Find response body
    char* body_start = strstr(buffer, "\r\n\r\n");
    if (!body_start) {
        mcp_log_error("Invalid response format");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }

    body_start += 4; // Skip \r\n\r\n

    // Copy response body
    *response = mcp_strdup(body_start);
    if (!*response) {
        mcp_log_error("Failed to allocate memory for response");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Free the response buffer
    free(buffer);

    free(full_path);
    return KMCP_SUCCESS;
}

/**
 * @brief Validate tool name for security
 *
 * @param tool_name Tool name to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if tool name is valid, or an error code otherwise
 */
static kmcp_error_t validate_tool_name(const char* tool_name) {
    if (!tool_name) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum length
    size_t len = strlen(tool_name);
    if (len == 0) {
        mcp_log_error("Tool name is empty");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for maximum length
    if (len > 128) {
        mcp_log_error("Tool name too long: %zu characters", len);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for valid characters (alphanumeric, underscore, hyphen, period)
    for (size_t i = 0; i < len; i++) {
        char c = tool_name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            mcp_log_error("Tool name contains invalid character: %c", c);
            return KMCP_ERROR_INVALID_PARAMETER;
        }
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Validate JSON string for security
 *
 * @param json_str JSON string to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if JSON is valid, or an error code otherwise
 */
static kmcp_error_t validate_json(const char* json_str) {
    if (!json_str) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum length
    size_t len = strlen(json_str);
    if (len < 2) { // At minimum "{}" or "[]"
        mcp_log_error("JSON string too short: %zu characters", len);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for balanced braces
    int brace_count = 0;
    int bracket_count = 0;

    for (size_t i = 0; i < len; i++) {
        char c = json_str[i];
        if (c == '{') brace_count++;
        else if (c == '}') brace_count--;
        else if (c == '[') bracket_count++;
        else if (c == ']') bracket_count--;

        // Check for negative counts (closing before opening)
        if (brace_count < 0 || bracket_count < 0) {
            mcp_log_error("Unbalanced braces or brackets in JSON");
            return KMCP_ERROR_INVALID_PARAMETER;
        }
    }

    // Check for balanced counts
    if (brace_count != 0 || bracket_count != 0) {
        mcp_log_error("Unbalanced braces or brackets in JSON");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Call a tool
 */
kmcp_error_t kmcp_http_client_call_tool(
    kmcp_http_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
) {
    if (!client || !tool_name || !params_json || !result_json) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Validate tool name
    kmcp_error_t result = validate_tool_name(tool_name);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Validate JSON parameters
    result = validate_json(params_json);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Initialize output parameter
    *result_json = NULL;

    // Build path with proper length checking
    size_t tool_name_len = strlen(tool_name);
    size_t path_len = 6 + tool_name_len; // "tools/" + tool_name
    char* path = (char*)malloc(path_len + 1); // +1 for null terminator

    if (!path) {
        mcp_log_error("Failed to allocate memory for path");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Use snprintf with explicit length to prevent buffer overflow
    int written = snprintf(path, path_len + 1, "tools/%s", tool_name);

    // Check if the path was truncated
    if (written < 0 || (size_t)written >= path_len + 1) {
        mcp_log_error("Path truncated: tools/%s", tool_name);
        free(path);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Build request body
    const char* content_type = "application/json";

    // Send request
    int status = 0;
    char* response = NULL;
    result = kmcp_http_client_send(
        client,
        "POST",
        path,
        content_type,
        params_json,
        &response,
        &status
    );

    // Free the dynamically allocated path
    free(path);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to call tool: %s", tool_name);
        return result;
    }

    // Check status code
    if (status != 200) {
        mcp_log_error("Tool call failed with status code: %d", status);
        free(response);
        return KMCP_ERROR_INTERNAL;
    }

    // Return response
    *result_json = response;
    return KMCP_SUCCESS;
}

/**
 * @brief Validate resource URI for security
 *
 * @param resource_uri Resource URI to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if resource URI is valid, or an error code otherwise
 */
static kmcp_error_t validate_resource_uri(const char* resource_uri) {
    if (!resource_uri) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum length
    size_t len = strlen(resource_uri);
    if (len == 0) {
        mcp_log_error("Resource URI is empty");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for maximum length
    if (len > 1024) {
        mcp_log_error("Resource URI too long: %zu characters", len);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for path traversal attempts
    if (strstr(resource_uri, "../") != NULL || strstr(resource_uri, "..\\") != NULL) {
        mcp_log_error("Resource URI contains path traversal sequence");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for invalid characters
    const char* invalid_chars = "<>\"{}|\\^`\0";
    if (strpbrk(resource_uri, invalid_chars) != NULL) {
        mcp_log_error("Resource URI contains invalid characters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Get a resource
 */
kmcp_error_t kmcp_http_client_get_resource(
    kmcp_http_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
) {
    if (!client || !resource_uri || !content || !content_type) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Validate resource URI
    kmcp_error_t result = validate_resource_uri(resource_uri);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Initialize output parameters
    *content = NULL;
    *content_type = NULL;

    // Build path with proper length checking
    size_t resource_uri_len = strlen(resource_uri);
    size_t path_len = 10 + resource_uri_len; // "resources/" + resource_uri
    char* path = (char*)malloc(path_len + 1); // +1 for null terminator

    if (!path) {
        mcp_log_error("Failed to allocate memory for path");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Use snprintf with explicit length to prevent buffer overflow
    int written = snprintf(path, path_len + 1, "resources/%s", resource_uri);

    // Check if the path was truncated
    if (written < 0 || (size_t)written >= path_len + 1) {
        mcp_log_error("Path truncated: resources/%s", resource_uri);
        free(path);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Send request
    int status = 0;
    char* response = NULL;
    result = kmcp_http_client_send(
        client,
        "GET",
        path,
        NULL,
        NULL,
        &response,
        &status
    );

    // Free the dynamically allocated path
    free(path);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get resource: %s", resource_uri);
        return result;
    }

    // Check status code
    if (status == 404) {
        mcp_log_error("Resource not found: %s", resource_uri);
        free(response);
        return KMCP_ERROR_RESOURCE_NOT_FOUND;
    } else if (status != 200) {
        mcp_log_error("Resource request failed with status code: %d", status);
        free(response);
        return KMCP_ERROR_INTERNAL;
    }

    // Set content
    *content = response;

    // Set content type (default to text/plain)
    *content_type = mcp_strdup("text/plain");
    if (!*content_type) {
        mcp_log_error("Failed to allocate memory for content type");
        free(response);
        *content = NULL;
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Close the HTTP client
 */
void kmcp_http_client_close(kmcp_http_client_t* client) {
    if (!client) {
        return;
    }

    // Free resources
    free(client->base_url);
    free(client->host);
    free(client->path);
    free(client->api_key);

    free(client);
}
