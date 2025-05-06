#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_websocket_transport.h"
#include "mcp_websocket_common.h"
#include "internal/transport_internal.h"
#include "internal/transport_interfaces.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_socket_utils.h"
#include "mcp_thread_local.h"
#include "mcp_buffer_pool.h"

#include "libwebsockets.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Define maximum number of simultaneous WebSocket clients
#define MAX_WEBSOCKET_CLIENTS 64

// WebSocket client connection state
typedef enum {
    WS_CLIENT_STATE_INACTIVE = 0,   // Client slot is unused
    WS_CLIENT_STATE_CONNECTING,     // Client is connecting
    WS_CLIENT_STATE_ACTIVE,         // Client is connected and active
    WS_CLIENT_STATE_CLOSING,        // Client is closing
    WS_CLIENT_STATE_ERROR           // Client encountered an error
} ws_client_state_t;

// WebSocket client connection information
typedef struct {
    struct lws* wsi;                // libwebsockets connection handle
    ws_client_state_t state;        // Connection state
    char* receive_buffer;           // Receive buffer
    size_t receive_buffer_len;      // Receive buffer length
    size_t receive_buffer_used;     // Used receive buffer length
    int client_id;                  // Client ID for tracking
    time_t last_activity;           // Time of last activity for timeout detection
    uint32_t ping_sent;             // Number of pings sent without response
} ws_client_t;

// WebSocket server transport data
typedef struct {
    struct lws_context* context;    // libwebsockets context
    const struct lws_protocols* protocols; // WebSocket protocols
    bool running;                   // Running flag
    mcp_thread_t event_thread;      // Event loop thread
    mcp_mutex_t* clients_mutex;     // Clients list mutex
    ws_client_t clients[MAX_WEBSOCKET_CLIENTS]; // Clients list
    uint32_t client_bitmap[MAX_WEBSOCKET_CLIENTS / 32 + 1]; // Bitmap for quick client slot lookup
    uint32_t active_clients;        // Number of active clients
    uint32_t peak_clients;          // Peak number of active clients
    uint32_t total_connections;     // Total number of connections since start
    uint32_t rejected_connections;  // Number of rejected connections due to max clients
    mcp_transport_t* transport;     // Transport handle
    mcp_websocket_config_t config;  // WebSocket configuration
    time_t last_ping_time;          // Time of last ping check
    time_t last_cleanup_time;       // Time of last inactive client cleanup
    time_t start_time;              // Time when the server was started

    // Buffer pool for efficient memory management
    mcp_buffer_pool_t* buffer_pool; // Pool of reusable buffers
    uint32_t buffer_allocs;         // Total number of buffer allocations
    uint32_t buffer_reuses;         // Number of buffer reuses from pool
    uint32_t buffer_misses;         // Number of times a buffer couldn't be acquired from pool
    size_t total_buffer_memory;     // Total memory used for buffers
} ws_server_data_t;

// Forward declarations
static int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);
static void ws_server_check_timeouts(ws_server_data_t* data);
static void ws_server_cleanup_inactive_clients(ws_server_data_t* data);
static ws_client_t* ws_server_find_client_by_wsi(ws_server_data_t* data, struct lws* wsi);
static void ws_client_update_activity(ws_client_t* client);
static int ws_client_send_ping(ws_client_t* client);
static void* ws_server_event_thread(void* arg);
static int ws_server_transport_start(mcp_transport_t* transport,
                                    mcp_transport_message_callback_t message_callback,
                                    void* user_data,
                                    mcp_transport_error_callback_t error_callback);
static int ws_server_transport_stop(mcp_transport_t* transport);
static int ws_server_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static int ws_server_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);
static void ws_server_transport_destroy(mcp_transport_t* transport);
static int ws_client_init(ws_client_t* client, int client_id, struct lws* wsi);
static void ws_client_cleanup(ws_client_t* client, ws_server_data_t* server_data);
static int ws_client_resize_buffer(ws_client_t* client, size_t needed_size, ws_server_data_t* server_data);
static int ws_client_process_message(ws_server_data_t* data, ws_client_t* client, struct lws* wsi);
static int ws_client_handle_received_data(ws_server_data_t* data, ws_client_t* client,
                                         struct lws* wsi, void* in, size_t len, bool is_final);
static int ws_client_send_response(ws_client_t* client, struct lws* wsi, const char* response, size_t response_len);

// WebSocket protocol definitions
static struct lws_protocols server_protocols[3];

// Bitmap helper functions for client slot management
static inline void ws_server_set_client_bit(uint32_t* bitmap, int index) {
    bitmap[index / 32] |= (1 << (index % 32));
}

static inline void ws_server_clear_client_bit(uint32_t* bitmap, int index) {
    bitmap[index / 32] &= ~(1 << (index % 32));
}

