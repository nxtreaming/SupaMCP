/**
 * @file mcp_http_client_sse.c
 * @brief Implementation of HTTP client Server-Sent Events (SSE) functionality
 *
 * This file contains the implementation of SSE client functionality for the MCP
 * HTTP transport. It handles connecting to SSE endpoints, processing events,
 * and maintaining connections with automatic reconnection.
 */
/* Internal headers */
#include "internal/http_client_sse.h"
#include "internal/http_client_internal.h"
#include "internal/transport_internal.h"
#include "internal/http_client_ssl.h"

/* MCP library headers */
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_socket_utils.h"
#include "mcp_sys_utils.h"
#include "mcp_sync.h"
#include "mcp_http_sse_common.h"

/* Standard library headers */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Platform-specific headers */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

/* Define Windows socket error codes for non-Windows platforms */
#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK EWOULDBLOCK
#endif
#ifndef WSAEINPROGRESS
#define WSAEINPROGRESS EINPROGRESS
#endif
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
#define SSE_HEARTBEAT_INTERVAL_MS 30000 /**< Interval for checking connection health (ms) */
#define SSE_MAX_IDLE_TIME_MS 300000 /**< Maximum time without receiving data before reconnecting (ms) (5 minutes) */

/**
 * @brief Creates a TCP socket and connects to the specified server with timeout.
 *
 * @param host Hostname to connect to
 * @param port Port to connect to
 * @param timeout_ms Connection timeout in milliseconds (0 for no timeout)
 * @return socket_t Connected socket or MCP_INVALID_SOCKET on failure
 */
static socket_t create_and_connect_socket(const char* host, uint16_t port, uint32_t timeout_ms) {
    if (host == NULL) {
        mcp_log_error("Invalid host parameter for socket connection");
        return MCP_INVALID_SOCKET;
    }

    // Create socket
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Failed to create socket for connection: %d", mcp_socket_get_lasterror());
        return MCP_INVALID_SOCKET;
    }

    // Set socket to non-blocking mode if timeout is specified
    if (timeout_ms > 0) {
#ifdef _WIN32
        u_long mode = 1;
        if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
            mcp_log_error("Failed to set socket to non-blocking mode: %d", mcp_socket_get_lasterror());
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }
#else
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
            mcp_log_error("Failed to set socket to non-blocking mode: %d", mcp_socket_get_lasterror());
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }
#endif
    }

    // Resolve host
    struct hostent* server = gethostbyname(host);
    if (server == NULL) {
        mcp_log_error("Failed to resolve host: %s (error: %d)", host, mcp_socket_get_lasterror());
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    // Prepare server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    // Connect to server (with timeout if specified)
    int connect_result = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));

    // If using timeout and connect is in progress
    if (timeout_ms > 0 && (connect_result == MCP_SOCKET_ERROR) &&
        (mcp_socket_get_lasterror() == WSAEWOULDBLOCK ||
         mcp_socket_get_lasterror() == WSAEINPROGRESS ||
         mcp_socket_get_lasterror() == EINPROGRESS)) {

        // Use select to wait for connection with timeout
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        // Wait for connection to complete or timeout
        int select_result = select((int)sock + 1, NULL, &write_fds, NULL, &tv);
        if (select_result <= 0) {
            // Timeout or error
            mcp_log_error("Connection to %s:%d timed out after %u ms", host, port, timeout_ms);
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }

        // Check if connection was successful
        int error = 0;
        socklen_t error_len = sizeof(error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &error_len) != 0 || error != 0) {
            mcp_log_error("Connection to %s:%d failed: %d", host, port, error);
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }

        // Set socket back to blocking mode
#ifdef _WIN32
        u_long mode = 0;
        if (ioctlsocket(sock, FIONBIO, &mode) != 0) {
            mcp_log_error("Failed to set socket back to blocking mode: %d", mcp_socket_get_lasterror());
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }
#else
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0 || fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            mcp_log_error("Failed to set socket back to blocking mode: %d", mcp_socket_get_lasterror());
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }
#endif
    }
    else if (connect_result == MCP_SOCKET_ERROR) {
        // Connection failed immediately
        mcp_log_error("Failed to connect to server: %s:%d (error: %d)",
                     host, port, mcp_socket_get_lasterror());
        mcp_socket_close(sock);
        return MCP_INVALID_SOCKET;
    }

    mcp_log_debug("Successfully connected to %s:%d", host, port);
    return sock;
}

