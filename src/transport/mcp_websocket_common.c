#include "internal/websocket_common.h"
#include "internal/websocket_client_internal.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

// Initialize WebSocket protocols array
void mcp_websocket_init_protocols(
    struct lws_protocols* protocols,
    lws_callback_function* callback
) {
    if (!protocols || !callback) {
        return;
    }

    // WebSocket protocol
    protocols[0].name = "mcp-protocol";
    protocols[0].callback = callback;
    protocols[0].per_session_data_size = 0;
    protocols[0].rx_buffer_size = WS_DEFAULT_BUFFER_SIZE;
    protocols[0].id = 0;
    protocols[0].user = NULL;
    protocols[0].tx_packet_size = 0;

    // HTTP protocol for handshake
    protocols[1].name = "http-only";
    protocols[1].callback = callback;
    protocols[1].per_session_data_size = 0;
    protocols[1].rx_buffer_size = WS_DEFAULT_BUFFER_SIZE;
    protocols[1].id = 0;
    protocols[1].user = NULL;
    protocols[1].tx_packet_size = 0;

    // Terminator entry
    protocols[2].name = NULL;
    protocols[2].callback = NULL;
    protocols[2].per_session_data_size = 0;
    protocols[2].rx_buffer_size = 0;
    protocols[2].id = 0;
    protocols[2].user = NULL;
    protocols[2].tx_packet_size = 0;
}

// Create a libwebsockets context for client or server
struct lws_context* mcp_websocket_create_context(
    const char* host,
    uint16_t port,
    const char* path,
    const struct lws_protocols* protocols,
    void* user_data,
    bool is_server,
    bool use_ssl,
    const char* cert_path,
    const char* key_path
) {
    // Suppress unused parameter warning
    (void)path;
    struct lws_context_creation_info info = {0};

    // Common settings
    info.port = is_server ? port : CONTEXT_PORT_NO_LISTEN;
    info.iface = is_server ? host : NULL;
    info.protocols = protocols;
    info.user = user_data;
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE |
                   LWS_SERVER_OPTION_VALIDATE_UTF8 |
                   LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Client-specific settings
    if (!is_server) {
        // Disable keep-alive during connection setup
        info.ka_time = 0;
        info.ka_interval = 0;
        info.ka_probes = 0;

        // Get connection timeout from client configuration
        uint32_t connect_timeout_ms = 0;
        if (user_data) {
            ws_client_data_t* client_data = (ws_client_data_t*)user_data;
            if (client_data && client_data->config.connect_timeout_ms > 0) {
                connect_timeout_ms = client_data->config.connect_timeout_ms;
                mcp_log_info("Using custom connection timeout: %u ms", connect_timeout_ms);
            }
        }

        // Set connection timeout (convert from ms to seconds)
        if (connect_timeout_ms > 0) {
            info.timeout_secs = (connect_timeout_ms / 1000) > 0 ? (connect_timeout_ms / 1000) : 1;
            info.connect_timeout_secs = info.timeout_secs;
        } else {
            info.timeout_secs = 5; // Default timeout (5 seconds)
        }

        mcp_log_info("WebSocket connection timeout set to %d seconds", info.timeout_secs);
        info.retry_and_idle_policy = NULL; // Disable automatic reconnection
    }

    // Server-specific settings
    if (is_server) {
        static const struct lws_http_mount mount = {
            .mountpoint = "/ws",
            .mountpoint_len = 3,
            .origin = "http://localhost",
            .origin_protocol = LWSMPRO_CALLBACK,
            .mount_next = NULL
        };
        info.mounts = &mount;

        // Optimize server behavior
        info.timeout_secs = 1; // Reduce timeout (default is 5)
        info.keepalive_timeout = 1; // Reduce keepalive timeout
        info.options |= LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    }

    // SSL settings
    if (use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.ssl_cert_filepath = cert_path;
        info.ssl_private_key_filepath = key_path;
    }

    // Create context
    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        mcp_log_error("Failed to create WebSocket context");
        return NULL;
    }

    return context;
}

// Calculate total size of multiple buffers
size_t mcp_websocket_calculate_total_size(
    const mcp_buffer_t* buffers,
    size_t buffer_count
) {
    if (!buffers || buffer_count == 0) {
        return 0;
    }

    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        total_size += buffers[i].size;
    }

    return total_size;
}

