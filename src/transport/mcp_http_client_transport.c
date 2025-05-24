#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_http_client_transport.h"
#include "internal/transport_internal.h"
#include "internal/http_client_internal.h"
#include "internal/http_client_request.h"
#include "internal/http_client_sse.h"
#include "internal/http_client_utils.h"
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
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

// Maximum URL length
#define HTTP_URL_MAX_LENGTH 256
// Default timeout in milliseconds (30 seconds)
#define HTTP_DEFAULT_TIMEOUT_MS 30000
// HTTP endpoint for tool calls
#define HTTP_ENDPOINT_CALL_TOOL "/call_tool"
// HTTP content type for JSON
#define HTTP_CONTENT_TYPE_JSON "application/json"
// Length of binary prefix header (4 bytes)
#define HTTP_BINARY_PREFIX_LENGTH 4

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
 *
 * @param host The hostname or IP address of the server
 * @param port The port number on the server
 * @return mcp_transport_t* A new transport instance or NULL on failure
 */
mcp_transport_t* mcp_transport_http_client_create(const char* host, uint16_t port) {
    if (host == NULL) {
        mcp_log_error("Invalid host parameter for HTTP client transport");
        return NULL;
    }

    // Create a basic configuration
    mcp_http_client_config_t config = {0};
    config.host = host;
    config.port = port;
    config.use_ssl = false;
    config.cert_path = NULL;
    config.key_path = NULL;
    config.timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
    config.api_key = NULL;

    // Use the detailed create function
    return mcp_transport_http_client_create_with_config(&config);
}

/**
 * @brief Helper function to clean up transport data on error.
 *
 * @param data The transport data to clean up
 */
static void cleanup_transport_data(http_client_transport_data_t* data) {
    if (data == NULL) {
        return;
    }

    safe_free_string(&data->host);
    safe_free_string(&data->cert_path);
    safe_free_string(&data->key_path);
    safe_free_string(&data->api_key);
    safe_free_string(&data->last_response);

    // Clean up SSL context if it exists
    if (data->ssl_ctx != NULL) {
        http_client_ssl_cleanup(data->ssl_ctx);
        data->ssl_ctx = NULL;
    }

    if (data->mutex != NULL) {
        mcp_mutex_destroy(data->mutex);
        data->mutex = NULL;
    }

    free(data);
}

/**
 * @brief Creates an HTTP client transport with detailed configuration.
 *
 * @param config The HTTP client configuration
 * @return mcp_transport_t* A new transport instance or NULL on failure
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

    // Initialize SSL context to NULL
    data->ssl_ctx = NULL;

    // Initialize response handling
    data->last_response = NULL;
    data->last_request_id = 0;

    // Copy configuration
    data->host = mcp_strdup(config->host);
    if (data->host == NULL) {
        mcp_log_error("Failed to allocate memory for host");
        cleanup_transport_data(data);
        free(transport);
        return NULL;
    }

    data->port = config->port;
    data->use_ssl = config->use_ssl;
    data->timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : HTTP_DEFAULT_TIMEOUT_MS;

    // Copy optional configuration fields
    if (config->cert_path != NULL) {
        data->cert_path = mcp_strdup(config->cert_path);
        if (data->cert_path == NULL && config->cert_path[0] != '\0') {
            mcp_log_error("Failed to allocate memory for cert_path");
            cleanup_transport_data(data);
            free(transport);
            return NULL;
        }
    }

    if (config->key_path != NULL) {
        data->key_path = mcp_strdup(config->key_path);
        if (data->key_path == NULL && config->key_path[0] != '\0') {
            mcp_log_error("Failed to allocate memory for key_path");
            cleanup_transport_data(data);
            free(transport);
            return NULL;
        }
    }

    if (config->api_key != NULL) {
        data->api_key = mcp_strdup(config->api_key);
        if (data->api_key == NULL && config->api_key[0] != '\0') {
            mcp_log_error("Failed to allocate memory for api_key");
            cleanup_transport_data(data);
            free(transport);
            return NULL;
        }
    }

    // Create mutex (used for SSE and thread safety)
    data->mutex = mcp_mutex_create();
    if (data->mutex == NULL) {
        mcp_log_error("Failed to create mutex for HTTP client transport");
        cleanup_transport_data(data);
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

    mcp_log_info("HTTP client transport created for %s:%d (SSL: %s)",
                data->host, data->port, data->use_ssl ? "enabled" : "disabled");

    return transport;
}

/**
 * @brief Destroys an HTTP client transport.
 *
 * @param transport The transport to destroy
 * @return int 0 on success, -1 on failure
 */