/**
 * @brief Sets up SSL for the connection if SSL is enabled.
 *
 * @param data Transport data containing connection information
 * @param sock Socket to use for SSL connection
 * @return int 0 on success, -1 on failure
 */
static int setup_ssl_connection(http_client_transport_data_t* data, socket_t sock) {
    if (!data->use_ssl) {
        return 0;
    }

    mcp_log_info("Initializing SSL for SSE connection");

    // Initialize SSL context
    http_client_ssl_ctx_t* ssl_ctx = http_client_ssl_init();
    if (ssl_ctx == NULL) {
        mcp_log_error("Failed to initialize SSL context for SSE connection");
        return -1;
    }

    // Establish SSL connection
    if (http_client_ssl_connect(ssl_ctx, sock, data->host) != 0) {
        mcp_log_error("Failed to establish SSL connection for SSE");
        http_client_ssl_cleanup(ssl_ctx);
        return -1;
    }

    // Store SSL context in transport data
    mcp_mutex_lock(data->mutex);
    data->ssl_ctx = ssl_ctx;
    mcp_mutex_unlock(data->mutex);

    mcp_log_info("SSL connection established for SSE");
    return 0;
}

/**
 * @brief Builds and sends the HTTP request for SSE connection.
 *
 * @param data Transport data containing connection information
 * @param sock Socket to send the request on
 * @return int 0 on success, -1 on failure
 */
