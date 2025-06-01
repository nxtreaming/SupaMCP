#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#include "win_socket_compat.h"
#endif

#include "internal/http_transport_internal.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// HTTP endpoint paths
#define HTTP_ENDPOINT_EVENTS "/events"
#define HTTP_ENDPOINT_TOOLS "/tools"
#define HTTP_ENDPOINT_CALL_TOOL "/call_tool"
#define HTTP_ENDPOINT_ROOT "/"

// HTTP methods
#define HTTP_METHOD_GET "GET"
#define HTTP_METHOD_POST "POST"
#define HTTP_METHOD_OPTIONS "OPTIONS"

// HTTP content types
#define HTTP_CONTENT_TYPE_JSON "application/json"
#define HTTP_CONTENT_TYPE_TEXT "text/plain"
#define HTTP_CONTENT_TYPE_HTML "text/html"
#define HTTP_CONTENT_TYPE_SSE "text/event-stream"

// Buffer sizes
#define HTTP_HEADER_BUFFER_SIZE 1024
#define HTTP_PATH_BUFFER_SIZE 512
#define HTTP_METHOD_BUFFER_SIZE 16
#define HTTP_QUERY_BUFFER_SIZE 1024
#define HTTP_ERROR_BUFFER_SIZE 512

// Forward declarations of static functions
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
static int send_http_response(struct lws* wsi, int status_code, const char* content_type, const char* body, size_t body_len);
static int send_http_error_response(struct lws* wsi, int status_code, const char* error_message);
static int send_http_json_response(struct lws* wsi, int status_code, const char* json_body);

static char* extract_session_id_from_query(const char* query);
static char* url_decode(const char* src);
static char* extract_query_param(const char* query, const char* param_name);
static char* build_jsonrpc_request_from_query(const char* query);
static const char* get_jsonrpc_error_message(int error_code);
static int create_jsonrpc_error_response(int error_code, const char* error_message, const char* id, char* buffer, size_t buffer_size);

/**
 * @brief Helper function to set a string property in a JSON object
 *
 * @param json The JSON object
 * @param name The property name
 * @param value The string value
 * @return int 0 on success, non-zero on failure
 */
static int mcp_json_object_set_string(mcp_json_t* json, const char* name, const char* value) {
    if (json == NULL || name == NULL || value == NULL) {
        return -1;
    }

    // Create a new JSON string value
    mcp_json_t* str_value = mcp_json_string_create(value);
    if (str_value == NULL) {
        return -1;
    }

    // Set the property in the object
    int result = mcp_json_object_set_property(json, name, str_value);

    // Note: We don't need to destroy str_value here because mcp_json_object_set_property
    // takes ownership of it and will destroy it when the object is destroyed

    return result;
}

/**
 * @brief Helper function to set a number property in a JSON object
 *
 * @param json The JSON object
 * @param name The property name
 * @param value The number value
 * @return int 0 on success, non-zero on failure
 */
static int mcp_json_object_set_number(mcp_json_t* json, const char* name, double value) {
    if (json == NULL || name == NULL) {
        return -1;
    }

    // Create a new JSON number value
    mcp_json_t* num_value = mcp_json_number_create(value);
    if (num_value == NULL) {
        return -1;
    }

    // Set the property in the object
    int result = mcp_json_object_set_property(json, name, num_value);

    // Note: We don't need to destroy num_value here because mcp_json_object_set_property
    // takes ownership of it and will destroy it when the object is destroyed

    return result;
}

/**
 * @brief Main HTTP callback function for libwebsockets
 *
 * This function handles all HTTP-related callbacks from libwebsockets.
 * It routes the callbacks to appropriate handler functions based on the reason and URI.
 *
 * @param wsi WebSocket instance
 * @param reason Callback reason
 * @param user User data (session data)
 * @param in Input data
 * @param len Length of input data
 * @return int 0 on success, non-zero on failure
 */
