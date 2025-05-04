#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "mcp_json_utils.h"
#include "mcp_client.h"
#include "mcp_transport_factory.h"
#include "mcp_websocket_transport.h"
#include "mcp_log.h"
#include "mcp_json_rpc.h"
#include "mcp_socket_utils.h"
#include "mcp_thread_local.h"

// Global client instance for signal handling
static mcp_client_t* g_client = NULL;

// Signal handler to gracefully shut down the client
static void handle_signal(int sig) {
    if (g_client) {
        printf("Received signal %d, shutting down...\n", sig);
        // Client will be destroyed in main
    }
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Default configuration
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* path = "/ws";
    const char* message = "Hello, WebSocket!";

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
        } else if (strcmp(argv[i], "--message") == 0 && i + 1 < argc) {
            message = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST       Host to connect to (default: 127.0.0.1)\n");
            printf("  --port PORT       Port to connect to (default: 8080)\n");
            printf("  --path PATH       WebSocket endpoint path (default: /ws)\n");
            printf("  --message MESSAGE Message to send (default: \"Hello, WebSocket!\")\n");
            printf("  --help            Show this help message\n");
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
        MCP_TRANSPORT_WS_CLIENT,
        &transport_config
    );

    if (!transport) {
        mcp_log_error("Failed to create WebSocket transport");
        return 1;
    }

    // Create client configuration
    mcp_client_config_t client_config = {
        .request_timeout_ms = 5000 // 5 second timeout
    };

    // Create client
    g_client = mcp_client_create(&client_config, transport);
    if (!g_client) {
        mcp_log_error("Failed to create client");
        mcp_transport_destroy(transport);
        return 1;
    }

    // Connection is established automatically when the client is created
    printf("Connecting to WebSocket server at %s:%d%s\n", host, port, path);

    // Add a small delay to ensure connection is established
    mcp_sleep_ms(1000);

    printf("Connected to server. Sending echo request...\n");

    // Create echo request parameters - format as a JSON-RPC call_tool request
    char params_buffer[1024];
    snprintf(params_buffer, sizeof(params_buffer),
        "{\"name\":\"echo\",\"arguments\":{\"message\":\"%s\"}}",
        message);

    // Send echo request
    char* response = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;
    int result = mcp_client_send_request(g_client, "call_tool", params_buffer, &response, &error_code, &error_message);

    // Handle error if any
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Request failed with error code %d: %s", error_code, error_message ? error_message : "Unknown error");
        free(error_message);
        mcp_client_destroy(g_client);
        return 1;
    }

    // Free error message if it was set (shouldn't be if error_code is NONE)
    if (error_message != NULL) {
        free(error_message);
    }

    if (result != 0) {
        mcp_log_error("Request failed with error code %d", result);
        mcp_client_destroy(g_client);
        return 1;
    }

    // Print response
    printf("Received response: %s\n", response);
    free(response);

    // Connection is closed automatically when the client is destroyed

    // Clean up
    mcp_client_destroy(g_client);
    g_client = NULL;

    // Close log file
    mcp_log_close();

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();

    printf("Client shutdown complete\n");
    return 0;
}