static int http_client_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        mcp_log_error("NULL transport passed to http_client_transport_destroy");
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_warn("HTTP client transport has no data to destroy");
        free(transport);
        return 0;
    }

    // Stop the transport if it's running
    if (data->running) {
        mcp_log_debug("Stopping HTTP client transport before destroying");
        http_client_transport_stop(transport);
    }

    // Clean up transport data
    cleanup_transport_data(data);

    // Free transport structure
    free(transport);

    mcp_log_info("HTTP client transport destroyed");
    return 0;
}

/**
 * @brief Starts an HTTP client transport.
 *
 * @param transport The transport to start
 * @param message_callback Callback function for received messages
 * @param user_data User data to pass to callbacks
 * @param error_callback Callback function for errors
 * @return int 0 on success, -1 on failure
 */
static int http_client_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback) {
    if (transport == NULL) {
        mcp_log_error("NULL transport passed to http_client_transport_start");
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_error("HTTP client transport has no data");
        return -1;
    }

    // Store callback info in transport structure
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

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library");
        return -1;
    }

    // Set running flag
    data->running = true;

    // Start event thread for SSE
    int thread_result = mcp_thread_create(&data->event_thread, http_client_event_thread_func, transport);
    if (thread_result != 0) {
        mcp_log_error("Failed to create HTTP client event thread: %d", thread_result);
        data->running = false;
        return -1;
    }

    mcp_log_info("HTTP client transport started for %s:%d", data->host, data->port);
    return 0;
}

/**
 * @brief Stops an HTTP client transport.
 *
 * @param transport The transport to stop
 * @return int 0 on success, -1 on failure
 */
static int http_client_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL) {
        mcp_log_error("NULL transport passed to http_client_transport_stop");
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_error("HTTP client transport has no data");
        return -1;
    }

    // Check if running
    if (!data->running) {
        mcp_log_warn("HTTP client transport not running");
        return 0;
    }

    mcp_log_debug("Stopping HTTP client transport for %s:%d", data->host, data->port);

    // Set running flag to false to signal threads to stop
    data->running = false;

    // Close the SSE socket and clean up SSL to unblock the event thread
    mcp_mutex_lock(data->mutex);

    // Clean up SSL if used
    if (data->ssl_ctx != NULL) {
        mcp_log_debug("Cleaning up SSL context");
        http_client_ssl_cleanup(data->ssl_ctx);
        data->ssl_ctx = NULL;
    }

    // Close socket
    if (data->sse_socket != MCP_INVALID_SOCKET) {
        mcp_log_debug("Closing SSE socket to unblock event thread");
        mcp_socket_close(data->sse_socket);
        data->sse_socket = MCP_INVALID_SOCKET;
    }

    mcp_mutex_unlock(data->mutex);

    // Wait for event thread to finish
    mcp_log_debug("Waiting for event thread to finish");
    int join_result = mcp_thread_join(data->event_thread, NULL);
    if (join_result != 0) {
        mcp_log_warn("Failed to join HTTP client event thread: %d", join_result);
    }

    mcp_log_info("HTTP client transport stopped");
    return 0;
}

/**
 * @brief Helper function to build the URL for HTTP requests.
 *
 * @param data The transport data
 * @param endpoint The endpoint path (e.g., "/call_tool")
 * @param url_buffer Buffer to store the URL
 * @param buffer_size Size of the URL buffer
 * @return bool true if successful, false on failure
 */
static bool build_url(const http_client_transport_data_t* data,
                     const char* endpoint,
                     char* url_buffer,
                     size_t buffer_size) {
    if (data == NULL || endpoint == NULL || url_buffer == NULL || buffer_size == 0) {
        return false;
    }

    int url_len = snprintf(url_buffer, buffer_size, "http%s://%s:%d%s",
                          data->use_ssl ? "s" : "",
                          data->host,
                          data->port,
                          endpoint);

    if (url_len < 0 || (size_t)url_len >= buffer_size) {
        mcp_log_error("URL buffer overflow");
        return false;
    }

    return true;
}

/**
 * @brief Helper function to process binary length prefix frames.
 *
 * @param data The input data buffer
 * @param size Size of the input data
 * @param json_data_out Pointer to store the JSON data pointer
 * @param json_size_out Pointer to store the JSON data size
 * @return bool true if successful, false on failure
 */
