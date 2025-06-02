/**
 * @file http_streamable_client.c
 * @brief Example HTTP Streamable Client
 *
 * This example demonstrates how to use the HTTP Streamable client transport
 * to connect to an MCP server and exchange messages.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "mcp_http_streamable_client_transport.h"
#include "mcp_log.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include "mcp_sys_utils.h"

// Global client instance for signal handling
static mcp_transport_t* g_client = NULL;
static volatile bool g_running = true;

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = false;
    
    if (g_client) {
        mcp_transport_stop(g_client);
    }
}

/**
 * @brief Connection state change callback
 */
static void state_callback(mcp_transport_t* transport, 
                          mcp_client_connection_state_t old_state,
                          mcp_client_connection_state_t new_state,
                          void* user_data) {
    (void)transport;
    (void)user_data;
    
    const char* state_names[] = {
        "DISCONNECTED", "CONNECTING", "CONNECTED", 
        "SSE_CONNECTING", "SSE_CONNECTED", "RECONNECTING", "ERROR"
    };
    
    printf("Connection state changed: %s -> %s\n", 
           state_names[old_state], state_names[new_state]);
}

/**
 * @brief SSE event callback
 */
static void sse_event_callback(mcp_transport_t* transport,
                              const char* event_id,
                              const char* event_type,
                              const char* data,
                              void* user_data) {
    (void)transport;
    (void)user_data;
    
    printf("SSE Event received:\n");
    if (event_id) {
        printf("  ID: %s\n", event_id);
    }
    if (event_type) {
        printf("  Type: %s\n", event_type);
    }
    if (data) {
        printf("  Data: %s\n", data);
    }
    printf("\n");
}

/**
 * @brief Message response callback
 */
static char* message_callback(void* user_data, const void* data, size_t size, int* error_code) {
    (void)user_data;
    (void)error_code;

    // Convert data to string for display
    char* message = (char*)malloc(size + 1);
    if (message) {
        memcpy(message, data, size);
        message[size] = '\0';
        printf("Response received (%zu bytes):\n%s\n", size, message);

        // Check response type
        if (strstr(message, "\"error\"")) {
            printf("Error response detected!\n");
            if (strstr(message, "Method not found")) {
                printf("Hint: Server doesn't recognize this method. Check method name.\n");
            }
        } else if (strstr(message, "\"result\"")) {
            printf("Success response detected!\n");
        }
        printf("\n");
        free(message);
    }

    // Return NULL as we don't need to send a response
    return NULL;
}

/**
 * @brief Error callback
 */
static void error_callback(void* user_data, int error_code) {
    (void)user_data;

    printf("Transport error occurred: %d\n", error_code);
}

/**
 * @brief Send ping request (server health check)
 */
static void send_ping_request(mcp_transport_t* client) {
    const char* ping_request =
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": 1,"
        "\"method\": \"ping\""
        "}";

    printf("Sending ping request...\n");
    if (mcp_transport_send(client, ping_request, strlen(ping_request)) != 0) {
        printf("Failed to send ping request\n");
    }
}

/**
 * @brief Send tools list request (using correct method name)
 */
static void send_tools_list_request(mcp_transport_t* client) {
    const char* tools_request =
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": 2,"
        "\"method\": \"list_tools\""
        "}";

    printf("Sending tools list request...\n");
    if (mcp_transport_send(client, tools_request, strlen(tools_request)) != 0) {
        printf("Failed to send tools list request\n");
    }
}

/**
 * @brief Send tool call request
 */
static void send_tool_call_request(mcp_transport_t* client, const char* tool_name, const char* text) {
    char* tool_request = (char*)malloc(512);
    if (tool_request == NULL) {
        printf("Failed to allocate tool request buffer\n");
        return;
    }

    snprintf(tool_request, 512,
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": 3,"
        "\"method\": \"call_tool\","
        "\"params\": {"
            "\"name\": \"%s\","
            "\"arguments\": {"
                "\"text\": \"%s\""
            "}"
        "}"
        "}", tool_name, text);

    printf("Sending tool call request (%s)...\n", tool_name);
    if (mcp_transport_send(client, tool_request, strlen(tool_request)) != 0) {
        printf("Failed to send tool call request\n");
    }

    free(tool_request);
}

