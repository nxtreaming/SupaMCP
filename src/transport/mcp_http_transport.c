#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

// Include our Windows socket compatibility header first
#include <win_socket_compat.h>

// On Windows, strcasecmp is _stricmp
#define strcasecmp _stricmp
#endif

#include "mcp_http_transport.h"
#include "internal/transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Include libwebsockets for HTTP server implementation
#include <libwebsockets.h>

// Maximum number of SSE clients
#define MAX_SSE_CLIENTS 10000

// Maximum number of stored SSE events for replay
#define MAX_SSE_STORED_EVENTS 1000

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

    // SSE event storage for reconnection
    sse_event_t stored_events[MAX_SSE_STORED_EVENTS];
    int stored_event_count;
    int next_event_id;
    mcp_mutex_t* event_mutex;

    // SSE heartbeat
    bool send_heartbeats;
    int heartbeat_interval_ms;
    time_t last_heartbeat;

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
} http_session_data_t;

// Forward declarations
static int http_transport_start(mcp_transport_t* transport,
                               mcp_transport_message_callback_t message_callback,
                               void* user_data,
                               mcp_transport_error_callback_t error_callback);
static int http_transport_stop(mcp_transport_t* transport);
static int http_transport_destroy(mcp_transport_t* transport);
static void* http_event_thread_func(void* arg);

// Forward declaration for libwebsockets function
int lws_http_transaction_completed(struct lws *wsi);

// LWS callback function
static int lws_callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                            void* user, void* in, size_t len);

// Root path handler
static int root_handler(struct lws* wsi, enum lws_callback_reasons reason,
                       void* user, void* in, size_t len) {
    (void)user; // Unused parameter
    (void)len;  // Unused parameter
    mcp_log_debug("Root handler: reason=%d", reason);

    // Handle protocol initialization
    if (reason == LWS_CALLBACK_PROTOCOL_INIT) {
        mcp_log_info("Root handler: Protocol init");
        return 0; // Return 0 to indicate success
    }

    // Handle HTTP requests
    if (reason == LWS_CALLBACK_HTTP) {
        char* uri = (char*)in;
        mcp_log_info("Root handler: HTTP request: %s", uri);

        // Only handle the root path ("/")
        if (strcmp(uri, "/") != 0) {
            mcp_log_info("Root handler: Not root path, passing to next handler");
            return -1; // Return -1 to let libwebsockets try the next protocol handler
        }

        mcp_log_info("Root handler: Serving root page");

        // Prepare response headers
        unsigned char buffer[LWS_PRE + 1024];
        unsigned char* p = &buffer[LWS_PRE];
        unsigned char* end = &buffer[sizeof(buffer) - 1];

        // Add headers
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                       "text/html",
                                       LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
            return -1;
        }

        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
            return -1;
        }

        // Create a simple HTML page
        const char* html =
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head>\n"
            "    <title>MCP HTTP Server</title>\n"
            "</head>\n"
            "<body>\n"
            "    <h1>MCP HTTP Server</h1>\n"
            "    <p>This is a test page created by the MCP HTTP server.</p>\n"
            "    <h2>Available Tools:</h2>\n"
            "    <ul>\n"
            "        <li><strong>echo</strong> - Echoes back the input text</li>\n"
            "        <li><strong>reverse</strong> - Reverses the input text</li>\n"
            "    </ul>\n"
            "    <h2>Tool Call Example:</h2>\n"
            "    <pre>curl -X POST http://127.0.0.1:8180/call_tool -H \"Content-Type: application/json\" -d \"{\\\"name\\\":\\\"echo\\\",\\\"params\\\":{\\\"text\\\":\\\"Hello, MCP Server!\\\"}}\"</pre>\n"
            "</body>\n"
            "</html>\n";

        // Write response body
        lws_write(wsi, (unsigned char*)html, strlen(html), LWS_WRITE_HTTP);

        // Complete HTTP transaction
        lws_http_transaction_completed(wsi);
        return 0;
    }

    // For all other callbacks, return 0 to indicate success
    return 0;
}

// LWS protocols
static struct lws_protocols http_protocols[] = {
    {
        "http-server",
        lws_callback_http,
        sizeof(http_session_data_t),
        0,  // rx buffer size (0 = default)
    },
    {
        "http-root",
        root_handler,
        0,  // user data size
        0,  // rx buffer size (0 = default)
    },
    { NULL, NULL, 0, 0 } // terminator
};

// Create HTTP transport
mcp_transport_t* mcp_transport_http_create(const mcp_http_config_t* config) {
    if (config == NULL || config->host == NULL) {
        return NULL;
    }

    // Allocate transport
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        return NULL;
    }

    // Allocate transport data
    http_transport_data_t* data = (http_transport_data_t*)calloc(1, sizeof(http_transport_data_t));
    if (data == NULL) {
        free(transport);
        return NULL;
    }

    // Copy configuration
    data->config.host = mcp_strdup(config->host);
    data->config.port = config->port;
    data->config.use_ssl = config->use_ssl;
    data->config.timeout_ms = config->timeout_ms;

    if (config->cert_path) {
        data->config.cert_path = mcp_strdup(config->cert_path);
    }

    if (config->key_path) {
        data->config.key_path = mcp_strdup(config->key_path);
    }

    if (config->doc_root) {
        data->config.doc_root = mcp_strdup(config->doc_root);
    }

    // Initialize SSE mutex
    data->sse_mutex = mcp_mutex_create();
    if (data->sse_mutex == NULL) {
        if (data->config.host) free((void*)data->config.host);
        if (data->config.cert_path) free((void*)data->config.cert_path);
        if (data->config.key_path) free((void*)data->config.key_path);
        if (data->config.doc_root) free((void*)data->config.doc_root);
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize event mutex
    data->event_mutex = mcp_mutex_create();
    if (data->event_mutex == NULL) {
        mcp_mutex_destroy(data->sse_mutex);
        if (data->config.host) free((void*)data->config.host);
        if (data->config.cert_path) free((void*)data->config.cert_path);
        if (data->config.key_path) free((void*)data->config.key_path);
        if (data->config.doc_root) free((void*)data->config.doc_root);
        free(data);
        free(transport);
        return NULL;
    }

    // Initialize SSE event storage
    data->stored_event_count = 0;
    data->next_event_id = 1;

    // Initialize SSE heartbeat
    data->send_heartbeats = true;
    data->heartbeat_interval_ms = 30000; // 30 seconds by default
    data->last_heartbeat = time(NULL);

    // Set transport type to server
    transport->type = MCP_TRANSPORT_TYPE_SERVER;

    // Initialize server operations
    transport->server.start = http_transport_start;
    transport->server.stop = http_transport_stop;
    transport->server.destroy = http_transport_destroy;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}

