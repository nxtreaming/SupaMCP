/**
 * @file mcp_http_client_tool.c
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

// Default timeout in milliseconds
#define HTTP_CLIENT_DEFAULT_TIMEOUT_MS 30000

// Maximum response size
#define HTTP_CLIENT_MAX_RESPONSE_SIZE (10 * 1024 * 1024) // 10MB

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

    mcp_log_info("HTTP client tool called");

    // Initialize output parameters
    *is_error = false;
    *content = NULL;
    *content_count = 0;
    *error_message = NULL;

    // Variables for parameter extraction
    const char* url = NULL;
    const char* method = "GET";  // Default method
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

    // Log the request
    mcp_log_info("HTTP client request: %s %s", method, url);

    // Log all parameters
    mcp_log_info("HTTP client parameters:");
    mcp_log_info("  URL: %s", url);
    mcp_log_info("  Method: %s", method);
    mcp_log_info("  Content-Type: %s", content_type ? content_type : "NULL");
    mcp_log_info("  Headers: %s", headers ? headers : "NULL");
    mcp_log_info("  Body: %s", body ? body : "NULL");
    mcp_log_info("  Timeout: %u ms", timeout_ms);

    // Send the HTTP request
    mcp_log_info("Sending HTTP request...");
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
        mcp_log_error("HTTP request failed");
        *is_error = true;
        *error_message = mcp_strdup("Failed to send HTTP request");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    mcp_log_info("HTTP request succeeded, status code: %d", response->status_code);

    // Create content items based on the response
    *content_count = 2; // One for metadata, one for the actual content
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*) * *content_count);
    if (!*content) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for content array");
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // First content item: metadata as JSON
    (*content)[0] = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!(*content)[0]) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for metadata content item");
        free(*content);
        *content = NULL;
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Create metadata JSON
    char metadata_json[256];
    snprintf(metadata_json, sizeof(metadata_json),
            "{\"status_code\": %d, \"content_length\": %zu, \"success\": true}",
            response->status_code, response->size);

    // Initialize metadata content item
    (*content)[0]->type = MCP_CONTENT_TYPE_JSON;
    (*content)[0]->mime_type = mcp_strdup("application/json");
    (*content)[0]->data = mcp_strdup(metadata_json);
    (*content)[0]->data_size = strlen(metadata_json);

    if (!(*content)[0]->mime_type || !(*content)[0]->data) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for metadata content");
        if ((*content)[0]->mime_type) free((*content)[0]->mime_type);
        if ((*content)[0]->data) free((*content)[0]->data);
        free((*content)[0]);
        free(*content);
        *content = NULL;
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Second content item: actual response content
    (*content)[1] = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!(*content)[1]) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for response content item");
        free((*content)[0]->mime_type);
        free((*content)[0]->data);
        free((*content)[0]);
        free(*content);
        *content = NULL;
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Determine MIME type from response headers
    const char* mime_type = "text/plain";
    if (response->headers) {
        const char* content_type_header = strstr(response->headers, "Content-Type:");
        if (content_type_header) {
            content_type_header += 13; // Skip "Content-Type:"
            // Skip whitespace
            while (*content_type_header == ' ') content_type_header++;
            // Extract MIME type (up to semicolon or newline)
            char mime_type_buf[128] = {0};
            size_t i = 0;
            while (i < sizeof(mime_type_buf) - 1 && *content_type_header && *content_type_header != ';' && *content_type_header != '\r' && *content_type_header != '\n') {
                mime_type_buf[i++] = *content_type_header++;
            }
            mime_type = mime_type_buf;
        }
    }

    // Initialize response content item
    (*content)[1]->type = MCP_CONTENT_TYPE_TEXT;
    (*content)[1]->mime_type = mcp_strdup(mime_type);
    (*content)[1]->data = NULL;
    (*content)[1]->data_size = 0;

    // Copy response data
    if (response->data && response->size > 0) {
        (*content)[1]->data = malloc(response->size + 1);
        if ((*content)[1]->data) {
            memcpy((*content)[1]->data, response->data, response->size);
            ((char*)(*content)[1]->data)[response->size] = '\0';
            (*content)[1]->data_size = response->size;
        }
    }

    if (!(*content)[1]->mime_type || (response->size > 0 && !(*content)[1]->data)) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for response content");
        if ((*content)[1]->mime_type) free((*content)[1]->mime_type);
        if ((*content)[1]->data) free((*content)[1]->data);
        free((*content)[1]);
        free((*content)[0]->mime_type);
        free((*content)[0]->data);
        free((*content)[0]);
        free(*content);
        *content = NULL;
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Content items already created above
    *content_count = 2;

    // Clean up
    http_response_free(response);

    return MCP_ERROR_NONE;
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

    mcp_log_info("Parsing URL: %s", url);
    char* url_copy = parse_url(url, &host, &port, &path, &use_ssl);
    if (!url_copy) {
        mcp_log_error("Failed to parse URL: %s", url);
        return NULL;
    }

    mcp_log_info("URL parsed - host: %s, port: %d, path: %s, use_ssl: %s",
                host ? host : "NULL",
                port,
                path ? path : "NULL",
                use_ssl ? "true" : "false");

    // Check for SSL
    if (use_ssl) {
        mcp_log_error("SSL not implemented yet");
        free(url_copy);
        return NULL;
    }

    // Log connection attempt
    mcp_log_info("Connecting to %s:%d...", host, port);

    // Connect to server using mcp_socket_utils
    socket_t sock = mcp_socket_connect(host, (uint16_t)port, timeout_ms);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to connect to server: %s:%d (error: %d)", host, port, mcp_socket_get_last_error());
        free(url_copy);
        return NULL;
    }

    mcp_log_info("Connected to %s:%d successfully", host, port);

    // Build HTTP request
    char request[8192] = {0};
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

    // Send request headers using mcp_socket_utils
    if (mcp_socket_send_exact(sock, request, request_len, NULL) != 0) {
        mcp_log_error("Failed to send HTTP request headers");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Send request body (if provided) using mcp_socket_utils
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
    response->capacity = 4096;
    response->data = (char*)malloc(response->capacity);
    if (!response->data) {
        mcp_log_error("Failed to allocate memory for HTTP response data");
        free(response);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Receive response
    char buffer[4096];
    int bytes_received;
    bool headers_complete = false;
    char* headers_end = NULL;
    size_t headers_end_offset = 0;

    mcp_log_info("Waiting for response from server...");

    // Use mcp_socket_wait_readable to check if data is available
    while (mcp_socket_wait_readable(sock, (int)timeout_ms, NULL) > 0) {
        // Receive data using standard recv since mcp_socket_utils doesn't have a non-exact receive function
        bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            mcp_log_info("Connection closed or error (bytes_received=%d, error=%d)",
                        bytes_received, mcp_socket_get_last_error());
            break; // Connection closed or error
        }

        mcp_log_info("Received %d bytes from server", bytes_received);

        // Ensure buffer is null-terminated
        buffer[bytes_received] = '\0';

        // Check if we need to resize the response buffer
        if (response->size + bytes_received >= response->capacity) {
            size_t new_capacity = response->capacity * 2;
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
        if (!headers_complete) {
            mcp_log_info("Checking for headers end marker...");
            // Make sure response->data is valid and contains at least 4 bytes
            if (response->data && response->size >= 4) {
                headers_end = strstr(response->data, "\r\n\r\n");
                if (headers_end) {
                    // Sanity check to ensure headers_end is within the response buffer
                    if (headers_end >= response->data && headers_end < response->data + response->size) {
                        headers_end_offset = headers_end - response->data;
                        mcp_log_info("Headers end marker found at offset %zu", headers_end_offset);
                    } else {
                        mcp_log_error("Headers end marker found but pointer is out of bounds");
                        headers_end = NULL;
                    }
                } else {
                    mcp_log_info("Headers end marker not found");
                }
            } else {
                mcp_log_error("Response data is invalid or too small");
                headers_end = NULL;
            }

            if (headers_end) {
                headers_complete = true;

                // Extract headers
                size_t headers_size = headers_end_offset + 2; // Include the first \r\n
                mcp_log_info("Allocating %zu bytes for headers", headers_size + 1);
                response->headers = (char*)malloc(headers_size + 1);
                if (response->headers) {
                    memcpy(response->headers, response->data, headers_size);
                    response->headers[headers_size] = '\0';
                    mcp_log_info("Headers extracted: %s", response->headers);

                    // Extract status code
                    char* status_line = response->headers;
                    if (status_line && *status_line) {
                        mcp_log_info("Status line: %s", status_line);
                        char* space = strchr(status_line, ' ');
                        if (space) {
                            response->status_code = atoi(space + 1);
                            mcp_log_info("Status code extracted: %d", response->status_code);
                        } else {
                            mcp_log_warn("No space found in status line, cannot extract status code");
                        }
                    } else {
                        mcp_log_warn("Empty status line");
                    }
                } else {
                    mcp_log_error("Failed to allocate memory for headers");
                }
            } else {
                mcp_log_info("Headers end marker not found yet");
            }
        }

        // Check if we've reached the maximum response size
        if (response->size >= HTTP_CLIENT_MAX_RESPONSE_SIZE) {
            mcp_log_warn("HTTP response exceeded maximum size (%d bytes)", HTTP_CLIENT_MAX_RESPONSE_SIZE);
            break;
        }
    }

    mcp_socket_close(sock);

    // Clean up
    free(url_copy);

    // If we didn't get any data, return error
    if (response->size == 0) {
        mcp_log_error("No data received from HTTP server");
        http_response_free(response);
        return NULL;
    }

    // If we didn't find the headers, extract them now
    if (!headers_complete) {
        mcp_log_info("Headers not complete, checking for headers end marker...");
        // Make sure response->data is valid and contains at least 4 bytes
        if (response->data && response->size >= 4) {
            headers_end = strstr(response->data, "\r\n\r\n");
            if (headers_end) {
                // Sanity check to ensure headers_end is within the response buffer
                if (headers_end >= response->data && headers_end < response->data + response->size) {
                    headers_end_offset = headers_end - response->data;
                    mcp_log_info("Headers end marker found at offset %zu", headers_end_offset);
                } else {
                    mcp_log_error("Headers end marker found but pointer is out of bounds");
                    headers_end = NULL;
                }
            } else {
                mcp_log_info("Headers end marker not found");
            }
        } else {
            mcp_log_error("Response data is invalid or too small");
            headers_end = NULL;
        }

        if (headers_end) {

            // Extract headers
            size_t headers_size = headers_end_offset + 2; // Include the first \r\n
            mcp_log_info("Allocating %zu bytes for headers", headers_size + 1);
            response->headers = (char*)malloc(headers_size + 1);
            if (response->headers) {
                memcpy(response->headers, response->data, headers_size);
                response->headers[headers_size] = '\0';
                mcp_log_info("Headers extracted: %s", response->headers);

                // Extract status code
                char* status_line = response->headers;
                if (status_line && *status_line) {
                    mcp_log_info("Status line: %s", status_line);
                    char* space = strchr(status_line, ' ');
                    if (space) {
                        response->status_code = atoi(space + 1);
                        mcp_log_info("Status code extracted: %d", response->status_code);
                    } else {
                        mcp_log_warn("No space found in status line, cannot extract status code");
                    }
                } else {
                    mcp_log_warn("Empty status line");
                }
            } else {
                mcp_log_error("Failed to allocate memory for headers");
            }
        } else {
            mcp_log_warn("Headers end marker not found in complete response");
        }
    }

    // If we found headers, move the body to the beginning of the data buffer
    if (headers_end) {
        mcp_log_info("Moving body to beginning of buffer");
        size_t headers_size = headers_end_offset + 4; // Include the \r\n\r\n

        // Sanity check to prevent integer overflow
        if (headers_size > response->size) {
            mcp_log_error("Headers size (%zu) is greater than response size (%zu), keeping response as is",
                         headers_size, response->size);
        } else {
            size_t body_size = response->size - headers_size;

            mcp_log_info("Headers size: %zu, Body size: %zu", headers_size, body_size);

            // Move body to beginning of buffer
            memmove(response->data, response->data + headers_size, body_size);
            response->size = body_size;
            response->data[response->size] = '\0';

            mcp_log_info("Body moved successfully");
        }
    } else {
        mcp_log_warn("No headers end marker found, keeping response as is");
    }

    mcp_log_info("HTTP request completed successfully");

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

    // Check for protocol
    char* protocol_end = strstr(url_copy, "://");
    char* host_start = url_copy;

    if (protocol_end) {
        // Null-terminate the protocol
        *protocol_end = '\0';

        // Check if it's HTTPS
        if (strcmp(url_copy, "https") == 0) {
            *use_ssl = true;
            *port = 443;
        }

        // Move host_start past the protocol
        host_start = protocol_end + 3;
    }

    // Find the path
    char* path_start = strchr(host_start, '/');
    if (path_start) {
        // Null-terminate the host
        *path_start = '\0';

        // Set the path (skip the leading slash)
        *path = path_start + 1;
    } else {
        // No path, use empty string
        static char empty_path[] = "";
        *path = empty_path;
    }

    // Check for port
    char* port_start = strchr(host_start, ':');
    if (port_start) {
        // Null-terminate the host
        *port_start = '\0';

        // Parse the port
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
