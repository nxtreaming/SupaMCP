#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/http_client_sse.h"
#include "internal/http_client_internal.h"
#include "internal/transport_internal.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_socket_utils.h"
#include "mcp_sync.h"
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

// Maximum buffer size for SSE events
#define SSE_BUFFER_SIZE 4096
// Maximum URL length
#define SSE_URL_MAX_LENGTH 256
// Maximum HTTP request length
#define SSE_REQUEST_MAX_LENGTH 1024
// Reconnection delay in milliseconds
#define SSE_RECONNECT_DELAY_MS 5000
// Sleep interval for reconnection loop in milliseconds
#define SSE_SLEEP_INTERVAL_MS 100
// Number of sleep intervals for reconnection delay
#define SSE_RECONNECT_INTERVALS (SSE_RECONNECT_DELAY_MS / SSE_SLEEP_INTERVAL_MS)

/**
 * @brief Creates an SSE event structure.
 *
 * @param id The event ID (can be NULL)
 * @param event The event type (can be NULL)
 * @param data The event data (can be NULL)
 * @return sse_event_t* Newly created event or NULL on failure
 */
sse_event_t* sse_event_create(const char* id, const char* event, const char* data) {
    sse_event_t* sse_event = (sse_event_t*)malloc(sizeof(sse_event_t));
    if (sse_event == NULL) {
        mcp_log_error("Failed to allocate memory for SSE event");
        return NULL;
    }

    // Initialize all fields to NULL/0
    sse_event->id = NULL;
    sse_event->event = NULL;
    sse_event->data = NULL;
    sse_event->timestamp = time(NULL);

    // Copy ID if provided
    if (id != NULL) {
        sse_event->id = mcp_strdup(id);
        if (sse_event->id == NULL && id[0] != '\0') {
            mcp_log_error("Failed to allocate memory for SSE event ID");
            sse_event_free(sse_event);
            return NULL;
        }
    }

    // Copy event type if provided
    if (event != NULL) {
        sse_event->event = mcp_strdup(event);
        if (sse_event->event == NULL && event[0] != '\0') {
            mcp_log_error("Failed to allocate memory for SSE event type");
            sse_event_free(sse_event);
            return NULL;
        }
    }

    // Copy data if provided
    if (data != NULL) {
        sse_event->data = mcp_strdup(data);
        if (sse_event->data == NULL && data[0] != '\0') {
            mcp_log_error("Failed to allocate memory for SSE event data");
            sse_event_free(sse_event);
            return NULL;
        }
    }

    return sse_event;
}

/**
 * @brief Frees an SSE event structure and all associated memory.
 *
 * @param event The event to free
 */
void sse_event_free(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    // Free all allocated memory
    if (event->id) {
        free(event->id);
        event->id = NULL;
    }

    if (event->event) {
        free(event->event);
        event->event = NULL;
    }

    if (event->data) {
        free(event->data);
        event->data = NULL;
    }

    // Free the event structure itself
    free(event);
}

/**
 * @brief Connects to the SSE endpoint.
 *
 * @param data Transport data containing connection information
 * @return socket_t Connected socket or MCP_INVALID_SOCKET on failure
 */