// Start HTTP transport
static int http_transport_start(mcp_transport_t* transport,
                               mcp_transport_message_callback_t message_callback,
                               void* user_data,
                               mcp_transport_error_callback_t error_callback) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    // Store callback and user data
    data->message_callback = message_callback;
    data->callback_user_data = user_data;
    data->error_callback = error_callback;

    // Create libwebsockets context
    struct lws_context_creation_info info = {0};
    info.port = data->config.port;
    info.iface = data->config.host;
    info.protocols = http_protocols;
    info.user = data;

    // Set options - use minimal options to avoid conflicts
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE |
                   LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Disable built-in 404 handling
    info.error_document_404 = NULL;

    // Store mount pointer for later cleanup
    data->mount = NULL;

    // Log the configuration
    mcp_log_info("Creating HTTP server on %s:%d", data->config.host, data->config.port);

    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.ssl_cert_filepath = data->config.cert_path;
        info.ssl_private_key_filepath = data->config.key_path;
    }

    // Set mount for static files if doc_root is provided
    if (data->config.doc_root) {
        mcp_log_info("Setting up static file mount for doc_root: %s", data->config.doc_root);

        // Check if directory exists
        FILE* test_file = NULL;
        char test_path[512];
        snprintf(test_path, sizeof(test_path), "%s/index.html", data->config.doc_root);
        test_file = fopen(test_path, "r");
        if (test_file) {
            fclose(test_file);
            mcp_log_info("Test file exists: %s", test_path);
        } else {
            mcp_log_error("Test file does not exist: %s", test_path);
        }

        // Allocate and initialize mount structure
        data->mount = (struct lws_http_mount*)calloc(1, sizeof(struct lws_http_mount));
        if (data->mount) {
            data->mount->mountpoint = "/";
            data->mount->mountpoint_len = 1;
            data->mount->origin = data->config.doc_root;
            data->mount->def = "index.html";
            data->mount->origin_protocol = LWSMPRO_FILE;
            data->mount->cgienv = NULL;
            data->mount->extra_mimetypes = NULL;
            data->mount->interpret = NULL;
            data->mount->cgi_timeout = 0;
            data->mount->cache_max_age = 0;
            data->mount->auth_mask = 0;
            data->mount->cache_reusable = 0;
            data->mount->cache_revalidate = 0;
            data->mount->cache_intermediaries = 0;
            data->mount->origin_protocol = LWSMPRO_FILE;
            // mountpoint_len is unsigned char, so we need to be careful with the length
            size_t len = strlen(data->mount->mountpoint);
            if (len > 255) {
                mcp_log_warn("Mountpoint length %zu truncated to 255", len);
                len = 255;
            }
            data->mount->mountpoint_len = (unsigned char)len;

            info.mounts = data->mount;
            mcp_log_info("Static file mount configured");
        }
    }

    data->context = lws_create_context(&info);
    if (data->context == NULL) {
        mcp_log_error("Failed to create HTTP server context");
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event thread
    if (mcp_thread_create(&data->event_thread, http_event_thread_func, transport) != 0) {
        mcp_log_error("Failed to create HTTP event thread");
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    mcp_log_info("HTTP transport started on %s:%d", data->config.host, data->config.port);
    return 0;
}

// Stop HTTP transport
static int http_transport_stop(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    // Set running flag to false
    data->running = false;

    // Wait for event thread to exit
    mcp_thread_join(data->event_thread, NULL);

    // Destroy context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("HTTP transport stopped");
    return 0;
}

// Destroy HTTP transport
static int http_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    // Stop transport if running
    if (data->running) {
        http_transport_stop(transport);
    }

    // Free configuration
    if (data->config.host) free((void*)data->config.host);
    if (data->config.cert_path) free((void*)data->config.cert_path);
    if (data->config.key_path) free((void*)data->config.key_path);
    if (data->config.doc_root) free((void*)data->config.doc_root);

    // Free mount structure
    if (data->mount) {
        free(data->mount);
        data->mount = NULL;
    }

    // Free stored SSE events
    mcp_mutex_lock(data->event_mutex);
    for (int i = 0; i < data->stored_event_count; i++) {
        if (data->stored_events[i].id) free(data->stored_events[i].id);
        if (data->stored_events[i].event_type) free(data->stored_events[i].event_type);
        if (data->stored_events[i].data) free(data->stored_events[i].data);
    }
    mcp_mutex_unlock(data->event_mutex);

    // Destroy mutexes
    if (data->sse_mutex) {
        mcp_mutex_destroy(data->sse_mutex);
    }

    if (data->event_mutex) {
        mcp_mutex_destroy(data->event_mutex);
    }

    // Free transport data
    free(data);

    // Free transport
    free(transport);

    return 0;
}

// Store an SSE event for replay on reconnection
static void store_sse_event(http_transport_data_t* data, const char* event, const char* event_data) {
    if (data == NULL || event_data == NULL) {
        return;
    }

    mcp_mutex_lock(data->event_mutex);

    // Get the current event ID
    int event_id = data->next_event_id++;

    // If we've reached the maximum number of stored events, remove the oldest one
    if (data->stored_event_count >= MAX_SSE_STORED_EVENTS) {
        // Free the oldest event's memory
        if (data->stored_events[0].id) free(data->stored_events[0].id);
        if (data->stored_events[0].event_type) free(data->stored_events[0].event_type);
        if (data->stored_events[0].data) free(data->stored_events[0].data);

        // Shift all events down by one
        for (int i = 0; i < data->stored_event_count - 1; i++) {
            data->stored_events[i] = data->stored_events[i + 1];
        }

        // Decrement the count
        data->stored_event_count--;
    }

    // Add the new event at the end
    int index = data->stored_event_count;

    // Allocate and store the event ID
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%d", event_id);
    data->stored_events[index].id = mcp_strdup(id_str);

    // Store the event type (if provided)
    data->stored_events[index].event_type = event ? mcp_strdup(event) : NULL;

    // Store the event data
    data->stored_events[index].data = mcp_strdup(event_data);

    // Store the timestamp
    data->stored_events[index].timestamp = time(NULL);

    // Increment the count
    data->stored_event_count++;

    mcp_mutex_unlock(data->event_mutex);
}

// Send a heartbeat to all SSE clients
static void send_sse_heartbeat(http_transport_data_t* data) {
    if (!data->send_heartbeats) {
        return;
    }

    time_t now = time(NULL);
    if (now - data->last_heartbeat < data->heartbeat_interval_ms / 1000) {
        return; // Not time for a heartbeat yet
    }

    // Update last heartbeat time
    data->last_heartbeat = now;

    // Send heartbeat to all clients
    mcp_mutex_lock(data->sse_mutex);
    for (int i = 0; i < data->sse_client_count; i++) {
        struct lws* wsi = data->sse_clients[i];

        // Send a comment as a heartbeat (will not trigger an event in the client)
        lws_write_http(wsi, ": heartbeat\n\n", 13);

        // Request a callback when the socket is writable again
        lws_callback_on_writable(wsi);
    }
    mcp_mutex_unlock(data->sse_mutex);
}

