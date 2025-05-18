/**
 * @file mcp_http_client_sse.c
 * @brief Implementation of HTTP client Server-Sent Events (SSE) functionality
 *
 * This file contains the implementation of SSE client functionality for the MCP
 * HTTP transport. It handles connecting to SSE endpoints, processing events,
 * and maintaining connections with automatic reconnection.
 */

#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

/* Internal headers */
#include "internal/http_client_sse.h"
#include "internal/http_client_internal.h"
#include "internal/transport_internal.h"

/* MCP library headers */
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_socket_utils.h"
#include "mcp_sync.h"

/* Standard library headers */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Platform-specific headers */
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

/* ===== Constants and Configuration ===== */

/**
 * @brief Buffer size constants for SSE client
 */
#define SSE_BUFFER_SIZE        4096  /**< Maximum buffer size for SSE events */
#define SSE_URL_MAX_LENGTH     256   /**< Maximum URL length for SSE endpoint */
#define SSE_REQUEST_MAX_LENGTH 1024  /**< Maximum HTTP request length */

/**
 * @brief Reconnection timing constants
 */
#define SSE_RECONNECT_DELAY_MS  5000  /**< Delay between reconnection attempts (ms) */
#define SSE_SLEEP_INTERVAL_MS   100   /**< Sleep interval for checking running flag (ms) */
#define SSE_RECONNECT_INTERVALS (SSE_RECONNECT_DELAY_MS / SSE_SLEEP_INTERVAL_MS)

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
 * @brief Creates an SSE event structure with the specified properties.
 *
 * This function allocates and initializes a new SSE event structure,
 * copying the provided ID, event type, and data strings if they are not NULL.
 * The event timestamp is set to the current time.
 *
 * @param id The event ID (can be NULL)
 * @param event The event type (can be NULL)
 * @param data The event data (can be NULL)
 * @return sse_event_t* Newly created event or NULL on failure
 */
sse_event_t* sse_event_create(const char* id, const char* event, const char* data) {
    // Allocate memory for the event structure
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

    mcp_log_debug("Created SSE event: id=%s, type=%s, data_length=%zu",
                 id ? id : "(none)",
                 event ? event : "(default)",
                 data ? strlen(data) : 0);

    return sse_event;
}

/**
 * @brief Frees an SSE event structure and all associated memory.
 *
 * This function safely releases all memory allocated for an SSE event,
 * including the ID, event type, data strings, and the event structure itself.
 * It handles NULL pointers safely at all levels.
 *
 * @param event The event to free (can be NULL)
 */
void sse_event_free(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    // Free all allocated string fields
    safe_free_string(&event->id);
    safe_free_string(&event->event);
    safe_free_string(&event->data);

    // Free the event structure itself
    free(event);
}

/**
 * @brief Connects to an SSE endpoint.
 *
 * This function establishes a connection to the specified SSE endpoint,
 * constructs and sends the appropriate HTTP request with headers,
 * including optional Last-Event-ID and Authorization headers.
 *
 * @param data Transport data containing connection information
 * @return socket_t Socket handle or MCP_INVALID_SOCKET on failure
 */
