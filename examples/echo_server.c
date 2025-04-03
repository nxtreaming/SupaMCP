#include <mcp_server.h>
#include <mcp_transport_factory.h>
#include <mcp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static mcp_server_t* g_echo_server = NULL;

// Simple echo tool handler
static int echo_tool_handler(
    mcp_server_t* server,
    const char* name,
    const char* params_json, // Expecting JSON like {"text": "some string"}
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count,
    bool* is_error)
{
    (void)server; (void)user_data; (void)name; // Unused

    log_message(LOG_LEVEL_INFO, "Echo tool called with params: %s", params_json);

    *is_error = false;
    *content = NULL;
    *content_count = 0;
    char* echo_text = NULL;

    // Basic parsing (a real server might use a JSON library here)
    // Find "text": "..." part
    const char* text_key = "\"text\":";
    const char* key_ptr = strstr(params_json, text_key);
    if (key_ptr) {
        const char* value_start = key_ptr + strlen(text_key);
        while (*value_start == ' ' || *value_start == '\t') value_start++; // Skip whitespace
        if (*value_start == '"') {
            value_start++; // Skip opening quote
            const char* value_end = strchr(value_start, '"');
            if (value_end) {
                size_t len = value_end - value_start;
                echo_text = (char*)malloc(len + 1);
                if (echo_text) {
                    memcpy(echo_text, value_start, len);
                    echo_text[len] = '\0';
                }
            }
        }
    }

    if (!echo_text) {
        // If parsing failed or text not found, echo an error message
        echo_text = mcp_strdup("Error: Could not parse 'text' parameter.");
        *is_error = true;
        if (!echo_text) return -1; // Allocation failed even for error message
    }

    // Create the response content item
    *content = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!*content) {
        free(echo_text);
        return -1; // Allocation failed
    }

    (*content)->type = MCP_CONTENT_TYPE_TEXT;
    (*content)->mime_type = mcp_strdup("text/plain");
    (*content)->data = echo_text; // Transfer ownership of echo_text
    (*content)->data_size = strlen(echo_text);

    if (!(*content)->mime_type) { // Check mime type allocation
        mcp_content_item_free(*content); // Frees data and struct pointer
        *content = NULL;
        return -1;
    }

    *content_count = 1;
    return 0; // Success
}

static void echo_cleanup(void) {
    log_message(LOG_LEVEL_INFO, "Cleaning up echo server...");
    if (g_echo_server != NULL) {
        mcp_server_destroy(g_echo_server);
        g_echo_server = NULL;
    }
    close_logging();
}

static void echo_signal_handler(int sig) {
    log_message(LOG_LEVEL_INFO, "Echo server received signal %d, shutting down...", sig);
    if (g_echo_server) {
         mcp_server_stop(g_echo_server);
    }
    // Let atexit handle the rest
    g_echo_server = NULL; // Signal main loop to exit
}

int main(int argc, char** argv) {
    (void)argc; (void)argv; // Basic example, no command line args yet

    // Initialize logging (log to stdout for example)
    init_logging(NULL, LOG_LEVEL_INFO);

    // Setup cleanup and signal handling
    atexit(echo_cleanup);
    signal(SIGINT, echo_signal_handler);
    signal(SIGTERM, echo_signal_handler);
#ifndef _WIN32
    signal(SIGHUP, echo_signal_handler);
#endif

    log_message(LOG_LEVEL_INFO, "Starting Echo MCP server...");

    // Server Configuration
    mcp_server_config_t server_config = {
        .name = "echo-server",
        .version = "1.0",
        .description = "Simple MCP Echo Server Example",
        .thread_pool_size = 2, // Small pool for example
        .task_queue_size = 16,
        .max_message_size = 1024 * 10, // 10KB limit for example
        // Disable cache, rate limiting, api key for simplicity
        .cache_capacity = 0,
        .rate_limit_window_seconds = 0,
        .api_key = NULL
    };
    mcp_server_capabilities_t capabilities = {
        .resources_supported = false, // No resources in this example
        .tools_supported = true
    };

    // Create Server
    g_echo_server = mcp_server_create(&server_config, &capabilities);
    if (g_echo_server == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create echo server");
        return 1;
    }

    // Create and add the "echo" tool
    mcp_tool_t* echo_tool = mcp_tool_create("echo", "Echoes back the provided text parameter.");
    if (!echo_tool || mcp_tool_add_param(echo_tool, "text", "string", "The text to echo", true) != 0) {
         log_message(LOG_LEVEL_ERROR, "Failed to create or add param to echo tool");
         mcp_tool_free(echo_tool);
         mcp_server_destroy(g_echo_server);
         return 1;
    }
    if (mcp_server_add_tool(g_echo_server, echo_tool) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add echo tool to server");
        mcp_tool_free(echo_tool);
        mcp_server_destroy(g_echo_server);
        return 1;
    }
    mcp_tool_free(echo_tool); // Server makes a copy

    // Set the tool handler
    if (mcp_server_set_tool_handler(g_echo_server, echo_tool_handler, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to set tool handler");
        mcp_server_destroy(g_echo_server);
        return 1;
    }

    // Create TCP Transport using the Transport Factory
    const char* host = "127.0.0.1";
    uint16_t port = 18889; // Use a different port than default server
    uint32_t idle_timeout = 300000; // 5 minutes idle timeout

    // Configure transport using factory config
    mcp_transport_config_t transport_config = {0};
    transport_config.tcp.host = host;
    transport_config.tcp.port = port;
    transport_config.tcp.idle_timeout_ms = idle_timeout;

    // Create transport using factory
    mcp_transport_t* transport = mcp_transport_factory_create(
        MCP_TRANSPORT_TCP,
        &transport_config
    );

    if (transport == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create TCP transport");
        mcp_server_destroy(g_echo_server);
        return 1;
    }

    // Start the server
    log_message(LOG_LEVEL_INFO, "Starting server on %s:%u...", host, port);
    if (mcp_server_start(g_echo_server, transport) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to start server");
        mcp_transport_destroy(transport); // Destroy transport if start fails
        mcp_server_destroy(g_echo_server);
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "Echo server running. Press Ctrl+C to stop.");

    // Keep running until signaled
    while (g_echo_server != NULL) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    log_message(LOG_LEVEL_INFO, "Echo server shut down gracefully.");
    return 0;
}
