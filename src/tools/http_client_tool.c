/**
 * @file http_client_tool.c
 * @brief Implementation of the HTTP client tool for MCP server.
 *
 * This tool allows making HTTP requests from the MCP server to external services.
 */

#include "mcp_server.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_json_utils.h"
#include "mcp_socket_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Configuration constants
#define HTTP_CLIENT_DEFAULT_TIMEOUT_MS 30000  // Default timeout: 30 seconds
#define HTTP_CLIENT_MAX_RESPONSE_SIZE (10 * 1024 * 1024)  // Max response: 10MB
#define HTTP_CLIENT_INITIAL_BUFFER_SIZE 4096  // Initial buffer: 4KB
#define HTTP_CLIENT_REQUEST_BUFFER_SIZE 8192  // Request buffer: 8KB

// HTTP response structure
typedef struct {
    char* data;                  // Response data
    size_t size;                 // Response size
    size_t capacity;             // Buffer capacity
    int status_code;             // HTTP status code
    char* headers;               // Response headers
} http_response_t;

// Forward declarations
static http_response_t* http_request(const char* method, const char* url,
                                    const char* content_type, const char* headers,
                                    const void* data, size_t data_size,
                                    uint32_t timeout_ms);
static void http_response_free(http_response_t* response);
static char* parse_url(const char* url, char** host, int* port, char** path, bool* use_ssl);
static bool extract_http_headers(http_response_t* response);
static const char* extract_mime_type(const char* headers);
static mcp_content_item_t* create_content_item(mcp_content_type_t type, const char* mime_type,
                                              const void* data, size_t data_size);
static void free_content_items(mcp_content_item_t** content, size_t count);

/**
 * @brief Extract MIME type from HTTP headers
 *
 * @param headers HTTP headers string
 * @return const char* MIME type string (static buffer, do not free)
 */
static const char* extract_mime_type(const char* headers)
{
    static char mime_type_buf[128];
    static const char* default_mime_type = "text/plain";

    if (!headers) {
        return default_mime_type;
    }

    const char* content_type_header = strstr(headers, "Content-Type:");
    if (!content_type_header) {
        return default_mime_type;
    }

    // Skip "Content-Type:" and whitespace
    content_type_header += 13;
    while (*content_type_header == ' ') {
        content_type_header++;
    }

    // Extract MIME type (up to semicolon or newline)
    size_t i = 0;
    while (i < sizeof(mime_type_buf) - 1 &&
           *content_type_header &&
           *content_type_header != ';' &&
           *content_type_header != '\r' &&
           *content_type_header != '\n') {
        mime_type_buf[i++] = *content_type_header++;
    }
    mime_type_buf[i] = '\0';

    return mime_type_buf[0] ? mime_type_buf : default_mime_type;
}

/**
 * @brief Create a content item
 *
 * @param type Content type
 * @param mime_type MIME type string
 * @param data Data buffer
 * @param data_size Size of data
 * @return mcp_content_item_t* Created content item or NULL on failure
 */
static mcp_content_item_t* create_content_item(
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size)
{
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!item) {
        return NULL;
    }

    item->type = type;
    item->mime_type = mcp_strdup(mime_type);

    if (data && data_size > 0) {
        item->data = malloc(data_size + 1);
        if (item->data) {
            memcpy(item->data, data, data_size);
            ((char*)item->data)[data_size] = '\0';
            item->data_size = data_size;
        } else {
            item->data = NULL;
            item->data_size = 0;
        }
    } else {
        item->data = NULL;
        item->data_size = 0;
    }

    // Check if allocation failed
    if (!item->mime_type || (data_size > 0 && !item->data)) {
        if (item->mime_type) free(item->mime_type);
        if (item->data) free(item->data);
        free(item);
        return NULL;
    }

    return item;
}

/**
 * @brief Free content items array
 *
 * @param content Array of content items
 * @param count Number of items in the array
 */
static void free_content_items(mcp_content_item_t** content, size_t count)
{
    if (!content) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (content[i]) {
            free(content[i]->mime_type);
            free(content[i]->data);
            free(content[i]);
        }
    }

    free(content);
}

/**
 * @brief HTTP client tool handler function.
 *
 * This function handles HTTP client tool calls from MCP clients.
 *
 * @param server The MCP server instance.
 * @param name The name of the tool being called.
 * @param params The parameters for the tool call.
 * @param user_data User data passed to the tool handler.
 * @param content Pointer to store the content items.
 * @param content_count Pointer to store the number of content items.
 * @param is_error Pointer to store whether an error occurred.
 * @param error_message Pointer to store the error message.
 * @return mcp_error_code_t Error code.
 */
