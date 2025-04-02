#include <mcp_client.h>
#include <mcp_tcp_client_transport.h>
#include <mcp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* server_host = "127.0.0.1";
    uint16_t server_port = 18889; // Port the echo server listens on
    const char* text_to_echo = "Hello, MCP!";

    // Allow overriding text from command line
    if (argc > 1) {
        text_to_echo = argv[1];
    }

    // Initialize logging (optional, logs to stderr by default)
    init_logging(NULL, LOG_LEVEL_INFO);

    log_message(LOG_LEVEL_INFO, "Creating MCP client...");

    // Client Configuration
    mcp_client_config_t client_config = {
        .request_timeout_ms = 5000 // 5 second timeout
        // Add .api_key = "your-key" here if the server requires it
    };

    // Create TCP Client Transport
    mcp_transport_t* transport = mcp_transport_tcp_client_create(server_host, server_port);
    if (transport == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create TCP client transport");
        return 1;
    }

    // Create Client (takes ownership of transport)
    mcp_client_t* client = mcp_client_create(&client_config, transport);
    if (client == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create MCP client");
        // Transport is destroyed by mcp_client_create on failure if it was passed in
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "Client created. Calling 'echo' tool...");

    // Prepare arguments for the echo tool
    // Needs to be a valid JSON object string: {"text": "..."}
    // Max length estimate: 10 ({"text":""}) + length + 2 (escapes if needed) + 1 (null)
    size_t arg_len_estimate = strlen(text_to_echo) + 15;
    char* echo_args = (char*)malloc(arg_len_estimate);
    if (!echo_args) {
         log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for echo arguments");
         mcp_client_destroy(client);
         return 1;
    }
    // Use snprintf for safe formatting
    snprintf(echo_args, arg_len_estimate, "{\"text\": \"%s\"}", text_to_echo);
    // NOTE: This simple snprintf doesn't escape special characters in text_to_echo.
    // A robust implementation should properly escape the string.

    // Call the tool
    mcp_content_item_t** result_content = NULL;
    size_t result_count = 0;
    bool is_error = false;
    int call_status = mcp_client_call_tool(client, "echo", echo_args, &result_content, &result_count, &is_error);

    free(echo_args); // Free the arguments string

    // Process result
    if (call_status != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to call tool 'echo'. Status: %d", call_status);
    } else if (is_error) {
        log_message(LOG_LEVEL_ERROR, "Tool 'echo' returned an error.");
        // Print error content if available
        if (result_count > 0 && result_content && result_content[0] && result_content[0]->type == MCP_CONTENT_TYPE_TEXT) {
             log_message(LOG_LEVEL_ERROR, "Error details: %s", (const char*)result_content[0]->data);
        }
    } else if (result_count > 0 && result_content && result_content[0] && result_content[0]->type == MCP_CONTENT_TYPE_TEXT) {
        log_message(LOG_LEVEL_INFO, "Server echoed: %s", (const char*)result_content[0]->data);
    } else {
        log_message(LOG_LEVEL_WARN, "Tool 'echo' returned unexpected content format.");
    }

    // Free the result content array and its items
    mcp_free_content(result_content, result_count);

    // Cleanup
    log_message(LOG_LEVEL_INFO, "Destroying client...");
    mcp_client_destroy(client);
    log_message(LOG_LEVEL_INFO, "Client finished.");
    close_logging();

    return (call_status == 0 && !is_error) ? 0 : 1;
}
