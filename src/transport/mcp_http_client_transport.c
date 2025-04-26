#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_http_client_transport.h"
#include "internal/transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
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

// Forward declarations
static int http_client_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback);
static int http_client_transport_stop(mcp_transport_t* transport);
static int http_client_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static int http_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);
static int http_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);
static int http_client_transport_destroy(mcp_transport_t* transport);
static void* http_client_event_thread_func(void* arg);

// HTTP response structure
typedef struct {
    char* data;                  // Response data
    size_t size;                 // Response size
    int status_code;             // HTTP status code
    char* content_type;          // Content type
} http_response_t;

// SSE event structure
typedef struct {
    char* id;                    // Event ID
    char* event;                 // Event type
    char* data;                  // Event data
    time_t timestamp;            // Event timestamp
} sse_event_t;

// We don't need a pending request structure anymore, as we'll use mcp_client's mechanism

// Internal data structure for HTTP client transport
typedef struct {
    char* host;                  // Host to connect to
    uint16_t port;               // Port to connect to
    bool use_ssl;                // Whether to use SSL
    char* cert_path;             // Path to SSL certificate
    char* key_path;              // Path to SSL private key
    uint32_t timeout_ms;         // Connection timeout in milliseconds
    char* api_key;               // API key for authentication

    volatile bool running;       // Whether the transport is running
    mcp_thread_t event_thread;   // Thread for SSE events
    mcp_mutex_t* mutex;          // Mutex for thread safety

    // SSE event handling
    char* last_event_id;         // Last event ID received

    // Message callback
    mcp_transport_message_callback_t message_callback;
    void* callback_user_data;
    mcp_transport_error_callback_t error_callback;


} http_client_transport_data_t;

// Forward declarations for HTTP request functions
static http_response_t* http_post_request(const char* url, const char* content_type,
                                         const void* data, size_t size,
                                         const char* api_key, uint32_t timeout_ms);


static void http_response_free(http_response_t* response);
static int connect_to_sse_endpoint(http_client_transport_data_t* data);
static void process_sse_event(http_client_transport_data_t* data, const sse_event_t* event);
static sse_event_t* sse_event_create(const char* id, const char* event, const char* data);
static void sse_event_free(sse_event_t* event);

/**
 * @brief Creates an HTTP client transport with basic configuration.
 */
mcp_transport_t* mcp_transport_http_client_create(const char* host, uint16_t port) {
    // Create a basic configuration
    mcp_http_client_config_t config = {0};
    config.host = host;
    config.port = port;
    config.use_ssl = false;
    config.cert_path = NULL;
    config.key_path = NULL;
    config.timeout_ms = 30000; // 30 seconds default timeout
    config.api_key = NULL;

    // Use the detailed create function
    return mcp_transport_http_client_create_with_config(&config);
}

/**
 * @brief Creates an HTTP client transport with detailed configuration.
 */
mcp_transport_t* mcp_transport_http_client_create_with_config(const mcp_http_client_config_t* config) {
    if (config == NULL || config->host == NULL) {
        mcp_log_error("Invalid HTTP client configuration");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        mcp_log_error("Failed to allocate memory for HTTP client transport");
        return NULL;
    }

    // Allocate transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)calloc(1, sizeof(http_client_transport_data_t));
    if (data == NULL) {
        mcp_log_error("Failed to allocate memory for HTTP client transport data");
        free(transport);
        return NULL;
    }

    // Copy configuration
    data->host = mcp_strdup(config->host);
    data->port = config->port;
    data->use_ssl = config->use_ssl;
    data->timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000; // Default to 30 seconds

    if (config->cert_path) {
        data->cert_path = mcp_strdup(config->cert_path);
    }

    if (config->key_path) {
        data->key_path = mcp_strdup(config->key_path);
    }

    if (config->api_key) {
        data->api_key = mcp_strdup(config->api_key);
    }

    // Create mutex
    data->mutex = mcp_mutex_create();
    if (data->mutex == NULL) {
        mcp_log_error("Failed to create mutex for HTTP client transport");
        if (data->host) free(data->host);
        if (data->cert_path) free(data->cert_path);
        if (data->key_path) free(data->key_path);
        if (data->api_key) free(data->api_key);
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize transport structure
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_HTTP;  // Set protocol type to HTTP
    transport->client.start = http_client_transport_start;
    transport->client.stop = http_client_transport_stop;
    transport->client.send = http_client_transport_send;
    transport->client.sendv = http_client_transport_sendv;
    transport->client.receive = http_client_transport_receive;
    transport->client.destroy = http_client_transport_destroy;
    transport->transport_data = data;

    mcp_log_info("HTTP client transport created for %s:%d", data->host, data->port);

    return transport;
}

/**
 * @brief Destroys an HTTP client transport.
 */
static int http_client_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        free(transport);
        return 0;
    }

    // Stop the transport if it's running
    if (data->running) {
        http_client_transport_stop(transport);
    }

    // Free resources
    if (data->host) free(data->host);
    if (data->cert_path) free(data->cert_path);
    if (data->key_path) free(data->key_path);
    if (data->api_key) free(data->api_key);

    // Destroy mutex
    if (data->mutex) {
        mcp_mutex_destroy(data->mutex);
    }

    // Free transport data
    free(data);

    // Free transport structure
    free(transport);

    mcp_log_info("HTTP client transport destroyed");

    return 0;
}