static int lws_callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                           void* user, void* in, size_t len) {
    if (wsi == NULL) {
        mcp_log_error("Invalid WebSocket instance (NULL)");
        return -1;
    }

    // Get session data and transport data
    http_session_data_t* session = (http_session_data_t*)user;
    http_transport_data_t* data = (http_transport_data_t*)lws_context_user(lws_get_context(wsi));

    if (data == NULL) {
        mcp_log_error("Failed to get transport data from WebSocket context");
        return -1;
    }

    // Log the callback reason for debugging
    handle_http_call_reason(wsi, reason);

    // Handle the callback based on the reason
    switch (reason) {
        case LWS_CALLBACK_WSI_CREATE:
            // Initialize session data when a new WebSocket instance is created
            return handle_wsi_create(wsi, session);

        case LWS_CALLBACK_HTTP:
            {
                // This is the main HTTP request handler
                if (in == NULL) {
                    mcp_log_error("Invalid HTTP request (NULL URI)");
                    return -1;
                }

                char* uri = (char*)in;
                mcp_log_info("HTTP request: %s", uri);

                // Route the request based on the URI

                // Handle SSE requests
                if (strcmp(uri, HTTP_ENDPOINT_EVENTS) == 0) {
                    return handle_http_sse_request(wsi, data, session);
                }

                // Handle tool discovery requests
                if (strcmp(uri, HTTP_ENDPOINT_TOOLS) == 0) {
                    return handle_http_tools_request(wsi, data);
                }

                // Handle tool call requests
                if (strcmp(uri, HTTP_ENDPOINT_CALL_TOOL) == 0) {
                    // Determine the HTTP method
                    char method[HTTP_METHOD_BUFFER_SIZE] = {0};

                    // In libwebsockets, the HTTP method is determined by checking specific tokens
                    if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) > 0) {
                        strncpy(method, HTTP_METHOD_POST, sizeof(method) - 1);
                        mcp_log_info("HTTP method: POST");
                    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) > 0) {
                        strncpy(method, HTTP_METHOD_GET, sizeof(method) - 1);
                        mcp_log_info("HTTP method: GET");
                    } else if (lws_hdr_total_length(wsi, WSI_TOKEN_OPTIONS_URI) > 0) {
                        strncpy(method, HTTP_METHOD_OPTIONS, sizeof(method) - 1);
                        mcp_log_info("HTTP method: OPTIONS (CORS preflight)");
                    } else {
                        mcp_log_error("Failed to determine HTTP method");
                    }

                    return handle_http_call_tool_request(wsi, data, method);
                }

                // Handle root path requests
                if (strcmp(uri, HTTP_ENDPOINT_ROOT) == 0) {
                    return handle_http_root_request(wsi);
                }

                // Handle static file requests if doc_root is configured
                if (data->config.doc_root != NULL) {
                    return handle_http_static_file_request(wsi, data, uri);
                }

                // If no handler matched, return a 404 error
                mcp_log_warn("No handler found for URI: %s", uri);
                return handle_http_404(wsi, uri);
            }

        case LWS_CALLBACK_HTTP_BODY:
            // Handle HTTP request body data
            return handle_http_body(wsi, session, in, len);

        case LWS_CALLBACK_HTTP_BODY_COMPLETION:
            // Handle HTTP request body completion
            return handle_http_body_completion(wsi, data, session);

        case LWS_CALLBACK_CLOSED_HTTP:
            // Handle HTTP connection closure
            return handle_closed_http(wsi, data, session);

        default:
            // For other callbacks, use the default dummy handler
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
    (void)wsi;

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
    (void)wsi;

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

/**
 * @brief Helper function to send an HTTP response
 *
 * @param wsi WebSocket instance
 * @param status_code HTTP status code
 * @param content_type Content type of the response
 * @param body Response body
 * @param body_len Length of the response body
 * @return int 0 on success, non-zero on failure
 */