// Event thread function
static void* http_event_thread_func(void* arg) {
    mcp_transport_t* transport = (mcp_transport_t*)arg;
    if (transport == NULL || transport->transport_data == NULL) {
        return NULL;
    }

    http_transport_data_t* data = (http_transport_data_t*)transport->transport_data;

    mcp_log_info("HTTP event thread started");

    // Run event loop
    while (data->running) {
        // Service libwebsockets
        lws_service(data->context, 100); // 100ms timeout

        // Send heartbeat if needed
        send_sse_heartbeat(data);
    }

    mcp_log_info("HTTP event thread exited");
    return NULL;
}

// Process HTTP request
static void process_http_request(struct lws* wsi, http_transport_data_t* data,
                                const char* request, size_t len) {
    mcp_log_info("Processing HTTP request: %.*s", (int)len, request);

    if (data->message_callback == NULL) {
        mcp_log_error("No message callback registered");
        return;
    }

    // Call message callback
    int error_code = 0;
    char* response = data->message_callback(data->callback_user_data, request, len, &error_code);

    mcp_log_info("Message callback returned: error_code=%d, response=%s",
                error_code, response ? response : "NULL");

    if (response == NULL) {
        // Send error response with proper JSON-RPC 2.0 format
        char error_buf[512];
        const char* error_message = "Internal server error";

        // Map error codes to standard JSON-RPC error codes and messages
        switch (error_code) {
            case -32700:
                error_message = "Parse error";
                break;
            case -32600:
                error_message = "Invalid request";
                break;
            case -32601:
                error_message = "Method not found";
                break;
            case -32602:
                error_message = "Invalid params";
                break;
            case -32603:
                error_message = "Internal error";
                break;
            default:
                if (error_code <= -32000 && error_code >= -32099) {
                    error_message = "Server error";
                }
                break;
        }

        snprintf(error_buf, sizeof(error_buf),
                 "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":null}",
                 error_code, error_message);

        mcp_log_info("Sending error response: %s", error_buf);

        // Prepare response headers
        unsigned char buffer[LWS_PRE + 1024];
        unsigned char* p = &buffer[LWS_PRE];
        unsigned char* end = &buffer[sizeof(buffer) - 1];

        // Add headers
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                       "application/json", strlen(error_buf), &p, end)) {
            mcp_log_error("Failed to add HTTP headers");
            return;
        }

        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
            mcp_log_error("Failed to finalize HTTP headers");
            return;
        }

        // Write response body
        int bytes_written = lws_write(wsi, (unsigned char*)error_buf, strlen(error_buf), LWS_WRITE_HTTP);
        mcp_log_info("Wrote %d bytes", bytes_written);
    } else {
        mcp_log_info("Sending success response: %s", response);

        // Prepare response headers
        unsigned char buffer[LWS_PRE + 1024];
        unsigned char* p = &buffer[LWS_PRE];
        unsigned char* end = &buffer[sizeof(buffer) - 1];

        // Add headers
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                       "application/json", strlen(response), &p, end)) {
            mcp_log_error("Failed to add HTTP headers");
            free(response);
            return;
        }

        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
            mcp_log_error("Failed to finalize HTTP headers");
            free(response);
            return;
        }

        // Write response body
        int bytes_written = lws_write(wsi, (unsigned char*)response, strlen(response), LWS_WRITE_HTTP);
        mcp_log_info("Wrote %d bytes", bytes_written);

        // Free response
        free(response);
    }

    // Complete HTTP transaction
    lws_http_transaction_completed(wsi);
    mcp_log_info("HTTP transaction completed");
}

// Handle SSE request
static void handle_sse_request(struct lws* wsi, http_transport_data_t* data) {
    // Prepare response headers
    unsigned char buffer[LWS_PRE + 1024];
    unsigned char* p = &buffer[LWS_PRE];
    unsigned char* end = &buffer[sizeof(buffer) - 1];

    // Add headers
    if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                   "text/event-stream",
                                   LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
        return;
    }

    // Add SSE specific headers
    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Cache-Control",
                                   (unsigned char*)"no-cache", 8, &p, end)) {
        return;
    }

    if (lws_add_http_header_by_name(wsi,
                                   (unsigned char*)"Connection",
                                   (unsigned char*)"keep-alive", 10, &p, end)) {
        return;
    }

    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
        return;
    }

    // Mark as SSE connection
    lws_http_mark_sse(wsi);

    // Get session data
    http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);
    if (session) {
        session->is_sse_client = true;
        session->last_event_id = 0;

        // Check for Last-Event-ID header
        // libwebsockets doesn't have a direct token for Last-Event-ID, so we need to use a custom header
        char last_event_id[32] = {0};

        // Try to get the Last-Event-ID from the query string first
        char query[256] = {0};
        int len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
        if (len > 0 && len < (int)sizeof(query)) {
            lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS);

            // Parse query string for 'lastEventId' parameter
            char* id_param = strstr(query, "lastEventId=");
            if (id_param) {
                id_param += 12; // Skip "lastEventId="

                // Find the end of the parameter value
                char* end_param = strchr(id_param, '&');
                if (end_param) {
                    *end_param = '\0';
                }

                // Copy the ID
                strncpy(last_event_id, id_param, sizeof(last_event_id) - 1);
                session->last_event_id = atoi(last_event_id);
                mcp_log_info("SSE client reconnected with Last-Event-ID: %d", session->last_event_id);
            }
        }

        // Check for event filter in query string (reuse the query string we already parsed)
        if (len == 0) {
            // If we didn't get the query string above, get it now
            len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
            if (len > 0 && len < (int)sizeof(query)) {
                lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS);
            }
        }

        if (len > 0) {
            // Parse query string for 'filter' parameter
            char* filter_param = strstr(query, "filter=");
            if (filter_param) {
                filter_param += 7; // Skip "filter="

                // Find the end of the parameter value
                char* end_param = strchr(filter_param, '&');
                if (end_param) {
                    *end_param = '\0';
                }

                // Store the filter
                session->event_filter = mcp_strdup(filter_param);
                mcp_log_info("SSE client connected with event filter: %s", session->event_filter);
            }
        }
    }

    // Add to SSE clients list
    mcp_mutex_lock(data->sse_mutex);
    if (data->sse_client_count < MAX_SSE_CLIENTS) {
        data->sse_clients[data->sse_client_count++] = wsi;
    }
    mcp_mutex_unlock(data->sse_mutex);

    // Send initial SSE message
    const char* initial_msg = "data: connected\n\n";
    lws_write(wsi, (unsigned char*)initial_msg, strlen(initial_msg), LWS_WRITE_HTTP);

    // If client reconnected with Last-Event-ID, replay missed events
    if (session && session->last_event_id > 0) {
        mcp_mutex_lock(data->event_mutex);

        // Find the first event with ID greater than last_event_id
        for (int i = 0; i < data->stored_event_count; i++) {
            int event_id = atoi(data->stored_events[i].id);

            // Skip events with ID less than or equal to last_event_id
            if (event_id <= session->last_event_id) {
                continue;
            }

            // Skip events that don't match the filter (if any)
            if (session->event_filter && data->stored_events[i].event_type &&
                strcmp(session->event_filter, data->stored_events[i].event_type) != 0) {
                continue;
            }

            // Replay the event
            if (data->stored_events[i].event_type) {
                // Write event type
                lws_write_http(wsi, "event: ", 7);
                lws_write_http(wsi, data->stored_events[i].event_type,
                              strlen(data->stored_events[i].event_type));
                lws_write_http(wsi, "\n", 1);
            }

            // Write event ID
            lws_write_http(wsi, "id: ", 4);
            lws_write_http(wsi, data->stored_events[i].id, strlen(data->stored_events[i].id));
            lws_write_http(wsi, "\n", 1);

            // Write event data
            lws_write_http(wsi, "data: ", 6);
            lws_write_http(wsi, data->stored_events[i].data, strlen(data->stored_events[i].data));
            lws_write_http(wsi, "\n\n", 2);

            // Request a callback when the socket is writable again
            lws_callback_on_writable(wsi);
        }

        mcp_mutex_unlock(data->event_mutex);
    }
}