/**
 * @brief Starts an HTTP client transport.
 */
static int http_client_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback) {
    if (transport == NULL) {
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        return -1;
    }

    // Store callback info
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    // Store callback info in data structure for use in event thread
    data->message_callback = message_callback;
    data->callback_user_data = user_data;
    data->error_callback = error_callback;

    // Check if already running
    if (data->running) {
        mcp_log_warn("HTTP client transport already running");
        return 0;
    }

    // Set running flag
    data->running = true;

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library");
        data->running = false;
        return -1;
    }

    // Start event thread for SSE
    if (mcp_thread_create(&data->event_thread, http_client_event_thread_func, transport) != 0) {
        mcp_log_error("Failed to create HTTP client event thread");
        data->running = false;
        return -1;
    }

    mcp_log_info("HTTP client transport started");

    return 0;
}

/**
 * @brief Stops an HTTP client transport.
 */
static int http_client_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL) {
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        return -1;
    }

    // Check if running
    if (!data->running) {
        mcp_log_warn("HTTP client transport not running");
        return 0;
    }

    // Set running flag to false to signal threads to stop
    data->running = false;

    // Wait for event thread to finish
    mcp_thread_join(data->event_thread, NULL);

    mcp_log_info("HTTP client transport stopped");

    return 0;
}

/**
 * @brief Extract request ID from JSON-RPC request.
 *
 * This function extracts the request ID from a JSON-RPC request string.
 * It uses a simple string search approach rather than full JSON parsing.
 */
static uint64_t extract_request_id(const char* json_data, size_t size) {
    (void)size; // Size is not used in this simple implementation
    // Look for "id":
    const char* id_str = "\"id\":";
    char* id_pos = strstr(json_data, id_str);
    if (id_pos == NULL) {
        return 0; // ID not found
    }

    // Skip "id": and any whitespace
    id_pos += strlen(id_str);
    while (*id_pos == ' ' || *id_pos == '\t' || *id_pos == '\r' || *id_pos == '\n') {
        id_pos++;
    }

    // Parse the ID value
    if (*id_pos == '"') {
        // String ID - not supported in this simple implementation
        return 0;
    } else {
        // Numeric ID
        return (uint64_t)atoll(id_pos);
    }
}

/**
 * @brief Sends data through an HTTP client transport.
 *
 * This function sends a JSON-RPC request to the HTTP server using the POST method.
 * It handles the binary length prefix format used by mcp_client.
 */