static inline bool ws_server_test_client_bit(uint32_t* bitmap, int index) {
    return (bitmap[index / 32] & (1 << (index % 32))) != 0;
}

// Find first free client slot using bitmap
static int ws_server_find_free_client_slot(ws_server_data_t* data) {
    // Quick check if we're already at max capacity
    if (data->active_clients >= MAX_WEBSOCKET_CLIENTS) {
        return -1;
    }

    // Use bitmap to find first free slot
    for (int i = 0; i < (MAX_WEBSOCKET_CLIENTS / 32 + 1); i++) {
        uint32_t word = data->client_bitmap[i];

        // If word is full (all bits set), skip to next word
        if (word == 0xFFFFFFFF) {
            continue;
        }

        // Find first zero bit in this word
        for (int j = 0; j < 32; j++) {
            int index = i * 32 + j;
            if (index >= MAX_WEBSOCKET_CLIENTS) {
                break; // Don't go beyond array bounds
            }

            if ((word & (1 << j)) == 0) {
                return index;
            }
        }
    }

    return -1; // No free slots found
}

// Helper function to initialize a client
static int ws_client_init(ws_client_t* client, int client_id, struct lws* wsi) {
    if (!client) {
        return -1;
    }

    client->wsi = wsi;
    client->state = WS_CLIENT_STATE_ACTIVE;
    client->receive_buffer = NULL;
    client->receive_buffer_len = 0;
    client->receive_buffer_used = 0;
    client->client_id = client_id;
    client->last_activity = time(NULL);
    client->ping_sent = 0;

    return 0;
}

// Helper function to clean up a client
static void ws_client_cleanup(ws_client_t* client, ws_server_data_t* server_data) {
    if (!client) {
        return;
    }

    // Free receive buffer
    if (client->receive_buffer) {
        // If buffer pool exists and buffer size matches pool buffer size, return to pool
        if (server_data && server_data->buffer_pool &&
            client->receive_buffer_len == WS_BUFFER_POOL_BUFFER_SIZE) {
            mcp_buffer_pool_release(server_data->buffer_pool, client->receive_buffer);
            mcp_log_debug("Returned buffer to pool for client %d", client->client_id);
        } else {
            // Otherwise free normally
            free(client->receive_buffer);

            // Update memory statistics
            if (server_data) {
                server_data->total_buffer_memory -= client->receive_buffer_len;
            }
        }

        client->receive_buffer = NULL;
        client->receive_buffer_len = 0;
        client->receive_buffer_used = 0;
    }

    // Reset client state
    client->state = WS_CLIENT_STATE_INACTIVE;
    client->wsi = NULL;

    // Update bitmap and statistics if server_data is provided
    if (server_data) {
        // Clear bit in bitmap
        ws_server_clear_client_bit(server_data->client_bitmap, client->client_id);

        // Decrement active client count
        if (server_data->active_clients > 0) {
            server_data->active_clients--;
        }
    }
}

// Helper function to resize a client's receive buffer
static int ws_client_resize_buffer(ws_client_t* client, size_t needed_size, ws_server_data_t* server_data) {
    if (!client) {
        return -1;
    }

    // Calculate new buffer size
    size_t new_len = client->receive_buffer_len == 0 ? WS_DEFAULT_BUFFER_SIZE : (int)(client->receive_buffer_len * WS_BUFFER_GROWTH_FACTOR);
    while (new_len < needed_size) {
        new_len = (size_t)(new_len * WS_BUFFER_GROWTH_FACTOR);
    }

    // Round up to the nearest multiple of WS_DEFAULT_BUFFER_SIZE for better reuse
    new_len = ((new_len + WS_DEFAULT_BUFFER_SIZE - 1) / WS_DEFAULT_BUFFER_SIZE) * WS_DEFAULT_BUFFER_SIZE;

    char* new_buffer = NULL;

    // Try to get buffer from pool if server_data is provided and buffer pool exists
    if (server_data && server_data->buffer_pool && new_len <= WS_BUFFER_POOL_BUFFER_SIZE) {
        // Try to get a buffer from the pool
        new_buffer = (char*)mcp_buffer_pool_acquire(server_data->buffer_pool);

        if (new_buffer) {
            // Successfully got a buffer from the pool
            server_data->buffer_reuses++;

            // Copy existing data if any
            if (client->receive_buffer && client->receive_buffer_used > 0) {
                memcpy(new_buffer, client->receive_buffer, client->receive_buffer_used);
            }

            // Free old buffer if it exists
            if (client->receive_buffer) {
                // If old buffer was from pool, return it to pool
                if (client->receive_buffer_len == WS_BUFFER_POOL_BUFFER_SIZE && server_data->buffer_pool) {
                    mcp_buffer_pool_release(server_data->buffer_pool, client->receive_buffer);
                } else {
                    free(client->receive_buffer);
                    server_data->total_buffer_memory -= client->receive_buffer_len;
                }
            }

            // Update buffer information
            client->receive_buffer = new_buffer;
            client->receive_buffer_len = WS_BUFFER_POOL_BUFFER_SIZE;
            return 0;
        } else {
            // Failed to get buffer from pool
            server_data->buffer_misses++;
        }
    }

    // Fall back to regular allocation if pool is not available or buffer is too large
    if (client->receive_buffer) {
        new_buffer = (char*)realloc(client->receive_buffer, new_len);

        // Update memory statistics if server_data is provided
        if (server_data) {
            server_data->total_buffer_memory -= client->receive_buffer_len;
            server_data->total_buffer_memory += new_len;
            server_data->buffer_allocs++;
        }
    } else {
        new_buffer = (char*)malloc(new_len);

        // Update memory statistics if server_data is provided
        if (server_data) {
            server_data->total_buffer_memory += new_len;
            server_data->buffer_allocs++;
        }
    }

    if (!new_buffer) {
        mcp_log_error("Failed to allocate WebSocket receive buffer of size %zu", new_len);
        return -1;
    }

    // Update buffer information
    client->receive_buffer = new_buffer;
    client->receive_buffer_len = new_len;
    return 0;
}