static int send_http_response(struct lws* wsi, int status_code, const char* content_type,
                             const char* body, size_t body_len) {
    if (wsi == NULL || content_type == NULL) {
        mcp_log_error("Invalid parameters for send_http_response");
        return -1;
    }

    // Prepare response headers
    unsigned char buffer[LWS_PRE + HTTP_HEADER_BUFFER_SIZE];
    unsigned char* p = &buffer[LWS_PRE];
    unsigned char* end = &buffer[sizeof(buffer) - 1];

    // Add common headers
    if (lws_add_http_common_headers(wsi, status_code, content_type,
                                   body ? body_len : LWS_ILLEGAL_HTTP_CONTENT_LEN,
                                   &p, end)) {
        mcp_log_error("Failed to add HTTP headers");
        return -1;
    }

    // Finalize headers
    if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
        mcp_log_error("Failed to finalize HTTP headers");
        return -1;
    }

    // Write response body if provided
    if (body != NULL && body_len > 0) {
        int bytes_written = lws_write(wsi, (unsigned char*)body, body_len, LWS_WRITE_HTTP);
        if (bytes_written < 0) {
            mcp_log_error("Failed to write HTTP response body");
            return -1;
        }
        mcp_log_debug("Wrote %d bytes of %zu total", bytes_written, body_len);
    }

    // Complete HTTP transaction
    int should_close = lws_http_transaction_completed(wsi);
    if (should_close) {
        mcp_log_debug("HTTP handler: Transaction completed, connection will close");
    }

    return 0;
}

/**
 * @brief Helper function to send an HTTP error response
 *
 * @param wsi WebSocket instance
 * @param status_code HTTP status code
 * @param error_message Error message
 * @return int 0 on success, non-zero on failure
 */
static int send_http_error_response(struct lws* wsi, int status_code, const char* error_message) {
    if (wsi == NULL || error_message == NULL) {
        mcp_log_error("Invalid parameters for send_http_error_response");
        return -1;
    }

    // Create a simple JSON error response
    char error_buf[HTTP_ERROR_BUFFER_SIZE];
    int len = snprintf(error_buf, sizeof(error_buf),
                      "{\"error\":\"%s\",\"status\":%d}",
                      error_message, status_code);

    if (len < 0 || len >= (int)sizeof(error_buf)) {
        mcp_log_error("Error buffer overflow");
        return -1;
    }

    return send_http_response(wsi, status_code, HTTP_CONTENT_TYPE_JSON, error_buf, len);
}

/**
 * @brief Helper function to send an HTTP JSON response
 *
 * @param wsi WebSocket instance
 * @param status_code HTTP status code
 * @param json_body JSON response body
 * @return int 0 on success, non-zero on failure
 */
static int send_http_json_response(struct lws* wsi, int status_code, const char* json_body) {
    if (wsi == NULL || json_body == NULL) {
        mcp_log_error("Invalid parameters for send_http_json_response");
        return -1;
    }

    return send_http_response(wsi, status_code, HTTP_CONTENT_TYPE_JSON, json_body, strlen(json_body));
}

/**
 * @brief URL decode a string
 *
 * This function decodes URL-encoded strings, handling %xx escapes and + as space.
 *
 * @param src The URL-encoded string to decode
 * @return char* The decoded string (caller must free) or NULL on failure
 */
static char* url_decode(const char* src) {
    if (src == NULL) {
        return NULL;
    }

    // Allocate memory for the decoded string (will be at most as long as the source)
    size_t src_len = strlen(src);
    char* decoded = (char*)malloc(src_len + 1);
    if (decoded == NULL) {
        mcp_log_error("Failed to allocate memory for URL decoding");
        return NULL;
    }

    // Decode the string
    const char* src_ptr = src;
    char* dst_ptr = decoded;

    while (*src_ptr) {
        if (*src_ptr == '%' && src_ptr[1] && src_ptr[2]) {
            // Handle %xx escape
            char hex[3] = {src_ptr[1], src_ptr[2], 0};
            *dst_ptr = (char)strtol(hex, NULL, 16);
            src_ptr += 3;
        } else if (*src_ptr == '+') {
            // Handle + as space
            *dst_ptr = ' ';
            src_ptr++;
        } else {
            // Copy character as-is
            *dst_ptr = *src_ptr;
            src_ptr++;
        }
        dst_ptr++;
    }

    // Null-terminate the decoded string
    *dst_ptr = '\0';

    return decoded;
}

/**
 * @brief Extract a parameter value from a query string
 *
 * This function extracts the value of a parameter from a query string.
 *
 * @param query The query string to parse
 * @param param_name The name of the parameter to extract
 * @return char* The parameter value (caller must free) or NULL if not found
 */
