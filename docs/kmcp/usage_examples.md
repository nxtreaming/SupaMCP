# KMCP Usage Examples

This document provides common usage scenarios and code examples for the KMCP module, helping developers get started quickly.

## Basic Usage Flow

The basic flow for using the KMCP module is as follows:

1. Initialize the client
2. Call tools or get resources
3. Process results
4. Close the client

## Example 1: Creating a Client from a Configuration File

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client from configuration file
    const char* config_file = "config.json";
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        mcp_log_error("Failed to create client from file: %s", config_file);
        return 1;
    }

    mcp_log_info("Client created successfully");

    // Use the client...

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 2: Manually Creating a Client

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client configuration
    kmcp_client_config_t config;
    config.name = strdup("example-client");
    config.version = strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create client
    kmcp_client_t* client = kmcp_client_create(&config);
    if (!client) {
        mcp_log_error("Failed to create client");
        free(config.name);
        free(config.version);
        return 1;
    }

    // Configuration has been copied, can be freed
    free(config.name);
    free(config.version);

    mcp_log_info("Client created successfully");

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        mcp_log_error("Failed to get server manager");
        kmcp_client_close(client);
        return 1;
    }

    // Create server configuration
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = strdup("local");
    server_config.command = strdup("mcp_server");

    // Set command arguments
    server_config.args_count = 3;
    server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
    server_config.args[0] = strdup("--tcp");
    server_config.args[1] = strdup("--port");
    server_config.args[2] = strdup("8080");

    // Add server
    kmcp_error_t result = kmcp_server_manager_add_server(manager, &server_config);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add server: %s", kmcp_error_message(result));

        // Free server configuration
        free(server_config.name);
        free(server_config.command);
        for (size_t i = 0; i < server_config.args_count; i++) {
            free(server_config.args[i]);
        }
        free(server_config.args);

        kmcp_client_close(client);
        return 1;
    }

    // Server configuration has been copied, can be freed
    free(server_config.name);
    free(server_config.command);
    for (size_t i = 0; i < server_config.args_count; i++) {
        free(server_config.args[i]);
    }
    free(server_config.args);

    mcp_log_info("Server added successfully");

    // Use the client...

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 3: Calling a Tool

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client from configuration file
    const char* config_file = "config.json";
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        mcp_log_error("Failed to create client from file: %s", config_file);
        return 1;
    }

    // Call a tool
    const char* tool_name = "echo";
    const char* params_json = "{\"text\":\"Hello, World!\"}";
    char* result_json = NULL;

    kmcp_error_t result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to call tool: %s, error: %s", tool_name, kmcp_error_message(result));
        kmcp_client_close(client);
        return 1;
    }

    // Process the result
    mcp_log_info("Tool call result: %s", result_json);

    // Free the result
    free(result_json);

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 4: Getting a Resource

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client from configuration file
    const char* config_file = "config.json";
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        mcp_log_error("Failed to create client from file: %s", config_file);
        return 1;
    }

    // Get a resource
    const char* uri = "example://hello";
    char* content = NULL;
    char* content_type = NULL;

    kmcp_error_t result = kmcp_client_get_resource(client, uri, &content, &content_type);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get resource: %s, error: %s", uri, kmcp_error_message(result));
        kmcp_client_close(client);
        return 1;
    }

    // Process the resource
    mcp_log_info("Resource content: %s", content);
    mcp_log_info("Content type: %s", content_type);

    // Free the resource
    free(content);
    free(content_type);

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 5: Tool Access Control

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create tool access control
    kmcp_tool_access_t* access = kmcp_tool_access_create(false); // Default deny
    if (!access) {
        mcp_log_error("Failed to create tool access control");
        return 1;
    }

    // Add allowed tool
    kmcp_error_t result = kmcp_tool_access_add(access, "echo", true);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add allowed tool: %s", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Add disallowed tool
    result = kmcp_tool_access_add(access, "dangerous_tool", false);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add disallowed tool: %s", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        return 1;
    }

    // Check tool access permissions
    bool allowed = kmcp_tool_access_check(access, "echo");
    mcp_log_info("Tool 'echo' is %s", allowed ? "allowed" : "disallowed");

    allowed = kmcp_tool_access_check(access, "dangerous_tool");
    mcp_log_info("Tool 'dangerous_tool' is %s", allowed ? "allowed" : "disallowed");

    allowed = kmcp_tool_access_check(access, "unknown_tool");
    mcp_log_info("Tool 'unknown_tool' is %s", allowed ? "allowed" : "disallowed");

    // Destroy tool access control
    kmcp_tool_access_destroy(access);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 6: Configuration File Example

