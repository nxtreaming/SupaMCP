#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <mcp_server.h>
#include <mcp_tcp_transport.h>
#include <mcp_log.h>
#include <mcp_string_utils.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(seconds) Sleep((seconds) * 1000)
#else
#include <unistd.h>
#endif

// Global server instance for signal handler
static mcp_server_t* g_server = NULL;

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    if (g_server) {
        printf("\nReceived signal %d, initiating graceful shutdown...\n", sig);
        mcp_server_stop(g_server);
    }
}

// Example resource handler
mcp_error_code_t resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    (void)server; // Unused parameter
    (void)user_data; // Unused parameter

    *content = NULL;
    *content_count = 0;

    // Simulate a slow resource handler
    if (strcmp(uri, "test://slow") == 0) {
        printf("Processing slow resource request (sleeping for 3 seconds)...\n");
        sleep(3); // Simulate slow processing

        // Create a content item
        mcp_content_item_t** items = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
        if (!items) {
            *error_message = mcp_strdup("Memory allocation failed");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        const char* response_text = "This is a slow response";
        items[0] = mcp_content_item_create(
            MCP_CONTENT_TYPE_TEXT,
            "text/plain",
            response_text,
            strlen(response_text) +1
        );

        if (!items[0]) {
            free(items);
            *error_message = mcp_strdup("Failed to create content item");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        *content = items;
        *content_count = 1;
        printf("Slow resource request completed\n");
        return MCP_ERROR_NONE;
    }

    // Handle normal resource
    if (strcmp(uri, "test://resource") == 0) {
        // Create a content item
        mcp_content_item_t** items = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
        if (!items) {
            *error_message = mcp_strdup("Memory allocation failed");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        const char* response_text = "This is a test resource";
        items[0] = mcp_content_item_create(
            MCP_CONTENT_TYPE_TEXT,
            "text/plain",
            response_text,
            strlen(response_text) + 1
        );

        if (!items[0]) {
            free(items);
            *error_message = mcp_strdup("Failed to create content item");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        *content = items;
        *content_count = 1;
        return MCP_ERROR_NONE;
    }

    *error_message = mcp_strdup("Resource not found");
    return MCP_ERROR_RESOURCE_NOT_FOUND;
}

int main(int argc, char* argv[]) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Parse command line arguments
    uint16_t port = 8080;
    uint32_t shutdown_timeout_ms = 5000; // Default 5 seconds

    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
    }

    if (argc > 2) {
        shutdown_timeout_ms = (uint32_t)atoi(argv[2]);
    }

    printf("Starting server on port %u with graceful shutdown timeout of %u ms\n", port, shutdown_timeout_ms);

    // Set up signal handlers
    #ifdef _WIN32
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    #else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    #endif

    // Create server configuration
    mcp_server_config_t config = {
        .name = "graceful-shutdown-example",
        .version = "1.0.0",
        .description = "Example server demonstrating graceful shutdown",
        .thread_pool_size = 4,
        .task_queue_size = 16,
        .enable_graceful_shutdown = true,
        .graceful_shutdown_timeout_ms = shutdown_timeout_ms
    };

    // Create server capabilities
    mcp_server_capabilities_t capabilities = {
        .resources_supported = true,
        .tools_supported = false
    };

    // Create server
    mcp_server_t* server = mcp_server_create(&config, &capabilities);
    if (!server) {
        printf("Failed to create server\n");
        return 1;
    }

    // Store server in global variable for signal handler
    g_server = server;

    // Set resource handler
    mcp_server_set_resource_handler(server, resource_handler, NULL);

    // Add resources
    mcp_resource_t resource1 = {
        .uri = "test://resource",
        .name = "Test Resource",
        .mime_type = "text/plain",
        .description = "A test resource"
    };

    mcp_resource_t resource2 = {
        .uri = "test://slow",
        .name = "Slow Resource",
        .mime_type = "text/plain",
        .description = "A slow-processing resource"
    };

    mcp_server_add_resource(server, &resource1);
    mcp_server_add_resource(server, &resource2);

    // Create TCP server transport
    mcp_transport_t* transport = mcp_transport_tcp_create("0.0.0.0", port, 30000); // 30 seconds idle timeout
    if (!transport) {
        printf("Failed to create transport\n");
        mcp_server_destroy(server);
        return 1;
    }

    // Start server
    printf("Starting server...\n");
    if (mcp_server_start(server, transport) != 0) {
        printf("Failed to start server\n");
        mcp_transport_destroy(transport);
        mcp_server_destroy(server);
        return 1;
    }

    printf("Server started. Press Ctrl+C to initiate graceful shutdown.\n");
    printf("You can test the server with curl:\n");
    printf("  curl -X POST -H \"Content-Type: application/json\" -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"read_resource\",\"params\":{\"uri\":\"test://resource\"}}' http://localhost:%u\n", port);
    printf("  curl -X POST -H \"Content-Type: application/json\" -d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"read_resource\",\"params\":{\"uri\":\"test://slow\"}}' http://localhost:%u\n", port);

    // Wait for user input to stop the server
    printf("Press Enter to stop the server...\n");
    getchar();

    // Stop the server (graceful shutdown)
    printf("Stopping server...\n");
    mcp_server_stop(server);

    // Clean up
    mcp_transport_destroy(transport);
    mcp_server_destroy(server);
    g_server = NULL;

    printf("Server stopped\n");
    return 0;
}
