#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "mcp_server.h"
#include "mcp_transport_factory.h"
#include "mcp_websocket_transport.h"
#include "mcp_log.h"
#include "mcp_json_rpc.h"
#include "mcp_string_utils.h"
#include "mcp_thread_local.h"
#include "mcp_json.h"
#include "mcp_socket_utils.h"

// Global server instance for signal handling
static mcp_server_t* g_server = NULL;

// Signal handler to gracefully shut down the server
static void handle_signal(int sig) {
    if (g_server) {
        printf("Received signal %d, shutting down...\n", sig);
        //mcp_server_t* server_to_stop = g_server;
        g_server = NULL; // Set to NULL first so the main loop can exit
        // Skip calling mcp_server_stop to allow faster shutdown
        //mcp_server_stop(server_to_stop);
    }
}

// Echo tool handler - handles tool calls
static mcp_error_code_t echo_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message
) {
    (void)server; // Unused
    (void)user_data; // Unused
    (void)name; // Unused

    // Initialize output parameters
    *content = NULL;
    *content_count = 0;
    *is_error = false;
    *error_message = NULL;

    // Extract message from params
    const char* message = NULL;
    mcp_json_t* message_node = NULL;

    if (params != NULL && mcp_json_get_type(params) == MCP_JSON_OBJECT) {
        message_node = mcp_json_object_get_property(params, "message");
        if (message_node != NULL && mcp_json_get_type(message_node) == MCP_JSON_STRING) {
            mcp_json_get_string(message_node, &message);
        }
    }

    // Create response
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (!*content) {
        *error_message = mcp_strdup("Memory allocation failed");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Create a content item
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!item) {
        free(*content);
        *content = NULL;
        *error_message = mcp_strdup("Failed to create content item");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Fill in the content item
    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");

    if (message) {
        char response_buffer[1024];
        snprintf(response_buffer, sizeof(response_buffer), "Echo: %s", message);
        item->data = mcp_strdup(response_buffer);
    } else {
        item->data = mcp_strdup("Echo response: No message provided");
    }

    item->data_size = strlen((char*)item->data);

    (*content)[0] = item;
    *content_count = 1;
    return MCP_ERROR_NONE;
}

// Echo handler - handles resource requests
static mcp_error_code_t echo_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    (void)server; // Unused
    (void)user_data; // Unused

    // Initialize output parameters
    *content = NULL;
    *content_count = 0;

    // Check if this is an echo request
    if (strncmp(uri, "echo://", 7) == 0) {
        // Allocate resources for error message
        *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
        if (!*content) {
            *error_message = mcp_strdup("Memory allocation failed");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        // Create a content item for this URI
        mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
        if (!item) {
            free(*content);
            *content = NULL;
            *error_message = mcp_strdup("Failed to create content item");
            return MCP_ERROR_INTERNAL_ERROR;
        }

        // Fill in the content item
        item->type = MCP_CONTENT_TYPE_TEXT;
        item->mime_type = mcp_strdup("text/plain");
        item->data = mcp_strdup("Hello from WebSocket server!");
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

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST     Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT     Port to listen on (default: 8080)\n");
            printf("  --path PATH     WebSocket endpoint path (default: /ws)\n");
            printf("  --help          Show this help message\n");
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

    // Create WebSocket transport configuration
    mcp_transport_config_t transport_config = {0};
    transport_config.ws.host = host;
    transport_config.ws.port = port;
    transport_config.ws.path = path;
    transport_config.ws.use_ssl = 0; // No SSL for this example

    // Create transport
    mcp_transport_t* transport = mcp_transport_factory_create(
        MCP_TRANSPORT_WS_SERVER,
        &transport_config
    );

    if (!transport) {
        mcp_log_error("Failed to create WebSocket transport");
        return 1;
    }

    // Create server configuration
    mcp_server_config_t server_config = {
        .name = "websocket-server",
        .version = "1.0.0",
        .description = "WebSocket MCP Server Example",
        .thread_pool_size = 2, // Small pool for example
        .task_queue_size = 16,
        .max_message_size = 1024 * 10, // 10KB limit for example
    };

    mcp_server_capabilities_t capabilities = {
        .resources_supported = false,
        .tools_supported = true
    };

    // Create server
    g_server = mcp_server_create(&server_config, &capabilities);
    if (!g_server) {
        mcp_log_error("Failed to create server");
        mcp_transport_destroy(transport);
        return 1;
    }

    // Register echo resource handler
    mcp_server_set_resource_handler(g_server, echo_handler, NULL);

    // Register echo tool handler
    mcp_server_set_tool_handler(g_server, echo_tool_handler, NULL);

    // Create and register echo tool
    mcp_tool_t* echo_tool = mcp_tool_create("echo", "Echoes back the provided message");
    if (echo_tool) {
        mcp_tool_add_param(echo_tool, "message", "string", "The message to echo", true);
        mcp_server_add_tool(g_server, echo_tool);
        mcp_tool_free(echo_tool); // Server makes a copy
    }

    // Start server
    printf("Starting WebSocket server on %s:%d%s\n", host, port, path);
    if (mcp_server_start(g_server, transport) != 0) {
        mcp_log_error("Failed to start server");
        mcp_server_destroy(g_server);
        return 1;
    }

    // Wait for server to stop (via signal handler)
    printf("Server running. Press Ctrl+C to stop.\n");
    mcp_server_t* server_to_destroy = g_server;
    while (g_server) {
        mcp_sleep_ms(100); // Sleep for 100 milliseconds
    }

    // Clean up
    if (server_to_destroy) {
        mcp_server_destroy(server_to_destroy);
    }

    // Close log file
    mcp_log_close();

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();

    printf("Server shutdown complete\n");
    return 0;
}
