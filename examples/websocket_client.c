#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

#include "mcp_json_utils.h"
#include "mcp_client.h"
#include "mcp_transport_factory.h"
#include "mcp_websocket_transport.h"
#include "mcp_log.h"
#include "mcp_json_rpc.h"
#include "mcp_socket_utils.h"
#include "mcp_thread_local.h"

// Global variables
static mcp_client_t* g_client = NULL;
static mcp_transport_t* g_transport = NULL;
static mcp_transport_config_t g_transport_config;
static mcp_client_config_t g_client_config;
static volatile bool g_running = true;

// Signal handler to gracefully shut down the client
static void handle_signal(int sig) {
    if (g_client) {
        printf("\nReceived signal %d, shutting down...\n", sig);
        g_running = false;

        // Cancel any pending requests and destroy client immediately
        mcp_client_destroy(g_client);
        g_client = NULL;

        // Close log file
        mcp_log_close();

        // Clean up thread-local arena
        mcp_arena_destroy_current_thread();

        printf("Client shutdown complete\n");

        // Exit immediately
        exit(0);
    }
}

// Helper function to read a line of input from the user
static char* read_user_input(const char* prompt) {
    static char buffer[1024];

    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NULL;
    }

    // Remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    return buffer;
}

// Function to create a new client connection
static bool create_client_connection(const char* host, uint16_t port, const char* path) {
    // Clean up existing client and transport if any
    if (g_client) {
        mcp_client_destroy(g_client);
        g_client = NULL;
    }

    if (g_transport) {
        // Transport is destroyed by client, no need to destroy it here
        g_transport = NULL;
    }

    // Create WebSocket transport configuration
    g_transport_config = (mcp_transport_config_t){0};
    g_transport_config.ws.host = host;
    g_transport_config.ws.port = port;
    g_transport_config.ws.path = path;
    g_transport_config.ws.use_ssl = 0; // No SSL for this example

    // Create transport
    g_transport = mcp_transport_factory_create(
        MCP_TRANSPORT_WS_CLIENT,
        &g_transport_config
    );

    if (!g_transport) {
        mcp_log_error("Failed to create WebSocket transport");
        return false;
    }

    // Create client configuration
    g_client_config = (mcp_client_config_t){
        .request_timeout_ms = 5000 // 5 second timeout
    };

    // Create client
    g_client = mcp_client_create(&g_client_config, g_transport);
    if (!g_client) {
        mcp_log_error("Failed to create client");
        mcp_transport_destroy(g_transport);
        g_transport = NULL;
        return false;
    }

    printf("Connecting to WebSocket server at %s:%d%s\n", host, port, path);

    // Wait for connection to be established with timeout
    int max_wait_attempts = 100; // 100 * 100ms = 10 seconds (longer timeout)
    int wait_attempts = 0;
    int connection_state = 0;

    while (wait_attempts < max_wait_attempts) {
        connection_state = mcp_client_is_connected(g_client);
        if (connection_state == 1) {
            printf("Connected to server (verified).\n");
            return true;
        }

        // Wait a bit before checking again
        mcp_sleep_ms(100);
        wait_attempts++;

        // Print progress every second
        if (wait_attempts % 10 == 0) {
            printf("Waiting for connection to be established... (%d seconds)\n", wait_attempts / 10);
        }
    }

    if (connection_state != 1) {
        printf("Error: Connection not established after %d seconds (state: %d).\n",
               max_wait_attempts / 10, connection_state);

        // Destroy the client since connection failed
        if (g_client) {
            mcp_client_destroy(g_client);
            g_client = NULL;
        }

        return false;
    }

    return true;
}

// Function to check if client is connected and reconnect if necessary
static bool ensure_client_connected(const char* host, uint16_t port, const char* path) {
    // Simple check - if client is NULL, we definitely need to reconnect
    if (g_client == NULL) {
        printf("Client not connected. Reconnecting...\n");
        return create_client_connection(host, port, path);
    }

    // Check if the transport is still valid
    if (g_transport == NULL) {
        printf("Transport is invalid. Reconnecting...\n");

        // Destroy the old client before creating a new one
        if (g_client) {
            mcp_client_destroy(g_client);
            g_client = NULL;
        }

        return create_client_connection(host, port, path);
    }

    // Check the actual connection state using the client API
    int connection_state = mcp_client_is_connected(g_client);
    if (connection_state != 1) {
        printf("Client connection is not established (state: %d). Reconnecting...\n", connection_state);

        // Destroy the old client before creating a new one
        if (g_client) {
            mcp_client_destroy(g_client);
            g_client = NULL;
        }

        return create_client_connection(host, port, path);
    }
    return true;
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

    // Create initial client connection
    if (!create_client_connection(host, port, path)) {
        mcp_log_error("Failed to create initial client connection");
        return 1;
    }

    // Interactive message loop
    printf("Enter messages to send. Type 'exit' to quit.\n");

    while (g_running) {
        // Get message from user input
        const char* input = read_user_input("Enter message: ");

        // Check for exit command
        if (input == NULL || strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            printf("Exiting...\n");
            break;
        }

        // Skip empty messages
        if (strlen(input) == 0) {
            continue;
        }

        // Copy input to user_message
        char user_message[1024];
        strcpy(user_message, input);

        // Ensure we're still connected before sending
        if (!ensure_client_connected(host, port, path)) {
            printf("Error: Failed to establish connection. Please check server status.\n");
            continue;
        }

        printf("Sending echo request with message: \"%s\"\n", user_message);

        // Try different formats for the echo request
        char params_buffer[1024];
        char* response = NULL;
        mcp_error_code_t error_code = MCP_ERROR_NONE;
        char* error_message = NULL;
        int result = -1;

        // Format 1: Using call_tool with message parameter
        snprintf(params_buffer, sizeof(params_buffer),
            "{\"name\":\"echo\",\"arguments\":{\"message\":\"%s\"}}",
            user_message);
        result = mcp_client_send_request(g_client, "call_tool", params_buffer, &response, &error_code, &error_message);

        // Handle error if any
        if (error_code != MCP_ERROR_NONE || result != 0) {
            printf("Request failed with error code %d: %s (result: %d)\n",
                   error_code, error_message ? error_message : "Unknown error", result);

            if (error_message) {
                free(error_message);
            }

            // Try to reconnect on error
            if (!ensure_client_connected(host, port, path)) {
                printf("Error: Failed to re-establish connection. Please check server status.\n");
            }

            continue;
        }

        // Free error message if it was set (shouldn't be if error_code is NONE)
        if (error_message != NULL) {
            free(error_message);
        }

        // Print response
        printf("Received response: %s\n", response);
        free(response);
    }

    // Connection is closed automatically when the client is destroyed

    // Clean up
    if (g_client) {
        mcp_client_destroy(g_client);
        g_client = NULL;
    }

    // Transport is destroyed by client, no need to destroy it here
    g_transport = NULL;

    // Close log file
    mcp_log_close();

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();

    printf("Client shutdown complete\n");
    return 0;
}