socket_t connect_to_sse_endpoint(http_client_transport_data_t* data) {
    if (data == NULL || data->host == NULL) {
        mcp_log_error("Invalid transport data for SSE connection");
        return MCP_INVALID_SOCKET;
    }

    // Build URL for logging purposes
    char url[SSE_URL_MAX_LENGTH];
    int url_len = snprintf(url, sizeof(url), "http%s://%s:%d/events",
                          data->use_ssl ? "s" : "",
                          data->host,
                          data->port);

    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        mcp_log_error("URL buffer overflow for SSE connection");
        return MCP_INVALID_SOCKET;
    }

    mcp_log_info("Connecting to SSE endpoint: %s", url);

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to create socket for SSE connection: %d", mcp_socket_get_last_error());
        return MCP_INVALID_SOCKET;
    }

    // Set socket options (non-blocking, etc.) if needed
    // For now, we'll use blocking mode

    // Resolve host
    struct hostent* server = gethostbyname(data->host);
    if (server == NULL) {
        mcp_log_error("Failed to resolve host: %s (error: %d)",
                     data->host, mcp_socket_get_last_error());
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)data->port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == MCP_SOCKET_ERROR) {
        mcp_log_error("Failed to connect to server: %s:%d (error: %d)",
                     data->host, data->port, mcp_socket_get_last_error());
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    // TODO: Implement SSL support if needed
    if (data->use_ssl) {
        mcp_log_error("SSL not implemented yet for SSE connection");
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    // Build HTTP request
    char request[SSE_REQUEST_MAX_LENGTH];
    int request_len = snprintf(request, sizeof(request),
                              "GET /events HTTP/1.1\r\n"
                              "Host: %s:%d\r\n"
                              "Accept: text/event-stream\r\n"
                              "Cache-Control: no-cache\r\n",
                              data->host, data->port);

    if (request_len < 0 || request_len >= (int)sizeof(request)) {
        mcp_log_error("HTTP request buffer overflow for SSE connection");
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    // Add Last-Event-ID if available
    if (data->last_event_id != NULL) {
        int id_len = snprintf(request + request_len, sizeof(request) - request_len,
                             "Last-Event-ID: %s\r\n", data->last_event_id);

        if (id_len < 0 || (request_len + id_len) >= (int)sizeof(request)) {
            mcp_log_error("HTTP request buffer overflow when adding Last-Event-ID");
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }

        request_len += id_len;
    }

    // Add API key if provided
    if (data->api_key != NULL) {
        int auth_len = snprintf(request + request_len, sizeof(request) - request_len,
                               "Authorization: Bearer %s\r\n", data->api_key);

        if (auth_len < 0 || (request_len + auth_len) >= (int)sizeof(request)) {
            mcp_log_error("HTTP request buffer overflow when adding Authorization");
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }

        request_len += auth_len;
    }

    // End headers
    int end_len = snprintf(request + request_len, sizeof(request) - request_len, "\r\n");

    if (end_len < 0 || (request_len + end_len) >= (int)sizeof(request)) {
        mcp_log_error("HTTP request buffer overflow when adding end of headers");
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    request_len += end_len;

    // Send request
    int sent_len = send(sock, request, request_len, 0);
    if (sent_len != request_len) {
        mcp_log_error("Failed to send HTTP request for SSE connection: sent %d of %d bytes (error: %d)",
                     sent_len, request_len, mcp_socket_get_last_error());
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    mcp_log_debug("Successfully connected to SSE endpoint: %s", url);

    // Return the socket
    return sock;
}

/**
 * @brief Processes an SSE event.
 *
 * @param data Transport data containing callback information
 * @param event The SSE event to process
 */
void process_sse_event(http_client_transport_data_t* data, const sse_event_t* event) {
    if (data == NULL || event == NULL) {
        mcp_log_error("Invalid parameters for process_sse_event");
        return;
    }

    // Store last event ID if provided
    if (event->id != NULL) {
        mcp_mutex_lock(data->mutex);  // Lock to safely update last_event_id

        if (data->last_event_id != NULL) {
            free(data->last_event_id);
            data->last_event_id = NULL;
        }

        data->last_event_id = mcp_strdup(event->id);
        if (data->last_event_id == NULL && event->id[0] != '\0') {
            mcp_log_error("Failed to allocate memory for last event ID");
        } else {
            mcp_log_debug("Updated last event ID: %s", data->last_event_id);
        }

        mcp_mutex_unlock(data->mutex);
    }

    // Process event data if provided
    if (event->data != NULL) {
        // Log event information
        const char* event_type = event->event ? event->event : "message";
        mcp_log_debug("Received SSE event: type=%s, id=%s, data_length=%zu",
                     event_type,
                     event->id ? event->id : "(none)",
                     strlen(event->data));

        // Call message callback if registered
        if (data->message_callback != NULL) {
            int error_code = 0;
            char* result = NULL;

            // Call the callback with the event data
            result = data->message_callback(
                data->callback_user_data,
                event->data,
                strlen(event->data),
                &error_code
            );

            // Check for errors
            if (error_code != 0) {
                mcp_log_error("Message callback returned error: %d", error_code);

                // Call error callback if registered
                if (data->error_callback != NULL) {
                    data->error_callback(data->callback_user_data, error_code);
                }
            }

            // Free result if returned
            if (result != NULL) {
                free(result);
                result = NULL;
            }
        } else {
            mcp_log_warn("No message callback registered for SSE events");
        }
    }
}

/**
 * @brief Helper function to trim leading spaces from a string.
 *
 * @param str The string to trim
 * @return char* Pointer to the first non-space character
 */
static char* trim_leading_spaces(char* str) {
    if (str == NULL) {
        return NULL;
    }

    while (*str == ' ') {
        str++;
    }

    return str;
}

/**
 * @brief Helper function to safely free a string pointer and set it to NULL.
 *
 * @param str Pointer to the string pointer to free
 */
static void safe_free_string(char** str) {
    if (str != NULL && *str != NULL) {
        free(*str);
        *str = NULL;
    }
}

/**
 * @brief Helper function to append a line to event data.
 *
 * @param event_data Pointer to the event data string
 * @param data_line Line to append
 * @return char* Updated event data string or NULL on failure
 */
static char* append_to_event_data(char* event_data, const char* data_line) {
    if (data_line == NULL) {
        return event_data;
    }

    if (event_data == NULL) {
        return mcp_strdup(data_line);
    }

    // Append to existing data with newline
    size_t old_len = strlen(event_data);
    size_t line_len = strlen(data_line);
    size_t new_len = old_len + line_len + 2; // +2 for newline and null terminator

    char* new_data = (char*)realloc(event_data, new_len);
    if (new_data == NULL) {
        mcp_log_error("Failed to allocate memory for appending event data");
        return event_data; // Return original data on failure
    }

    // Append newline and data line
    strcat(new_data, "\n");
    strcat(new_data, data_line);

    return new_data;
}

/**
 * @brief Process a single SSE message line.
 *
 * @param line The line to process
 * @param event_type Pointer to the event type string
 * @param event_id Pointer to the event ID string
 * @param event_data Pointer to the event data string
 * @return bool true if an event is complete, false otherwise
 */
static bool process_sse_line(const char* line, char** event_type, char** event_id, char** event_data) {
    if (line == NULL || event_type == NULL || event_id == NULL || event_data == NULL) {
        return false;
    }

    // Empty line indicates end of event
    if (strlen(line) == 0) {
        return *event_data != NULL; // Event is complete if we have data
    }

    // Event type
    if (strncmp(line, "event:", 6) == 0) {
        safe_free_string(event_type);
        *event_type = mcp_strdup(trim_leading_spaces((char*)line + 6));
    }
    // Event ID
    else if (strncmp(line, "id:", 3) == 0) {
        safe_free_string(event_id);
        *event_id = mcp_strdup(trim_leading_spaces((char*)line + 3));
    }
    // Event data
    else if (strncmp(line, "data:", 5) == 0) {
        char* data_line = trim_leading_spaces((char*)line + 5);
        *event_data = append_to_event_data(*event_data, data_line);
    }

    return false;
}

/**
 * @brief Clean up event parsing state.
 *
 * @param event_type Pointer to the event type string
 * @param event_id Pointer to the event ID string
 * @param event_data Pointer to the event data string
 */
static void cleanup_event_state(char** event_type, char** event_id, char** event_data) {
    safe_free_string(event_type);
    safe_free_string(event_id);
    safe_free_string(event_data);
}

/**
 * @brief Event thread function for HTTP client transport.
 *
 * This thread connects to the SSE endpoint and processes events.
 *
 * @param arg Pointer to the transport structure
 * @return void* Always returns NULL
 */
void* http_client_event_thread_func(void* arg) {
    // Validate input
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL) {
        mcp_log_error("Invalid transport for SSE event thread");
        return NULL;
    }

    // Get transport data
    http_client_transport_data_t* data = (http_client_transport_data_t*)transport->transport_data;
    if (data == NULL) {
        mcp_log_error("Invalid transport data for SSE event thread");
        return NULL;
    }

    mcp_log_info("HTTP client event thread started");

    // Main loop - continue while running flag is set
    while (data->running) {
        // Connect to SSE endpoint
        socket_t sock = connect_to_sse_endpoint(data);
        if (sock == MCP_INVALID_SOCKET) {
            mcp_log_error("Failed to connect to SSE endpoint, retrying in %d ms",
                         SSE_RECONNECT_DELAY_MS);

            // Sleep and retry, checking running flag periodically
            for (int i = 0; i < SSE_RECONNECT_INTERVALS && data->running; i++) {
                mcp_sleep_ms(SSE_SLEEP_INTERVAL_MS);
            }
            continue;
        }

        // Store the socket for later use (e.g., to close it in stop function)
        mcp_mutex_lock(data->mutex);
        data->sse_socket = sock;
        mcp_mutex_unlock(data->mutex);

        mcp_log_info("Connected to SSE endpoint, waiting for events");

        // Read SSE events
        char buffer[SSE_BUFFER_SIZE];
        int bytes_read = 0;

        // Event parsing state
        char* event_type = NULL;
        char* event_id = NULL;
        char* event_data = NULL;

        // Read loop - continue while running flag is set and connection is active
        while (data->running &&
               (bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            // Null-terminate the received data
            buffer[bytes_read] = '\0';

            mcp_log_debug("Received %d bytes from SSE endpoint", bytes_read);

            // Parse SSE events line by line
            char* line = strtok(buffer, "\r\n");
            while (line != NULL && data->running) {
                // Process the line and check if an event is complete
                if (process_sse_line(line, &event_type, &event_id, &event_data)) {
                    // Create and process the event
                    sse_event_t* event = sse_event_create(event_id, event_type, event_data);
                    if (event != NULL) {
                        process_sse_event(data, event);
                        sse_event_free(event);
                    } else {
                        mcp_log_error("Failed to create SSE event");
                    }

                    // Reset event state
                    cleanup_event_state(&event_type, &event_id, &event_data);
                }

                // Get next line
                line = strtok(NULL, "\r\n");
            }
        }

        // Check if we exited the loop due to an error
        if (bytes_read < 0 && data->running) {
            mcp_log_error("Error reading from SSE endpoint: %d", mcp_socket_get_last_error());
        } else if (bytes_read == 0 && data->running) {
            mcp_log_info("SSE connection closed by server");
        }

        // Clean up socket
        mcp_mutex_lock(data->mutex);
        mcp_socket_close(sock);
        data->sse_socket = MCP_INVALID_SOCKET;
        mcp_mutex_unlock(data->mutex);

        // Clean up event parsing state
        cleanup_event_state(&event_type, &event_id, &event_data);

        // If we're still running, retry connection after delay
        if (data->running) {
            mcp_log_info("SSE connection closed, retrying in %d ms", SSE_RECONNECT_DELAY_MS);

            // Sleep and retry, checking running flag periodically
            for (int i = 0; i < SSE_RECONNECT_INTERVALS && data->running; i++) {
                mcp_sleep_ms(SSE_SLEEP_INTERVAL_MS);
            }
        }
    }

    mcp_log_info("HTTP client event thread stopped");
    return NULL;
}