static int build_and_send_sse_request(http_client_transport_data_t* data, socket_t sock) {
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
        return -1;
    }

    // Add Last-Event-ID header if available (for reconnection)
    if (data->last_event_id != NULL) {
        int id_len = snprintf(request + request_len, sizeof(request) - request_len,
                             "Last-Event-ID: %s\r\n", data->last_event_id);

        if (id_len < 0 || (request_len + id_len) >= (int)sizeof(request)) {
            mcp_log_error("HTTP request buffer overflow when adding Last-Event-ID");
            return -1;
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
            return -1;
        }

        request_len += auth_len;
        mcp_log_debug("Added Authorization header");
    }

    // End headers with an empty line
    int end_len = snprintf(request + request_len, sizeof(request) - request_len, "\r\n");
    if (end_len < 0 || (request_len + end_len) >= (int)sizeof(request)) {
        mcp_log_error("HTTP request buffer overflow when adding end of headers");
        return -1;
    }

    request_len += end_len;

    // Send HTTP request
    int sent_len;
    if (data->use_ssl) {
        sent_len = http_client_ssl_write(data->ssl_ctx, request, request_len);
    } else {
        sent_len = send(sock, request, request_len, 0);
    }

    if (sent_len != request_len) {
        mcp_log_error("Failed to send HTTP request for SSE connection: sent %d of %d bytes (error: %d)",
                     sent_len, request_len, mcp_socket_get_lasterror());
        return -1;
    }

    return 0;
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

    // Create socket and connect to server with timeout
    socket_t sock = create_and_connect_socket(data->host, data->port, data->timeout_ms);
    if (sock == MCP_INVALID_SOCKET) {
        return MCP_INVALID_SOCKET;
    }

    // Setup SSL if needed
    if (data->use_ssl) {
        if (setup_ssl_connection(data, sock) != 0) {
            mcp_socket_close(sock);
            return MCP_INVALID_SOCKET;
        }
    }

    // Build and send HTTP request for SSE
    if (build_and_send_sse_request(data, sock) != 0) {
        if (data->use_ssl && data->ssl_ctx != NULL) {
            http_client_ssl_cleanup(data->ssl_ctx);
            data->ssl_ctx = NULL;
        }
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
        char* new_event_id = mcp_strdup(event->id);
        if (new_event_id == NULL && event->id[0] != '\0') {
            mcp_log_error("Failed to allocate memory for last event ID");
            return;
        }

        mcp_mutex_lock(data->mutex);

        // Free previous event ID if exists
        safe_free_string(&data->last_event_id);

        // Store new event ID
        data->last_event_id = new_event_id;
        mcp_mutex_unlock(data->mutex);

        mcp_log_debug("Updated last event ID: %s", data->last_event_id);
    }

    // Process event data if provided
    if (event->data != NULL) {
        size_t data_length = strlen(event->data);
        // Check if data is too large (arbitrary limit to prevent processing extremely large events)
        if (data_length > 1024 * 1024) { // 1MB limit
            mcp_log_warn("SSE event data too large (%zu bytes), truncating to 1MB", data_length);
            data_length = 1024 * 1024; // Limit to 1MB
        }

        // Log detailed event information
        mcp_log_debug("Received SSE event data: type=%s, id=%s, data_length=%zu",
                     event_type, event_id, data_length);

        // Call message callback if registered
        if (data->message_callback != NULL) {
            int error_code = 0;

            // Call the callback with the event data
            char *result = data->message_callback(
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
        mcp_log_warn("Received SSE event with no data payload (type=%s, id=%s)",
                    event_type, event_id);
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
        return *event_data != NULL;
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
    if (intervals <= 0)
        intervals = 1;

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

        // Reset reconnect counter on successful connection
        static int successful_connections = 0;
        successful_connections++;

        // Log with connection count for better diagnostics
        mcp_log_info("Connected to SSE endpoint (connection #%d), waiting for events",
                   successful_connections);

        // Initialize buffer and event parsing state
        char buffer[SSE_BUFFER_SIZE];
        char* event_type = NULL;
        char* event_id = NULL;
        char* event_data = NULL;
        int bytes_read = 0;

        // Initialize heartbeat tracking
        time_t last_activity_time = time(NULL);
        int timeout_counter = 0;  // Counter for timeout events to reduce logging

        // Event reading loop - continue while running flag is set and connection is active
        while (data->running) {
            // Set up select for timeout on receive
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);

            // Set timeout for select
            struct timeval tv;
            tv.tv_sec = SSE_HEARTBEAT_INTERVAL_MS / 1000;
            tv.tv_usec = (SSE_HEARTBEAT_INTERVAL_MS % 1000) * 1000;

            // Wait for data or timeout
            int select_result = select((int)sock + 1, &read_fds, NULL, NULL, &tv);

            // Check if we need to reconnect due to inactivity
            time_t current_time = time(NULL);
            double idle_time_ms = difftime(current_time, last_activity_time) * 1000;
            if (idle_time_ms > SSE_MAX_IDLE_TIME_MS) {
                mcp_log_warn("SSE connection idle for %d seconds (threshold: %d seconds), reconnecting",
                           (int)(idle_time_ms / 1000), SSE_MAX_IDLE_TIME_MS / 1000);
                break;
            }

            // If select timed out, continue to next iteration (will check idle time again)
            if (select_result == 0) {
                // Increment timeout counter
                timeout_counter++;

                // Only log at most once per hour to reduce noise
                // Log at 1 hour mark (120 timeouts with 30 second interval)
                if (timeout_counter == 120) {
                    mcp_log_info("SSE connection healthy but idle for 1 hour");
                    timeout_counter = 1;  // Reset counter but not to 0 to maintain sequence
                }
                continue;
            }

            // If select failed, break the loop
            if (select_result < 0) {
                mcp_log_error("Select failed while waiting for SSE data: %d", mcp_socket_get_lasterror());
                break;
            }

            // Receive data from the socket
            if (data->use_ssl) {
                bytes_read = http_client_ssl_read(data->ssl_ctx, buffer, sizeof(buffer) - 1);
            } else {
                bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
            }

            // Check for connection errors or closure
            if (bytes_read <= 0) {
                break; // Exit the reading loop
            }

            // Update last activity time and reset timeout counter
            last_activity_time = time(NULL);
            timeout_counter = 0;  // Reset counter when data is received

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
            mcp_log_error("Error reading from SSE endpoint: %d", mcp_socket_get_lasterror());
        } else if (bytes_read == 0 && data->running) {
            mcp_log_info("SSE connection closed by server");
        }

        // Clean up socket and SSL (thread-safe)
        mcp_mutex_lock(data->mutex);

        // Clean up SSL if used
        if (data->use_ssl && data->ssl_ctx != NULL) {
            http_client_ssl_cleanup(data->ssl_ctx);
            data->ssl_ctx = NULL;
        }

        // Close socket
        mcp_socket_close(sock);
        data->sse_socket = MCP_INVALID_SOCKET;

        mcp_mutex_unlock(data->mutex);

        // Clean up event parsing state
        cleanup_event_state(&event_type, &event_id, &event_data);

        // If we're still running, retry connection after delay
        if (data->running) {
            static int reconnect_attempts = 0;
            reconnect_attempts++;

            // Log with reconnect attempt count for better diagnostics
            mcp_log_info("SSE connection closed (attempt #%d), retrying in %d ms",
                       reconnect_attempts, SSE_RECONNECT_DELAY_MS);

            wait_with_running_check(data, SSE_RECONNECT_DELAY_MS);
        }
    }

    mcp_log_info("HTTP client event thread stopped");
    return NULL;
}
