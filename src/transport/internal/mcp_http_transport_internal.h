#ifndef MCP_HTTP_TRANSPORT_INTERNAL_H
#define MCP_HTTP_TRANSPORT_INTERNAL_H

#include "mcp_http_transport.h"
#include "internal/transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Include libwebsockets for HTTP server implementation
#include <libwebsockets.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of SSE clients
#define MAX_SSE_CLIENTS 50000

// Maximum number of stored SSE events for replay
#define MAX_SSE_STORED_EVENTS 5000

// SSE event structure for storing events for replay
typedef struct {
    char* id;           // Event ID
    char* event_type;   // Event type
    char* data;         // Event data
    time_t timestamp;   // Event timestamp
} sse_event_t;

// HTTP transport data structure
typedef struct {
    mcp_http_config_t config;
    struct lws_context* context;
    volatile bool running;
    mcp_thread_t event_thread;

    // Static file serving
    struct lws_http_mount* mount;

    // SSE clients
    struct lws* sse_clients[MAX_SSE_CLIENTS];
    int sse_client_count;
    mcp_mutex_t* sse_mutex;

    // SSE event storage for reconnection (circular buffer)
    sse_event_t stored_events[MAX_SSE_STORED_EVENTS];
    int event_head;          // Index of the oldest event
    int event_tail;          // Index where the next event will be stored
    int stored_event_count;  // Current number of stored events
    int next_event_id;       // Next event ID to assign
    mcp_mutex_t* event_mutex;

    // SSE heartbeat
    bool send_heartbeats;
    int heartbeat_interval_ms;
    time_t last_heartbeat;

    // CORS settings
    bool enable_cors;
    char* cors_allow_origin;
    char* cors_allow_methods;
    char* cors_allow_headers;
    int cors_max_age;

    // Message callback
    mcp_transport_message_callback_t message_callback;
    void* callback_user_data;
    mcp_transport_error_callback_t error_callback;
} http_transport_data_t;

// Per-session data
typedef struct {
    char* request_buffer;
    size_t request_len;
    bool is_sse_client;
    int last_event_id;     // Last event ID received by this client
    char* event_filter;    // Event type filter (NULL = all events)
    char* session_id;      // Session ID for targeting specific clients (NULL = no session)
} http_session_data_t;

// Forward declarations for functions in mcp_http_transport.c
int http_transport_start(mcp_transport_t* transport,
                        mcp_transport_message_callback_t message_callback,
                        void* user_data,
                        mcp_transport_error_callback_t error_callback);
int http_transport_stop(mcp_transport_t* transport);
int http_transport_destroy(mcp_transport_t* transport);
void* http_event_thread_func(void* arg);

// Forward declarations for functions in mcp_http_server_handlers.c
void process_http_request(struct lws* wsi, http_transport_data_t* data,
                         const char* request, size_t len);
void handle_sse_request(struct lws* wsi, http_transport_data_t* data);
int add_cors_headers(struct lws* wsi, http_transport_data_t* data,
                    unsigned char** p, unsigned char* end);
int root_handler(struct lws* wsi, enum lws_callback_reasons reason,
                void* user, void* in, size_t len);

// Forward declarations for functions in mcp_http_server_callbacks.c
int lws_callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                     void* user, void* in, size_t len);

// Forward declarations for functions in mcp_http_server_sse.c
void store_sse_event(http_transport_data_t* data, const char* event, const char* event_data);
void send_sse_heartbeat(http_transport_data_t* data);

// LWS protocols
extern struct lws_protocols http_protocols[];

#ifdef __cplusplus
}
#endif

#endif // MCP_HTTP_TRANSPORT_INTERNAL_H
