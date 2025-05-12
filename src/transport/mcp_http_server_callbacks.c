#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include <win_socket_compat.h>

// On Windows, strcasecmp is _stricmp
#define strcasecmp _stricmp
#endif

#include "internal/http_transport_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void handle_http_call_reason(struct lws* wsi, enum lws_callback_reasons reason);
static int handle_wsi_create(struct lws* wsi, http_session_data_t* session);
static int handle_http_sse_request(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session);
static int handle_http_tools_request(struct lws* wsi, http_transport_data_t* data);
static int handle_http_call_tool_request(struct lws* wsi, http_transport_data_t* data, const char* method);
static int handle_http_root_request(struct lws* wsi);
static int handle_http_static_file_request(struct lws* wsi, http_transport_data_t* data, const char* uri);
static int handle_http_404(struct lws* wsi, const char* uri);
static int handle_http_body(struct lws* wsi, http_session_data_t* session, void* in, size_t len);
static int handle_http_body_completion(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session);
static int handle_closed_http(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session);

// LWS callback function
static int lws_callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                    void* user, void* in, size_t len) {
    http_session_data_t* session = (http_session_data_t*)user;
    http_transport_data_t* data = (http_transport_data_t*)lws_context_user(lws_get_context(wsi));

    handle_http_call_reason(wsi, reason);

    switch (reason) {
        case LWS_CALLBACK_WSI_CREATE:
            return handle_wsi_create(wsi, session);

        case LWS_CALLBACK_HTTP:
            {
                char* uri = (char*)in;
                mcp_log_info("HTTP request: %s", uri);

                // Check if this is an SSE request
                if (strcmp(uri, "/events") == 0) {
                    return handle_http_sse_request(wsi, data, session);
                }

                // Check if this is a tool discovery request
                if (strcmp(uri, "/tools") == 0) {
                    return handle_http_tools_request(wsi, data);
                }

                // Check if this is a tool call
                if (strcmp(uri, "/call_tool") == 0) {
                    // Check the request method
                    char method[16] = {0};

                    // Try to get the request method
                    // In libwebsockets, the HTTP method is part of the URI
                    if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0) {
                        strcpy(method, "POST");
                        mcp_log_info("HTTP method: POST");
                    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) > 0) {
                        strcpy(method, "GET");
                        mcp_log_info("HTTP method: GET");
                    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_OPTIONS_URI) > 0) {
                        strcpy(method, "OPTIONS");
                        mcp_log_info("HTTP method: OPTIONS (CORS preflight)");
                    } else {
                        mcp_log_error("Failed to determine HTTP method");
                    }

                    return handle_http_call_tool_request(wsi, data, method);
                }

                // For the root path, return a simple HTML page
                if (strcmp(uri, "/") == 0) {
                    return handle_http_root_request(wsi);
                }

                // For other requests, try to serve static files if doc_root is set
                if (data->config.doc_root) {
                    return handle_http_static_file_request(wsi, data, uri);
                }

                // If we get here, return a 404 error
                return handle_http_404(wsi, uri);
            }

        case LWS_CALLBACK_HTTP_BODY:
            return handle_http_body(wsi, session, in, len);

        case LWS_CALLBACK_HTTP_BODY_COMPLETION:
            return handle_http_body_completion(wsi, data, session);

        case LWS_CALLBACK_CLOSED_HTTP:
            return handle_closed_http(wsi, data, session);

        default:
            return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
}

// LWS protocols
struct lws_protocols http_protocols[] = {
    {
        "http-server",
        lws_callback_http,
        sizeof(http_session_data_t),
        0,  // rx buffer size (0 = default)
    },
    {
        "http-root",
        lws_root_handler,
        0,  // user data size
        0,  // rx buffer size (0 = default)
    },
    { NULL, NULL, 0, 0 } // terminator
};

static void handle_http_call_reason(struct lws* wsi, enum lws_callback_reasons reason) {
    (void)wsi; // Unused parameter

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
}

// Handle WSI_CREATE callback
static int handle_wsi_create(struct lws* wsi, http_session_data_t* session) {
    (void)wsi; // Unused parameter

    // Initialize session data
    if (session) {
        session->request_buffer = NULL;
        session->request_len = 0;
        session->is_sse_client = false;
        session->last_event_id = 0;
        session->event_filter = NULL;
        session->session_id = NULL;
        mcp_log_debug("Session data initialized");
    }
    return 0;
}