static bool process_binary_frame(const void* data, size_t size,
                                const char** json_data_out, size_t* json_size_out) {
    if (data == NULL || json_data_out == NULL || json_size_out == NULL) {
        return false;
    }

    // Initialize output parameters
    *json_data_out = NULL;
    *json_size_out = 0;

    // Check if this is a binary length prefix frame
    // The first 4 bytes are the length in network byte order
    if (size >= HTTP_BINARY_PREFIX_LENGTH) {
        const uint8_t* bytes = (const uint8_t*)data;
        uint32_t length_prefix = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];

        // If the length prefix matches the remaining data size, this is a binary frame
        if (length_prefix == size - HTTP_BINARY_PREFIX_LENGTH) {
            // Skip the 4-byte length prefix and use only the JSON data
            *json_data_out = (const char*)data + HTTP_BINARY_PREFIX_LENGTH;
            *json_size_out = size - HTTP_BINARY_PREFIX_LENGTH;
            mcp_log_debug("HTTP client transport detected binary frame, skipping 4-byte length prefix");
            return true;
        }
    }

    // Not a binary frame or invalid length prefix, use the entire data
    *json_data_out = (const char*)data;
    *json_size_out = size;
    return true;
}

/**
 * @brief Helper function to clean up JSON response data.
 *
 * @param response_data The raw response data
 * @param response_size Size of the response data
 * @return char* Newly allocated, cleaned JSON string or NULL on failure
 */
static char* clean_json_response(const char* response_data, size_t response_size) {
    if (response_data == NULL || response_size == 0) {
        return NULL;
    }

    // Allocate memory for the cleaned JSON (add space for null terminator)
    char* clean_json = (char*)malloc(response_size + 1);
    if (clean_json == NULL) {
        mcp_log_error("Failed to allocate memory for JSON response");
        return NULL;
    }

    // Copy the response data and ensure it's null-terminated
    memcpy(clean_json, response_data, response_size);
    clean_json[response_size] = '\0';

    // Find the end of the JSON data (in case there are trailing characters)
    if (clean_json[0] == '{') {
        // Find the matching closing brace
        int brace_count = 1;
        for (size_t i = 1; i < response_size; i++) {
            if (clean_json[i] == '{') {
                brace_count++;
            } else if (clean_json[i] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    // Null-terminate at the end of the JSON object
                    clean_json[i + 1] = '\0';
                    break;
                }
            }
        }
    }

    return clean_json;
}

/**
 * @brief Sends data through an HTTP client transport.
 *
 * This function sends a JSON-RPC request to the HTTP server using the POST method.
 * It handles the binary length prefix format used by mcp_client.
 *
 * @param transport The transport to use
 * @param data The data to send
 * @param size Size of the data
 * @return int 0 on success, -1 on failure
 */
static int http_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (transport == NULL || data == NULL || size == 0) {
        mcp_log_error("Invalid parameters for http_client_transport_send");
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data_struct = (http_client_transport_data_t*)transport->transport_data;
    if (data_struct == NULL) {
        mcp_log_error("HTTP client transport has no data");
        return -1;
    }

    // Check if running
    if (!data_struct->running) {
        mcp_log_error("HTTP client transport not running");
        return -1;
    }

    // Build URL
    char url[HTTP_URL_MAX_LENGTH];
    if (!build_url(data_struct, HTTP_ENDPOINT_CALL_TOOL, url, sizeof(url))) {
        return -1;
    }

    // Process the data to extract the JSON-RPC request
    const char* json_data = NULL;
    size_t json_size = 0;

    if (!process_binary_frame(data, size, &json_data, &json_size)) {
        mcp_log_error("Failed to process binary frame");
        return -1;
    }

    // Extract the request ID from the JSON-RPC request
    uint64_t request_id = extract_request_id(json_data, json_size);
    if (request_id == 0) {
        mcp_log_error("Failed to extract request ID from JSON-RPC request");
        return -1;
    }

    // Log the request data for debugging
    mcp_log_debug("HTTP client transport sending request (ID: %llu, size: %zu bytes)",
                 (unsigned long long)request_id, json_size);

    // Send HTTP POST request
    http_response_t* response = http_post_request(
        url,
        HTTP_CONTENT_TYPE_JSON,
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
        mcp_log_debug("HTTP response status code: 200 OK");
    }

    // Process response
    int result = -1;

    if (response->data != NULL && response->size > 0) {
        // Clean and process the JSON response
        char* clean_json = clean_json_response(response->data, response->size);
        if (clean_json != NULL) {
            // Log the cleaned response data for debugging
            mcp_log_debug("HTTP client transport received response for request ID %llu (size: %zu bytes)",
                         (unsigned long long)request_id, strlen(clean_json));

            // Store the response in the transport data structure
            mcp_mutex_lock(data_struct->mutex);

            // Free previous response if any
            safe_free_string(&data_struct->last_response);

            // Store the new response
            data_struct->last_response = mcp_strdup(clean_json);
            if (data_struct->last_response != NULL) {
                data_struct->last_request_id = request_id;
                result = 0; // Success
            } else {
                mcp_log_error("Failed to store HTTP response");
            }

            mcp_mutex_unlock(data_struct->mutex);

            // Free the temporary JSON data
            free(clean_json);
        } else {
            mcp_log_error("Failed to clean JSON response");
        }
    } else {
        mcp_log_error("Empty response from server");
    }

    // Free response
    http_response_free(response);

    if (result == 0) {
        mcp_log_info("HTTP client transport successfully sent %zu bytes and received response", json_size);
    }

    return result;
}

