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
#define MAX_SSE_CLIENTS 100

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
            "    <pre>curl -X POST http://127.0.0.1:8280/call_tool -H \"Content-Type: application/json\" -d \"{\\\"name\\\":\\\"echo\\\",\\\"params\\\":{\\\"text\\\":\\\"Hello, MCP Server!\\\"}}\"</pre>\n"
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

    // Destroy SSE mutex
    if (data->sse_mutex) {
        mcp_mutex_destroy(data->sse_mutex);
    }

    // Free transport data
    free(data);

    // Free transport
    free(transport);

    return 0;
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
        lws_service(data->context, 100); // 100ms timeout
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
        // Send error response
        char error_buf[256];
        snprintf(error_buf, sizeof(error_buf),
                 "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"Internal error\"},\"id\":null}",
                 error_code);

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

    // Add to SSE clients list
    mcp_mutex_lock(data->sse_mutex);
    if (data->sse_client_count < MAX_SSE_CLIENTS) {
        data->sse_clients[data->sse_client_count++] = wsi;

        // Get session data
        http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);
        if (session) {
            session->is_sse_client = true;
        }
    }
    mcp_mutex_unlock(data->sse_mutex);

    // Send initial SSE message
    const char* initial_msg = "data: connected\n\n";
    lws_write(wsi, (unsigned char*)initial_msg, strlen(initial_msg), LWS_WRITE_HTTP);
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

                    // For simplicity, let's just handle the request directly
                    // Create a simple JSON response
                    const char* json_response = "{\"result\":\"Hello from MCP Server!\"}";

                    // Prepare response headers
                    unsigned char buffer[LWS_PRE + 1024];
                    unsigned char* p = &buffer[LWS_PRE];
                    unsigned char* end = &buffer[sizeof(buffer) - 1];

                    // Add headers
                    if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
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
                        "    <pre>curl -X POST http://127.0.0.1:8280/call_tool -H \"Content-Type: application/json\" -d \"{\\\"name\\\":\\\"echo\\\",\\\"params\\\":{\\\"text\\\":\\\"Hello, MCP Server!\\\"}}\"</pre>\n"
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
                            if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
                                mime_type = "text/html";
                            } else if (strcasecmp(ext, "css") == 0) {
                                mime_type = "text/css";
                            } else if (strcasecmp(ext, "js") == 0) {
                                mime_type = "application/javascript";
                            } else if (strcasecmp(ext, "png") == 0) {
                                mime_type = "image/png";
                            } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
                                mime_type = "image/jpeg";
                            } else if (strcasecmp(ext, "gif") == 0) {
                                mime_type = "image/gif";
                            } else if (strcasecmp(ext, "svg") == 0) {
                                mime_type = "image/svg+xml";
                            } else if (strcasecmp(ext, "json") == 0) {
                                mime_type = "application/json";
                            }
                        }

                        int ret = lws_serve_http_file(wsi, path, mime_type, NULL, 0);
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

                // For simplicity, let's just return a simple JSON response
                const char* json_response = "{\"result\":\"Hello from MCP Server!\"}";

                // Prepare response headers
                unsigned char buffer[LWS_PRE + 1024];
                unsigned char* p = &buffer[LWS_PRE];
                unsigned char* end = &buffer[sizeof(buffer) - 1];

                // Add headers
                if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
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

    // Prepare SSE message
    char* message;
    if (event != NULL) {
        // Format: event: <event>\ndata: <data>\n\n
        message = (char*)malloc(strlen(event) + strlen(data) + 16);
        if (message == NULL) {
            return -1;
        }
        sprintf(message, "event: %s\ndata: %s\n\n", event, data);
    } else {
        // Format: data: <data>\n\n
        message = (char*)malloc(strlen(data) + 8);
        if (message == NULL) {
            return -1;
        }
        sprintf(message, "data: %s\n\n", data);
    }

    // Send to all SSE clients
    mcp_mutex_lock(transport_data->sse_mutex);
    for (int i = 0; i < transport_data->sse_client_count; i++) {
        struct lws* wsi = transport_data->sse_clients[i];
        lws_write(wsi, (unsigned char*)message, strlen(message), LWS_WRITE_HTTP);
    }
    mcp_mutex_unlock(transport_data->sse_mutex);

    // Free message
    free(message);

    return 0;
}