// Helper function to update client activity timestamp
static void ws_client_update_activity(ws_client_t* client) {
    if (!client) {
        return;
    }

    client->last_activity = time(NULL);
    client->ping_sent = 0; // Reset ping counter on activity
}

// Helper function to send a ping to a client
static int ws_client_send_ping(ws_client_t* client) {
    if (!client || !client->wsi || client->state != WS_CLIENT_STATE_ACTIVE) {
        return -1;
    }

    // Increment ping counter
    client->ping_sent++;

    // Request a callback to send a ping
    return lws_callback_on_writable(client->wsi);
}

// Helper function to find a client by WebSocket instance
static ws_client_t* ws_server_find_client_by_wsi(ws_server_data_t* data, struct lws* wsi) {
    if (!data || !wsi) {
        return NULL;
    }

    // First try to get the client from opaque user data (faster)
    ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
    if (client) {
        return client;
    }

    // If not found, search through the client list
    mcp_mutex_lock(data->clients_mutex);

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (data->clients[i].wsi == wsi) {
            client = &data->clients[i];
            break;
        }
    }

    mcp_mutex_unlock(data->clients_mutex);

    return client;
}

// Helper function to send a response to a client
static int ws_client_send_response(ws_client_t* client, struct lws* wsi, const char* response, size_t response_len) {
    if (!client || !wsi || !response || response_len == 0) {
        return -1;
    }

    // Allocate buffer with LWS_PRE padding
    unsigned char* buffer = (unsigned char*)malloc(LWS_PRE + response_len);
    if (!buffer) {
        mcp_log_error("Failed to allocate WebSocket response buffer");
        return -1;
    }

    // Copy response data
    memcpy(buffer + LWS_PRE, response, response_len);

    // Send data directly
    int result = lws_write(wsi, buffer + LWS_PRE, response_len, LWS_WRITE_TEXT);

    // Free buffer
    free(buffer);

    if (result < 0) {
        mcp_log_error("WebSocket server direct write failed");
        return -1;
    }

    // Update activity timestamp
    ws_client_update_activity(client);

    return 0;
}

// Helper function to process a complete message
static int ws_client_process_message(ws_server_data_t* data, ws_client_t* client, struct lws* wsi) {
    if (!data || !client || !wsi) {
        return -1;
    }

    // Ensure the buffer is null-terminated for string operations
    if (client->receive_buffer_used < client->receive_buffer_len) {
        client->receive_buffer[client->receive_buffer_used] = '\0';
    } else {
        if (ws_client_resize_buffer(client, client->receive_buffer_used + 1, data) != 0) {
            return -1;
        }
        client->receive_buffer[client->receive_buffer_used] = '\0';
    }

    // Process complete message
    if (data->transport && data->transport->message_callback) {
        // Initialize thread-local arena for JSON parsing
        mcp_log_debug("Initializing thread-local arena for server message processing");
        if (mcp_arena_init_current_thread(4096) != 0) {
            mcp_log_error("Failed to initialize thread-local arena in WebSocket server callback");
        }

        int error_code = 0;
        char* response = data->transport->message_callback(
            data->transport->callback_user_data,
            client->receive_buffer,
            client->receive_buffer_used,
            &error_code
        );

        // If there's a response, send it directly
        if (response) {
            size_t response_len = strlen(response);
            ws_client_send_response(client, wsi, response, response_len);

            // Free the response
            free(response);
        }
    }

    // Reset buffer
    client->receive_buffer_used = 0;

    return 0;
}

