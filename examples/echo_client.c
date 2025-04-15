#include <mcp_client.h>
#include <mcp_transport_factory.h>
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
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO); // Use new init and enum

    mcp_log_info("Creating MCP client...");

    // Client Configuration
    mcp_client_config_t client_config = {
        .request_timeout_ms = 5000 // 5 second timeout
        // Add .api_key = "your-key" here if the server requires it
    };

    // Create TCP Client Transport using the Transport Factory
    mcp_transport_config_t transport_config = {0};
    transport_config.tcp.host = server_host;
    transport_config.tcp.port = server_port;

    mcp_transport_t* transport = mcp_transport_factory_create(
        MCP_TRANSPORT_TCP_CLIENT,
        &transport_config
    );

    if (transport == NULL) {
        mcp_log_error("Failed to create TCP client transport");
        return 1;
    }

    // Create Client (takes ownership of transport)
    mcp_client_t* client = mcp_client_create(&client_config, transport);
    if (client == NULL) {
        mcp_log_error("Failed to create MCP client");
        // Transport is destroyed by mcp_client_create on failure if it was passed in
        return 1;
    }

    mcp_log_info("Client created. Calling 'echo' tool...");

    // Prepare arguments for the echo tool
    // Needs to be a valid JSON object string: {"text": "..."}
    // Max length estimate: 10 ({"text":""}) + length + 2 (escapes if needed) + 1 (null)
    size_t arg_len_estimate = strlen(text_to_echo) + 15;
    char* echo_args = (char*)malloc(arg_len_estimate);
    if (!echo_args) {
        mcp_log_error("Failed to allocate memory for echo arguments");
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
        mcp_log_error("Failed to call tool 'echo'. Status: %d", call_status);
    } else if (is_error) {
        mcp_log_error("Tool 'echo' returned an error.");
        // Print error content if available
        if (result_count > 0 && result_content && result_content[0] && result_content[0]->type == MCP_CONTENT_TYPE_TEXT) {
            mcp_log_error("Error details: %s", (const char*)result_content[0]->data);
        }
    } else if (result_count > 0 && result_content && result_content[0] && result_content[0]->type == MCP_CONTENT_TYPE_TEXT) {
        mcp_log_info("Server echoed: %s", (const char*)result_content[0]->data);
    } else {
        mcp_log_warn("Tool 'echo' returned unexpected content format.");
    }

    // Free the result content array and its items
    mcp_free_content(result_content, result_count);

    // Cleanup
    mcp_log_info("Destroying client...");
    mcp_client_destroy(client);
    mcp_log_info("Client finished.");
    mcp_log_close(); // Use renamed function

    return (call_status == 0 && !is_error) ? 0 : 1;
}
