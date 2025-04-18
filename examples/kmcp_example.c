#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#define MAX_PATH PATH_MAX
#endif

/**
 * @brief Example configuration file content
 */
const char* example_config =
"{\n"
"  \"clientConfig\": {\n"
"    \"clientName\": \"kmcp-example-client\",\n"
"    \"clientVersion\": \"1.0.0\",\n"
"    \"useServerManager\": true,\n"
"    \"requestTimeoutMs\": 30000\n"
"  },\n"
"  \"mcpServers\": {\n"
"    \"local\": {\n"
"      \"command\": \"D:\\\\workspace\\\\SupaMCPServer\\\\build\\\\Debug\\\\mcp_server.exe\",\n"
"      \"args\": [\"--tcp\", \"--port\", \"8080\", \"--log-file\", \"D:\\\\workspace\\\\SupaMCPServer\\\\build\\\\Debug\\\\mcp_server.log\", \"--log-level\", \"debug\"],\n"
"      \"env\": {\n"
"        \"MCP_DEBUG\": \"1\"\n"
"      }\n"
"    },\n"
"    \"remote\": {\n"
"      \"url\": \"http://localhost:8931/sse\"\n"
"    }\n"
"  },\n"
"  \"toolAccessControl\": {\n"
"    \"defaultAllow\": true,\n"
"    \"disallowedTools\": [\"file_write\", \"execute_command\"]\n"
"  }\n"
"}";

/**
 * @brief Create example configuration file
 *
 * @param file_path Configuration file path
 * @return int Returns 0 on success, non-zero error code on failure
 */
int create_example_config(const char* file_path) {
    // Open file in text mode for JSON
    FILE* file = fopen(file_path, "w");
    if (!file) {
        mcp_log_error("Failed to create config file: %s", file_path);
        return -1;
    }

    // Write the configuration
    size_t written = fprintf(file, "%s", example_config);

    // Flush and close the file
    fflush(file);
    fclose(file);

    // Verify the file was written correctly
    if (written != strlen(example_config)) {
        mcp_log_error("Failed to write complete config file: %s (wrote %zu of %zu bytes)",
                     file_path, written, strlen(example_config));
        return -1;
    }

    // Verify the file exists and can be read
    file = fopen(file_path, "r");
    if (!file) {
        mcp_log_error("Failed to verify config file: %s", file_path);
        return -1;
    }
    fclose(file);

    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    mcp_log_info("KMCP Example");

    // Create example configuration file with full path
    char config_file[MAX_PATH];

#ifdef _WIN32
    if (GetCurrentDirectoryA(MAX_PATH, config_file) == 0) {
        mcp_log_error("Failed to get current directory");
        return 1;
    }

    // Append file name to path
    strcat(config_file, "\\kmcp_example.json");
#else
    if (getcwd(config_file, MAX_PATH) == NULL) {
        mcp_log_error("Failed to get current directory");
        return 1;
    }

    // Append file name to path
    strcat(config_file, "/kmcp_example.json");
#endif

    mcp_log_info("Using config file path: %s", config_file);

    if (create_example_config(config_file) != 0) {
        mcp_log_error("Failed to create example config file");
        return 1;
    }

    mcp_log_info("Created example config file: %s", config_file);

    // Create client
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        mcp_log_error("Failed to create client");
        return 1;
    }

    mcp_log_info("Created client successfully");

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        mcp_log_error("Failed to get server manager");
        kmcp_client_close(client);
        return 1;
    }

    // Display server count
    size_t server_count = kmcp_server_manager_get_count(manager);
    mcp_log_info("Server count: %zu", server_count);

    // Try to call a tool
    char* result = NULL;
    int ret = kmcp_client_call_tool(client, "echo", "{\"text\":\"Hello, World!\"}", &result);
    if (ret == 0 && result) {
        mcp_log_info("Tool call result: %s", result);
        free(result);
    } else {
        mcp_log_warn("Failed to call tool, this is expected if no real server is running");
    }

    // Try to get a resource
    char* content = NULL;
    char* content_type = NULL;
    ret = kmcp_client_get_resource(client, "example://hello", &content, &content_type);
    if (ret == 0 && content) {
        mcp_log_info("Resource content: %s", content);
        mcp_log_info("Content type: %s", content_type);
        free(content);
        free(content_type);
    } else {
        mcp_log_warn("Failed to get resource, this is expected if no real server is running");
    }

    // Close client
    mcp_log_info("Closing client...");
    kmcp_client_close(client);
    mcp_log_info("Client closed");

    // Close logging
    mcp_log_close();

    return 0;
}