// Helper function to handle received data
static int ws_client_handle_received_data(ws_server_data_t* data, ws_client_t* client,
                                         struct lws* wsi, void* in, size_t len, bool is_final) {
    if (!data || !client || !wsi || !in || len == 0) {
        return -1;
    }

    // Update client activity timestamp
    ws_client_update_activity(client);

    // Ensure buffer is large enough
    if (client->receive_buffer_used + len > client->receive_buffer_len) {
        if (ws_client_resize_buffer(client, client->receive_buffer_used + len, data) != 0) {
            return -1;
        }
    }

    // Log the raw message data for debugging (if not too large)
    #ifdef MCP_VERBOSE_DEBUG
    if (len < 1000) {
        char debug_buffer[1024] = {0};
        size_t copy_len = len < 1000 ? len : 1000;
        memcpy(debug_buffer, in, copy_len);
        debug_buffer[copy_len] = '\0';

        // Log as hex for the first 32 bytes
        char hex_buffer[200] = {0};
        size_t hex_len = len < 32 ? len : 32;
        for (size_t i = 0; i < hex_len; i++) {
            sprintf(hex_buffer + i*3, "%02x ", (unsigned char)((char*)in)[i]);
        }
        mcp_log_debug("WebSocket server raw data (hex): %s", hex_buffer);

        // Check if this is a JSON message
        if (len > 0 && ((char*)in)[0] == '{') {
            mcp_log_debug("Detected JSON message");
        }
    }
    #endif

    // Check if this might be a length-prefixed message
    if (len >= 4) {
        // Interpret first 4 bytes as a 32-bit length (network byte order)
        uint32_t msg_len = 0;
        // Convert from network byte order (big endian) to host byte order
        msg_len = ((unsigned char)((char*)in)[0] << 24) |
                  ((unsigned char)((char*)in)[1] << 16) |
                  ((unsigned char)((char*)in)[2] << 8) |
                  ((unsigned char)((char*)in)[3]);

        #ifdef MCP_VERBOSE_DEBUG
        // Log the extracted length
        mcp_log_debug("Possible message length prefix: %u bytes (total received: %zu bytes)",
                     msg_len, len);
        #endif

        // If this looks like a length-prefixed message, skip the length prefix
        if (msg_len <= len - 4 && msg_len > 0) {
            mcp_log_debug("Detected length-prefixed message, skipping 4-byte prefix");
            // Copy data without the length prefix
            memcpy(client->receive_buffer + client->receive_buffer_used,
                   (char*)in + 4, len - 4);
            client->receive_buffer_used += (len - 4);

            // Process the message immediately if it's a complete message
            if (is_final) {
                return ws_client_process_message(data, client, wsi);
            }

            return 0;  // Skip the rest of the processing
        }
    }

    // Normal copy if not a length-prefixed message or if length prefix check failed
    memcpy(client->receive_buffer + client->receive_buffer_used, in, len);
    client->receive_buffer_used += len;

    // Check if this is a complete message
    if (is_final) {
        return ws_client_process_message(data, client, wsi);
    }

    return 0;
}

