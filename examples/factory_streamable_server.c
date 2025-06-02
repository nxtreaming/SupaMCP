/**
 * @file factory_streamable_server.c
 * @brief Example using Transport Factory to create Streamable HTTP transport
 *
 * This example demonstrates how to use the transport factory to create
 * a Streamable HTTP transport server.
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

#include "mcp_server.h"
#include "mcp_transport_factory.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include "mcp_string_utils.h"

// Global server instance for signal handling
static mcp_server_t* g_server = NULL;
static mcp_transport_t* g_transport = NULL;

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    
    if (g_server) {
        mcp_server_stop(g_server);
    }
    
    if (g_transport) {
        mcp_transport_stop(g_transport);
    }
}

/**
 * @brief Simple echo tool handler
 */
static mcp_error_code_t echo_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message) {
    
    (void)server;
    (void)user_data;
    
    if (strcmp(name, "echo") != 0) {
        *is_error = true;
        *error_message = mcp_strdup("Unknown tool");
        return MCP_ERROR_INVALID_PARAMS;
    }
    
    // Extract text parameter
    const mcp_json_t* text_param = mcp_json_object_get_property(params, "text");
    if (!text_param || !mcp_json_is_string(text_param)) {
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid 'text' parameter");
        return MCP_ERROR_INVALID_PARAMS;
    }
    
    const char* text = mcp_json_string_value(text_param);
    
    // Create response content
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        *is_error = true;
        *error_message = mcp_strdup("Memory allocation failed");
        return MCP_ERROR_INTERNAL_ERROR;
    }
    
    (*content)[0] = mcp_content_item_create(MCP_CONTENT_TYPE_TEXT, "text/plain", text, strlen(text)+1);
    if ((*content)[0] == NULL) {
        free(*content);
        *is_error = true;
        *error_message = mcp_strdup("Failed to create content item");
        return MCP_ERROR_INTERNAL_ERROR;
    }
    
    *content_count = 1;
    *is_error = false;
    
    printf("Echo tool called with text: %s\n", text);
    return MCP_ERROR_NONE;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* mcp_endpoint = "/mcp";
    
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
    }
    if (argc > 2) {
        host = argv[2];
    }
    if (argc > 3) {
        mcp_endpoint = argv[3];
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logging
    mcp_log_set_level(MCP_LOG_LEVEL_INFO);
    
    printf("Starting MCP Streamable HTTP Server using Transport Factory...\n");
    printf("Host: %s\n", host);
    printf("Port: %d\n", port);
    printf("MCP Endpoint: %s\n", mcp_endpoint);
    printf("\n");
    
    // Create transport configuration using factory
    mcp_transport_config_t config = {0};
    config.http_streamable.host = host;
    config.http_streamable.port = port;
    config.http_streamable.use_ssl = 0;
    config.http_streamable.mcp_endpoint = mcp_endpoint;
    config.http_streamable.enable_sessions = 1;
    config.http_streamable.session_timeout_seconds = 3600;
    config.http_streamable.validate_origin = 1;
    config.http_streamable.allowed_origins = "http://localhost:*,https://localhost:*,http://127.0.0.1:*,https://127.0.0.1:*";
    config.http_streamable.enable_cors = 1;
    config.http_streamable.cors_allow_origin = "*";
    config.http_streamable.cors_allow_methods = "GET, POST, OPTIONS, DELETE";
    config.http_streamable.cors_allow_headers = "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID";
    config.http_streamable.cors_max_age = 86400;
    config.http_streamable.enable_sse_resumability = 1;
    config.http_streamable.max_stored_events = 1000;
    config.http_streamable.send_heartbeats = 1;
    config.http_streamable.heartbeat_interval_ms = 30000;
    config.http_streamable.enable_legacy_endpoints = 1;
    
    // Create transport using factory
    g_transport = mcp_transport_factory_create(MCP_TRANSPORT_HTTP_STREAMABLE, &config);
    if (g_transport == NULL) {
        fprintf(stderr, "Failed to create Streamable HTTP transport using factory\n");
        return 1;
    }
    
    // Create server configuration
    mcp_server_config_t server_config = {
        .name = "SupaMCP Factory Streamable HTTP Server",
        .version = "1.0.0"
    };
    
    mcp_server_capabilities_t capabilities = {
        .tools_supported = true,
        .resources_supported = false
    };
    
    // Create server
    g_server = mcp_server_create(&server_config, &capabilities);
    if (g_server == NULL) {
        fprintf(stderr, "Failed to create MCP server\n");
        mcp_transport_destroy(g_transport);
        return 1;
    }
    
    // Register echo tool
    mcp_tool_t* echo_tool = mcp_tool_create("echo", "Echo the input text");
    if (echo_tool) {
        mcp_tool_add_param(echo_tool, "text", "string", "Text to echo", true);
        mcp_server_add_tool(g_server, echo_tool);
        mcp_tool_free(echo_tool);
    }
    
    // Set tool handler
    mcp_server_set_tool_handler(g_server, echo_tool_handler, NULL);
    
    // Start server with transport
    if (mcp_server_start(g_server, g_transport) != 0) {
        fprintf(stderr, "Failed to start server\n");
        mcp_server_destroy(g_server);
        mcp_transport_destroy(g_transport);
        return 1;
    }
    
    printf("Server started successfully using Transport Factory!\n");
    printf("MCP endpoint: http://%s:%d%s\n", host, port, mcp_endpoint);
    printf("Legacy endpoints:\n");
    printf("  - http://%s:%d/call_tool\n", host, port);
    printf("  - http://%s:%d/events\n", host, port);
    printf("  - http://%s:%d/tools\n", host, port);
    printf("Session management: enabled\n");
    printf("\nPress Ctrl+C to stop the server.\n");
    
    // Wait for server to finish (simple loop)
    while (g_server != NULL) {
        // Sleep for 1 second
        #ifdef _WIN32
        Sleep(1000);
        #else
        sleep(1);
        #endif
    }
    
    // Cleanup
    printf("Shutting down...\n");
    mcp_server_destroy(g_server);
    mcp_transport_destroy(g_transport);
    
    printf("Server stopped.\n");
    return 0;
}
