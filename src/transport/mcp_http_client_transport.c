#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_http_client_transport.h"
#include "internal/transport_internal.h"
#include "internal/mcp_http_client_internal.h"
#include "internal/mcp_http_client_request.h"
#include "internal/mcp_http_client_sse.h"
#include "internal/mcp_http_client_utils.h"
#include "mcp_log.h"
#include "mcp_sync.h"
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

    // Initialize socket to invalid
    data->sse_socket = MCP_INVALID_SOCKET;

    // Initialize response handling
    data->last_response = NULL;
    data->last_request_id = 0;

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

    // Create mutex (only used for SSE)
    data->mutex = mcp_mutex_create();
    if (data->mutex == NULL) {
        mcp_log_error("Failed to create mutex for HTTP client transport");
        free(data->host);
        free(data->cert_path);
        free(data->key_path);
        free(data->api_key);
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize transport structure
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_HTTP;
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
    free(data->host);
    free(data->cert_path);
    free(data->key_path);
    free(data->api_key);

    // Clean up response handling
    if (data->last_response) {
        free(data->last_response);
        data->last_response = NULL;
    }

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

    // Close the SSE socket to unblock the event thread
    // Use mutex to protect access to sse_socket
    mcp_mutex_lock(data->mutex);
    if (data->sse_socket != MCP_INVALID_SOCKET) {
        mcp_socket_close(data->sse_socket);
        data->sse_socket = MCP_INVALID_SOCKET;
    }
    mcp_mutex_unlock(data->mutex);

    // Wait for event thread to finish (with timeout)
    mcp_thread_join(data->event_thread, NULL);

    mcp_log_info("HTTP client transport stopped");

    return 0;
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

    // Log response status
    if (response->status_code != 200) {
        mcp_log_warn("HTTP response status code: %d (not 200 OK)", response->status_code);
    } else {
        mcp_log_info("HTTP response status code: 200 OK");
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
        mcp_log_debug("HTTP client transport received response: %s", clean_json);

        // For HTTP transport, we don't call the message callback directly
        // Instead, we store the response in the transport data structure
        // and let the receive function return it to the caller

        // Free previous response if any
        if (data_struct->last_response != NULL) {
            free(data_struct->last_response);
            data_struct->last_response = NULL;
        }

        // Store the new response
        data_struct->last_response = mcp_strdup(clean_json);
        data_struct->last_request_id = request_id;

        mcp_log_debug("HTTP transport: Stored response for request ID %llu", (unsigned long long)request_id);

        // For HTTP transport, we don't call the message callback
        // Instead, we store the response in the transport data structure
        // and let the client retrieve it directly through the receive function

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
 * This function retrieves the response stored by the send function.
 * For HTTP transport, the response is already available when this function is called,
 * because HTTP is a synchronous request-response protocol.
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

    // Get the stored response
    if (data_struct->last_response != NULL) {
        // Make a copy of the response
        *data = mcp_strdup(data_struct->last_response);
        if (*data != NULL) {
            *size = strlen(*data);
            mcp_log_debug("HTTP client transport receive: Retrieved stored response (%zu bytes)", *size);

            // Free the stored response to avoid memory leaks
            free(data_struct->last_response);
            data_struct->last_response = NULL;
            data_struct->last_request_id = 0;

            return 0;
        }
    }

    mcp_log_debug("HTTP client transport receive: No stored response available");
    return -1;
}