static char* extract_query_param(const char* query, const char* param_name) {
    if (query == NULL || param_name == NULL || *query == '\0' || *param_name == '\0') {
        return NULL;
    }

    // Build the parameter prefix (param_name=)
    size_t prefix_len = strlen(param_name);
    char* prefix = (char*)malloc(prefix_len + 2); // +2 for '=' and '\0'
    if (prefix == NULL) {
        mcp_log_error("Failed to allocate memory for parameter prefix");
        return NULL;
    }

    sprintf(prefix, "%s=", param_name);

    // Find the parameter in the query string
    const char* param_start = strstr(query, prefix);
    free(prefix); // Free the prefix buffer

    if (param_start == NULL) {
        return NULL; // Parameter not found
    }

    // Skip the parameter name and '='
    param_start += prefix_len + 1;

    // Find the end of the parameter value
    const char* param_end = strchr(param_start, '&');

    // Extract the parameter value
    if (param_end != NULL) {
        // Parameter is not the last one in the query string
        size_t value_len = param_end - param_start;
        char* value = (char*)malloc(value_len + 1);
        if (value == NULL) {
            mcp_log_error("Failed to allocate memory for parameter value");
            return NULL;
        }

        strncpy(value, param_start, value_len);
        value[value_len] = '\0';
        return value;
    } else {
        // Parameter is the last one in the query string
        return mcp_strdup(param_start);
    }
}

/**
 * @brief Build a JSON-RPC request from query parameters
 *
 * This function builds a JSON-RPC request from query parameters for tool calls.
 * It extracts the tool name and parameters from the query string and formats them
 * into a JSON-RPC request.
 *
 * Expected query parameters:
 * - name: The name of the tool to call
 * - param_<name>: Tool parameters (e.g., param_text=hello for the "text" parameter)
 *
 * @param query The query string to parse
 * @return char* The JSON-RPC request (caller must free) or NULL on failure
 */
static char* build_jsonrpc_request_from_query(const char* query) {
    if (query == NULL || *query == '\0') {
        mcp_log_error("Empty query string");
        return NULL;
    }

    // Extract the tool name
    char* tool_name = extract_query_param(query, "name");
    if (tool_name == NULL) {
        mcp_log_error("Missing 'name' parameter in query string");
        return NULL;
    }

    // URL decode the tool name
    char* decoded_tool_name = url_decode(tool_name);
    free(tool_name);

    if (decoded_tool_name == NULL) {
        mcp_log_error("Failed to decode tool name");
        return NULL;
    }

    // Create a JSON object for the tool arguments
    mcp_json_t* args_obj = mcp_json_object_create();
    if (args_obj == NULL) {
        mcp_log_error("Failed to create JSON object for tool arguments");
        free(decoded_tool_name);
        return NULL;
    }

    // Parse the query string to extract parameters
    const char* param_prefix = "param_";
    size_t param_prefix_len = strlen(param_prefix);

    // Make a copy of the query string for tokenization
    char* query_copy = mcp_strdup(query);
    if (query_copy == NULL) {
        mcp_log_error("Failed to duplicate query string");
        mcp_json_destroy(args_obj);
        free(decoded_tool_name);
        return NULL;
    }

    // Split the query string by '&'
    char* token = strtok(query_copy, "&");
    while (token != NULL) {
        // Check if this is a parameter token
        if (strncmp(token, param_prefix, param_prefix_len) == 0) {
            // Extract parameter name and value
            char* equals = strchr(token, '=');
            if (equals != NULL) {
                // Extract parameter name (after "param_" prefix)
                size_t name_len = equals - (token + param_prefix_len);
                char* param_name = (char*)malloc(name_len + 1);
                if (param_name != NULL) {
                    strncpy(param_name, token + param_prefix_len, name_len);
                    param_name[name_len] = '\0';

                    // Extract parameter value
                    char* param_value = mcp_strdup(equals + 1);
                    if (param_value != NULL) {
                        // URL decode the parameter value
                        char* decoded_value = url_decode(param_value);
                        free(param_value);

                        if (decoded_value != NULL) {
                            // Add the parameter to the arguments object
                            mcp_json_object_set_string(args_obj, param_name, decoded_value);
                            free(decoded_value);
                        }
                    }

                    free(param_name);
                }
            }
        }

        // Get the next token
        token = strtok(NULL, "&");
    }

    free(query_copy);

    // Create the JSON-RPC request
    mcp_json_t* request_obj = mcp_json_object_create();
    if (request_obj == NULL) {
        mcp_log_error("Failed to create JSON object for request");
        mcp_json_destroy(args_obj);
        free(decoded_tool_name);
        return NULL;
    }

    // Add the JSON-RPC version
    mcp_json_object_set_string(request_obj, "jsonrpc", "2.0");

    // Add the request ID (use 1 as a default)
    mcp_json_object_set_number(request_obj, "id", 1);

    // Add the method
    mcp_json_object_set_string(request_obj, "method", "call_tool");

    // Create the params object
    mcp_json_t* params_obj = mcp_json_object_create();
    if (params_obj == NULL) {
        mcp_log_error("Failed to create JSON object for params");
        mcp_json_destroy(request_obj);
        mcp_json_destroy(args_obj);
        free(decoded_tool_name);
        return NULL;
    }

    // Add the tool name to the params
    mcp_json_object_set_string(params_obj, "name", decoded_tool_name);

    // Add the arguments to the params
    mcp_json_object_set_property(params_obj, "arguments", args_obj);

    // Add the params to the request
    mcp_json_object_set_property(request_obj, "params", params_obj);

    // Convert the request object to a string
    char* request_json = mcp_json_stringify(request_obj);

    // Clean up
    mcp_json_destroy(request_obj); // This will also destroy params_obj and args_obj
    free(decoded_tool_name);

    return request_json;
}