// Handle HTTP_BODY callback
static int handle_http_body(struct lws* wsi, http_session_data_t* session, void* in, size_t len) {
    (void)wsi; // Unused parameter

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

// Handle root path request
static int handle_http_root_request(struct lws* wsi) {
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
        "            <li><a href=\"/tools\"><code>/tools</code></a> - Tool discovery API (returns available tools)</li>\n"
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

// Handle tool call request
static int handle_http_call_tool_request(struct lws* wsi, http_transport_data_t* data, const char* method) {
    mcp_log_info("Handling tool call request");

    // Handle OPTIONS request (CORS preflight)
    if (method[0] != '\0' && strcmp(method, "OPTIONS") == 0) {
        // Prepare response headers for OPTIONS request
        unsigned char buffer[LWS_PRE + 1024];
        unsigned char* p = &buffer[LWS_PRE];
        unsigned char* end = &buffer[sizeof(buffer) - 1];

        // Add common headers
        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                      "text/plain",
                                      0, &p, end)) {
            mcp_log_error("Failed to add HTTP headers for OPTIONS");
            return -1;
        }

        // Add CORS headers
        if (add_cors_headers(wsi, data, &p, end) != 0) {
            mcp_log_error("Failed to add CORS headers for OPTIONS");
            return -1;
        }

        if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
            mcp_log_error("Failed to finalize HTTP headers for OPTIONS");
            return -1;
        }

        // Complete HTTP transaction (no body for OPTIONS)
        lws_http_transaction_completed(wsi);
        return 0;
    }
    // Handle POST request
    else if (method[0] != '\0' && strcmp(method, "POST") == 0) {
        // This is a POST request, wait for body
        mcp_log_info("Waiting for POST body");
        return 0;
    }
    // Handle other methods (GET, etc.)
    else {
        // For non-POST/OPTIONS requests, return a simple JSON response
        const char* json_response = "{\"error\":\"Method not allowed. Use POST for tool calls or OPTIONS for preflight.\"}";

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

        // Add CORS headers
        if (add_cors_headers(wsi, data, &p, end) != 0) {
            mcp_log_error("Failed to add CORS headers");
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

// Handle tools discovery request
static int handle_http_tools_request(struct lws* wsi, http_transport_data_t* data) {
    mcp_log_info("Handling tool discovery request");

    // Prepare a simple JSON response with tool information
    // In a real implementation, this would query the server for available tools
    const char* tools_json = "{\n"
        "  \"tools\": [\n"
        "    {\n"
        "      \"name\": \"echo\",\n"
        "      \"description\": \"Echoes back the input text\",\n"
        "      \"parameters\": {\n"
        "        \"text\": {\n"
        "          \"type\": \"string\",\n"
        "          \"description\": \"Text to echo\",\n"
        "          \"required\": true\n"
        "        }\n"
        "      }\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"reverse\",\n"
        "      \"description\": \"Reverses the input text\",\n"
        "      \"parameters\": {\n"
        "        \"text\": {\n"
        "          \"type\": \"string\",\n"
        "          \"description\": \"Text to reverse\",\n"
        "          \"required\": true\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}";

    // Prepare response headers
    unsigned char buffer[LWS_PRE + 1024];
    unsigned char* p = &buffer[LWS_PRE];
    unsigned char* end = &buffer[sizeof(buffer) - 1];

    // Add headers
    if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                  "application/json",
                                  strlen(tools_json), &p, end)) {
        mcp_log_error("Failed to add HTTP headers for tool discovery");
        return -1;
    }

    // Add CORS headers
    if (add_cors_headers(wsi, data, &p, end) != 0) {
        mcp_log_error("Failed to add CORS headers for tool discovery");
        return -1;
    }

    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
        mcp_log_error("Failed to finalize HTTP headers for tool discovery");
        return -1;
    }

    // Write response body
    int bytes_written = lws_write(wsi, (unsigned char*)tools_json, strlen(tools_json), LWS_WRITE_HTTP);
    mcp_log_info("Wrote %d bytes for tool discovery", bytes_written);

    // Complete HTTP transaction
    lws_http_transaction_completed(wsi);
    return 0;
}

// Handle static file request
static int handle_http_static_file_request(struct lws* wsi, http_transport_data_t* data, const char* uri) {
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

    // File not found, return -1 to indicate failure
    return -1;
}

// Handle 404 error
static int handle_http_404(struct lws* wsi, const char* uri) {
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

// Handle SSE request
static int handle_http_sse_request(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
    mcp_log_info("Handling SSE request");

    // Extract session_id from query string
    char query[256] = {0};
    int query_len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);
    if (query_len > 0 && query_len < (int)sizeof(query)) {
        lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS);
        mcp_log_debug("SSE request query string: '%s'", query);

        // Extract and store session_id directly in the HTTP callback
        if (session) {
            // Free any existing session_id
            if (session->session_id) {
                free(session->session_id);
                session->session_id = NULL;
            }

            // Extract session_id from query string
            char* session_id_param = strstr(query, "session_id=");
            if (session_id_param) {
                session_id_param += 11; // Skip "session_id="

                // Find the end of the parameter value
                char* end_param = strchr(session_id_param, '&');
                if (end_param) {
                    // Allocate memory for the session ID
                    size_t param_len = end_param - session_id_param;
                    char* session_id_str = (char*)malloc(param_len + 1);
                    if (session_id_str) {
                        strncpy(session_id_str, session_id_param, param_len);
                        session_id_str[param_len] = '\0';
                        session->session_id = session_id_str;
                    }
                } else {
                    // Session ID is the last parameter
                    session->session_id = mcp_strdup(session_id_param);
                }

                mcp_log_info("SSE client connected with session ID: %s",
                           session->session_id ? session->session_id : "NULL");
            }
        }
    } else {
        mcp_log_debug("SSE request has no query string (len=%d)", query_len);
    }

    handle_sse_request(wsi, data);
    return 0;
}

// Handle HTTP_BODY_COMPLETION callback
static int handle_http_body_completion(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
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

            // Write response body in chunks if it's too large
            size_t response_len = strlen(response);
            size_t chunk_size = 4096; // 4KB chunks
            size_t offset = 0;
            int bytes_written = 0;

            while (offset < response_len) {
                size_t current_chunk_size = (response_len - offset < chunk_size) ? response_len - offset : chunk_size;
                int result = lws_write(wsi, (unsigned char*)(response + offset), current_chunk_size, LWS_WRITE_HTTP);

                if (result < 0) {
                    mcp_log_error("Failed to write response chunk: %d, %s", result, response);
                    break;
                }

                bytes_written += result;
                offset += current_chunk_size;

                // Request a callback when the socket is writable again
                if (offset < response_len) {
                    lws_callback_on_writable(wsi);
                }
            }

            mcp_log_info("Wrote %d bytes of %zu total", bytes_written, response_len);

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

// Handle CLOSED_HTTP callback
static int handle_closed_http(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
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

    // Free session ID if set
    if (session->session_id) {
        free(session->session_id);
        session->session_id = NULL;
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