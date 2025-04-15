#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_client.h"
#include <mcp_tcp_client_transport.h>
#include <mcp_log.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(seconds) Sleep((seconds) * 1000)
#else
#include <unistd.h>
#endif

// Connection state callback
void connection_state_callback(void* user_data, mcp_connection_state_t state, int attempt) {
    (void)user_data; // Unused parameter
    const char* state_str = "UNKNOWN";

    switch (state) {
        case MCP_CONNECTION_STATE_DISCONNECTED:
            state_str = "DISCONNECTED";
            break;
        case MCP_CONNECTION_STATE_CONNECTING:
            state_str = "CONNECTING";
            break;
        case MCP_CONNECTION_STATE_CONNECTED:
            state_str = "CONNECTED";
            break;
        case MCP_CONNECTION_STATE_RECONNECTING:
            state_str = "RECONNECTING";
            break;
        case MCP_CONNECTION_STATE_FAILED:
            state_str = "FAILED";
            break;
    }

    printf("Connection state changed: %s (attempt: %d)\n", state_str, attempt);
}

// Helper function to send a request and print the response
int send_request(mcp_client_t* client, const char* method, const char* params) {
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;

    printf("Sending %s request...\n", method);
    int ret = mcp_client_send_request(client, method, params, &result, &error_code, &error_message);

    if (ret != 0) {
        printf("Failed to send request: %d\n", ret);
        return -1;
    }

    if (error_code != 0) {
        printf("Error in response: %d - %s\n", error_code, error_message ? error_message : "Unknown error");
        free(error_message);
        free(result);
        return -1;
    }

    if (result) {
        printf("Received response: %s\n", result);
        free(result);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Parse command line arguments
    const char* host = "localhost";
    uint16_t port = 8080;

    if (argc > 1) {
        host = argv[1];
    }

    if (argc > 2) {
        port = (uint16_t)atoi(argv[2]);
    }

    printf("Connecting to %s:%u with reconnection enabled\n", host, port);

    // Configure reconnection
    mcp_reconnect_config_t reconnect_config = {
        .enable_reconnect = true,
        .max_reconnect_attempts = 5,
        .initial_reconnect_delay_ms = 1000,
        .max_reconnect_delay_ms = 10000,
        .backoff_factor = 2.0f,
        .randomize_delay = true
    };

    // Create transport with reconnection support
    mcp_transport_t* transport = mcp_tcp_client_create_reconnect(
        host, port, &reconnect_config);

    if (!transport) {
        printf("Failed to create transport\n");
        return 1;
    }

    // Set connection state callback
    mcp_tcp_client_set_connection_state_callback(transport, connection_state_callback, NULL);

    // Create client configuration
    mcp_client_config_t config = {
        .request_timeout_ms = 5000 // 5 seconds timeout
    };

    // Create client
    mcp_client_t* client = mcp_client_create(&config, transport);
    if (!client) {
        printf("Failed to create client\n");
        mcp_transport_destroy(transport);
        return 1;
    }

    // Wait a moment for connection
    sleep(1);

    // Send ping request
    send_request(client, "ping", NULL);

    // Wait a moment
    sleep(2);

    // Send another request
    const char* params = "{\"message\":\"Hello, world!\"}";
    send_request(client, "echo", params);

    // Wait a moment
    sleep(2);

    // Manually trigger reconnection
    printf("Manually triggering reconnection...\n");
    mcp_tcp_client_reconnect(transport);

    // Wait for reconnection
    sleep(5);

    // Send another request after reconnection
    send_request(client, "ping", NULL);

    // Wait a moment
    sleep(2);

    // Clean up
    mcp_client_destroy(client);

    printf("Example completed\n");
    return 0;
}