// Server callback function implementation
static int ws_server_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len) {
    // Suppress unused parameter warning
    (void)user;
    struct lws_context* context = lws_get_context(wsi);
    ws_server_data_t* data = (ws_server_data_t*)lws_context_user(context);

    if (!data) {
        return 0;
    }

    // Debug log for important callback reasons only
    if (reason != LWS_CALLBACK_SERVER_WRITEABLE &&
        reason != LWS_CALLBACK_RECEIVE &&
        reason != LWS_CALLBACK_RECEIVE_PONG) {
        mcp_log_debug("WebSocket server callback: reason=%d (%s)", reason, websocket_get_callback_reason_string(reason));
    }

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            // New client connection
            mcp_log_info("WebSocket connection established");

            mcp_mutex_lock(data->clients_mutex);

            // Find an empty client slot using bitmap
            int client_index = ws_server_find_free_client_slot(data);

            if (client_index == -1) {
                // Update rejection statistics
                data->rejected_connections++;

                mcp_mutex_unlock(data->clients_mutex);
                mcp_log_error("Maximum WebSocket clients reached (%d active, %d total connections, %d rejected)",
                             data->active_clients, data->total_connections, data->rejected_connections);
                return -1; // Reject connection
            }

            // Initialize client data
            ws_client_t* client = &data->clients[client_index];
            ws_client_init(client, client_index, wsi);

            // Store client pointer in user data
            lws_set_opaque_user_data(wsi, client);

            // Update bitmap and statistics
            ws_server_set_client_bit(data->client_bitmap, client_index);
            data->active_clients++;
            data->total_connections++;

            // Update peak clients if needed
            if (data->active_clients > data->peak_clients) {
                data->peak_clients = data->active_clients;
            }

            mcp_log_info("Client %d connected (active: %d, peak: %d, total: %d)",
                        client_index, data->active_clients, data->peak_clients, data->total_connections);

            mcp_mutex_unlock(data->clients_mutex);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            // Client disconnected
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);

            if (client) {
                mcp_log_info("Client %d disconnected", client->client_id);

                mcp_mutex_lock(data->clients_mutex);

                // Mark client as closing but don't free resources yet
                // This allows any pending messages to be processed
                client->state = WS_CLIENT_STATE_CLOSING;
                client->wsi = NULL;
                client->last_activity = time(NULL);

                // Reset ping counter
                client->ping_sent = 0;

                // If there's no pending data, clean up immediately
                // This reduces resource usage and makes slots available faster
                if (client->receive_buffer_used == 0) {
                    mcp_log_debug("No pending data for client %d, cleaning up immediately", client->client_id);
                    ws_client_cleanup(client, data);
                }

                mcp_mutex_unlock(data->clients_mutex);
            } else {
                mcp_log_info("Unknown client disconnected");
            }
            break;
        }

        case LWS_CALLBACK_PROTOCOL_DESTROY: {
            // Protocol is being destroyed, clean up all clients
            mcp_log_info("WebSocket protocol being destroyed, cleaning up all clients");

            mcp_mutex_lock(data->clients_mutex);

            for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
                ws_client_t* client = &data->clients[i];
                if (client->state != WS_CLIENT_STATE_INACTIVE) {
                    ws_client_cleanup(client, data);
                }
            }

            mcp_mutex_unlock(data->clients_mutex);
            break;
        }

        case LWS_CALLBACK_RECEIVE_PONG: {
            // Received pong from client
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);

            if (client) {
                mcp_log_debug("Received pong from client %d", client->client_id);
                ws_client_update_activity(client);
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            // Receive data
            mcp_log_debug("WebSocket data received: %zu bytes", len);

            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            // Handle the received data
            return ws_client_handle_received_data(data, client, wsi, in, len, lws_is_final_fragment(wsi));
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            // Ready to send data to client
            ws_client_t* client = (ws_client_t*)lws_get_opaque_user_data(wsi);
            if (!client) {
                mcp_log_error("WebSocket client not found");
                return -1;
            }

            // No message queue anymore, so nothing to send here
            // Messages are sent directly in the LWS_CALLBACK_RECEIVE handler

            // Update activity timestamp
            ws_client_update_activity(client);
            break;
        }

        case LWS_CALLBACK_HTTP: {
            // HTTP request (not WebSocket)
            mcp_log_info("HTTP request received: %s", (char*)in);

            // Return 200 OK with a simple message
            unsigned char buffer[LWS_PRE + 128];
            unsigned char *p = &buffer[LWS_PRE];
            int head_len = sprintf((char *)p, "HTTP WebSocket server is running. Please use a WebSocket client to connect.");

            if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK, "text/plain",
                                           head_len, &p, &buffer[sizeof(buffer)])) {
                return 1;
            }

            if (lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, &buffer[sizeof(buffer)])) {
                return 1;
            }

            lws_callback_on_writable(wsi);
            return 0;
        }

        case LWS_CALLBACK_HTTP_WRITEABLE: {
            // Ready to send HTTP response
            unsigned char buffer[LWS_PRE + 128];
            unsigned char *p = &buffer[LWS_PRE];
            int head_len = sprintf((char *)p, "HTTP WebSocket server is running. Please use a WebSocket client to connect.");

            if (lws_write(wsi, p, head_len, LWS_WRITE_HTTP) != head_len) {
                return 1;
            }

            // Close the connection after sending the response
            return -1;
        }

        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            // This callback allows us to examine the HTTP headers and reject connections
            mcp_log_debug("WebSocket filter protocol connection");

            // Check if we're at or near capacity
            if (data->active_clients >= MAX_WEBSOCKET_CLIENTS - 5) {
                mcp_log_warn("WebSocket server near capacity (%d/%d), applying stricter filtering",
                           data->active_clients, MAX_WEBSOCKET_CLIENTS);

                // Here we could implement additional filtering logic
                // For example, rate limiting based on IP, authentication checks, etc.
            }
            return 0;
        }

        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
            // This is called when a client initiates a connection
            mcp_log_debug("WebSocket filter network connection");

            // Check if we're at capacity
            if (data->active_clients >= MAX_WEBSOCKET_CLIENTS) {
                mcp_log_warn("WebSocket server at capacity (%d/%d), rejecting connection",
                           data->active_clients, MAX_WEBSOCKET_CLIENTS);
                return -1; // Reject connection
            }
            return 0;
        }

        case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
            // A new client connection is being instantiated
            mcp_log_debug("WebSocket new client instantiated");
            return 0;
        }

        case LWS_CALLBACK_WSI_CREATE: {
            // A new WebSocket instance is being created
            mcp_log_debug("WebSocket instance created");
            return 0;
        }

        default:
            break;
    }

    return 0;
}

