#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/mcp_http_client_sse.h"
#include "internal/mcp_http_client_internal.h"
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

/**
 * @brief Creates an SSE event structure.
 */
sse_event_t* sse_event_create(const char* id, const char* event, const char* data) {
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
void sse_event_free(sse_event_t* event) {
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
 * @brief Connects to the SSE endpoint.
 */
socket_t connect_to_sse_endpoint(http_client_transport_data_t* data) {
    if (data == NULL) {
        return MCP_INVALID_SOCKET;
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
        return MCP_INVALID_SOCKET;
    }

    // Resolve host
    struct hostent* server = gethostbyname(data->host);
    if (server == NULL) {
        mcp_log_error("Failed to resolve host: %s", data->host);
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
        mcp_log_error("Failed to connect to server: %s:%d", data->host, data->port);
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
        return MCP_INVALID_SOCKET;
    }

    // Return the socket
    return sock;
}

/**
 * @brief Processes an SSE event.
 */
void process_sse_event(http_client_transport_data_t* data, const sse_event_t* event) {
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
void* http_client_event_thread_func(void* arg) {
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
        socket_t sock = connect_to_sse_endpoint(data);
        if (sock == MCP_INVALID_SOCKET) {
            mcp_log_error("Failed to connect to SSE endpoint, retrying in 5 seconds");
            // Sleep and retry
            for (int i = 0; i < 50 && data->running; i++) {
                mcp_sleep_ms(100);
            }
            continue;
        }

        // Store the socket for later use (e.g., to close it in stop function)
        // Use mutex to protect access to sse_socket
        mcp_mutex_lock(data->mutex);
        data->sse_socket = sock;
        mcp_mutex_unlock(data->mutex);

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
        // Use mutex to protect access to sse_socket
        mcp_mutex_lock(data->mutex);
        mcp_socket_close(sock);
        data->sse_socket = MCP_INVALID_SOCKET;
        mcp_mutex_unlock(data->mutex);

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
