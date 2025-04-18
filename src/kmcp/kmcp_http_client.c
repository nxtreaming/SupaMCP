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
 * Note: This is a simple HTTP client implementation that does not support SSL, redirects, compression, or other advanced features.
 * In a real project, it is recommended to use a mature HTTP client library such as libcurl.
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

    // Build HTTP request
    char request[4096] = {0};
    int request_len = 0;

    // Request line
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
        "%s %s HTTP/1.1\r\n", method, full_path);

    // Host header
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
        "Host: %s\r\n", client->host);

    // Content-Type header
    if (content_type) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
            "Content-Type: %s\r\n", content_type);
    }

    // Content-Length header
    if (body) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
            "Content-Length: %zu\r\n", strlen(body));
    }

    // API key header
    if (client->api_key) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
            "Authorization: Bearer %s\r\n", client->api_key);
    }

    // Connection header
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
        "Connection: close\r\n");

    // Empty line
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
        "\r\n");

    // Request body
    if (body) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
            "%s", body);
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

    // Send request
    if (send(sock, request, request_len, 0) != request_len) {
        mcp_log_error("Failed to send request");
        free(full_path);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Receive response
    char buffer[4096] = {0};
    int total_received = 0;
    int bytes_received = 0;

    while ((bytes_received = recv(sock, buffer + total_received, sizeof(buffer) - total_received - 1, 0)) > 0) {
        total_received += bytes_received;
        if (total_received >= (int)sizeof(buffer) - 1) {
            break;
        }
    }

    buffer[total_received] = '\0';

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
        free(full_path);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Parse status code
    char* status_line = buffer;
    char* status_code_str = strchr(status_line, ' ');
    if (!status_code_str) {
        mcp_log_error("Invalid response format");
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }

    *status = atoi(status_code_str + 1);

    // Find response body
    char* body_start = strstr(buffer, "\r\n\r\n");
    if (!body_start) {
        mcp_log_error("Invalid response format");
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }

    body_start += 4; // Skip \r\n\r\n

    // Copy response body
    *response = mcp_strdup(body_start);
    if (!*response) {
        mcp_log_error("Failed to allocate memory for response");
        free(full_path);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    free(full_path);
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

    // Initialize output parameter
    *result_json = NULL;

    // Build path
    char path[256] = {0};
    snprintf(path, sizeof(path), "tools/%s", tool_name);

    // Build request body
    const char* content_type = "application/json";

    // Send request
    int status = 0;
    char* response = NULL;
    kmcp_error_t result = kmcp_http_client_send(
        client,
        "POST",
        path,
        content_type,
        params_json,
        &response,
        &status
    );

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

    // Initialize output parameters
    *content = NULL;
    *content_type = NULL;

    // Build path
    char path[256] = {0};
    snprintf(path, sizeof(path), "resources/%s", resource_uri);

    // Send request
    int status = 0;
    char* response = NULL;
    kmcp_error_t result = kmcp_http_client_send(
        client,
        "GET",
        path,
        NULL,
        NULL,
        &response,
        &status
    );

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