// Helper function to check for client timeouts and send pings
static void ws_server_check_timeouts(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only check every PING_INTERVAL_MS milliseconds
    if (difftime(now, data->last_ping_time) * 1000 < WS_PING_INTERVAL_MS) {
        return;
    }

    data->last_ping_time = now;

    mcp_mutex_lock(data->clients_mutex);

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        ws_client_t* client = &data->clients[i];

        if (client->state == WS_CLIENT_STATE_ACTIVE && client->wsi) {
            // Check if client has been inactive for too long
            if (difftime(now, client->last_activity) * 1000 > WS_PING_TIMEOUT_MS) {
                // If we've sent too many pings without response, close the connection
                if (client->ping_sent >= WS_MAX_PING_FAILURES) {
                    mcp_log_warn("Client %d timed out after %d ping failures, closing connection",
                                client->client_id, client->ping_sent);
                    client->state = WS_CLIENT_STATE_CLOSING;

                    // Set a short timeout to force connection closure
                    lws_set_timeout(client->wsi, PENDING_TIMEOUT_CLOSE_SEND, 1);
                } else {
                    // Send a ping to check if client is still alive
                    mcp_log_debug("Sending ping to client %d (attempt %d/%d)",
                                client->client_id, client->ping_sent + 1, WS_MAX_PING_FAILURES);
                    ws_client_send_ping(client);
                }
            }
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

// Helper function to clean up inactive clients
static void ws_server_cleanup_inactive_clients(ws_server_data_t* data) {
    if (!data) {
        return;
    }

    time_t now = time(NULL);

    // Only clean up every CLEANUP_INTERVAL_MS milliseconds
    if (difftime(now, data->last_cleanup_time) * 1000 < WS_CLEANUP_INTERVAL_MS) {
        return;
    }

    data->last_cleanup_time = now;

    mcp_mutex_lock(data->clients_mutex);

    // Use bitmap to quickly skip inactive clients
    for (int i = 0; i < (MAX_WEBSOCKET_CLIENTS / 32 + 1); i++) {
        uint32_t word = data->client_bitmap[i];

        // Skip words with no active clients
        if (word == 0) {
            continue;
        }

        // Process only active bits in this word
        for (int j = 0; j < 32; j++) {
            if ((word & (1 << j)) == 0) {
                continue; // Skip inactive slots
            }

            int client_index = i * 32 + j;
            if (client_index >= MAX_WEBSOCKET_CLIENTS) {
                break; // Don't go beyond array bounds
            }

            ws_client_t* client = &data->clients[client_index];

            // Clean up clients in error state or closing state without a valid wsi
            if ((client->state == WS_CLIENT_STATE_ERROR ||
                 (client->state == WS_CLIENT_STATE_CLOSING && !client->wsi)) &&
                difftime(now, client->last_activity) > 5) { // 5 seconds grace period (reduced from 10s)

                mcp_log_info("Cleaning up inactive client %d (state: %d, last activity: %.1f seconds ago)",
                           client->client_id, client->state, difftime(now, client->last_activity));
                ws_client_cleanup(client, data);
            }
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

// Server event loop thread function
static void* ws_server_event_thread(void* arg) {
    ws_server_data_t* data = (ws_server_data_t*)arg;

    // Use a shorter service timeout for more responsive handling
    // but not too short to avoid excessive CPU usage
    const int service_timeout_ms = 20; // 20ms timeout (reduced from 50ms)

    // Track last service time for performance monitoring
    time_t last_service_time = time(NULL);
    unsigned long service_count = 0;

    mcp_log_info("WebSocket server event thread started");

    while (data->running) {
        // Service libwebsockets
        int service_result = lws_service(data->context, service_timeout_ms);
        if (service_result < 0) {
            mcp_log_warn("lws_service returned error: %d", service_result);
            // Don't exit the loop, just continue and try again
        }

        // Increment service counter
        service_count++;

        // Log performance stats every ~60 seconds
        time_t now = time(NULL);
        if (difftime(now, last_service_time) >= 60) {
            double elapsed = difftime(now, last_service_time);
            double rate = service_count / elapsed;
            mcp_log_debug("WebSocket server performance: %.1f service calls/sec, %lu active clients",
                         rate, data->active_clients);

            // Reset counters
            last_service_time = now;
            service_count = 0;
        }

        // Check for client timeouts and send pings
        ws_server_check_timeouts(data);

        // Clean up inactive clients
        ws_server_cleanup_inactive_clients(data);
    }

    mcp_log_info("WebSocket server event thread exiting");
    return NULL;
}

// Server transport start function
static int ws_server_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // Store callback information
    transport->message_callback = message_callback;
    transport->callback_user_data = user_data;
    transport->error_callback = error_callback;

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Initialize protocols
    mcp_websocket_init_protocols(server_protocols, ws_server_callback);

    // Create libwebsockets context using common function
    data->context = mcp_websocket_create_context(
        data->config.host,
        data->config.port,
        data->config.path,
        server_protocols,
        data,
        true, // is_server
        data->config.use_ssl,
        data->config.cert_path,
        data->config.key_path
    );

    if (!data->context) {
        mcp_log_error("Failed to create WebSocket server context");
        return -1;
    }

    // Initialize client list
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        data->clients[i].state = WS_CLIENT_STATE_INACTIVE;
        data->clients[i].wsi = NULL;
        data->clients[i].receive_buffer = NULL;
        data->clients[i].receive_buffer_len = 0;
        data->clients[i].receive_buffer_used = 0;
        data->clients[i].client_id = i;
        data->clients[i].last_activity = 0;
        data->clients[i].ping_sent = 0;
    }

    // Initialize bitmap (all bits to 0 = all slots free)
    memset(data->client_bitmap, 0, sizeof(data->client_bitmap));

    // Initialize statistics
    data->active_clients = 0;
    data->peak_clients = 0;
    data->total_connections = 0;
    data->rejected_connections = 0;

    // Initialize timestamps
    time_t now = time(NULL);
    data->last_ping_time = now;
    data->last_cleanup_time = now;
    data->start_time = now;

    // Initialize buffer pool statistics
    data->buffer_allocs = 0;
    data->buffer_reuses = 0;
    data->buffer_misses = 0;
    data->total_buffer_memory = 0;

    // Create buffer pool
    data->buffer_pool = mcp_buffer_pool_create(WS_BUFFER_POOL_BUFFER_SIZE, WS_BUFFER_POOL_SIZE);
    if (!data->buffer_pool) {
        mcp_log_error("Failed to create WebSocket buffer pool");
        // Continue without buffer pool, will fall back to regular malloc/free
    } else {
        mcp_log_info("WebSocket buffer pool created with %d buffers of %d bytes each",
                    WS_BUFFER_POOL_SIZE, WS_BUFFER_POOL_BUFFER_SIZE);
    }

    // Create mutex
    data->clients_mutex = mcp_mutex_create();
    if (!data->clients_mutex) {
        mcp_log_error("Failed to create WebSocket server mutex");
        lws_context_destroy(data->context);
        data->context = NULL;
        return -1;
    }

    // Set running flag
    data->running = true;

    // Create event loop thread
    if (mcp_thread_create(&data->event_thread, ws_server_event_thread, data) != 0) {
        mcp_log_error("Failed to create WebSocket server event thread");
        mcp_mutex_destroy(data->clients_mutex);
        lws_context_destroy(data->context);
        data->context = NULL;
        data->running = false;
        return -1;
    }

    mcp_log_info("WebSocket server started on %s:%d", data->config.host, data->config.port);

    return 0;
}

// Server transport stop function
static int ws_server_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Set running flag to false to stop event loop
    data->running = false;

    // Force libwebsockets to break out of its service loop
    if (data->context) {
        lws_cancel_service(data->context);
        mcp_log_info("Cancelled libwebsockets service");
    }

    // Wait for event thread to exit
    if (data->event_thread) {
        mcp_log_info("Waiting for WebSocket server event thread to exit...");
        mcp_thread_join(data->event_thread, NULL);
        data->event_thread = 0;
    }

    // Clean up client resources
    mcp_mutex_lock(data->clients_mutex);
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (data->clients[i].state != WS_CLIENT_STATE_INACTIVE) {
            ws_client_cleanup(&data->clients[i], data);
        }
    }
    mcp_mutex_unlock(data->clients_mutex);

    // Destroy mutex
    mcp_mutex_destroy(data->clients_mutex);

    // Destroy buffer pool
    if (data->buffer_pool) {
        mcp_log_info("Destroying WebSocket buffer pool (allocs: %u, reuses: %u, misses: %u, memory: %zu bytes)",
                    data->buffer_allocs, data->buffer_reuses, data->buffer_misses, data->total_buffer_memory);
        mcp_buffer_pool_destroy(data->buffer_pool);
        data->buffer_pool = NULL;
    }

    // Destroy libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    mcp_log_info("WebSocket server stopped");

    return 0;
}

// Server transport send function
static int ws_server_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    // Suppress unused parameter warnings
    (void)transport;
    (void)data;
    (void)size;
    // Server transport doesn't support direct send
    // Responses are sent in the callback
    mcp_log_error("WebSocket server transport doesn't support direct send");
    return -1;
}

