#include "internal/websocket_server_internal.h"

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
            ws_server_client_cleanup(&data->clients[i], data);
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