/**
 * @brief Extract session ID from query string
 *
 * @param query Query string
 * @return char* Extracted session ID or NULL if not found (caller must free)
 */
static char* extract_session_id_from_query(const char* query) {
    if (query == NULL || *query == '\0') {
        return NULL;
    }

    // Look for session_id parameter
    const char* session_id_param = strstr(query, "session_id=");
    if (session_id_param == NULL) {
        return NULL;
    }

    // Skip "session_id="
    session_id_param += 11;

    // Find the end of the parameter value
    const char* end_param = strchr(session_id_param, '&');
    if (end_param != NULL) {
        // Allocate memory for the session ID
        size_t param_len = end_param - session_id_param;
        char* session_id_str = (char*)malloc(param_len + 1);
        if (session_id_str != NULL) {
            strncpy(session_id_str, session_id_param, param_len);
            session_id_str[param_len] = '\0';
            return session_id_str;
        }
    } else {
        // Session ID is the last parameter
        return mcp_strdup(session_id_param);
    }

    return NULL;
}

/**
 * @brief Handle HTTP_BODY callback
 *
 * This function accumulates the HTTP request body data.
 *
 * @param wsi WebSocket instance
 * @param session Session data
 * @param in Input data
 * @param len Length of input data
 * @return int 0 on success, non-zero on failure
 */