socket_t connect_to_sse_endpoint(http_client_transport_data_t* data) {
    if (data == NULL || data->host == NULL) {
        mcp_log_error("Invalid parameters for connect_to_sse_endpoint");
        return MCP_INVALID_SOCKET;
    }

    // Construct URL for logging
    char url[SSE_URL_MAX_LENGTH];
    int url_len = snprintf(url, sizeof(url), "%s://%s:%d/events",
                          data->use_ssl ? "https" : "http",
                          data->host, data->port);
    
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
        mcp_log_warn("SSL support not implemented yet for SSE connection");
        // For now, continue without SSL
    }

    // Prepare HTTP request with basic headers
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

    // Add Last-Event-ID header if available (for reconnection)
    if (data->last_event_id != NULL) {
        int id_len = snprintf(request + request_len, sizeof(request) - request_len,
                             "Last-Event-ID: %s\r\n", data->last_event_id);

        if (id_len < 0 || (request_len + id_len) >= (int)sizeof(request)) {
            mcp_log_error("HTTP request buffer overflow when adding Last-Event-ID");
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }

        request_len += id_len;
        mcp_log_debug("Added Last-Event-ID header: %s", data->last_event_id);
    }

    // Add Authorization header if API key is provided
    if (data->api_key != NULL) {
        int auth_len = snprintf(request + request_len, sizeof(request) - request_len,
                               "Authorization: Bearer %s\r\n", data->api_key);

        if (auth_len < 0 || (request_len + auth_len) >= (int)sizeof(request)) {
            mcp_log_error("HTTP request buffer overflow when adding Authorization");
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }

        request_len += auth_len;
        mcp_log_debug("Added Authorization header");
    }

    // End headers with an empty line
    int end_len = snprintf(request + request_len, sizeof(request) - request_len, "\r\n");

    if (end_len < 0 || (request_len + end_len) >= (int)sizeof(request)) {
        mcp_log_error("HTTP request buffer overflow when adding end of headers");
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    request_len += end_len;

    // Send HTTP request
    int sent_len = send(sock, request, request_len, 0);
    if (sent_len != request_len) {
        mcp_log_error("Failed to send HTTP request for SSE connection: sent %d of %d bytes (error: %d)",
                     sent_len, request_len, mcp_socket_get_last_error());
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    mcp_log_debug("Successfully connected to SSE endpoint: %s", url);

    // Return the connected socket
    return sock;
}

/**
 * @brief Processes an SSE event.
 *
 * This function handles an SSE event by updating the last event ID and
 * calling the appropriate callbacks with the event data. It ensures thread-safe
 * updates to shared data using mutex locks.
 *
 * @param data Transport data containing callback information
 * @param event The SSE event to process
 */
void process_sse_event(http_client_transport_data_t* data, const sse_event_t* event) {
    if (data == NULL || event == NULL) {
        mcp_log_error("Invalid parameters for process_sse_event");
        return;
    }

    // Get event type (default to "message" if not specified)
    const char* event_type = event->event ? event->event : "message";
    const char* event_id = event->id ? event->id : "(none)";
    
    // Log basic event information
    mcp_log_debug("Processing SSE event: type=%s, id=%s, timestamp=%ld",
                 event_type, event_id, (long)event->timestamp);

    // Update last event ID if provided (thread-safe)
    if (event->id != NULL) {
        mcp_mutex_lock(data->mutex);  // Lock to safely update last_event_id

        // Free previous event ID if exists
        safe_free_string(&data->last_event_id);

        // Store new event ID
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
        size_t data_length = strlen(event->data);
        
        // Log detailed event information
        mcp_log_debug("Received SSE event data: type=%s, id=%s, data_length=%zu",
                     event_type, event_id, data_length);

        // Call message callback if registered
        if (data->message_callback != NULL) {
            int error_code = 0;
            char* result = NULL;

            // Call the callback with the event data
            result = data->message_callback(
                data->callback_user_data,
                event->data,
                data_length,
                &error_code
            );

            // Handle callback results
            if (error_code != 0) {
                mcp_log_error("Message callback returned error: %d", error_code);

                // Call error callback if registered
                if (data->error_callback != NULL) {
                    data->error_callback(data->callback_user_data, error_code);
                }
            } else {
                mcp_log_debug("Message callback processed successfully");
            }

            // Free result if returned
            safe_free_string(&result);
        } else {
            mcp_log_warn("No message callback registered for SSE events");
        }
    } else {
        mcp_log_debug("Received SSE event with no data payload");
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
 * @brief Helper function to wait for a specified time while checking the running flag.
 *
 * This function breaks the wait into smaller intervals and checks the running flag
 * between each interval, allowing for faster thread termination.
 *
 * @param data Transport data containing the running flag
 * @param wait_ms Total time to wait in milliseconds
 */
static void wait_with_running_check(http_client_transport_data_t* data, int wait_ms) {
    if (data == NULL || wait_ms <= 0) {
        return;
    }

    // Calculate number of intervals based on sleep interval constant
    int intervals = wait_ms / SSE_SLEEP_INTERVAL_MS;
    if (intervals <= 0) intervals = 1;

    // Sleep in small intervals, checking running flag each time
    for (int i = 0; i < intervals && data->running; i++) {
        mcp_sleep_ms(SSE_SLEEP_INTERVAL_MS);
    }
}

/**
 * @brief Event thread function for HTTP client transport.
 *
 * This thread connects to the SSE endpoint and processes events in a loop.
 * It handles reconnection attempts when the connection is lost and ensures
 * proper cleanup of resources. The thread continues running until explicitly
 * stopped by setting the running flag to false.
 *
 * @param arg Pointer to the transport structure
 * @return void* Always returns NULL
 */
void* http_client_event_thread_func(void* arg) {
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

    // Main connection loop - continue while running flag is set
    while (data->running) {
        // Connect to SSE endpoint
        socket_t sock = connect_to_sse_endpoint(data);
        if (sock == MCP_INVALID_SOCKET) {
            mcp_log_error("Failed to connect to SSE endpoint, retrying in %d ms",
                         SSE_RECONNECT_DELAY_MS);

            // Wait before retry, checking running flag periodically
            wait_with_running_check(data, SSE_RECONNECT_DELAY_MS);
            continue;
        }

        // Store the socket for later use (thread-safe)
        mcp_mutex_lock(data->mutex);
        data->sse_socket = sock;
        mcp_mutex_unlock(data->mutex);

        mcp_log_info("Connected to SSE endpoint, waiting for events");

        // Initialize buffer and event parsing state
        char buffer[SSE_BUFFER_SIZE];
        char* event_type = NULL;
        char* event_id = NULL;
        char* event_data = NULL;
        int bytes_read = 0;

        // Event reading loop - continue while running flag is set and connection is active
        while (data->running) {
            // Receive data from the socket
            bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
            
            // Check for connection errors or closure
            if (bytes_read <= 0) {
                break; // Exit the reading loop
            }
            
            // Null-terminate the received data for string operations
            buffer[bytes_read] = '\0';

            mcp_log_debug("Received %d bytes from SSE endpoint", bytes_read);

            // Parse SSE events line by line
            char* line = strtok(buffer, "\r\n");
            while (line != NULL && data->running) {
                // Process the line and check if an event is complete
                if (process_sse_line(line, &event_type, &event_id, &event_data)) {
                    // Create and process the complete event
                    sse_event_t* event = sse_event_create(event_id, event_type, event_data);
                    if (event != NULL) {
                        process_sse_event(data, event);
                        sse_event_free(event);
                    } else {
                        mcp_log_error("Failed to create SSE event");
                    }

                    // Reset event state for next event
                    cleanup_event_state(&event_type, &event_id, &event_data);
                }

                // Get next line
                line = strtok(NULL, "\r\n");
            }
        }

        // Log reason for loop exit
        if (bytes_read < 0 && data->running) {
            mcp_log_error("Error reading from SSE endpoint: %d", mcp_socket_get_last_error());
        } else if (bytes_read == 0 && data->running) {
            mcp_log_info("SSE connection closed by server");
        }

        // Clean up socket (thread-safe)
        mcp_mutex_lock(data->mutex);
        mcp_socket_close(sock);
        data->sse_socket = MCP_INVALID_SOCKET;
        mcp_mutex_unlock(data->mutex);

        // Clean up event parsing state
        cleanup_event_state(&event_type, &event_id, &event_data);

        // If we're still running, retry connection after delay
        if (data->running) {
            mcp_log_info("SSE connection closed, retrying in %d ms", SSE_RECONNECT_DELAY_MS);
            wait_with_running_check(data, SSE_RECONNECT_DELAY_MS);
        }
    }

    mcp_log_info("HTTP client event thread stopped");
    return NULL;
}
