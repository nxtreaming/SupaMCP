/**
 * @file http_streamable_server.c
 * @brief Example HTTP Streamable transport server
 *
 * This example demonstrates how to use the Streamable HTTP transport
 * as specified in MCP 2025-03-26.
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
#include "mcp_http_streamable_transport.h"
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
 * @brief Tool handler for the echo tool
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

    (*content)[0] = mcp_content_item_create(MCP_CONTENT_TYPE_TEXT, "text/plain", text, strlen(text));
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

/**
 * @brief Tool handler for the reverse tool
 */
static mcp_error_code_t reverse_tool_handler(
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
    
    if (strcmp(name, "reverse") != 0) {
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
    size_t len = strlen(text);
    
    // Reverse the text
    char* reversed = (char*)malloc(len + 1);
    if (reversed == NULL) {
        *is_error = true;
        *error_message = mcp_strdup("Memory allocation failed");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    for (size_t i = 0; i < len; i++) {
        reversed[i] = text[len - 1 - i];
    }
    reversed[len] = '\0';

    // Create response content
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        free(reversed);
        *is_error = true;
        *error_message = mcp_strdup("Memory allocation failed");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    (*content)[0] = mcp_content_item_create(MCP_CONTENT_TYPE_TEXT, "text/plain", reversed, len);
    free(reversed);

    if ((*content)[0] == NULL) {
        free(*content);
        *is_error = true;
        *error_message = mcp_strdup("Failed to create content item");
        return MCP_ERROR_INTERNAL_ERROR;
    }
    
    *content_count = 1;
    *is_error = false;
    
    printf("Reverse tool called with text: %s\n", text);
    return MCP_ERROR_NONE;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* mcp_endpoint = "/mcp";
    bool enable_sessions = true;
    bool enable_legacy = true;
    
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
    
    printf("Starting MCP Streamable HTTP Server...\n");
    printf("Host: %s\n", host);
    printf("Port: %d\n", port);
    printf("MCP Endpoint: %s\n", mcp_endpoint);
    printf("Sessions: %s\n", enable_sessions ? "enabled" : "disabled");
    printf("Legacy endpoints: %s\n", enable_legacy ? "enabled" : "disabled");
    printf("\n");
    
    // Create transport configuration
    mcp_http_streamable_config_t config = MCP_HTTP_STREAMABLE_CONFIG_DEFAULT;
    config.host = host;
    config.port = port;
    config.mcp_endpoint = mcp_endpoint;
    config.enable_sessions = enable_sessions;
    config.enable_legacy_endpoints = enable_legacy;
    config.validate_origin = true;
    config.allowed_origins = "http://localhost:*,https://localhost:*,http://127.0.0.1:*,https://127.0.0.1:*";
    
    // Create transport
    g_transport = mcp_transport_http_streamable_create(&config);
    if (g_transport == NULL) {
        fprintf(stderr, "Failed to create Streamable HTTP transport\n");
        return 1;
    }
    
    // Create server configuration
    mcp_server_config_t server_config = {
        .name = "SupaMCP Streamable HTTP Server",
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
    
    // Register tools
    mcp_tool_t* echo_tool = mcp_tool_create("echo", "Echo the input text");
    if (echo_tool) {
        mcp_tool_add_param(echo_tool, "text", "string", "Text to echo", true);
        mcp_server_add_tool(g_server, echo_tool);
        mcp_tool_free(echo_tool);
    }
    
    mcp_tool_t* reverse_tool = mcp_tool_create("reverse", "Reverse the input text");
    if (reverse_tool) {
        mcp_tool_add_param(reverse_tool, "text", "string", "Text to reverse", true);
        mcp_server_add_tool(g_server, reverse_tool);
        mcp_tool_free(reverse_tool);
    }
    
    // Set tool handlers
    mcp_server_set_tool_handler(g_server, echo_tool_handler, NULL);
    
    // Start server with transport
    if (mcp_server_start(g_server, g_transport) != 0) {
        fprintf(stderr, "Failed to start server\n");
        mcp_server_destroy(g_server);
        mcp_transport_destroy(g_transport);
        return 1;
    }
    
    printf("Server started successfully!\n");
    printf("MCP endpoint: http://%s:%d%s\n", host, port, mcp_endpoint);
    
    if (enable_legacy) {
        printf("Legacy endpoints:\n");
        printf("  - http://%s:%d/call_tool\n", host, port);
        printf("  - http://%s:%d/events\n", host, port);
        printf("  - http://%s:%d/tools\n", host, port);
    }
    
    if (enable_sessions) {
        printf("Session management: enabled\n");
        printf("Session count: %zu\n", mcp_transport_http_streamable_get_session_count(g_transport));
    }
    
    printf("\nPress Ctrl+C to stop the server.\n");

    // Wait for server to finish (simple loop since mcp_server_wait doesn't exist)
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