// LWS callback function
static int lws_callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                            void* user, void* in, size_t len) {
    http_session_data_t* session = (http_session_data_t*)user;
    http_transport_data_t* data = (http_transport_data_t*)lws_context_user(lws_get_context(wsi));

    // Log the callback reason
    const char* reason_str = "unknown";
    switch (reason) {
        case LWS_CALLBACK_HTTP: reason_str = "LWS_CALLBACK_HTTP"; break;
        case LWS_CALLBACK_HTTP_BODY: reason_str = "LWS_CALLBACK_HTTP_BODY"; break;
        case LWS_CALLBACK_HTTP_BODY_COMPLETION: reason_str = "LWS_CALLBACK_HTTP_BODY_COMPLETION"; break;
        case LWS_CALLBACK_HTTP_FILE_COMPLETION: reason_str = "LWS_CALLBACK_HTTP_FILE_COMPLETION"; break;
        case LWS_CALLBACK_HTTP_WRITEABLE: reason_str = "LWS_CALLBACK_HTTP_WRITEABLE"; break;
        case LWS_CALLBACK_FILTER_HTTP_CONNECTION: reason_str = "LWS_CALLBACK_FILTER_HTTP_CONNECTION"; break;
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: reason_str = "LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION"; break;
        case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: reason_str = "LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED"; break;
        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: reason_str = "LWS_CALLBACK_FILTER_NETWORK_CONNECTION"; break;
        case LWS_CALLBACK_ESTABLISHED: reason_str = "LWS_CALLBACK_ESTABLISHED"; break;
        case LWS_CALLBACK_CLOSED: reason_str = "LWS_CALLBACK_CLOSED"; break;
        case LWS_CALLBACK_RECEIVE: reason_str = "LWS_CALLBACK_RECEIVE"; break;
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: reason_str = "LWS_CALLBACK_CLIENT_CONNECTION_ERROR"; break;
        case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: reason_str = "LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH"; break;
        case LWS_CALLBACK_CLIENT_ESTABLISHED: reason_str = "LWS_CALLBACK_CLIENT_ESTABLISHED"; break;
        case LWS_CALLBACK_CLIENT_RECEIVE: reason_str = "LWS_CALLBACK_CLIENT_RECEIVE"; break;
        case LWS_CALLBACK_CLIENT_WRITEABLE: reason_str = "LWS_CALLBACK_CLIENT_WRITEABLE"; break;
        case LWS_CALLBACK_CLIENT_CLOSED: reason_str = "LWS_CALLBACK_CLIENT_CLOSED"; break;
        case LWS_CALLBACK_WSI_CREATE: reason_str = "LWS_CALLBACK_WSI_CREATE"; break;
        case LWS_CALLBACK_WSI_DESTROY: reason_str = "LWS_CALLBACK_WSI_DESTROY"; break;
        case LWS_CALLBACK_GET_THREAD_ID: reason_str = "LWS_CALLBACK_GET_THREAD_ID"; break;
        case LWS_CALLBACK_ADD_POLL_FD: reason_str = "LWS_CALLBACK_ADD_POLL_FD"; break;
        case LWS_CALLBACK_DEL_POLL_FD: reason_str = "LWS_CALLBACK_DEL_POLL_FD"; break;
        case LWS_CALLBACK_CHANGE_MODE_POLL_FD: reason_str = "LWS_CALLBACK_CHANGE_MODE_POLL_FD"; break;
        case LWS_CALLBACK_LOCK_POLL: reason_str = "LWS_CALLBACK_LOCK_POLL"; break;
        case LWS_CALLBACK_UNLOCK_POLL: reason_str = "LWS_CALLBACK_UNLOCK_POLL"; break;
        case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: reason_str = "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS"; break;
        case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS: reason_str = "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS"; break;
        case LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION: reason_str = "LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION"; break;
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: reason_str = "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER"; break;
        case LWS_CALLBACK_CONFIRM_EXTENSION_OKAY: reason_str = "LWS_CALLBACK_CONFIRM_EXTENSION_OKAY"; break;
        case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED: reason_str = "LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED"; break;
        case LWS_CALLBACK_PROTOCOL_INIT: reason_str = "LWS_CALLBACK_PROTOCOL_INIT"; break;
        case LWS_CALLBACK_PROTOCOL_DESTROY: reason_str = "LWS_CALLBACK_PROTOCOL_DESTROY"; break;
        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: reason_str = "LWS_CALLBACK_WS_PEER_INITIATED_CLOSE"; break;
        case LWS_CALLBACK_WS_EXT_DEFAULTS: reason_str = "LWS_CALLBACK_WS_EXT_DEFAULTS"; break;
        case LWS_CALLBACK_CGI: reason_str = "LWS_CALLBACK_CGI"; break;
        case LWS_CALLBACK_CGI_TERMINATED: reason_str = "LWS_CALLBACK_CGI_TERMINATED"; break;
        case LWS_CALLBACK_CGI_STDIN_DATA: reason_str = "LWS_CALLBACK_CGI_STDIN_DATA"; break;
        case LWS_CALLBACK_CGI_STDIN_COMPLETED: reason_str = "LWS_CALLBACK_CGI_STDIN_COMPLETED"; break;
        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: reason_str = "LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP"; break;
        case LWS_CALLBACK_CLOSED_CLIENT_HTTP: reason_str = "LWS_CALLBACK_CLOSED_CLIENT_HTTP"; break;
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: reason_str = "LWS_CALLBACK_RECEIVE_CLIENT_HTTP"; break;
        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: reason_str = "LWS_CALLBACK_COMPLETED_CLIENT_HTTP"; break;
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: reason_str = "LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ"; break;
        case LWS_CALLBACK_HTTP_BIND_PROTOCOL: reason_str = "LWS_CALLBACK_HTTP_BIND_PROTOCOL"; break;
        case LWS_CALLBACK_HTTP_DROP_PROTOCOL: reason_str = "LWS_CALLBACK_HTTP_DROP_PROTOCOL"; break;
        case LWS_CALLBACK_CHECK_ACCESS_RIGHTS: reason_str = "LWS_CALLBACK_CHECK_ACCESS_RIGHTS"; break;
        case LWS_CALLBACK_PROCESS_HTML: reason_str = "LWS_CALLBACK_PROCESS_HTML"; break;
        case LWS_CALLBACK_ADD_HEADERS: reason_str = "LWS_CALLBACK_ADD_HEADERS"; break;
        case LWS_CALLBACK_SESSION_INFO: reason_str = "LWS_CALLBACK_SESSION_INFO"; break;
        case LWS_CALLBACK_GS_EVENT: reason_str = "LWS_CALLBACK_GS_EVENT"; break;
        case LWS_CALLBACK_HTTP_PMO: reason_str = "LWS_CALLBACK_HTTP_PMO"; break;
        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: reason_str = "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE"; break;
        case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION: reason_str = "LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION"; break;
        case LWS_CALLBACK_RAW_RX: reason_str = "LWS_CALLBACK_RAW_RX"; break;
        case LWS_CALLBACK_RAW_CLOSE: reason_str = "LWS_CALLBACK_RAW_CLOSE"; break;
        case LWS_CALLBACK_RAW_WRITEABLE: reason_str = "LWS_CALLBACK_RAW_WRITEABLE"; break;
        case LWS_CALLBACK_RAW_ADOPT: reason_str = "LWS_CALLBACK_RAW_ADOPT"; break;
        case LWS_CALLBACK_RAW_ADOPT_FILE: reason_str = "LWS_CALLBACK_RAW_ADOPT_FILE"; break;
        case LWS_CALLBACK_RAW_RX_FILE: reason_str = "LWS_CALLBACK_RAW_RX_FILE"; break;
        case LWS_CALLBACK_RAW_WRITEABLE_FILE: reason_str = "LWS_CALLBACK_RAW_WRITEABLE_FILE"; break;
        case LWS_CALLBACK_RAW_CLOSE_FILE: reason_str = "LWS_CALLBACK_RAW_CLOSE_FILE"; break;
        case LWS_CALLBACK_SSL_INFO: reason_str = "LWS_CALLBACK_SSL_INFO"; break;
        case LWS_CALLBACK_TIMER: reason_str = "LWS_CALLBACK_TIMER"; break;
        case LWS_CALLBACK_CLOSED_HTTP: reason_str = "LWS_CALLBACK_CLOSED_HTTP"; break;
        case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: reason_str = "LWS_CALLBACK_HTTP_CONFIRM_UPGRADE"; break;
        case LWS_CALLBACK_USER: reason_str = "LWS_CALLBACK_USER"; break;
        default: break;
    }
    mcp_log_debug("HTTP callback: reason=%s (%d)", reason_str, reason);

    switch (reason) {
        case LWS_CALLBACK_HTTP:
            {
                char* uri = (char*)in;
                mcp_log_info("HTTP request: %s", uri);

                // Check if this is an SSE request
                if (strcmp(uri, "/events") == 0) {
                    mcp_log_info("Handling SSE request");
                    handle_sse_request(wsi, data);
                    return 0;
                }

                // Check if this is a tool call
                if (strcmp(uri, "/call_tool") == 0) {
                    mcp_log_info("Handling tool call request");

                    // Check if this is a POST request
                    char method[16] = {0};

                    // Try to get the request method
                    // In libwebsockets, the HTTP method is part of the URI
                    // We can check if it's a POST request by checking for the POST_URI token
                    if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0) {
                        strcpy(method, "POST");
                        mcp_log_info("HTTP method: POST");
                    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) > 0) {
                        strcpy(method, "GET");
                        mcp_log_info("HTTP method: GET");
                    } else {
                        mcp_log_error("Failed to determine HTTP method");
                    }

                    // Check if this is a POST request
                    if (method[0] != '\0' && strcmp(method, "POST") == 0) {
                        // This is a POST request, wait for body
                        mcp_log_info("Waiting for POST body");
                        return 0;
                    } else {
                        // For non-POST requests, return a simple JSON response
                        const char* json_response = "{\"error\":\"Method not allowed. Use POST for tool calls.\"}";

                        // Prepare response headers
                        unsigned char buffer[LWS_PRE + 1024];
                        unsigned char* p = &buffer[LWS_PRE];
                        unsigned char* end = &buffer[sizeof(buffer) - 1];

                        // Add headers
                        if (lws_add_http_common_headers(wsi, HTTP_STATUS_METHOD_NOT_ALLOWED,
                                                      "application/json",
                                                      strlen(json_response), &p, end)) {
                            mcp_log_error("Failed to add HTTP headers");
                            return -1;
                        }

                        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                            mcp_log_error("Failed to finalize HTTP headers");
                            return -1;
                        }

                        // Write response body
                        int bytes_written = lws_write(wsi, (unsigned char*)json_response, strlen(json_response), LWS_WRITE_HTTP);
                        mcp_log_info("Wrote %d bytes", bytes_written);

                        // Complete HTTP transaction
                        lws_http_transaction_completed(wsi);
                        return 0;
                    }
                }

                // For the root path, return a simple HTML page
                if (strcmp(uri, "/") == 0) {
                    mcp_log_info("Serving root page");

                    // Prepare response headers
                    unsigned char buffer[LWS_PRE + 1024];
                    unsigned char* p = &buffer[LWS_PRE];
                    unsigned char* end = &buffer[sizeof(buffer) - 1];

                    // Add headers
                    if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                                   "text/html",
                                                   LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
                        return -1;
                    }

                    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                        return -1;
                    }

                    // Create a simple HTML page
                    const char* html =
                        "<!DOCTYPE html>\n"
                        "<html>\n"
                        "<head>\n"
                        "    <title>MCP HTTP Server</title>\n"
                        "    <style>\n"
                        "        body { font-family: Arial, sans-serif; margin: 20px; line-height: 1.6; }\n"
                        "        h1, h2 { color: #333; }\n"
                        "        pre { background-color: #f5f5f5; padding: 10px; border-radius: 4px; overflow-x: auto; }\n"
                        "        .endpoint { background-color: #e9f7ef; padding: 15px; margin: 15px 0; border-radius: 4px; }\n"
                        "        .endpoint h3 { margin-top: 0; }\n"
                        "        a { color: #0066cc; text-decoration: none; }\n"
                        "        a:hover { text-decoration: underline; }\n"
                        "        code { background-color: #f5f5f5; padding: 2px 4px; border-radius: 3px; }\n"
                        "    </style>\n"
                        "</head>\n"
                        "<body>\n"
                        "    <h1>MCP HTTP Server</h1>\n"
                        "    <p>This is the MCP HTTP server, providing HTTP and SSE functionality for the MCP server.</p>\n"
                        "    \n"
                        "    <div class=\"endpoint\">\n"
                        "        <h2>Available Endpoints:</h2>\n"
                        "        <ul>\n"
                        "            <li><a href=\"/call_tool\"><code>/call_tool</code></a> - JSON-RPC endpoint for calling tools</li>\n"
                        "            <li><a href=\"/events\"><code>/events</code></a> - Server-Sent Events (SSE) endpoint</li>\n"
                        "            <li><a href=\"/sse_test.html\"><code>/sse_test.html</code></a> - SSE test page</li>\n"
                        "        </ul>\n"
                        "    </div>\n"
                        "    \n"
                        "    <div class=\"endpoint\">\n"
                        "        <h2>Available Tools:</h2>\n"
                        "        <ul>\n"
                        "            <li><strong>echo</strong> - Echoes back the input text</li>\n"
                        "            <li><strong>reverse</strong> - Reverses the input text</li>\n"
                        "        </ul>\n"
                        "    </div>\n"
                        "    \n"
                        "    <div class=\"endpoint\">\n"
                        "        <h2>Tool Call Example:</h2>\n"
                        "        <h3>Using curl:</h3>\n"
                        "        <pre>curl -X POST http://127.0.0.1:8180/call_tool \\\n"
                        "     -H \"Content-Type: application/json\" \\\n"
                        "     -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"call_tool\",\"params\":{\"name\":\"echo\",\"arguments\":{\"text\":\"Hello, MCP Server!\"}}}'\n"
                        "</pre>\n"
                        "        <h3>Using JavaScript:</h3>\n"
                        "        <pre>fetch('/call_tool', {\n"
                        "    method: 'POST',\n"
                        "    headers: {\n"
                        "        'Content-Type': 'application/json'\n"
                        "    },\n"
                        "    body: JSON.stringify({\n"
                        "        jsonrpc: '2.0',\n"
                        "        id: 1,\n"
                        "        method: 'call_tool',\n"
                        "        params: {\n"
                        "            name: 'echo',\n"
                        "            arguments: {\n"
                        "                text: 'Hello, MCP Server!'\n"
                        "            }\n"
                        "        }\n"
                        "    })\n"
                        "})\n"
                        ".then(response => response.json())\n"
                        ".then(data => console.log(data));</pre>\n"
                        "    </div>\n"
                        "    \n"
                        "    <div class=\"endpoint\">\n"
                        "        <h2>SSE Example:</h2>\n"
                        "        <p>Connect to the SSE endpoint to receive real-time events:</p>\n"
                        "        <pre>const eventSource = new EventSource('/events');\n"
                        "\n"
                        "eventSource.onmessage = function(event) {\n"
                        "    console.log('Received event:', event.data);\n"
                        "};\n"
                        "\n"
                        "eventSource.addEventListener('tool_call', function(event) {\n"
                        "    console.log('Tool call event:', event.data);\n"
                        "});\n"
                        "\n"
                        "eventSource.addEventListener('tool_result', function(event) {\n"
                        "    console.log('Tool result event:', event.data);\n"
                        "});</pre>\n"
                        "        <p>Visit the <a href=\"/sse_test.html\">SSE test page</a> to see it in action.</p>\n"
                        "    </div>\n"
                        "</body>\n"
                        "</html>\n";

                    // Write response body
                    lws_write(wsi, (unsigned char*)html, strlen(html), LWS_WRITE_HTTP);

                    // Complete HTTP transaction
                    lws_http_transaction_completed(wsi);
                    return 0;
                }

                // For other requests, try to serve static files if doc_root is set
                if (data->config.doc_root) {
                    char path[512];

                    // Convert URI forward slashes to backslashes on Windows
                    char file_path[512];
#ifdef _WIN32
                    // Copy URI to file_path, replacing '/' with '\\'
                    size_t uri_len = strlen(uri);
                    for (size_t i = 0; i < uri_len && i < sizeof(file_path) - 1; i++) {
                        file_path[i] = (uri[i] == '/') ? '\\' : uri[i];
                    }
                    file_path[uri_len < sizeof(file_path) - 1 ? uri_len : sizeof(file_path) - 1] = '\0';

                    snprintf(path, sizeof(path), "%s%s", data->config.doc_root, file_path);
#else
                    snprintf(path, sizeof(path), "%s%s", data->config.doc_root, uri);
#endif
                    mcp_log_info("Serving file from path: %s", path);

                    // Check if file exists
                    FILE* f = fopen(path, "r");
                    if (f) {
                        fclose(f);
                        mcp_log_info("File exists, serving...");

                        // Determine MIME type based on file extension
                        const char* mime_type = "text/plain";
                        char* ext = strrchr(path, '.');
                        if (ext) {
                            ext++; // Skip the dot

                            // Text types
                            if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
                                mime_type = "text/html";
                            } else if (strcasecmp(ext, "css") == 0) {
                                mime_type = "text/css";
                            } else if (strcasecmp(ext, "js") == 0) {
                                mime_type = "application/javascript";
                            } else if (strcasecmp(ext, "json") == 0) {
                                mime_type = "application/json";
                            } else if (strcasecmp(ext, "xml") == 0) {
                                mime_type = "application/xml";
                            } else if (strcasecmp(ext, "txt") == 0) {
                                mime_type = "text/plain";
                            } else if (strcasecmp(ext, "csv") == 0) {
                                mime_type = "text/csv";
                            } else if (strcasecmp(ext, "md") == 0 || strcasecmp(ext, "markdown") == 0) {
                                mime_type = "text/markdown";
                            }

                            // Image types
                            else if (strcasecmp(ext, "png") == 0) {
                                mime_type = "image/png";
                            } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
                                mime_type = "image/jpeg";
                            } else if (strcasecmp(ext, "gif") == 0) {
                                mime_type = "image/gif";
                            } else if (strcasecmp(ext, "svg") == 0) {
                                mime_type = "image/svg+xml";
                            } else if (strcasecmp(ext, "ico") == 0) {
                                mime_type = "image/x-icon";
                            } else if (strcasecmp(ext, "webp") == 0) {
                                mime_type = "image/webp";
                            } else if (strcasecmp(ext, "bmp") == 0) {
                                mime_type = "image/bmp";
                            } else if (strcasecmp(ext, "tiff") == 0 || strcasecmp(ext, "tif") == 0) {
                                mime_type = "image/tiff";
                            }

                            // Audio types
                            else if (strcasecmp(ext, "mp3") == 0) {
                                mime_type = "audio/mpeg";
                            } else if (strcasecmp(ext, "wav") == 0) {
                                mime_type = "audio/wav";
                            } else if (strcasecmp(ext, "ogg") == 0) {
                                mime_type = "audio/ogg";
                            } else if (strcasecmp(ext, "m4a") == 0) {
                                mime_type = "audio/mp4";
                            }

                            // Video types
                            else if (strcasecmp(ext, "mp4") == 0) {
                                mime_type = "video/mp4";
                            } else if (strcasecmp(ext, "webm") == 0) {
                                mime_type = "video/webm";
                            } else if (strcasecmp(ext, "avi") == 0) {
                                mime_type = "video/x-msvideo";
                            } else if (strcasecmp(ext, "mov") == 0) {
                                mime_type = "video/quicktime";
                            }

                            // Font types
                            else if (strcasecmp(ext, "ttf") == 0) {
                                mime_type = "font/ttf";
                            } else if (strcasecmp(ext, "otf") == 0) {
                                mime_type = "font/otf";
                            } else if (strcasecmp(ext, "woff") == 0) {
                                mime_type = "font/woff";
                            } else if (strcasecmp(ext, "woff2") == 0) {
                                mime_type = "font/woff2";
                            }

                            // Application types
                            else if (strcasecmp(ext, "pdf") == 0) {
                                mime_type = "application/pdf";
                            } else if (strcasecmp(ext, "zip") == 0) {
                                mime_type = "application/zip";
                            } else if (strcasecmp(ext, "gz") == 0) {
                                mime_type = "application/gzip";
                            } else if (strcasecmp(ext, "doc") == 0) {
                                mime_type = "application/msword";
                            } else if (strcasecmp(ext, "docx") == 0) {
                                mime_type = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
                            } else if (strcasecmp(ext, "xls") == 0) {
                                mime_type = "application/vnd.ms-excel";
                            } else if (strcasecmp(ext, "xlsx") == 0) {
                                mime_type = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
                            } else if (strcasecmp(ext, "ppt") == 0) {
                                mime_type = "application/vnd.ms-powerpoint";
                            } else if (strcasecmp(ext, "pptx") == 0) {
                                mime_type = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
                            }
                        }

                        // Add cache control headers based on file type
                        char cache_control[128] = "";

                        // For static assets like images, fonts, CSS, JS, etc., use longer cache times
                        if (strncmp(mime_type, "image/", 6) == 0 ||
                            strncmp(mime_type, "font/", 5) == 0 ||
                            strcmp(mime_type, "text/css") == 0 ||
                            strcmp(mime_type, "application/javascript") == 0) {
                            // Cache for 1 week (604800 seconds)
                            strcpy(cache_control, "max-age=604800, public");
                        }
                        // For HTML, JSON, and other dynamic content, use shorter cache times
                        else if (strcmp(mime_type, "text/html") == 0 ||
                                 strcmp(mime_type, "application/json") == 0) {
                            // Cache for 1 hour (3600 seconds)
                            strcpy(cache_control, "max-age=3600, public");
                        }
                        // For other types, use a moderate cache time
                        else {
                            // Cache for 1 day (86400 seconds)
                            strcpy(cache_control, "max-age=86400, public");
                        }

                        // Serve the file with cache control headers
                        int ret = lws_serve_http_file(wsi, path, mime_type, cache_control, 0);
                        mcp_log_info("lws_serve_http_file returned: %d", ret);
                        return ret;
                    } else {
                        mcp_log_error("File does not exist: %s", path);
                    }
                }

                // If we get here, return a 404 error
                mcp_log_info("Returning 404 for URI: %s", uri);

                // Prepare response headers
                unsigned char buffer[LWS_PRE + 1024];
                unsigned char* p = &buffer[LWS_PRE];
                unsigned char* end = &buffer[sizeof(buffer) - 1];

                // Add headers
                if (lws_add_http_common_headers(wsi, HTTP_STATUS_NOT_FOUND,
                                               "text/html",
                                               LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end)) {
                    return -1;
                }

                if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                    return -1;
                }

                // Create a simple 404 page
                const char* html =
                    "<!DOCTYPE html>\n"
                    "<html>\n"
                    "<head>\n"
                    "    <title>404 Not Found</title>\n"
                    "</head>\n"
                    "<body>\n"
                    "    <h1>404 Not Found</h1>\n"
                    "    <p>The requested resource was not found on this server.</p>\n"
                    "</body>\n"
                    "</html>\n";

                // Write response body
                lws_write(wsi, (unsigned char*)html, strlen(html), LWS_WRITE_HTTP);

                // Complete HTTP transaction
                lws_http_transaction_completed(wsi);
                return 0;
            }

        case LWS_CALLBACK_HTTP_BODY:
            {
                // Accumulate request body
                if (session->request_buffer == NULL) {
                    session->request_buffer = (char*)malloc(len + 1);
                    if (session->request_buffer == NULL) {
                        return -1;
                    }
                    memcpy(session->request_buffer, in, len);
                    session->request_buffer[len] = '\0';
                    session->request_len = len;
                } else {
                    char* new_buffer = (char*)realloc(session->request_buffer,
                                                     session->request_len + len + 1);
                    if (new_buffer == NULL) {
                        free(session->request_buffer);
                        session->request_buffer = NULL;
                        return -1;
                    }
                    session->request_buffer = new_buffer;
                    memcpy(session->request_buffer + session->request_len, in, len);
                    session->request_len += len;
                    session->request_buffer[session->request_len] = '\0';
                }
                return 0;
            }

        case LWS_CALLBACK_HTTP_BODY_COMPLETION:
            {
                mcp_log_info("HTTP body completion");

                // Check if we have a request buffer
                if (session->request_buffer == NULL || session->request_len == 0) {
                    mcp_log_error("No request buffer or empty request");

                    // Return a simple JSON response
                    const char* json_response = "{\"error\":\"Empty request\"}";

                    // Prepare response headers
                    unsigned char buffer[LWS_PRE + 1024];
                    unsigned char* p = &buffer[LWS_PRE];
                    unsigned char* end = &buffer[sizeof(buffer) - 1];

                    // Add headers
                    if (lws_add_http_common_headers(wsi, HTTP_STATUS_BAD_REQUEST,
                                                  "application/json",
                                                  strlen(json_response), &p, end)) {
                        mcp_log_error("Failed to add HTTP headers");
                        return -1;
                    }

                    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                        mcp_log_error("Failed to finalize HTTP headers");
                        return -1;
                    }

                    // Write response body
                    int bytes_written = lws_write(wsi, (unsigned char*)json_response, strlen(json_response), LWS_WRITE_HTTP);
                    mcp_log_info("Wrote %d bytes", bytes_written);

                    // Complete HTTP transaction
                    lws_http_transaction_completed(wsi);
                    return 0;
                }

                mcp_log_info("Request body: %s", session->request_buffer);

                // Process the request using the message callback
                if (data->message_callback) {
                    int error_code = 0;
                    char* response = data->message_callback(data->callback_user_data,
                                                          session->request_buffer,
                                                          session->request_len,
                                                          &error_code);

                    if (response) {
                        mcp_log_info("Message callback returned: %s", response);

                        // Prepare response headers
                        unsigned char buffer[LWS_PRE + 1024];
                        unsigned char* p = &buffer[LWS_PRE];
                        unsigned char* end = &buffer[sizeof(buffer) - 1];

                        // Add headers
                        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                                      "application/json",
                                                      strlen(response), &p, end)) {
                            mcp_log_error("Failed to add HTTP headers");
                            free(response);
                            return -1;
                        }

                        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                            mcp_log_error("Failed to finalize HTTP headers");
                            free(response);
                            return -1;
                        }

                        // Write response body
                        int bytes_written = lws_write(wsi, (unsigned char*)response, strlen(response), LWS_WRITE_HTTP);
                        mcp_log_info("Wrote %d bytes", bytes_written);

                        // Free the response
                        free(response);
                    } else {
                        // Error occurred, return error response with proper JSON-RPC 2.0 format
                        char error_buf[512];
                        const char* error_message = "Internal server error";

                        // Map error codes to standard JSON-RPC error codes and messages
                        switch (error_code) {
                            case -32700:
                                error_message = "Parse error";
                                break;
                            case -32600:
                                error_message = "Invalid request";
                                break;
                            case -32601:
                                error_message = "Method not found";
                                break;
                            case -32602:
                                error_message = "Invalid params";
                                break;
                            case -32603:
                                error_message = "Internal error";
                                break;
                            default:
                                if (error_code <= -32000 && error_code >= -32099) {
                                    error_message = "Server error";
                                }
                                break;
                        }

                        snprintf(error_buf, sizeof(error_buf),
                                "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":null}",
                                error_code, error_message);

                        // Prepare response headers
                        unsigned char buffer[LWS_PRE + 1024];
                        unsigned char* p = &buffer[LWS_PRE];
                        unsigned char* end = &buffer[sizeof(buffer) - 1];

                        // Add headers
                        if (lws_add_http_common_headers(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                                      "application/json",
                                                      strlen(error_buf), &p, end)) {
                            mcp_log_error("Failed to add HTTP headers");
                            return -1;
                        }

                        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                            mcp_log_error("Failed to finalize HTTP headers");
                            return -1;
                        }

                        // Write response body
                        int bytes_written = lws_write(wsi, (unsigned char*)error_buf, strlen(error_buf), LWS_WRITE_HTTP);
                        mcp_log_info("Wrote %d bytes", bytes_written);
                    }
                } else {
                    // No message callback registered, return error
                    const char* json_response = "{\"error\":\"No message handler registered\"}";

                    // Prepare response headers
                    unsigned char buffer[LWS_PRE + 1024];
                    unsigned char* p = &buffer[LWS_PRE];
                    unsigned char* end = &buffer[sizeof(buffer) - 1];

                    // Add headers
                    if (lws_add_http_common_headers(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                                  "application/json",
                                                  strlen(json_response), &p, end)) {
                        mcp_log_error("Failed to add HTTP headers");
                        return -1;
                    }

                    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                        mcp_log_error("Failed to finalize HTTP headers");
                        return -1;
                    }

                    // Write response body
                    int bytes_written = lws_write(wsi, (unsigned char*)json_response, strlen(json_response), LWS_WRITE_HTTP);
                    mcp_log_info("Wrote %d bytes", bytes_written);
                }

                // Complete HTTP transaction
                lws_http_transaction_completed(wsi);

                // Free request buffer
                free(session->request_buffer);
                session->request_buffer = NULL;
                session->request_len = 0;

                return 0;
            }

        case LWS_CALLBACK_CLOSED_HTTP:
            {
                // Clean up session
                if (session->request_buffer) {
                    free(session->request_buffer);
                    session->request_buffer = NULL;
                }

                // Free event filter if set
                if (session->event_filter) {
                    free(session->event_filter);
                    session->event_filter = NULL;
                }

                // Remove from SSE clients if this was an SSE client
                if (session->is_sse_client) {
                    mcp_mutex_lock(data->sse_mutex);
                    for (int i = 0; i < data->sse_client_count; i++) {
                        if (data->sse_clients[i] == wsi) {
                            // Remove by shifting remaining clients
                            for (int j = i; j < data->sse_client_count - 1; j++) {
                                data->sse_clients[j] = data->sse_clients[j + 1];
                            }
                            data->sse_client_count--;
                            mcp_log_info("SSE client disconnected, %d clients remaining", data->sse_client_count);
                            break;
                        }
                    }
                    mcp_mutex_unlock(data->sse_mutex);
                }
                return 0;
            }

        default:
            return lws_callback_http_dummy(wsi, reason, user, in, len);
    }

    //return 0;
}