/**
 * @brief Test SSE connection status
 */
static void test_sse_connection(mcp_transport_t* client) {
    printf("Testing SSE connection status...\n");

    // Get client statistics to check SSE status
    mcp_client_connection_stats_t stats;
    if (mcp_http_streamable_client_get_stats(client, &stats) == 0) {
        printf("SSE Connection Status:\n");
        printf("  - SSE Events Received: %llu\n", (unsigned long long)stats.sse_events_received);
        printf("  - Connection Errors: %llu\n", (unsigned long long)stats.connection_errors);

        if (stats.sse_events_received > 0) {
            printf("SSE connection appears to be working!\n");
        } else {
            printf("No SSE events received yet. Connection may have issues.\n");
        }
    } else {
        printf("Failed to get connection statistics\n");
    }
    printf("\n");
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = (uint16_t)atoi(argv[2]);
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logging
    mcp_log_set_level(MCP_LOG_LEVEL_DEBUG);
    
    printf("Starting MCP Streamable HTTP Client...\n");
    printf("Server: %s:%d\n", host, port);
    printf("\n");
    
    // Create client configuration
    mcp_http_streamable_client_config_t config = MCP_HTTP_STREAMABLE_CLIENT_CONFIG_DEFAULT;
    config.host = host;
    config.port = port;
    config.enable_sessions = true;
    config.enable_sse_streams = true;
    config.auto_reconnect_sse = true;
    
    // Create client transport
    g_client = mcp_transport_http_streamable_client_create(&config);
    if (g_client == NULL) {
        fprintf(stderr, "Failed to create HTTP Streamable client transport\n");
        return 1;
    }
    
    // Set callbacks
    mcp_http_streamable_client_set_state_callback(g_client, state_callback, NULL);
    mcp_http_streamable_client_set_sse_callback(g_client, sse_event_callback, NULL);

    // Start client with callbacks
    if (mcp_transport_start(g_client, message_callback, NULL, error_callback) != 0) {
        fprintf(stderr, "Failed to start client transport\n");
        mcp_transport_destroy(g_client);
        return 1;
    }
    
    printf("Client started successfully!\n");
    
    // Wait a moment for connection to establish
    mcp_sleep_ms(1000);

    // Test SSE connection first
    test_sse_connection(g_client);

    // Send some test requests
    send_ping_request(g_client);

    mcp_sleep_ms(2000);
    send_tools_list_request(g_client);

    mcp_sleep_ms(2000);
    send_tool_call_request(g_client, "echo", "Hello from client!");

    mcp_sleep_ms(2000);
    send_tool_call_request(g_client, "reverse", "Hello World");

    mcp_sleep_ms(2000);
    test_sse_connection(g_client);
    
    // Keep running until interrupted
    printf("\nClient is running. Press Ctrl+C to stop.\n");
    printf("Session ID: %s\n", mcp_http_streamable_client_get_session_id(g_client));
    
    // Print statistics periodically
    while (g_running) {
        mcp_sleep_ms(5000);        
        if (!g_running) break;
        
        mcp_client_connection_stats_t stats;
        if (mcp_http_streamable_client_get_stats(g_client, &stats) == 0) {
            printf("Statistics: Requests=%llu, Responses=%llu, SSE Events=%llu, Errors=%llu\n",
                   (unsigned long long)stats.requests_sent,
                   (unsigned long long)stats.responses_received,
                   (unsigned long long)stats.sse_events_received,
                   (unsigned long long)stats.connection_errors);
        }
    }
    
    // Cleanup
    printf("Shutting down client...\n");
    mcp_transport_destroy(g_client);
    
    printf("Client stopped.\n");
    return 0;
}
