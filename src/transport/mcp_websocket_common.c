#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_websocket_common.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

// Initialize WebSocket protocols
void mcp_websocket_init_protocols(
    struct lws_protocols* protocols,
    lws_callback_function* callback
) {
    if (!protocols || !callback) {
        return;
    }

    // Protocol for WebSocket connections
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

    // Terminator
    protocols[2].name = NULL;
    protocols[2].callback = NULL;
    protocols[2].per_session_data_size = 0;
    protocols[2].rx_buffer_size = 0;
    protocols[2].id = 0;
    protocols[2].user = NULL;
    protocols[2].tx_packet_size = 0;
}

// Enqueue a message to a WebSocket message queue
int mcp_websocket_enqueue_message(
    ws_message_item_t** queue_head,
    ws_message_item_t** queue_tail,
    mcp_mutex_t* queue_mutex,
    const void* message,
    size_t size,
    ws_message_type_t type
) {
    if (!queue_head || !queue_tail || !message || size == 0 || !queue_mutex) {
        return -1;
    }

    // Allocate message item
    ws_message_item_t* item = (ws_message_item_t*)malloc(sizeof(ws_message_item_t));
    if (!item) {
        mcp_log_error("Failed to allocate WebSocket message item");
        return -1;
    }

    // Allocate buffer with LWS_PRE padding
    item->data = (unsigned char*)malloc(LWS_PRE + size);
    if (!item->data) {
        mcp_log_error("Failed to allocate WebSocket message buffer");
        free(item);
        return -1;
    }

    // Copy message data
    memcpy(item->data + LWS_PRE, message, size);
    item->size = size;
    item->type = type;
    item->next = NULL;

    // Add to queue
    mcp_mutex_lock(queue_mutex);
    if (*queue_tail) {
        (*queue_tail)->next = item;
        *queue_tail = item;
    } else {
        *queue_head = item;
        *queue_tail = item;
    }
    mcp_mutex_unlock(queue_mutex);

    return 0;
}

// Dequeue a message from a WebSocket message queue
ws_message_item_t* mcp_websocket_dequeue_message(
    ws_message_item_t** queue_head,
    ws_message_item_t** queue_tail,
    mcp_mutex_t* queue_mutex
) {
    if (!queue_head || !queue_tail || !queue_mutex) {
        return NULL;
    }

    ws_message_item_t* item = NULL;

    mcp_mutex_lock(queue_mutex);
    if (*queue_head) {
        item = *queue_head;
        *queue_head = item->next;
        if (!*queue_head) {
            *queue_tail = NULL;
        }
        item->next = NULL;
    }
    mcp_mutex_unlock(queue_mutex);

    return item;
}

// Free all messages in a WebSocket message queue
void mcp_websocket_free_message_queue(
    ws_message_item_t** queue_head,
    ws_message_item_t** queue_tail,
    mcp_mutex_t* queue_mutex
) {
    if (!queue_head || !queue_mutex) {
        return;
    }

    mcp_mutex_lock(queue_mutex);
    ws_message_item_t* item = *queue_head;
    *queue_head = NULL;
    *queue_tail = NULL;
    mcp_mutex_unlock(queue_mutex);

    while (item) {
        ws_message_item_t* next = item->next;
        if (item->data) {
            free(item->data);
        }
        free(item);
        item = next;
    }
}

// Create a libwebsockets context
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
            return -1;
        }
        memcpy((char*)combined_buffer + offset, buffers[i].data, buffers[i].size);
        offset += buffers[i].size;
    }

    return 0;
}
