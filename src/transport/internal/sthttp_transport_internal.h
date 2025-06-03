/**
 * @file sthttp_transport_internal.h
 * @brief Internal definitions for Streamable HTTP Transport
 *
 * This header contains internal structures and functions for the
 * Streamable HTTP transport implementation.
 */
#ifndef STHTTP_TRANSPORT_INTERNAL_H
#define STHTTP_TRANSPORT_INTERNAL_H

#include "mcp_sthttp_transport.h"
#include "mcp_http_session_manager.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include "mcp_http_sse_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <libwebsockets.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of SSE clients per session
#define MAX_SSE_CLIENTS_PER_SESSION 10

// Maximum number of stored SSE events for resumability
#define MAX_SSE_STORED_EVENTS_DEFAULT 1000

// Default MCP endpoint path
#define MCP_ENDPOINT_DEFAULT "/mcp"

// LWS service timeout in milliseconds
#define STHTTP_LWS_SERVICE_TIMEOUT_MS 100

// Cleanup thread interval in seconds
#define STHTTP_CLEANUP_INTERVAL_SECONDS 60

// HTTP status codes
#define HTTP_STATUS_OK 200
#define HTTP_STATUS_ACCEPTED 202
#define HTTP_STATUS_BAD_REQUEST 400
#define HTTP_STATUS_NOT_FOUND 404
#define HTTP_STATUS_METHOD_NOT_ALLOWED 405
#define HTTP_STATUS_INTERNAL_SERVER_ERROR 500

// Buffer sizes
#define HTTP_HEADER_BUFFER_SIZE 512
#define HTTP_ORIGIN_BUFFER_SIZE 256
#define HTTP_SESSION_ID_BUFFER_SIZE 128
#define HTTP_LAST_EVENT_ID_BUFFER_SIZE 64

/**
 * @brief SSE stream context for resumability
 */
typedef struct {
    char* stream_id;                    /**< Unique stream identifier */
    char* last_event_id;                /**< Last event ID sent on this stream */
    sse_event_t* stored_events;         /**< Circular buffer of stored events */
    size_t event_head;                  /**< Head index in circular buffer */
    size_t event_tail;                  /**< Tail index in circular buffer */
    size_t stored_event_count;          /**< Number of stored events */
    size_t max_stored_events;           /**< Maximum events to store */
    uint64_t next_event_id;             /**< Next event ID to assign */
    mcp_mutex_t* mutex;                 /**< Mutex for thread safety */
} sse_stream_context_t;

/**
 * @brief Session data for HTTP connections
 */
typedef struct {
    char session_id[MCP_SESSION_ID_MAX_LENGTH];  /**< Session ID if using sessions */
    bool has_session;                            /**< Whether this connection has a session */
    mcp_http_session_t* session;                 /**< Session handle */
    
    // SSE stream data
    bool is_sse_stream;                          /**< Whether this is an SSE stream */
    sse_stream_context_t* sse_context;           /**< SSE stream context for resumability */
    
    // Request data
    char* request_body;                          /**< Accumulated request body */
    size_t request_body_size;                    /**< Size of accumulated request body */
    size_t request_body_capacity;                /**< Capacity of request body buffer */
    char request_uri[256];                       /**< Request URI saved for POST body completion */
    
    // Origin validation
    char origin[HTTP_ORIGIN_BUFFER_SIZE];        /**< Origin header value */
    bool origin_validated;                       /**< Whether origin has been validated */
} sthttp_session_data_t;

/**
 * @brief Streamable HTTP transport data structure
 */
