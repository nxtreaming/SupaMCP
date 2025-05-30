#ifndef HTTP_CLIENT_INTERNAL_H
#define HTTP_CLIENT_INTERNAL_H

#include "mcp_transport.h"
#include "mcp_socket_utils.h"
#include "mcp_sync.h"
#include "http_client_ssl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Internal data structure for HTTP client transport
typedef struct http_client_transport_data {
    char* host;                  // Host to connect to
    uint16_t port;               // Port to connect to
    bool use_ssl;                // Whether to use SSL
    char* cert_path;             // Path to SSL certificate
    char* key_path;              // Path to SSL private key
    uint32_t timeout_ms;         // Connection timeout in milliseconds
    char* api_key;               // API key for authentication

    volatile bool running;       // Whether the transport is running
    mcp_thread_t event_thread;   // Thread for SSE events
    mcp_mutex_t* mutex;          // Mutex for thread safety (only used for SSE events)

    // SSE event handling
    char* last_event_id;         // Last event ID received
    socket_t sse_socket;         // Socket for SSE connection
    http_client_ssl_ctx_t* ssl_ctx; // SSL context for secure connections

    // Message callback
    mcp_transport_message_callback_t message_callback;
    void* callback_user_data;
    mcp_transport_error_callback_t error_callback;

    // Response handling
    char* last_response;         // Last HTTP response received
    uint64_t last_request_id;    // ID of the last request sent
} http_client_transport_data_t;

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_INTERNAL_H