```json
{
  "clientConfig": {
    "clientName": "example-client",
    "clientVersion": "1.0.0",
    "useServerManager": true,
    "requestTimeoutMs": 30000
  },
  "mcpServers": {
    "local": {
      "command": "mcp_server",
      "args": ["--tcp", "--port", "8080"],
      "env": {
        "MCP_DEBUG": "1"
      }
    },
    "remote": {
      "url": "http://example.com:8080"
    }
  },
  "toolAccessControl": {
    "defaultAllow": false,
    "allowedTools": ["echo", "calculator", "translator"],
    "disallowedTools": ["dangerous_tool", "system_tool"]
  }
}
```

## Error Handling Best Practices

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client from configuration file
    const char* config_file = "config.json";
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        mcp_log_error("Failed to create client from file: %s", config_file);
        return 1;
    }

    // Call a tool
    const char* tool_name = "echo";
    const char* params_json = "{\"text\":\"Hello, World!\"}";
    char* result_json = NULL;

    kmcp_error_t result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);

    // Error handling
    switch (result) {
        case KMCP_SUCCESS:
            mcp_log_info("Tool call successful");
            break;
        case KMCP_ERROR_INVALID_PARAMETER:
            mcp_log_error("Invalid parameter for tool call");
            break;
        case KMCP_ERROR_PERMISSION_DENIED:
            mcp_log_error("Permission denied for tool: %s", tool_name);
            break;
        case KMCP_ERROR_TOOL_NOT_FOUND:
            mcp_log_error("Tool not found: %s", tool_name);
            break;
        case KMCP_ERROR_CONNECTION_FAILED:
            mcp_log_error("Connection failed to server");
            break;
        case KMCP_ERROR_TIMEOUT:
            mcp_log_error("Tool call timed out");
            break;
        default:
            mcp_log_error("Unexpected error: %s", kmcp_error_message(result));
            break;
    }

    // Process the result
    if (result == KMCP_SUCCESS && result_json) {
        mcp_log_info("Tool call result: %s", result_json);
        free(result_json);
    }

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return (result == KMCP_SUCCESS) ? 0 : 1;
}
```

## Resource Management Best Practices

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Error handling macro
#define HANDLE_ERROR(result, message, cleanup) \
    do { \
        if ((result) != KMCP_SUCCESS) { \
            mcp_log_error("%s: %s", (message), kmcp_error_message(result)); \
            cleanup; \
            return 1; \
        } \
    } while (0)

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client configuration
    kmcp_client_config_t config;
    config.name = strdup("example-client");
    config.version = strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create client
    kmcp_client_t* client = kmcp_client_create(&config);
    if (!client) {
        mcp_log_error("Failed to create client");
        free(config.name);
        free(config.version);
        return 1;
    }

    // Configuration has been copied, can be freed
    free(config.name);
    free(config.version);

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        mcp_log_error("Failed to get server manager");
        kmcp_client_close(client);
        return 1;
    }

    // Create server configuration
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = strdup("local");
    server_config.command = strdup("mcp_server");

    // Set command arguments
    server_config.args_count = 3;
    server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
    if (!server_config.args) {
        mcp_log_error("Failed to allocate memory for args");
        free(server_config.name);
        free(server_config.command);
        kmcp_client_close(client);
        return 1;
    }

    server_config.args[0] = strdup("--tcp");
    server_config.args[1] = strdup("--port");
    server_config.args[2] = strdup("8080");

    // Add server
    kmcp_error_t result = kmcp_server_manager_add_server(manager, &server_config);

    // Free server configuration (regardless of success)
    free(server_config.name);
    free(server_config.command);
    for (size_t i = 0; i < server_config.args_count; i++) {
        free(server_config.args[i]);
    }
    free(server_config.args);

    // Check the result of adding the server
    HANDLE_ERROR(result, "Failed to add server", kmcp_client_close(client));

    // Call a tool
    const char* tool_name = "echo";
    const char* params_json = "{\"text\":\"Hello, World!\"}";
    char* result_json = NULL;

    result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
    HANDLE_ERROR(result, "Failed to call tool", kmcp_client_close(client));

    // Process the result
    mcp_log_info("Tool call result: %s", result_json);

    // Free the result
    free(result_json);

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```