typedef struct {
    mcp_sthttp_config_t config;
    struct lws_context* context;
    volatile bool running;
    mcp_thread_t event_thread;
    mcp_thread_t cleanup_thread;

    // MCP endpoint configuration
    char* mcp_endpoint;

    // Session management
    mcp_http_session_manager_t* session_manager;

    // Static file serving
    struct lws_http_mount* mount;

    // SSE clients tracking
    struct lws** sse_clients;
    size_t sse_client_count;
    size_t max_sse_clients;
    mcp_mutex_t* sse_mutex;

    // Global SSE event storage for non-session streams
    sse_stream_context_t* global_sse_context;

    // CORS settings
    bool enable_cors;
    char* cors_allow_origin;
    char* cors_allow_methods;
    char* cors_allow_headers;
    int cors_max_age;

    // Security settings
    bool validate_origin;
    char** allowed_origins;
    size_t allowed_origins_count;

    // Heartbeat settings
    bool send_heartbeats;
    uint32_t heartbeat_interval_ms;
    time_t last_heartbeat_time;
    uint64_t heartbeat_counter;

    // Message callback
    mcp_transport_message_callback_t message_callback;
    void* callback_user_data;
    mcp_transport_error_callback_t error_callback;
} sthttp_transport_data_t;

// Function declarations

/**
 * @brief Thread function for HTTP event processing
 */
void* sthttp_event_thread_func(void* arg);

/**
 * @brief Thread function for periodic cleanup
 */
void* sthttp_cleanup_thread_func(void* arg);

/**
 * @brief Create SSE stream context
 */
sse_stream_context_t* sse_stream_context_create(size_t max_stored_events);

/**
 * @brief Destroy SSE stream context
 */
void sse_stream_context_destroy(sse_stream_context_t* context);

/**
 * @brief Store an event in SSE stream context
 */
void sse_stream_context_store_event(sse_stream_context_t* context, const char* event_id, const char* event_type, const char* data);

/**
 * @brief Replay events from a given last event ID
 */
int sse_stream_context_replay_events(sse_stream_context_t* context, struct lws* wsi, const char* last_event_id);

/**
 * @brief Validate origin against allowed origins list
 */
bool validate_origin(sthttp_transport_data_t* data, const char* origin);

/**
 * @brief Parse allowed origins string into array
 */
bool parse_allowed_origins(const char* origins_str, char*** origins_out, size_t* count_out);

/**
 * @brief Free allowed origins array
 */
void free_allowed_origins(char** origins, size_t count);

/**
 * @brief Send HTTP error response
 */
int send_http_error_response(struct lws* wsi, int status_code, const char* message);

/**
 * @brief Send HTTP JSON response
 */
int send_http_json_response(struct lws* wsi, const char* json_data, const char* session_id);

/**
 * @brief Send SSE event
 */
int send_sse_event(struct lws* wsi, const char* event_id, const char* event_type, const char* data);

/**
 * @brief Send SSE heartbeat to a specific WebSocket instance
 */
int send_sse_heartbeat_to_wsi(struct lws* wsi);

/**
 * @brief Handle MCP endpoint request
 */
int handle_mcp_endpoint_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data);

/**
 * @brief Handle MCP endpoint POST request
 */
int handle_mcp_post_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data);

/**
 * @brief Handle MCP endpoint GET request (SSE stream)
 */
int handle_mcp_get_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data);

/**
 * @brief Handle MCP endpoint DELETE request (session termination)
 */
int handle_mcp_delete_request(struct lws* wsi, sthttp_transport_data_t* data, sthttp_session_data_t* session_data);

/**
 * @brief Handle OPTIONS request (CORS preflight)
 */
int handle_options_request(struct lws* wsi, sthttp_transport_data_t* data);

/**
 * @brief Process JSON-RPC request and generate response
 */
char* process_jsonrpc_request(sthttp_transport_data_t* data, const char* request_json, const char* session_id);

/**
 * @brief Extract session ID from headers
 */
bool extract_session_id(struct lws* wsi, char* session_id_out);

/**
 * @brief Extract last event ID from headers
 */
bool extract_last_event_id(struct lws* wsi, char* last_event_id_out);

/**
 * @brief Add CORS headers to response for streamable transport
 */
void add_streamable_cors_headers(struct lws* wsi, sthttp_transport_data_t* data,
                                unsigned char** p, unsigned char* end);

/**
 * @brief Validate SSE text input for control characters
 */
bool validate_sse_text_input(const char* text);

// LWS protocols for streamable HTTP transport
extern struct lws_protocols sthttp_protocols[];

#ifdef __cplusplus
}
#endif

#endif // STHTTP_TRANSPORT_INTERNAL_H