mcp_error_code_t http_client_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message)
{
    (void)server;
    (void)user_data;
    (void)name;

    // Initialize output parameters
    *is_error = false;
    *content = NULL;
    *content_count = 0;
    *error_message = NULL;

    // Variables for parameter extraction
    const char* url = NULL;
    const char* method = "GET";
    const char* headers = NULL;
    const char* body = NULL;
    const char* content_type = NULL;
    uint32_t timeout_ms = HTTP_CLIENT_DEFAULT_TIMEOUT_MS;

    // Extract required URL parameter
    mcp_json_t* url_node = mcp_json_object_get_property(params, "url");
    if (!url_node || mcp_json_get_type(url_node) != MCP_JSON_STRING ||
        mcp_json_get_string(url_node, &url) != 0) {
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid 'url' parameter");
        return MCP_ERROR_INVALID_PARAMS;
    }

    // Extract optional method parameter
    mcp_json_t* method_node = mcp_json_object_get_property(params, "method");
    if (method_node && mcp_json_get_type(method_node) == MCP_JSON_STRING) {
        mcp_json_get_string(method_node, &method);
    }

    // Extract optional headers parameter
    mcp_json_t* headers_node = mcp_json_object_get_property(params, "headers");
    if (headers_node && mcp_json_get_type(headers_node) == MCP_JSON_STRING) {
        mcp_json_get_string(headers_node, &headers);
    }

    // Extract optional body parameter
    mcp_json_t* body_node = mcp_json_object_get_property(params, "body");
    if (body_node && mcp_json_get_type(body_node) == MCP_JSON_STRING) {
        mcp_json_get_string(body_node, &body);
    }

    // Extract optional content_type parameter
    mcp_json_t* content_type_node = mcp_json_object_get_property(params, "content_type");
    if (content_type_node && mcp_json_get_type(content_type_node) == MCP_JSON_STRING) {
        mcp_json_get_string(content_type_node, &content_type);
    } else if (body != NULL) {
        // Default content type for requests with body
        content_type = "application/json";
    }

    // Extract optional timeout parameter
    mcp_json_t* timeout_node = mcp_json_object_get_property(params, "timeout");
    if (timeout_node && mcp_json_get_type(timeout_node) == MCP_JSON_NUMBER) {
        double timeout_seconds;
        if (mcp_json_get_number(timeout_node, &timeout_seconds) == 0) {
            timeout_ms = (uint32_t)(timeout_seconds * 1000);
        }
    }

    // Send the HTTP request
    http_response_t* response = http_request(
        method,
        url,
        content_type,
        headers,
        body,
        body ? strlen(body) : 0,
        timeout_ms
    );

    if (!response) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to send HTTP request");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Allocate content items array (metadata + response)
    *content_count = 2;
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*) * *content_count);
    if (!*content) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for content array");
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Create metadata JSON
    char metadata_json[256];
    snprintf(metadata_json, sizeof(metadata_json),
            "{\"status_code\": %d, \"content_length\": %zu, \"success\": true}",
            response->status_code, response->size);

    (*content)[0] = create_content_item(MCP_CONTENT_TYPE_JSON, "application/json", metadata_json, strlen(metadata_json));

    const char* mime_type = extract_mime_type(response->headers);
    (*content)[1] = create_content_item(MCP_CONTENT_TYPE_TEXT, mime_type, response->data, response->size);

    // Check if content item creation failed
    if (!(*content)[0] || !(*content)[1]) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to create content items");
        free_content_items(*content, *content_count);
        *content = NULL;
        *content_count = 0;
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Clean up
    http_response_free(response);

    return MCP_ERROR_NONE;
}

/**
 * @brief Extract HTTP headers and status code from response data
 *
 * @param response The HTTP response structure to update
 * @return bool True if headers were successfully extracted, false otherwise
 */
static bool extract_http_headers(http_response_t* response)
{
    if (!response || !response->data || response->size < 4) {
        return false;
    }

    // Find the end of headers marker
    char* headers_end = strstr(response->data, "\r\n\r\n");
    if (!headers_end || headers_end < response->data || headers_end >= response->data + response->size) {
        return false;
    }

    // Calculate header size and body position
    size_t headers_end_offset = headers_end - response->data;
    size_t headers_size = headers_end_offset + 2; // Include the first \r\n
    size_t headers_total_size = headers_end_offset + 4; // Include \r\n\r\n

    // Allocate and copy headers
    response->headers = (char*)malloc(headers_size + 1);
    if (!response->headers) {
        return false;
    }

    memcpy(response->headers, response->data, headers_size);
    response->headers[headers_size] = '\0';

    // Extract status code
    char* status_line = response->headers;
    char* space = strchr(status_line, ' ');
    if (space) {
        response->status_code = atoi(space + 1);
    }

    // Move body to beginning of buffer
    size_t body_size = response->size - headers_total_size;
    memmove(response->data, response->data + headers_total_size, body_size);
    response->size = body_size;
    response->data[response->size] = '\0';

    return true;
}