/**
 * @brief Sends data from multiple buffers through an HTTP client transport.
 *
 * This function handles the binary length prefix format used by mcp_client.
 * The first buffer typically contains the 4-byte length prefix, and the second buffer
 * contains the JSON data.
 *
 * @param transport The transport to use
 * @param buffers Array of buffers to send
 * @param buffer_count Number of buffers in the array
 * @return int 0 on success, -1 on failure
 */
static int http_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (transport == NULL || buffers == NULL || buffer_count == 0) {
        mcp_log_error("Invalid parameters for http_client_transport_sendv");
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_error("HTTP client transport has no data");
        return -1;
    }

    // Check if running
    if (!data->running) {
        mcp_log_error("HTTP client transport not running");
        return -1;
    }

    // Optimization for common case: binary length prefix frame
    // (buffer_count == 2 and first buffer is 4 bytes)
    if (buffer_count == 2 && buffers[0].size == HTTP_BINARY_PREFIX_LENGTH) {
        // This is likely a binary length prefix frame
        // Skip the first buffer (length prefix) and use only the second buffer (JSON data)
        mcp_log_debug("HTTP client transport detected binary frame in sendv, using second buffer directly");
        return http_client_transport_send(transport, buffers[1].data, buffers[1].size);
    }

    // For other cases, combine all buffers and send
    mcp_log_debug("HTTP client transport combining %zu buffers for sending", buffer_count);

    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        if (buffers[i].data == NULL) {
            mcp_log_error("Buffer %zu has NULL data pointer", i);
            return -1;
        }
        total_size += buffers[i].size;
    }

    if (total_size == 0) {
        mcp_log_error("Total buffer size is zero");
        return -1;
    }

    // Allocate a single buffer
    char* combined_buffer = (char*)malloc(total_size);
    if (combined_buffer == NULL) {
        mcp_log_error("Failed to allocate memory for combined buffer (%zu bytes)", total_size);
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
 *
 * @param transport The transport to use
 * @param data Pointer to store the received data
 * @param size Pointer to store the size of the received data
 * @param timeout_ms Timeout in milliseconds (unused for HTTP)
 * @return int 0 on success, -1 on failure
 */
static int http_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    (void)timeout_ms;

    // Validate input parameters
    if (transport == NULL || data == NULL || size == NULL) {
        mcp_log_error("Invalid parameters for http_client_transport_receive");
        return -1;
    }

    // Get transport data
    http_client_transport_data_t* data_struct = (http_client_transport_data_t*)transport->transport_data;
    if (data_struct == NULL) {
        mcp_log_error("HTTP client transport has no data");
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

    // Get the stored response with thread safety
    mcp_mutex_lock(data_struct->mutex);

    if (data_struct->last_response != NULL) {
        // Make a copy of the response
        *data = mcp_strdup(data_struct->last_response);
        if (*data != NULL) {
            *size = strlen(*data);
            mcp_log_debug("HTTP client transport receive: Retrieved stored response (ID: %llu, %zu bytes)",
                         (unsigned long long)data_struct->last_request_id, *size);

            // Free the stored response to avoid memory leaks
            safe_free_string(&data_struct->last_response);
            data_struct->last_request_id = 0;

            mcp_mutex_unlock(data_struct->mutex);
            return 0;
        } else {
            mcp_log_error("Failed to duplicate response string");
        }
    }

    mcp_mutex_unlock(data_struct->mutex);

    mcp_log_debug("HTTP client transport receive: No stored response available");
    return -1;
}

