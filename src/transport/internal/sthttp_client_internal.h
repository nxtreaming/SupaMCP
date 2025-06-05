/**
 * @file sthttp_client_internal.h
 * @brief Internal definitions for HTTP Streamable Client Transport
 *
 * This header contains internal structures and functions for the
 * HTTP Streamable client transport implementation.
 */
#ifndef STHTTP_CLIENT_INTERNAL_H
#define STHTTP_CLIENT_INTERNAL_H

#include "mcp_sthttp_client_transport.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "mcp_socket_utils.h"

// Define ssize_t for Windows
#ifdef _WIN32
#ifndef ssize_t
typedef intptr_t ssize_t;
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Buffer sizes
#define HTTP_CLIENT_BUFFER_SIZE 8192
#define HTTP_CLIENT_HEADER_BUFFER_SIZE 2048
#define HTTP_CLIENT_URL_BUFFER_SIZE 512
#define HTTP_CLIENT_SESSION_ID_BUFFER_SIZE 128
#define HTTP_CLIENT_EVENT_ID_BUFFER_SIZE 64

// HTTP response parsing states
typedef enum {
    HTTP_PARSE_STATE_STATUS_LINE,
    HTTP_PARSE_STATE_HEADERS,
    HTTP_PARSE_STATE_BODY,
    HTTP_PARSE_STATE_COMPLETE,
    HTTP_PARSE_STATE_ERROR
} http_parse_state_t;

/**
 * @brief HTTP response structure
 */
typedef struct {
    int status_code;                     /**< HTTP status code */
    char* headers;                       /**< Raw headers string */
    char* body;                          /**< Response body */
    size_t body_length;                  /**< Length of response body */
    char* content_type;                  /**< Content-Type header value */
    char* session_id;                    /**< Session ID from response headers */
} http_response_t;

/**
 * @brief SSE event structure
 */
typedef struct {
    char* id;                            /**< Event ID */
    char* event;                         /**< Event type */
    char* data;                          /**< Event data */
} sse_event_t;

/**
 * @brief SSE connection context
 */
typedef struct {
    socket_t socket_fd;                  /**< Socket file descriptor */
    bool connected;                      /**< Whether SSE stream is connected */
    char* last_event_id;                 /**< Last received event ID */
    char* buffer;                        /**< Receive buffer */
    size_t buffer_size;                  /**< Size of receive buffer */
    size_t buffer_used;                  /**< Used bytes in buffer */
    http_parse_state_t parse_state;      /**< Current parsing state */
    mcp_thread_t sse_thread;             /**< SSE receive thread */
    volatile bool sse_thread_running;    /**< Whether SSE thread is running */
} sse_connection_t;

/**
 * @brief HTTP Streamable client transport data
 */
typedef struct {
    mcp_sthttp_client_config_t config;

    // Connection state
    mcp_client_connection_state_t state;
    mcp_mutex_t* state_mutex;

    // Session management
    char* session_id;
    bool has_session;

    // SSE connection
    sse_connection_t* sse_conn;
    mcp_mutex_t* sse_mutex;

    // Statistics
    mcp_client_connection_stats_t stats;
    mcp_mutex_t* stats_mutex;

    // Callbacks
    mcp_client_state_callback_t state_callback;
    void* state_callback_user_data;
    mcp_client_sse_event_callback_t sse_callback;
    void* sse_callback_user_data;

    // Transport callbacks
    mcp_transport_message_callback_t message_callback;
    void* message_callback_user_data;
    mcp_transport_error_callback_t error_callback;

    // Reconnection
    bool auto_reconnect;
    uint32_t reconnect_attempts;
    time_t last_reconnect_time;

    // Threading
    mcp_thread_t reconnect_thread;
    volatile bool reconnect_thread_running;
    volatile bool shutdown_requested;
} sthttp_client_data_t;

// Function declarations

/**
 * @brief Initialize HTTP client transport data
 */
int sthttp_client_init_data(sthttp_client_data_t* data, const mcp_sthttp_client_config_t* config);

/**
 * @brief Cleanup HTTP client transport data
 */
void sthttp_client_cleanup(sthttp_client_data_t* data);

/**
 * @brief Send HTTP POST request
 */
int http_client_send_request(sthttp_client_data_t* data, const char* json_data, http_response_t* response);

/**
 * @brief Parse HTTP response
 */
int http_client_parse_response(const char* raw_response, size_t response_length, http_response_t* response);

/**
 * @brief Free HTTP response
 */
void http_client_free_response(http_response_t* response);

/**
 * @brief Connect SSE stream
 */
int sse_client_connect(sthttp_client_data_t* data);

/**
 * @brief Disconnect SSE stream
 */
void sse_client_disconnect(sthttp_client_data_t* data);

/**
 * @brief SSE receive thread function
 */
void* sse_client_thread_func(void* arg);

/**
 * @brief Parse SSE event from buffer
 */
int sse_parse_event(const char* buffer, size_t length, sse_event_t* event);

/**
 * @brief Free SSE event
 */
void sse_free_event(sse_event_t* event);

/**
 * @brief Change connection state
 */
void http_client_set_state(sthttp_client_data_t* data, mcp_client_connection_state_t new_state);

/**
 * @brief Update connection statistics
 */
void http_client_update_stats(sthttp_client_data_t* data, const char* stat_type);

/**
 * @brief Reconnection thread function
 */
void* http_client_reconnect_thread_func(void* arg);

/**
 * @brief Create socket connection
 */
socket_t http_client_create_socket(const char* host, uint16_t port, uint32_t timeout_ms);

/**
 * @brief Send HTTP request over socket
 */
int http_client_send_raw_request(socket_t socket_fd, const char* request, uint32_t timeout_ms);

/**
 * @brief Receive HTTP response over socket
 */
int http_client_receive_response(socket_t socket_fd, char* buffer, size_t buffer_size, uint32_t timeout_ms);

/**
 * @brief Build HTTP request string
 */
char* http_client_build_request(sthttp_client_data_t* data, const char* method, const char* json_data);

/**
 * @brief Extract session ID from response headers
 */
char* http_client_extract_session_id(const char* headers);

/**
 * @brief Validate SSL certificate (if SSL is enabled)
 */
int http_client_validate_ssl_cert(const char* host, const char* cert_path);

#ifdef __cplusplus
}
#endif

#endif // STHTTP_CLIENT_INTERNAL_H
