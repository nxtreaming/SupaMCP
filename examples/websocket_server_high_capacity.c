#include <mcp_server.h>
#include <mcp_transport_factory.h>
#include <mcp_log.h>
#include <mcp_thread_local.h>
#include <mcp_string_utils.h>
#include <mcp_websocket_transport.h>
#include <mcp_types.h>
#include <mcp_socket_utils.h>
#include <mcp_sys_utils.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global server handle for signal handler
static mcp_server_t* g_server = NULL;

// Signal handler to gracefully shut down the server
static void handle_signal(int sig) {
    printf("Received signal %d, shutting down...\n", sig);
    if (g_server) {
        mcp_server_stop(g_server);
        mcp_server_destroy(g_server);
        g_server = NULL;
    }
    exit(0);
}


// Resource callback function
static mcp_error_code_t resource_callback(mcp_server_t* server, const char* resource_path,
                                        void* user_data,
                                        mcp_content_item_t*** content, size_t* content_count,
                                        char** error_message) {
    (void)server; // Suppress unused parameter warning
    (void)user_data; // Suppress unused parameter warning

    mcp_log_info("Resource requested: %s", resource_path);

    // Handle a simple resource
    if (strcmp(resource_path, "/info") == 0) {
        *content_count = 1;
        *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
        if (!*content) {
            *error_message = mcp_strdup("Out of memory");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
        if (!item) {
            free(*content);
            *content = NULL;
            *error_message = mcp_strdup("Out of memory");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        // Fill in the content item
        item->type = MCP_CONTENT_TYPE_TEXT;
        item->mime_type = mcp_strdup("text/plain");
        item->data = mcp_strdup("High-capacity WebSocket server example!");
        item->data_size = strlen((char*)item->data);

        (*content)[0] = item;
        *content_count = 1;
        return MCP_ERROR_NONE;
    }

    *error_message = mcp_strdup("Resource not found");
    return MCP_ERROR_RESOURCE_NOT_FOUND;
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Default configuration
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* path = "/ws";
    uint32_t max_clients = 1024;
    uint32_t segment_count = 16;
    uint32_t buffer_pool_size = 256;
    uint32_t buffer_size = 4096;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--max-clients") == 0 && i + 1 < argc) {
            max_clients = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--segment-count") == 0 && i + 1 < argc) {
            segment_count = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--buffer-pool-size") == 0 && i + 1 < argc) {
            buffer_pool_size = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            buffer_size = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST               Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT               Port to bind to (default: 8080)\n");
            printf("  --path PATH               WebSocket path (default: /ws)\n");
            printf("  --max-clients NUM         Maximum number of clients (default: 1024)\n");
            printf("  --segment-count NUM       Number of mutex segments (default: 16)\n");
            printf("  --buffer-pool-size NUM    Size of buffer pool (default: 256)\n");
            printf("  --buffer-size NUM         Size of each buffer in pool (default: 4096)\n");
            printf("  --help                    Show this help message\n");
            return 0;
        }
    }

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Initialize thread-local arena for JSON parsing
    if (mcp_arena_init_current_thread(4096) != 0) {
        printf("Failed to initialize thread-local arena\n");
        return 1;
    }

    printf("High-Capacity WebSocket Server Example\n");
    printf("Binding to %s:%d%s\n", host, port, path);
    printf("Maximum clients: %u\n", max_clients);
    printf("Segment count: %u\n", segment_count);
    printf("Buffer pool size: %u\n", buffer_pool_size);
    printf("Buffer size: %u\n", buffer_size);

    // Create WebSocket transport configuration with high capacity settings
    mcp_transport_config_t transport_config = {0};
    transport_config.ws.host = host;
    transport_config.ws.port = port;
    transport_config.ws.path = path;
    transport_config.ws.use_ssl = 0; // No SSL for this example

    // Create a custom WebSocket configuration with high capacity settings
    mcp_websocket_config_t ws_config = {
        .host = host,
        .port = port,
        .path = path,
        .use_ssl = 0,
        .max_clients = max_clients,
        .segment_count = segment_count,
        .buffer_pool_size = buffer_pool_size,
        .buffer_size = buffer_size
    };

    // Create transport directly using the WebSocket configuration
    mcp_transport_t* transport = mcp_transport_websocket_server_create(&ws_config);

    if (!transport) {
        mcp_log_error("Failed to create WebSocket transport");
        return 1;
    }

    // Create server configuration
    mcp_server_config_t server_config = {
        .name = "high-capacity-websocket-server",
        .version = "1.0.0",
        .description = "High-Capacity WebSocket MCP Server Example",
        .thread_pool_size = 4, // Increased thread pool for better concurrency
        .task_queue_size = 64, // Increased task queue for higher throughput
        .max_message_size = 1024 * 10, // 10KB limit for example
    };

    mcp_server_capabilities_t capabilities = {
        .resources_supported = true,
        .tools_supported = true
    };

    // Create server
    g_server = mcp_server_create(&server_config, &capabilities);
    if (!g_server) {
        mcp_log_error("Failed to create server");
        mcp_transport_destroy(transport);
        return 1;
    }

    // Set resource handler
    if (mcp_server_set_resource_handler(g_server, resource_callback, NULL) != 0) {
        mcp_log_error("Failed to set resource handler");
        mcp_server_destroy(g_server);
        mcp_transport_destroy(transport);
        return 1;
    }

    // Start server - the transport will use the server's internal message handler
    if (mcp_server_start(g_server, transport) != 0) {
        mcp_log_error("Failed to start server");
        mcp_server_destroy(g_server);
        mcp_transport_destroy(transport);
        return 1;
    }

    mcp_log_info("Server started successfully");

    // Wait for signal to exit
    printf("Press Ctrl+C to exit\n");
    while (g_server) {
        mcp_sleep_ms(1000);

        // Get and print server statistics every 5 seconds
        static int counter = 0;
        if (++counter % 5 == 0) {
            uint32_t active_clients = 0;
            uint32_t peak_clients = 0;
            uint32_t total_connections = 0;
            uint32_t rejected_connections = 0;
            double uptime_seconds = 0;

            mcp_transport_websocket_server_get_stats(
                transport,
                &active_clients,
                &peak_clients,
                &total_connections,
                &rejected_connections,
                &uptime_seconds
            );

            printf("Server stats: active=%u, peak=%u, total=%u, rejected=%u, uptime=%.1f seconds\n",
                   active_clients, peak_clients, total_connections, rejected_connections, uptime_seconds);
        }
    }

    // Clean up (should not reach here due to signal handler)
    mcp_server_stop(g_server);
    mcp_server_destroy(g_server);
    mcp_arena_destroy_current_thread();

    return 0;
}