// Send SSE event to all connected clients
int mcp_http_transport_send_sse(mcp_transport_t* transport, const char* event, const char* data) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL) {
        return -1;
    }

    http_transport_data_t* transport_data = (http_transport_data_t*)transport->transport_data;

    // Store the event for replay on reconnection
    store_sse_event(transport_data, event, data);

    // Get the current event ID
    int event_id = transport_data->next_event_id - 1; // The ID was already incremented in store_sse_event
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%d", event_id);

    // Send to all SSE clients
    mcp_mutex_lock(transport_data->sse_mutex);
    for (int i = 0; i < transport_data->sse_client_count; i++) {
        struct lws* wsi = transport_data->sse_clients[i];

        // Get session data to check for event filter
        http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);

        // Skip this client if it has a filter and the event doesn't match
        if (session && session->event_filter && event &&
            strcmp(session->event_filter, event) != 0) {
            continue;
        }

        if (event != NULL) {
            // Write in multiple pieces to avoid any heap allocation
            // 1. Write "event: "
            lws_write_http(wsi, "event: ", 7);

            // 2. Write the event name
            lws_write_http(wsi, event, strlen(event));

            // 3. Write "\nid: "
            lws_write_http(wsi, "\nid: ", 5);

            // 4. Write the event ID
            lws_write_http(wsi, id_str, strlen(id_str));

            // 5. Write "\ndata: "
            lws_write_http(wsi, "\ndata: ", 7);

            // 6. Write the data
            lws_write_http(wsi, data, strlen(data));

            // 7. Write final "\n\n"
            lws_write_http(wsi, "\n\n", 2);
        } else {
            // No event specified, simpler format
            // 1. Write "id: "
            lws_write_http(wsi, "id: ", 4);

            // 2. Write the event ID
            lws_write_http(wsi, id_str, strlen(id_str));

            // 3. Write "\ndata: "
            lws_write_http(wsi, "\ndata: ", 7);

            // 4. Write the data
            lws_write_http(wsi, data, strlen(data));

            // 5. Write final "\n\n"
            lws_write_http(wsi, "\n\n", 2);
        }

        // Update the client's last event ID
        if (session) {
            session->last_event_id = event_id;
        }

        // Request a callback when the socket is writable again
        // This ensures that libwebsockets will flush the data
        lws_callback_on_writable(wsi);
    }
    mcp_mutex_unlock(transport_data->sse_mutex);

    return 0;
}