/**
 * @brief Sends an HTTP request.
 *
 * @param method HTTP method (GET, POST, etc.)
 * @param url URL to request
 * @param content_type Content type for request body
 * @param headers Additional headers
 * @param data Request body data
 * @param data_size Size of request body data
 * @param timeout_ms Timeout in milliseconds
 * @return http_response_t* Response object, or NULL on error
 */
static http_response_t* http_request(const char* method, const char* url,
                                    const char* content_type, const char* headers,
                                    const void* data, size_t data_size,
                                    uint32_t timeout_ms)
{
    if (!method || !url) {
        return NULL;
    }

    // Parse URL
    char* host = NULL;
    int port = 0;
    char* path = NULL;
    bool use_ssl = false;

    char* url_copy = parse_url(url, &host, &port, &path, &use_ssl);
    if (!url_copy) {
        mcp_log_error("Failed to parse URL: %s", url);
        return NULL;
    }

    // Check for SSL
    if (use_ssl) {
        mcp_log_error("SSL not implemented yet");
        free(url_copy);
        return NULL;
    }

    // Connect to server using non-blocking socket
    socket_t sock = mcp_socket_connect_nonblocking(host, (uint16_t)port, timeout_ms);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to server: %s:%d", host, port);
        free(url_copy);
        return NULL;
    }

    // Build HTTP request
    char request[HTTP_CLIENT_REQUEST_BUFFER_SIZE] = {0};
    int request_len = 0;

    // Request line
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
                           "%s /%s HTTP/1.1\r\n", method, path ? path : "");

    // Host header
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
                           "Host: %s\r\n", host);

    // Content-Type header (if body is provided)
    if (data && data_size > 0 && content_type) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Content-Type: %s\r\n", content_type);
    }

    // Content-Length header (if body is provided)
    if (data && data_size > 0) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Content-Length: %zu\r\n", data_size);
    }

    // Connection header
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
                           "Connection: close\r\n");

    // Additional headers
    if (headers) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "%s\r\n", headers);
    }

    // End of headers
    request_len += snprintf(request + request_len, sizeof(request) - request_len, "\r\n");

    // Send request headers
    if (mcp_socket_send_exact(sock, request, request_len, NULL) != 0) {
        mcp_log_error("Failed to send HTTP request headers");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Send request body (if provided)
    if (data && data_size > 0) {
        if (mcp_socket_send_exact(sock, (const char*)data, data_size, NULL) != 0) {
            mcp_log_error("Failed to send HTTP request body");
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        }
    }

    // Create response structure
    http_response_t* response = (http_response_t*)calloc(1, sizeof(http_response_t));
    if (!response) {
        mcp_log_error("Failed to allocate memory for HTTP response");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Allocate initial buffer for response
    response->capacity = HTTP_CLIENT_INITIAL_BUFFER_SIZE;
    response->data = (char*)malloc(response->capacity);
    if (!response->data) {
        mcp_log_error("Failed to allocate memory for HTTP response data");
        free(response);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Receive response
    char buffer[HTTP_CLIENT_INITIAL_BUFFER_SIZE];
    int bytes_received;
    bool headers_complete = false;

    // Use mcp_socket_wait_readable to check if data is available
    while (mcp_socket_wait_readable(sock, (int)timeout_ms, NULL) > 0) {
        // Receive data
        bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            // Connection closed or error
            break;
        }

        // Ensure buffer is null-terminated
        buffer[bytes_received] = '\0';

        // Check if we need to resize the response buffer
        if (response->size + bytes_received >= response->capacity) {
            size_t new_capacity = response->capacity * 2;
            if (new_capacity > HTTP_CLIENT_MAX_RESPONSE_SIZE) {
                new_capacity = HTTP_CLIENT_MAX_RESPONSE_SIZE;
            }

            char* new_data = (char*)realloc(response->data, new_capacity);
            if (!new_data) {
                mcp_log_error("Failed to resize HTTP response buffer");
                http_response_free(response);
                free(url_copy);
                mcp_socket_close(sock);
                return NULL;
            }
            response->data = new_data;
            response->capacity = new_capacity;
        }

        // Append received data to response
        memcpy(response->data + response->size, buffer, bytes_received);
        response->size += bytes_received;
        response->data[response->size] = '\0';

        // Check if we've received the complete headers
        if (!headers_complete && strstr(response->data, "\r\n\r\n")) {
            headers_complete = true;
        }

        // Check if we've reached the maximum response size
        if (response->size >= HTTP_CLIENT_MAX_RESPONSE_SIZE) {
            mcp_log_warn("HTTP response exceeded maximum size (%d bytes)", HTTP_CLIENT_MAX_RESPONSE_SIZE);
            break;
        }
    }

    mcp_socket_close(sock);
    free(url_copy);

    // If we didn't get any data, return error
    if (response->size == 0) {
        mcp_log_error("No data received from HTTP server");
        http_response_free(response);
        return NULL;
    }

    // Extract headers and move body to beginning of buffer
    if (!extract_http_headers(response)) {
        mcp_log_warn("Failed to extract HTTP headers, returning raw response");
    }

    return response;
}

/**
 * @brief Frees an HTTP response structure.
 *
 * @param response Response structure to free
 */
static void http_response_free(http_response_t* response)
{
    if (response) {
        free(response->data);
        free(response->headers);
        free(response);
    }
}

/**
 * @brief Parses a URL into its components.
 *
 * @param url URL to parse
 * @param host Pointer to store the host
 * @param port Pointer to store the port
 * @param path Pointer to store the path
 * @param use_ssl Pointer to store whether SSL should be used
 * @return char* Duplicated URL string that should be freed by the caller
 */
static char* parse_url(const char* url, char** host, int* port, char** path, bool* use_ssl)
{
    if (!url || !host || !port || !path || !use_ssl) {
        return NULL;
    }

    // Duplicate the URL
    char* url_copy = mcp_strdup(url);
    if (!url_copy) {
        return NULL;
    }

    // Default values
    *host = NULL;
    *port = 80;
    *path = NULL;
    *use_ssl = false;

    // Parse protocol (http:// or https://)
    char* host_start = url_copy;
    if (strncmp(url_copy, "http://", 7) == 0) {
        host_start = url_copy + 7;
    } else if (strncmp(url_copy, "https://", 8) == 0) {
        host_start = url_copy + 8;
        *use_ssl = true;
        *port = 443;
    }

    // Find the path
    char* path_start = strchr(host_start, '/');
    if (path_start) {
        *path_start = '\0';  // Null-terminate the host:port part
        *path = path_start + 1;  // Skip the leading slash
    } else {
        static char empty_path[] = "";
        *path = empty_path;
    }

    // Check for port in host
    char* port_start = strchr(host_start, ':');
    if (port_start) {
        *port_start = '\0';  // Null-terminate the host part
        *port = atoi(port_start + 1);
    }

    // Set the host
    *host = host_start;

    return url_copy;
}

/**
 * @brief Registers the HTTP client tool with the MCP server.
 *
 * @param server The MCP server instance.
 * @return int 0 on success, non-zero on error.
 */
int register_http_client_tool(mcp_server_t* server)
{
    if (!server) {
        return -1;
    }

    // Create the HTTP client tool
    mcp_tool_t* http_tool = mcp_tool_create("http_client", "Make HTTP requests to external services");
    if (!http_tool) {
        mcp_log_error("Failed to create HTTP client tool");
        return -1;
    }

    // Add parameters
    if (mcp_tool_add_param(http_tool, "url", "string", "URL to request", true) != 0 ||
        mcp_tool_add_param(http_tool, "method", "string", "HTTP method (GET, POST, PUT, DELETE, etc.)", false) != 0 ||
        mcp_tool_add_param(http_tool, "headers", "string", "Additional HTTP headers", false) != 0 ||
        mcp_tool_add_param(http_tool, "body", "string", "Request body", false) != 0 ||
        mcp_tool_add_param(http_tool, "content_type", "string", "Content type for request body", false) != 0 ||
        mcp_tool_add_param(http_tool, "timeout", "number", "Request timeout in seconds", false) != 0) {
        mcp_log_error("Failed to add parameters to HTTP client tool");
        mcp_tool_free(http_tool);
        return -1;
    }

    // Add the tool to the server
    if (mcp_server_add_tool(server, http_tool) != 0) {
        mcp_log_error("Failed to add HTTP client tool to server");
        mcp_tool_free(http_tool);
        return -1;
    }

    // Free the tool (server makes a copy)
    mcp_tool_free(http_tool);

    mcp_log_info("HTTP client tool registered");

    return 0;
}
