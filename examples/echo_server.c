#include <mcp_server.h>
#include <mcp_transport_factory.h>
#include <mcp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <mcp_json.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static mcp_server_t* g_echo_server = NULL;

// Simple echo tool handler - Updated Signature
static mcp_error_code_t echo_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params, // Now a parsed JSON object
    void* user_data,
    mcp_content_item_t*** content, // Now pointer-to-pointer-to-pointer
    size_t* content_count,
    bool* is_error,
    char** error_message) // Added error message output
{
    (void)server; (void)user_data; (void)name; // Unused

    mcp_log_info("Echo tool called.");

    // Initialize output parameters
    *is_error = false;
    *content = NULL;
    *content_count = 0;
    *error_message = NULL;
    char* echo_text_copy = NULL;
    const char* extracted_text = NULL;
    mcp_error_code_t err_code = MCP_ERROR_NONE;

    // Use mcp_json library to parse params
    if (params == NULL || mcp_json_get_type(params) != MCP_JSON_OBJECT) {
        mcp_log_warn("Echo tool: Invalid or missing params object.");
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid parameters object.");
        err_code = MCP_ERROR_INVALID_PARAMS;
        goto cleanup;
    }

    mcp_json_t* text_node = mcp_json_object_get_property(params, "text");
    if (text_node == NULL || mcp_json_get_type(text_node) != MCP_JSON_STRING || mcp_json_get_string(text_node, &extracted_text) != 0 || extracted_text == NULL) {
        mcp_log_warn("Echo tool: Missing or invalid 'text' string parameter.");
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid 'text' string parameter.");
        err_code = MCP_ERROR_INVALID_PARAMS;
        goto cleanup;
    }

    // Duplicate the extracted text as we need to transfer ownership
    echo_text_copy = mcp_strdup(extracted_text);
    if (!echo_text_copy) {
        mcp_log_error("Echo tool: Failed to duplicate echo text.");
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    // --- Create the response content ---

    // 1. Allocate the array of pointers (size 1)
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (!*content) {
        mcp_log_error("Echo tool: Failed to allocate content array.");
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }
    (*content)[0] = NULL; // Initialize pointer

    // 2. Allocate the content item struct
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!item) {
        mcp_log_error("Echo tool: Failed to allocate content item struct.");
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        // Need to free the already allocated array *content
        free(*content);
        *content = NULL;
        goto cleanup;
    }

    // 3. Populate the item
    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");
    item->data = echo_text_copy; // Transfer ownership of the duplicated string
    item->data_size = strlen(echo_text_copy) + 1; // Include null terminator
    echo_text_copy = NULL; // Avoid double free in cleanup

    if (!item->mime_type) {
        mcp_log_error("Echo tool: Failed to allocate mime type string.");
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        mcp_content_item_free(item); // Frees data if allocated
        free(*content);
        *content = NULL;
        goto cleanup;
    }

    // 4. Assign item to array
    (*content)[0] = item;
    *content_count = 1;
    err_code = MCP_ERROR_NONE; // Success

cleanup:
    // Free the duplicated text only if it wasn't transferred to the content item
    free(echo_text_copy);

    // If an error occurred but no specific message was allocated, provide a default
    if (err_code != MCP_ERROR_NONE && *error_message == NULL) {
        *error_message = mcp_strdup("An unexpected error occurred in the echo tool.");
        // If even this fails, we can't do much more
    }

    // Ensure content is NULL if an error occurred
    if (err_code != MCP_ERROR_NONE) {
        // Content should have been freed during error handling above
        if (*content) {
             mcp_log_error("Echo tool: Content array not freed on error path!");
             // Attempt cleanup anyway
             if ((*content)[0]) {
                 mcp_content_item_free((*content)[0]);
             }
             free(*content);
             *content = NULL;
        }
        *content_count = 0;
    }

    return err_code;
}

static void echo_cleanup(void) {
    mcp_log_info("Cleaning up echo server...");
    if (g_echo_server != NULL) {
        mcp_server_destroy(g_echo_server);
        g_echo_server = NULL;
    }
    mcp_log_close(); // Use renamed function
}

static void echo_signal_handler(int sig) {
    mcp_log_info("Echo server received signal %d, shutting down...", sig);
    if (g_echo_server) {
         mcp_server_stop(g_echo_server);
    }
    // Let atexit handle the rest
    g_echo_server = NULL; // Signal main loop to exit
}

int main(int argc, char** argv) {
    (void)argc; (void)argv; // Basic example, no command line args yet

    // Initialize logging (log to stdout for example)
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO); // Use new init and enum

    // Setup cleanup and signal handling
    atexit(echo_cleanup);
    signal(SIGINT, echo_signal_handler);
    signal(SIGTERM, echo_signal_handler);
#ifndef _WIN32
    signal(SIGHUP, echo_signal_handler);
#endif

    mcp_log_info("Starting Echo MCP server...");

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
        mcp_log_error("Failed to create echo server");
        return 1;
    }

    // Create and add the "echo" tool
    mcp_tool_t* echo_tool = mcp_tool_create("echo", "Echoes back the provided text parameter.");
    if (!echo_tool || mcp_tool_add_param(echo_tool, "text", "string", "The text to echo", true) != 0) {
         mcp_log_error("Failed to create or add param to echo tool");
         mcp_tool_free(echo_tool);
         mcp_server_destroy(g_echo_server);
         return 1;
    }
    if (mcp_server_add_tool(g_echo_server, echo_tool) != 0) {
        mcp_log_error("Failed to add echo tool to server");
        mcp_tool_free(echo_tool);
        mcp_server_destroy(g_echo_server);
        return 1;
    }
    mcp_tool_free(echo_tool); // Server makes a copy

    // Set the tool handler
    if (mcp_server_set_tool_handler(g_echo_server, echo_tool_handler, NULL) != 0) {
        mcp_log_error("Failed to set tool handler");
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
        mcp_log_error("Failed to create TCP transport");
        mcp_server_destroy(g_echo_server);
        return 1;
    }

    // Start the server
    mcp_log_info("Starting server on %s:%u...", host, port);
    if (mcp_server_start(g_echo_server, transport) != 0) {
        mcp_log_error("Failed to start server");
        mcp_transport_destroy(transport); // Destroy transport if start fails
        mcp_server_destroy(g_echo_server);
        return 1;
    }

    mcp_log_info("Echo server running. Press Ctrl+C to stop.");

    // Keep running until signaled
    while (g_echo_server != NULL) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    mcp_log_info("Echo server shut down gracefully.");
    return 0;
}