static int handle_http_body(struct lws* wsi, http_session_data_t* session, void* in, size_t len) {
    (void)wsi;

    if (session == NULL || in == NULL || len == 0) {
        mcp_log_error("Invalid parameters for handle_http_body");
        return -1;
    }

    mcp_log_debug("Received HTTP body chunk: %zu bytes", len);

    // Accumulate request body
    if (session->request_buffer == NULL) {
        // First chunk - allocate new buffer
        session->request_buffer = (char*)malloc(len + 1);
        if (session->request_buffer == NULL) {
            mcp_log_error("Failed to allocate memory for request buffer");
            return -1;
        }

        // Copy data and null-terminate
        memcpy(session->request_buffer, in, len);
        session->request_buffer[len] = '\0';
        session->request_len = len;

        mcp_log_debug("Created new request buffer: %zu bytes", len);
    } else {
        // Subsequent chunk - expand existing buffer
        char* new_buffer = (char*)realloc(session->request_buffer,
                                         session->request_len + len + 1);
        if (new_buffer == NULL) {
            mcp_log_error("Failed to reallocate memory for request buffer");
            free(session->request_buffer);
            session->request_buffer = NULL;
            session->request_len = 0;
            return -1;
        }

        // Update buffer pointer, append data, and null-terminate
        session->request_buffer = new_buffer;
        memcpy(session->request_buffer + session->request_len, in, len);
        session->request_len += len;
        session->request_buffer[session->request_len] = '\0';

        mcp_log_debug("Expanded request buffer: %zu bytes total", session->request_len);
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
        "        <h2>Tool Call Examples:</h2>\n"
        "        <h3>Using POST with curl:</h3>\n"
        "        <pre>curl -X POST http://127.0.0.1:8180/call_tool \\\n"
        "     -H \"Content-Type: application/json\" \\\n"
        "     -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"call_tool\",\"params\":{\"name\":\"echo\",\"arguments\":{\"text\":\"Hello, MCP Server!\"}}}'\n"
        "</pre>\n"
        "        <h3>Using GET with curl:</h3>\n"
        "        <pre>curl \"http://127.0.0.1:8180/call_tool?name=echo&param_text=Hello%2C%20MCP%20Server%21\"</pre>\n"
        "        <h3>Using JavaScript (POST):</h3>\n"
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
        "        <h3>Using JavaScript (GET):</h3>\n"
        "        <pre>fetch('/call_tool?name=echo&param_text=Hello%2C%20MCP%20Server%21')\n"
        "    .then(response => response.json())\n"
        "    .then(data => console.log(data));</pre>\n"
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
    // Handle GET request
    else if (method[0] != '\0' && strcmp(method, "GET") == 0) {
        mcp_log_info("Processing GET request for tool call");

        // Get query string
        char query[HTTP_QUERY_BUFFER_SIZE] = {0};
        int query_len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);

        if (query_len <= 0 || query_len >= (int)sizeof(query)) {
            mcp_log_error("Missing or invalid query parameters for GET tool call");
            return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Missing or invalid query parameters");
        }

        // Copy query string
        if (lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS) < 0) {
            mcp_log_error("Failed to copy query string");
            return send_http_error_response(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to process query parameters");
        }

        mcp_log_debug("Tool call query string: '%s'", query);

        // Build JSON-RPC request from query parameters
        char* request_json = build_jsonrpc_request_from_query(query);
        if (!request_json) {
            mcp_log_error("Failed to build JSON-RPC request from query parameters");
            return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Invalid tool call parameters");
        }

        // Process the request using the message callback
        if (data->message_callback != NULL) {
            int error_code = 0;
            char* response = data->message_callback(data->callback_user_data,
                                                  request_json,
                                                  strlen(request_json),
                                                  &error_code);

            // Free the request JSON
            free(request_json);

            // Handle successful response
            if (response != NULL) {
                mcp_log_debug("Message callback returned response: %zu bytes", strlen(response));

                // Send the response
                int result = send_http_json_response(wsi, HTTP_STATUS_OK, response);

                // Free the response
                free(response);

                return result;
            }
            // Handle error response
            else {
                mcp_log_error("Message callback returned error: %d", error_code);

                // Get error message for the error code
                const char* error_message = get_jsonrpc_error_message(error_code);

                // Create JSON-RPC error response
                char error_buf[HTTP_ERROR_BUFFER_SIZE];
                int len = create_jsonrpc_error_response(error_code, error_message, NULL,
                                                      error_buf, sizeof(error_buf));

                if (len < 0) {
                    mcp_log_error("Failed to create JSON-RPC error response");
                    return -1;
                }

                // Send error response
                int status_code = (error_code == -32602 || error_code == -32600) ?
                                 HTTP_STATUS_BAD_REQUEST : HTTP_STATUS_INTERNAL_SERVER_ERROR;

                return send_http_response(wsi, status_code, HTTP_CONTENT_TYPE_JSON,
                                         error_buf, len);
            }
        }
        // No message callback registered
        else {
            mcp_log_error("No message callback registered");
            free(request_json);
            return send_http_error_response(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                           "No message handler registered");
        }
    }
    // Handle other methods
    else {
        // For non-POST/GET/OPTIONS requests, return a simple JSON response
        const char* json_response = "{\"error\":\"Method not allowed. Use GET or POST for tool calls or OPTIONS for preflight.\"}";

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

/**
 * @brief Handle SSE (Server-Sent Events) request
 *
 * This function processes an SSE request, extracts the session ID from the query string,
 * and sets up the SSE connection.
 *
 * @param wsi WebSocket instance
 * @param data Transport data
 * @param session Session data
 * @return int 0 on success, non-zero on failure
 */
static int handle_http_sse_request(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
    if (wsi == NULL || data == NULL || session == NULL) {
        mcp_log_error("Invalid parameters for handle_http_sse_request");
        return -1;
    }

    mcp_log_info("Handling SSE request");

    // Extract session_id from query string
    char query[HTTP_QUERY_BUFFER_SIZE] = {0};
    int query_len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_URI_ARGS);

    if (query_len > 0 && query_len < (int)sizeof(query)) {
        // Copy query string
        if (lws_hdr_copy(wsi, query, sizeof(query), WSI_TOKEN_HTTP_URI_ARGS) < 0) {
            mcp_log_error("Failed to copy query string");
            return -1;
        }

        mcp_log_debug("SSE request query string: '%s'", query);

        // Free any existing session_id
        if (session->session_id != NULL) {
            free(session->session_id);
            session->session_id = NULL;
        }

        // Extract session_id from query string
        session->session_id = extract_session_id_from_query(query);

        if (session->session_id != NULL) {
            mcp_log_info("SSE client connected with session ID: %s", session->session_id);
        } else {
            mcp_log_debug("SSE client connected without session ID");
        }

        // Extract other parameters if needed (e.g., event_filter)
        // TODO: Add support for event filtering
    } else {
        mcp_log_debug("SSE request has no query string (len=%d)", query_len);
    }

    // Set up the SSE connection
    handle_sse_request(wsi, data);
    return 0;
}

/**
 * @brief Get JSON-RPC error message for a given error code
 *
 * @param error_code JSON-RPC error code
 * @return const char* Error message
 */
static const char* get_jsonrpc_error_message(int error_code) {
    switch (error_code) {
        case -32700: return "Parse error";
        case -32600: return "Invalid request";
        case -32601: return "Method not found";
        case -32602: return "Invalid params";
        case -32603: return "Internal error";
        default:
            if (error_code <= -32000 && error_code >= -32099) {
                return "Server error";
            }
            return "Internal server error";
    }
}

/**
 * @brief Create a JSON-RPC error response
 *
 * @param error_code JSON-RPC error code
 * @param error_message Error message
 * @param id Request ID (can be NULL)
 * @param buffer Buffer to store the response
 * @param buffer_size Size of the buffer
 * @return int Length of the response or -1 on failure
 */
static int create_jsonrpc_error_response(int error_code, const char* error_message,
                                        const char* id, char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    int len;
    if (id != NULL) {
        len = snprintf(buffer, buffer_size,
                      "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":%s}",
                      error_code, error_message, id);
    } else {
        len = snprintf(buffer, buffer_size,
                      "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":%d,\"message\":\"%s\"},\"id\":null}",
                      error_code, error_message);
    }

    if (len < 0 || (size_t)len >= buffer_size) {
        return -1;
    }

    return len;
}

