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
#include "sthttp_client_internal.h"
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

// Dynamic SSE client array settings
#define STHTTP_INITIAL_SSE_CLIENTS 64      // Start with 64 clients
#define STHTTP_SSE_GROWTH_FACTOR 2         // Double when full

// Event ID hash map settings
#define STHTTP_EVENT_HASH_INITIAL_SIZE 256 // Initial hash map size

/**
 * @brief Hash map entry for event ID to position mapping
 */
typedef struct event_hash_entry {
    char* event_id;                     /**< Event ID string */
    size_t position;                    /**< Position in circular buffer */
    struct event_hash_entry* next;      /**< Next entry for collision handling */
} event_hash_entry_t;

/**
 * @brief Hash map for fast event ID lookup
 */
typedef struct {
    event_hash_entry_t** buckets;       /**< Hash buckets */
    size_t bucket_count;                /**< Number of buckets */
    size_t entry_count;                 /**< Number of entries */
    mcp_mutex_t* mutex;                 /**< Mutex for thread safety */
} event_hash_map_t;

/**
 * @brief Dynamic SSE client array
 */
typedef struct {
    struct lws** clients;               /**< Array of client pointers */
    size_t count;                       /**< Current number of clients */
    size_t capacity;                    /**< Current array capacity */
    mcp_mutex_t* mutex;                 /**< Mutex for thread safety */
} dynamic_sse_clients_t;

// SSE parser states (separate from HTTP parser states)
typedef enum {
    SSE_PARSE_STATE_FIELD_NAME,
    SSE_PARSE_STATE_FIELD_VALUE,
    SSE_PARSE_STATE_EVENT_COMPLETE,
    SSE_PARSE_STATE_ERROR
} sse_parse_state_t;

/**
 * @brief HTTP parser context (uses http_parse_state_t from sthttp_client_internal.h)
 */
typedef struct http_parser_context {
    http_parse_state_t state;
    int status_code;
    size_t content_length;
    bool has_content_length;
    bool is_chunked;
    bool connection_close;

    // Header parsing
    char* current_header_name;
    char* current_header_value;
    size_t header_name_len;
    size_t header_value_len;

    // Buffer management
    char* line_buffer;
    size_t line_buffer_size;
    size_t line_buffer_used;

    // Body tracking
    size_t body_bytes_received;
    size_t chunk_size;
    bool in_chunk_data;
} http_parser_context_t;

/**
 * @brief SSE parser context
 */
typedef struct sse_parser_context {
    sse_parse_state_t state;

    // Current event being parsed
    char* event_id;
    char* event_type;
    char* event_data;

    // Field parsing
    char* current_field_name;
    char* current_field_value;
    size_t field_name_capacity;
    size_t field_value_capacity;
    size_t field_name_length;
    size_t field_value_length;

    // Line parsing
    char* line_buffer;
    size_t line_buffer_capacity;
    size_t line_buffer_length;

    // Data accumulation for multi-line data fields
    char* data_accumulator;
    size_t data_accumulator_capacity;
    size_t data_accumulator_length;

    // Parser state
    bool in_field_value;
    bool field_value_started;
} sse_parser_context_t;

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
    event_hash_map_t* event_hash;       /**< Hash map for fast event lookup */
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

    // SSE clients tracking (dynamic array)
    dynamic_sse_clients_t* sse_clients;

    // Cleanup thread synchronization
    mcp_cond_t* cleanup_condition;
    mcp_mutex_t* cleanup_mutex;
    volatile bool cleanup_shutdown;

    // Optimization settings
    bool use_optimized_parsers;         /**< Whether to use optimized HTTP/SSE parsers */

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

/**
 * @brief Create dynamic SSE clients array
 */
dynamic_sse_clients_t* dynamic_sse_clients_create(size_t initial_capacity);

/**
 * @brief Destroy dynamic SSE clients array
 */
void dynamic_sse_clients_destroy(dynamic_sse_clients_t* clients);

/**
 * @brief Add client to dynamic array
 */
int dynamic_sse_clients_add(dynamic_sse_clients_t* clients, struct lws* wsi);

/**
 * @brief Remove client from dynamic array
 */
int dynamic_sse_clients_remove(dynamic_sse_clients_t* clients, struct lws* wsi);

/**
 * @brief Get client count
 */
size_t dynamic_sse_clients_count(dynamic_sse_clients_t* clients);

/**
 * @brief Cleanup disconnected clients
 */
size_t dynamic_sse_clients_cleanup(dynamic_sse_clients_t* clients);

/**
 * @brief Send message to all connected clients
 */
int dynamic_sse_clients_broadcast(dynamic_sse_clients_t* clients,
                                 const char* event_id, const char* event_type, const char* data);

/**
 * @brief Send heartbeat to all connected clients
 */
int dynamic_sse_clients_broadcast_heartbeat(dynamic_sse_clients_t* clients);

/**
 * @brief Create event hash map
 */
event_hash_map_t* event_hash_map_create(size_t initial_size);

/**
 * @brief Destroy event hash map
 */
void event_hash_map_destroy(event_hash_map_t* map);

/**
 * @brief Add event to hash map
 */
int event_hash_map_put(event_hash_map_t* map, const char* event_id, size_t position);

/**
 * @brief Find event position in hash map
 */
int event_hash_map_get(event_hash_map_t* map, const char* event_id, size_t* position);

/**
 * @brief Remove event from hash map
 */
int event_hash_map_remove(event_hash_map_t* map, const char* event_id);

/**
 * @brief Create HTTP parser context
 */
http_parser_context_t* http_parser_create(void);

/**
 * @brief Destroy HTTP parser context
 */
void http_parser_destroy(http_parser_context_t* parser);

/**
 * @brief Reset HTTP parser for new request
 */
void http_parser_reset(http_parser_context_t* parser);

/**
 * @brief Process HTTP data chunk
 */
int http_parser_process(http_parser_context_t* parser, const char* data, size_t length,
                       http_response_t* response);

/**
 * @brief Check if HTTP parsing is complete
 */
bool http_parser_is_complete(const http_parser_context_t* parser);

/**
 * @brief Check if HTTP parser has error
 */
bool http_parser_has_error(const http_parser_context_t* parser);

/**
 * @brief Create SSE parser context
 */
sse_parser_context_t* sse_parser_create(void);

/**
 * @brief Destroy SSE parser context
 */
void sse_parser_destroy(sse_parser_context_t* parser);

/**
 * @brief Reset SSE parser for new event
 */
void sse_parser_reset(sse_parser_context_t* parser);

/**
 * @brief Process SSE data chunk incrementally
 */
int sse_parser_process(sse_parser_context_t* parser, const char* data, size_t length,
                      sse_event_t* event_out);

/**
 * @brief Check if SSE event parsing is complete
 */
bool sse_parser_is_complete(const sse_parser_context_t* parser);

/**
 * @brief Check if SSE parser has error
 */
bool sse_parser_has_error(const sse_parser_context_t* parser);

/**
 * @brief Initialize CORS header cache
 */
int cors_header_cache_init(void);

/**
 * @brief Cleanup CORS header cache
 */
void cors_header_cache_cleanup(void);

/**
 * @brief Add optimized CORS headers to response
 */
int add_optimized_cors_headers(struct lws* wsi, const sthttp_transport_data_t* data,
                              unsigned char** p, unsigned char* end);

// LWS protocols for streamable HTTP transport
extern struct lws_protocols sthttp_protocols[];

#ifdef __cplusplus
}
#endif

#endif // STHTTP_TRANSPORT_INTERNAL_H