// Combine multiple buffers into a single buffer
int mcp_websocket_combine_buffers(
    const mcp_buffer_t* buffers,
    size_t buffer_count,
    void* combined_buffer,
    size_t combined_size
) {
    if (!buffers || buffer_count == 0 || !combined_buffer || combined_size == 0) {
        return -1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        if (offset + buffers[i].size > combined_size) {
            return -1; // Buffer overflow
        }
        memcpy((char*)combined_buffer + offset, buffers[i].data, buffers[i].size);
        offset += buffers[i].size;
    }

    return 0;
}

// Get a human-readable string for a libwebsockets callback reason
const char* websocket_get_callback_reason_string(enum lws_callback_reasons reason) {
    // Provide our own mapping of reason codes to strings
    switch (reason) {
        // Server callbacks
        case LWS_CALLBACK_ESTABLISHED: return "LWS_CALLBACK_ESTABLISHED";
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: return "LWS_CALLBACK_CLIENT_CONNECTION_ERROR";
        case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: return "LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH";
        case LWS_CALLBACK_CLIENT_ESTABLISHED: return "LWS_CALLBACK_CLIENT_ESTABLISHED";
        case LWS_CALLBACK_CLOSED: return "LWS_CALLBACK_CLOSED";
        case LWS_CALLBACK_CLOSED_HTTP: return "LWS_CALLBACK_CLOSED_HTTP";
        case LWS_CALLBACK_RECEIVE: return "LWS_CALLBACK_RECEIVE";
        case LWS_CALLBACK_RECEIVE_PONG: return "LWS_CALLBACK_RECEIVE_PONG";
        case LWS_CALLBACK_CLIENT_RECEIVE: return "LWS_CALLBACK_CLIENT_RECEIVE";
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG: return "LWS_CALLBACK_CLIENT_RECEIVE_PONG";
        case LWS_CALLBACK_CLIENT_WRITEABLE: return "LWS_CALLBACK_CLIENT_WRITEABLE";
        case LWS_CALLBACK_SERVER_WRITEABLE: return "LWS_CALLBACK_SERVER_WRITEABLE";
        case LWS_CALLBACK_HTTP: return "LWS_CALLBACK_HTTP";
        case LWS_CALLBACK_HTTP_BODY: return "LWS_CALLBACK_HTTP_BODY";
        case LWS_CALLBACK_HTTP_BODY_COMPLETION: return "LWS_CALLBACK_HTTP_BODY_COMPLETION";
        case LWS_CALLBACK_HTTP_FILE_COMPLETION: return "LWS_CALLBACK_HTTP_FILE_COMPLETION";
        case LWS_CALLBACK_HTTP_WRITEABLE: return "LWS_CALLBACK_HTTP_WRITEABLE";
        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: return "LWS_CALLBACK_FILTER_NETWORK_CONNECTION";
        case LWS_CALLBACK_FILTER_HTTP_CONNECTION: return "LWS_CALLBACK_FILTER_HTTP_CONNECTION";
        case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: return "LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED";
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: return "LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION";
        case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: return "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS";
        case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS: return "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS";
        case LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION: return "LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION";
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: return "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER";
        case LWS_CALLBACK_CONFIRM_EXTENSION_OKAY: return "LWS_CALLBACK_CONFIRM_EXTENSION_OKAY";
        case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED: return "LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED";
        case LWS_CALLBACK_PROTOCOL_INIT: return "LWS_CALLBACK_PROTOCOL_INIT";
        case LWS_CALLBACK_PROTOCOL_DESTROY: return "LWS_CALLBACK_PROTOCOL_DESTROY";
        case LWS_CALLBACK_WSI_CREATE: return "LWS_CALLBACK_WSI_CREATE";
        case LWS_CALLBACK_WSI_DESTROY: return "LWS_CALLBACK_WSI_DESTROY";
        case LWS_CALLBACK_GET_THREAD_ID: return "LWS_CALLBACK_GET_THREAD_ID";
        case LWS_CALLBACK_ADD_POLL_FD: return "LWS_CALLBACK_ADD_POLL_FD";
        case LWS_CALLBACK_DEL_POLL_FD: return "LWS_CALLBACK_DEL_POLL_FD";
        case LWS_CALLBACK_CHANGE_MODE_POLL_FD: return "LWS_CALLBACK_CHANGE_MODE_POLL_FD";
        case LWS_CALLBACK_LOCK_POLL: return "LWS_CALLBACK_LOCK_POLL";
        case LWS_CALLBACK_UNLOCK_POLL: return "LWS_CALLBACK_UNLOCK_POLL";
        case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE: return "LWS_CALLBACK_WS_PEER_INITIATED_CLOSE";
        case LWS_CALLBACK_WS_EXT_DEFAULTS: return "LWS_CALLBACK_WS_EXT_DEFAULTS";
        case LWS_CALLBACK_CGI: return "LWS_CALLBACK_CGI";
        case LWS_CALLBACK_CGI_TERMINATED: return "LWS_CALLBACK_CGI_TERMINATED";
        case LWS_CALLBACK_CGI_STDIN_DATA: return "LWS_CALLBACK_CGI_STDIN_DATA";
        case LWS_CALLBACK_CGI_STDIN_COMPLETED: return "LWS_CALLBACK_CGI_STDIN_COMPLETED";
        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: return "LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP";
        case LWS_CALLBACK_CLOSED_CLIENT_HTTP: return "LWS_CALLBACK_CLOSED_CLIENT_HTTP";
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: return "LWS_CALLBACK_RECEIVE_CLIENT_HTTP";
        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: return "LWS_CALLBACK_COMPLETED_CLIENT_HTTP";
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: return "LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ";
        case LWS_CALLBACK_HTTP_BIND_PROTOCOL: return "LWS_CALLBACK_HTTP_BIND_PROTOCOL";
        case LWS_CALLBACK_HTTP_DROP_PROTOCOL: return "LWS_CALLBACK_HTTP_DROP_PROTOCOL";
        case LWS_CALLBACK_CHECK_ACCESS_RIGHTS: return "LWS_CALLBACK_CHECK_ACCESS_RIGHTS";
        case LWS_CALLBACK_PROCESS_HTML: return "LWS_CALLBACK_PROCESS_HTML";
        case LWS_CALLBACK_ADD_HEADERS: return "LWS_CALLBACK_ADD_HEADERS";
        case LWS_CALLBACK_SESSION_INFO: return "LWS_CALLBACK_SESSION_INFO";
        case LWS_CALLBACK_GS_EVENT: return "LWS_CALLBACK_GS_EVENT";
        case LWS_CALLBACK_HTTP_PMO: return "LWS_CALLBACK_HTTP_PMO";
        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: return "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE";
        case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION: return "LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION";
        case LWS_CALLBACK_RAW_RX: return "LWS_CALLBACK_RAW_RX";
        case LWS_CALLBACK_RAW_CLOSE: return "LWS_CALLBACK_RAW_CLOSE";
        case LWS_CALLBACK_RAW_WRITEABLE: return "LWS_CALLBACK_RAW_WRITEABLE";
        case LWS_CALLBACK_RAW_ADOPT: return "LWS_CALLBACK_RAW_ADOPT";
        case LWS_CALLBACK_RAW_ADOPT_FILE: return "LWS_CALLBACK_RAW_ADOPT_FILE";
        case LWS_CALLBACK_RAW_RX_FILE: return "LWS_CALLBACK_RAW_RX_FILE";
        case LWS_CALLBACK_RAW_WRITEABLE_FILE: return "LWS_CALLBACK_RAW_WRITEABLE_FILE";
        case LWS_CALLBACK_RAW_CLOSE_FILE: return "LWS_CALLBACK_RAW_CLOSE_FILE";
        case LWS_CALLBACK_SSL_INFO: return "LWS_CALLBACK_SSL_INFO";
        case LWS_CALLBACK_CGI_PROCESS_ATTACH: return "LWS_CALLBACK_CGI_PROCESS_ATTACH";
        case LWS_CALLBACK_EVENT_WAIT_CANCELLED: return "LWS_CALLBACK_EVENT_WAIT_CANCELLED";
        case LWS_CALLBACK_VHOST_CERT_AGING: return "LWS_CALLBACK_VHOST_CERT_AGING";
        case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: return "LWS_CALLBACK_HTTP_CONFIRM_UPGRADE";
        case LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL: return "LWS_CALLBACK_CLIENT_HTTP_BIND_PROTOCOL";
        case LWS_CALLBACK_CONNECTING: return "LWS_CALLBACK_CONNECTING";
        case LWS_CALLBACK_CLIENT_CLOSED: return "LWS_CALLBACK_CLIENT_CLOSED";
        case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: return "LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL";

        // Add more cases as needed for your specific libwebsockets version

        default: {
            // For unknown reasons, return a string with the numeric value
            static char buf[32];
            snprintf(buf, sizeof(buf), "UNKNOWN_REASON(%d)", reason);
            return buf;
        }
    }
}