/**
 * @brief Handle HTTP_BODY_COMPLETION callback
 *
 * This function processes the complete HTTP request body and generates a response.
 *
 * @param wsi WebSocket instance
 * @param data Transport data
 * @param session Session data
 * @return int 0 on success, non-zero on failure
 */
static int handle_http_body_completion(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
    if (wsi == NULL || data == NULL || session == NULL) {
        mcp_log_error("Invalid parameters for handle_http_body_completion");
        return -1;
    }

    mcp_log_info("HTTP body completion");

    // Check if we have a request buffer
    if (session->request_buffer == NULL || session->request_len == 0) {
        mcp_log_error("No request buffer or empty request");
        return send_http_error_response(wsi, HTTP_STATUS_BAD_REQUEST, "Empty request");
    }

    mcp_log_debug("Processing request body: %zu bytes", session->request_len);

    // Process the request using the message callback
    if (data->message_callback != NULL) {
        int error_code = 0;
        char* response = data->message_callback(data->callback_user_data,
                                              session->request_buffer,
                                              session->request_len,
                                              &error_code);

        // Handle successful response
        if (response != NULL) {
            mcp_log_debug("Message callback returned response: %zu bytes", strlen(response));

            // Write response body in chunks if it's too large
            size_t response_len = strlen(response);
            size_t chunk_size = 4096; // 4KB chunks
            int result = 0;

            // Prepare response headers
            unsigned char buffer[LWS_PRE + HTTP_HEADER_BUFFER_SIZE];
            unsigned char* p = &buffer[LWS_PRE];
            unsigned char* end = &buffer[sizeof(buffer) - 1];

            // Add headers
            if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                          HTTP_CONTENT_TYPE_JSON,
                                          response_len, &p, end)) {
                mcp_log_error("Failed to add HTTP headers");
                free(response);
                return -1;
            }

            if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end)) {
                mcp_log_error("Failed to finalize HTTP headers");
                free(response);
                return -1;
            }

            // Write response body in chunks
            size_t offset = 0;
            int bytes_written = 0;

            while (offset < response_len) {
                size_t current_chunk_size = (response_len - offset < chunk_size) ?
                                           response_len - offset : chunk_size;

                result = lws_write(wsi, (unsigned char*)(response + offset),
                                  current_chunk_size, LWS_WRITE_HTTP);

                if (result < 0) {
                    mcp_log_error("Failed to write response chunk: %d", result);
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

            // Complete HTTP transaction
            int should_close = lws_http_transaction_completed(wsi);
            if (should_close) {
                mcp_log_debug("HTTP handler: Transaction completed, connection will close");
            }
            return 0;
        }
        // Handle error response
        else {
            mcp_log_error("Message callback returned error: %d", error_code);

            // Get error message for the error code
            const char* error_message = get_jsonrpc_error_message(error_code);

            // Create JSON-RPC error response
            char error_buf[HTTP_ERROR_BUFFER_SIZE];
            int len = create_jsonrpc_error_response(error_code, error_message, NULL,
                                                  error_buf, sizeof(error_buf));

            if (len < 0) {
                mcp_log_error("Failed to create JSON-RPC error response");
                return -1;
            }

            // Send error response
            int status_code = (error_code == -32602 || error_code == -32600) ?
                             HTTP_STATUS_BAD_REQUEST : HTTP_STATUS_INTERNAL_SERVER_ERROR;

            return send_http_response(wsi, status_code, HTTP_CONTENT_TYPE_JSON,
                                     error_buf, len);
        }
    }
    // No message callback registered
    else {
        mcp_log_error("No message callback registered");
        return send_http_error_response(wsi, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                       "No message handler registered");
    }
}