static int http_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (transport == NULL || data == NULL || size == 0) {
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data_struct = (http_client_transport_data_t*)transport->transport_data;
    if (data_struct == NULL) {
        return -1;
    }

    // Check if running
    if (!data_struct->running) {
        mcp_log_error("HTTP client transport not running");
        return -1;
    }

    // Build URL
    char url[256];
    snprintf(url, sizeof(url), "http%s://%s:%d/call_tool",
             data_struct->use_ssl ? "s" : "",
             data_struct->host,
             data_struct->port);

    // Process the data to extract the JSON-RPC request
    const char* json_data = NULL;
    size_t json_size = 0;

    // Check if this is a binary length prefix frame
    // The first 4 bytes are the length in network byte order
    if (size >= 4) {
        const uint8_t* bytes = (const uint8_t*)data;
        uint32_t length_prefix = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];

        // If the length prefix matches the remaining data size, this is a binary frame
        if (length_prefix == size - 4) {
            // Skip the 4-byte length prefix and use only the JSON data
            json_data = (const char*)data + 4;
            json_size = size - 4;
            mcp_log_debug("HTTP client transport detected binary frame, skipping 4-byte length prefix");
        } else {
            json_data = (const char*)data;
            json_size = size;
        }
    } else {
        json_data = (const char*)data;
        json_size = size;
    }

    // Extract the request ID from the JSON-RPC request
    uint64_t request_id = extract_request_id(json_data, json_size);
    if (request_id == 0) {
        mcp_log_error("Failed to extract request ID from JSON-RPC request");
        return -1;
    }

    // Log the request data for debugging
    mcp_log_debug("HTTP client transport sending request (ID: %llu): %.*s",
                 (unsigned long long)request_id, (int)json_size, json_data);

    // Send HTTP POST request
    http_response_t* response = http_post_request(
        url,
        "application/json",
        json_data,
        json_size,
        data_struct->api_key,
        data_struct->timeout_ms
    );

    if (response == NULL) {
        mcp_log_error("Failed to send HTTP request");
        return -1;
    }

    // Check response status
    if (response->status_code != 200) {
        mcp_log_error("HTTP request failed with status code %d", response->status_code);
        http_response_free(response);
        return -1;
    }

    // Process response
    if (response->data != NULL && response->size > 0) {
        // Ensure the response data is null-terminated and valid JSON
        char* clean_json = (char*)malloc(response->size + 1);
        if (clean_json == NULL) {
            mcp_log_error("Failed to allocate memory for JSON response");
            http_response_free(response);
            return -1;
        }

        // Copy the response data and ensure it's null-terminated
        memcpy(clean_json, response->data, response->size);
        clean_json[response->size] = '\0';

        // Find the end of the JSON data (in case there are trailing characters)
        char* json_end = NULL;
        if (clean_json[0] == '{') {
            // Find the matching closing brace
            int brace_count = 1;
            for (size_t i = 1; i < response->size; i++) {
                if (clean_json[i] == '{') {
                    brace_count++;
                } else if (clean_json[i] == '}') {
                    brace_count--;
                    if (brace_count == 0) {
                        json_end = clean_json + i + 1;
                        break;
                    }
                }
            }
        }

        // If we found the end of the JSON data, truncate the string
        if (json_end != NULL) {
            *json_end = '\0';
        }

        // Log the cleaned response data for debugging
        // For HTTP transport, we don't call the message callback here
        // because we're handling the response directly in mcp_client_send_request
        // This avoids the "Received response with unexpected ID" warning
        mcp_log_debug("HTTP client transport received response: %s", clean_json);

        // Free the JSON data
        free(clean_json);
    } else {
        mcp_log_error("Empty response from server");
    }

    // Free response
    http_response_free(response);

    mcp_log_info("HTTP client transport send: %zu bytes", json_size);

    return 0;
}

/**
 * @brief Sends data from multiple buffers through an HTTP client transport.
 *
 * This function handles the binary length prefix format used by mcp_client.
 * The first buffer typically contains the 4-byte length prefix, and the second buffer
 * contains the JSON data.
 */
static int http_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (transport == NULL || buffers == NULL || buffer_count == 0) {
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        return -1;
    }

    // Check if running
    if (!data->running) {
        mcp_log_error("HTTP client transport not running");
        return -1;
    }

    // Check if this is a binary length prefix frame (buffer_count == 2 and first buffer is 4 bytes)
    if (buffer_count == 2 && buffers[0].size == 4) {
        // This is likely a binary length prefix frame
        // Skip the first buffer (length prefix) and use only the second buffer (JSON data)
        mcp_log_debug("HTTP client transport detected binary frame in sendv, skipping 4-byte length prefix");
        return http_client_transport_send(transport, buffers[1].data, buffers[1].size);
    }

    // If not a binary length prefix frame, combine all buffers and send
    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        total_size += buffers[i].size;
    }

    // Allocate a single buffer
    char* combined_buffer = (char*)malloc(total_size);
    if (combined_buffer == NULL) {
        mcp_log_error("Failed to allocate memory for combined buffer");
        return -1;
    }

    // Copy data from multiple buffers
    size_t offset = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        memcpy(combined_buffer + offset, buffers[i].data, buffers[i].size);
        offset += buffers[i].size;
    }

    // Send the combined buffer
    int result = http_client_transport_send(transport, combined_buffer, total_size);

    // Free the combined buffer
    free(combined_buffer);

    return result;
}

