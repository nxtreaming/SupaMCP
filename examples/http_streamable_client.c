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
        printf("Response received:\n%s\n\n", message);
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
 * @brief Send initialize request
 */
static void send_initialize_request(mcp_transport_t* client) {
    const char* initialize_request = 
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": 1,"
        "\"method\": \"initialize\","
        "\"params\": {"
            "\"protocolVersion\": \"2024-11-05\","
            "\"capabilities\": {"
                "\"roots\": {"
                    "\"listChanged\": true"
                "}"
            "},"
            "\"clientInfo\": {"
                "\"name\": \"SupaMCP-Client\","
                "\"version\": \"1.0.0\""
            "}"
        "}"
        "}";
    
    printf("Sending initialize request...\n");
    if (mcp_transport_send(client, initialize_request, strlen(initialize_request)) != 0) {
        printf("Failed to send initialize request\n");
    }
}

/**
 * @brief Send tools list request
 */
static void send_tools_list_request(mcp_transport_t* client) {
    const char* tools_request = 
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": 2,"
        "\"method\": \"tools/list\""
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
        "\"method\": \"tools/call\","
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
    mcp_log_set_level(MCP_LOG_LEVEL_INFO);
    
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
    #ifdef _WIN32
    Sleep(1000);
    #else
    sleep(1);
    #endif
    
    // Send some test requests
    send_initialize_request(g_client);
    
    #ifdef _WIN32
    Sleep(2000);
    #else
    sleep(2);
    #endif
    
    send_tools_list_request(g_client);
    
    #ifdef _WIN32
    Sleep(2000);
    #else
    sleep(2);
    #endif
    
    send_tool_call_request(g_client, "echo", "Hello from client!");
    
    #ifdef _WIN32
    Sleep(2000);
    #else
    sleep(2);
    #endif
    
    send_tool_call_request(g_client, "reverse", "Hello World");
    
    // Keep running until interrupted
    printf("\nClient is running. Press Ctrl+C to stop.\n");
    printf("Session ID: %s\n", mcp_http_streamable_client_get_session_id(g_client));
    
    // Print statistics periodically
    while (g_running) {
        #ifdef _WIN32
        Sleep(5000);
        #else
        sleep(5);
        #endif
        
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