// Server transport sendv function
static int ws_server_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    // Suppress unused parameter warnings
    (void)transport;
    (void)buffers;
    (void)buffer_count;
    // Server transport doesn't support direct send
    // Responses are sent in the callback
    mcp_log_error("WebSocket server transport doesn't support direct sendv");
    return -1;
}

// Server transport destroy function
static void ws_server_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;
    if (!data) {
        free(transport);
        return;
    }

    // Stop transport if running
    if (data->running) {
        ws_server_transport_stop(transport);
    }

    // Free transport data
    free(data);

    // Free transport
    free(transport);
}

// Get WebSocket server statistics
int mcp_transport_websocket_server_get_stats(mcp_transport_t* transport,
                                           uint32_t* active_clients,
                                           uint32_t* peak_clients,
                                           uint32_t* total_connections,
                                           uint32_t* rejected_connections,
                                           double* uptime_seconds) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // Verify this is a WebSocket server transport
    if (transport->type != MCP_TRANSPORT_TYPE_SERVER ||
        transport->protocol_type != MCP_TRANSPORT_PROTOCOL_WEBSOCKET) {
        return -1;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Lock mutex to ensure consistent data
    mcp_mutex_lock(data->clients_mutex);

    // Copy statistics
    if (active_clients) *active_clients = data->active_clients;
    if (peak_clients) *peak_clients = data->peak_clients;
    if (total_connections) *total_connections = data->total_connections;
    if (rejected_connections) *rejected_connections = data->rejected_connections;

    // Calculate uptime
    if (uptime_seconds) {
        time_t now = time(NULL);
        *uptime_seconds = difftime(now, data->start_time); // difftime returns double, perfect for uptime
    }

    mcp_mutex_unlock(data->clients_mutex);

    return 0;
}