/**
 * @brief Receives data from an HTTP client transport.
 *
 * This function is not used in the HTTP client transport, as responses are handled
 * directly in the send function. This is because HTTP is a request-response protocol,
 * and we get the response immediately after sending the request.
 */
static int http_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    (void)timeout_ms; // Unused parameter

    if (transport == NULL || data == NULL || size == NULL) {
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data_struct = (http_client_transport_data_t*)transport->transport_data;
    if (data_struct == NULL) {
        return -1;
    }

    // Check if running
    if (!data_struct->running) {
        mcp_log_error("HTTP client transport not running");
        return -1;
    }

    // Initialize output parameters
    *data = NULL;
    *size = 0;

    // HTTP client transport doesn't support synchronous receive
    // Responses are handled directly in the send function
    mcp_log_debug("HTTP client transport doesn't support synchronous receive");

    return -1;
}

/**
 * @brief Connects to the SSE endpoint.
 */
static int connect_to_sse_endpoint(http_client_transport_data_t* data) {
    if (data == NULL) {
        return -1;
    }

    // Build URL
    char url[256];
    snprintf(url, sizeof(url), "http%s://%s:%d/events",
             data->use_ssl ? "s" : "",
             data->host,
             data->port);

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to create socket for SSE connection");
        return -1;
    }

    // Resolve host
    struct hostent* server = gethostbyname(data->host);
    if (server == NULL) {
        mcp_log_error("Failed to resolve host: %s", data->host);
        mcp_socket_close(sock);
        return -1;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)data->port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == MCP_SOCKET_ERROR) {
        mcp_log_error("Failed to connect to server: %s:%d", data->host, data->port);
        mcp_socket_close(sock);
        return -1;
    }

    // TODO: Implement SSL support if needed
    if (data->use_ssl) {
        mcp_log_error("SSL not implemented yet for SSE connection");
        mcp_socket_close(sock);
        return -1;
    }

    // Build HTTP request
    char request[1024];
    int request_len = snprintf(request, sizeof(request),
                              "GET /events HTTP/1.1\r\n"
                              "Host: %s:%d\r\n"
                              "Accept: text/event-stream\r\n"
                              "Cache-Control: no-cache\r\n",
                              data->host, data->port);

    // Add Last-Event-ID if available
    if (data->last_event_id != NULL) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Last-Event-ID: %s\r\n", data->last_event_id);
    }

    // Add API key if provided
    if (data->api_key != NULL) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Authorization: Bearer %s\r\n", data->api_key);
    }

    // End headers
    request_len += snprintf(request + request_len, sizeof(request) - request_len, "\r\n");

    // Send request
    if (send(sock, request, request_len, 0) != request_len) {
        mcp_log_error("Failed to send HTTP request for SSE connection");
        mcp_socket_close(sock);
        return -1;
    }

    // Cast to int to avoid warning
    return (int)sock;
}

/**
 * @brief Processes an SSE event.
 */
static void process_sse_event(http_client_transport_data_t* data, const sse_event_t* event) {
    if (data == NULL || event == NULL) {
        return;
    }

    // Store last event ID
    if (event->id != NULL) {
        if (data->last_event_id != NULL) {
            free(data->last_event_id);
        }
        data->last_event_id = mcp_strdup(event->id);
    }

    // Process event data
    if (event->data != NULL) {
        // Call message callback
        int error_code = 0;
        char* result = NULL;

        if (data->message_callback != NULL) {
            result = data->message_callback(
                data->callback_user_data,
                event->data,
                strlen(event->data),
                &error_code
            );

            // Free result if returned
            if (result != NULL) {
                free(result);
            }
        }
    }
}

/**
 * @brief Event thread function for HTTP client transport.
 *
 * This thread connects to the SSE endpoint and processes events.
 */