/**
 * @brief Handle CLOSED_HTTP callback
 *
 * This function cleans up resources when an HTTP connection is closed.
 *
 * @param wsi WebSocket instance
 * @param data Transport data
 * @param session Session data
 * @return int 0 on success, non-zero on failure
 */
static int handle_closed_http(struct lws* wsi, http_transport_data_t* data, http_session_data_t* session) {
    if (wsi == NULL || data == NULL || session == NULL) {
        mcp_log_error("Invalid parameters for handle_closed_http");
        return -1;
    }

    mcp_log_debug("HTTP connection closed");

    // Clean up request buffer
    if (session->request_buffer != NULL) {
        free(session->request_buffer);
        session->request_buffer = NULL;
        session->request_len = 0;
    }

    // Free event filter if set
    if (session->event_filter != NULL) {
        free(session->event_filter);
        session->event_filter = NULL;
    }

    // Free session ID if set
    if (session->session_id != NULL) {
        free(session->session_id);
        session->session_id = NULL;
    }

    // Remove from SSE clients if this was an SSE client
    if (session->is_sse_client) {
        mcp_log_info("SSE client disconnected");

        // Lock the SSE mutex to safely modify the client list
        mcp_mutex_lock(data->sse_mutex);

        // Find the client in the list
        int client_index = -1;
        for (int i = 0; i < data->sse_client_count; i++) {
            if (data->sse_clients[i] == wsi) {
                client_index = i;
                break;
            }
        }

        // Remove the client if found
        if (client_index >= 0) {
            // Remove by shifting remaining clients
            for (int j = client_index; j < data->sse_client_count - 1; j++) {
                data->sse_clients[j] = data->sse_clients[j + 1];
            }

            // Decrement the client count
            data->sse_client_count--;

            mcp_log_info("SSE client removed from list, %d clients remaining", data->sse_client_count);
        } else {
            mcp_log_warn("SSE client not found in client list");
        }

        // Unlock the SSE mutex
        mcp_mutex_unlock(data->sse_mutex);

        // Reset the SSE client flag
        session->is_sse_client = false;
    }

    return 0;
}