// Get WebSocket server memory statistics
int mcp_transport_websocket_server_get_memory_stats(mcp_transport_t* transport,
                                                  uint32_t* buffer_allocs,
                                                  uint32_t* buffer_reuses,
                                                  uint32_t* buffer_misses,
                                                  size_t* total_buffer_memory,
                                                  uint32_t* pool_size,
                                                  size_t* pool_buffer_size) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // Verify this is a WebSocket server transport
    if (transport->type != MCP_TRANSPORT_TYPE_SERVER ||
        transport->protocol_type != MCP_TRANSPORT_PROTOCOL_WEBSOCKET) {
        return -1;
    }

    ws_server_data_t* data = (ws_server_data_t*)transport->transport_data;

    // Lock mutex to ensure consistent data
    mcp_mutex_lock(data->clients_mutex);

    // Copy statistics
    if (buffer_allocs) *buffer_allocs = data->buffer_allocs;
    if (buffer_reuses) *buffer_reuses = data->buffer_reuses;
    if (buffer_misses) *buffer_misses = data->buffer_misses;
    if (total_buffer_memory) *total_buffer_memory = data->total_buffer_memory;

    // Pool information
    if (pool_size) *pool_size = WS_BUFFER_POOL_SIZE;
    if (pool_buffer_size) *pool_buffer_size = WS_BUFFER_POOL_BUFFER_SIZE;

    mcp_mutex_unlock(data->clients_mutex);

    return 0;
}

// Create WebSocket server transport
mcp_transport_t* mcp_transport_websocket_server_create(const mcp_websocket_config_t* config) {
    if (!config || !config->host) {
        return NULL;
    }

    // Allocate transport
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (!transport) {
        return NULL;
    }

    // Allocate server data
    ws_server_data_t* data = (ws_server_data_t*)calloc(1, sizeof(ws_server_data_t));
    if (!data) {
        free(transport);
        return NULL;
    }

    // Copy config
    data->config = *config;
    data->protocols = server_protocols;
    data->transport = transport;

    // Initialize protocols
    mcp_websocket_init_protocols(server_protocols, ws_server_callback);

    // Initialize timestamps
    data->last_ping_time = time(NULL);
    data->last_cleanup_time = time(NULL);

    // Set transport type and operations
    transport->type = MCP_TRANSPORT_TYPE_SERVER;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_WEBSOCKET;
    transport->server.start = ws_server_transport_start;
    transport->server.stop = ws_server_transport_stop;
    transport->server.destroy = ws_server_transport_destroy;

    // Set transport data
    transport->transport_data = data;
    transport->message_callback = NULL;
    transport->callback_user_data = NULL;
    transport->error_callback = NULL;

    return transport;
}