static void* http_client_event_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL) {
        return NULL;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        return NULL;
    }

    mcp_log_info("HTTP client event thread started");

    // Main loop
    while (data->running) {
        // Connect to SSE endpoint
        int sock = connect_to_sse_endpoint(data);
        if (sock < 0) {
            mcp_log_error("Failed to connect to SSE endpoint, retrying in 5 seconds");
            // Sleep and retry
            for (int i = 0; i < 50 && data->running; i++) {
                mcp_sleep_ms(100);
            }
            continue;
        }

        mcp_log_info("Connected to SSE endpoint");

        // Read SSE events
        char buffer[4096];
        int bytes_read = 0;

        // Event parsing state
        char* event_type = NULL;
        char* event_id = NULL;
        char* event_data = NULL;

        // Read loop
        while (data->running && (bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_read] = '\0';

            // Parse SSE events
            char* line = strtok(buffer, "\r\n");
            while (line != NULL) {
                if (strlen(line) == 0) {
                    // Empty line indicates end of event
                    if (event_data != NULL) {
                        // Create event
                        sse_event_t* event = sse_event_create(event_id, event_type, event_data);
                        if (event != NULL) {
                            // Process event
                            process_sse_event(data, event);
                            sse_event_free(event);
                        }

                        // Reset event data
                        if (event_type != NULL) {
                            free(event_type);
                            event_type = NULL;
                        }
                        if (event_id != NULL) {
                            free(event_id);
                            event_id = NULL;
                        }
                        if (event_data != NULL) {
                            free(event_data);
                            event_data = NULL;
                        }
                    }
                } else if (strncmp(line, "event:", 6) == 0) {
                    // Event type
                    if (event_type != NULL) {
                        free(event_type);
                    }
                    event_type = mcp_strdup(line + 6);
                    // Trim leading spaces
                    while (*event_type == ' ') {
                        event_type++;
                    }
                } else if (strncmp(line, "id:", 3) == 0) {
                    // Event ID
                    if (event_id != NULL) {
                        free(event_id);
                    }
                    event_id = mcp_strdup(line + 3);
                    // Trim leading spaces
                    while (*event_id == ' ') {
                        event_id++;
                    }
                } else if (strncmp(line, "data:", 5) == 0) {
                    // Event data
                    char* data_line = line + 5;
                    // Trim leading spaces
                    while (*data_line == ' ') {
                        data_line++;
                    }

                    if (event_data == NULL) {
                        event_data = mcp_strdup(data_line);
                    } else {
                        // Append to existing data with newline
                        size_t old_len = strlen(event_data);
                        size_t new_len = old_len + strlen(data_line) + 2; // +2 for newline and null terminator
                        char* new_data = (char*)realloc(event_data, new_len);
                        if (new_data != NULL) {
                            event_data = new_data;
                            strcat(event_data, "\n");
                            strcat(event_data, data_line);
                        }
                    }
                }

                line = strtok(NULL, "\r\n");
            }
        }

        // Clean up
        mcp_socket_close(sock);

        // Free event data
        if (event_type != NULL) {
            free(event_type);
        }
        if (event_id != NULL) {
            free(event_id);
        }
        if (event_data != NULL) {
            free(event_data);
        }

        // If we're still running, retry connection
        if (data->running) {
            mcp_log_info("SSE connection closed, retrying in 5 seconds");
            // Sleep and retry
            for (int i = 0; i < 50 && data->running; i++) {
                mcp_sleep_ms(100);
            }
        }
    }

    mcp_log_info("HTTP client event thread stopped");

    return NULL;
}

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
static void http_response_free(http_response_t* response) {
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
 * @brief Creates an SSE event structure.
 */
static sse_event_t* sse_event_create(const char* id, const char* event, const char* data) {
    sse_event_t* sse_event = (sse_event_t*)malloc(sizeof(sse_event_t));
    if (sse_event == NULL) {
        return NULL;
    }

    sse_event->id = id ? mcp_strdup(id) : NULL;
    sse_event->event = event ? mcp_strdup(event) : NULL;
    sse_event->data = data ? mcp_strdup(data) : NULL;
    sse_event->timestamp = time(NULL);

    return sse_event;
}

/**
 * @brief Frees an SSE event structure.
 */
static void sse_event_free(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    if (event->id) {
        free(event->id);
    }

    if (event->event) {
        free(event->event);
    }

    if (event->data) {
        free(event->data);
    }

    free(event);
}

/**
 * @brief Sends an HTTP POST request.
 *
 * This is a simplified implementation that uses sockets directly.
 * In a production environment, you might want to use a more robust HTTP client library.
 */
static http_response_t* http_post_request(const char* url, const char* content_type,
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

