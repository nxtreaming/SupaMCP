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

## Example 7: Profile Management

```c
#include "kmcp.h"
#include "kmcp_profile_manager.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        mcp_log_error("Failed to create profile manager");
        return 1;
    }

    // Create development profile
    kmcp_error_t result = kmcp_profile_create(manager, "development");
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to create development profile: %s", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Create production profile
    result = kmcp_profile_create(manager, "production");
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to create production profile: %s", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Add a local server to the development profile
    kmcp_server_config_t dev_server;
    memset(&dev_server, 0, sizeof(dev_server));
    dev_server.name = strdup("local-dev");
    dev_server.command = strdup("mcp_server");
    dev_server.args_count = 3;
    dev_server.args = (char**)malloc(dev_server.args_count * sizeof(char*));
    dev_server.args[0] = strdup("--tcp");
    dev_server.args[1] = strdup("--port");
    dev_server.args[2] = strdup("8080");

    result = kmcp_profile_add_server(manager, "development", &dev_server);

    // Free server configuration
    free(dev_server.name);
    free(dev_server.command);
    for (size_t i = 0; i < dev_server.args_count; i++) {
        free(dev_server.args[i]);
    }
    free(dev_server.args);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add server to development profile: %s", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Add a remote server to the production profile
    kmcp_server_config_t prod_server;
    memset(&prod_server, 0, sizeof(prod_server));
    prod_server.name = strdup("remote-prod");
    prod_server.url = strdup("https://production.example.com:8080");

    result = kmcp_profile_add_server(manager, "production", &prod_server);

    // Free server configuration
    free(prod_server.name);
    free(prod_server.url);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add server to production profile: %s", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Activate the development profile
    result = kmcp_profile_activate(manager, "development");
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to activate development profile: %s", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Get the active profile
    const char* active_profile = kmcp_profile_get_active(manager);
    mcp_log_info("Active profile: %s", active_profile ? active_profile : "None");

    // Get the server manager for the active profile
    kmcp_server_manager_t* server_manager = kmcp_profile_get_server_manager(manager, active_profile);
    if (!server_manager) {
        mcp_log_error("Failed to get server manager for active profile");
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Use the server manager...

    // Save profiles to a file
    result = kmcp_profile_save(manager, "profiles.json");
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to save profiles: %s", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 1;
    }

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 8: Registry Usage

```c
#include "kmcp.h"
#include "kmcp_registry.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a registry connection
    kmcp_registry_t* registry = kmcp_registry_create("https://registry.example.com");
    if (!registry) {
        mcp_log_error("Failed to create registry connection");
        return 1;
    }

    // Get all servers from the registry
    kmcp_server_info_t* servers = NULL;
    size_t count = 0;
    kmcp_error_t result = kmcp_registry_get_servers(registry, &servers, &count);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get servers: %s", kmcp_error_message(result));
        kmcp_registry_close(registry);
        return 1;
    }

    // Process the servers
    mcp_log_info("Found %zu servers:", count);
    for (size_t i = 0; i < count; i++) {
        mcp_log_info("Server %zu: %s (%s)", i, servers[i].name, servers[i].url);
        mcp_log_info("  Description: %s", servers[i].description);
        mcp_log_info("  Version: %s", servers[i].version);
        mcp_log_info("  Public: %s", servers[i].is_public ? "Yes" : "No");

        char time_str[32];
        struct tm* timeinfo = localtime(&servers[i].last_seen);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
        mcp_log_info("  Last seen: %s", time_str);

        mcp_log_info("  Capabilities (%zu):", servers[i].capabilities_count);
        for (size_t j = 0; j < servers[i].capabilities_count; j++) {
            mcp_log_info("    %s", servers[i].capabilities[j]);
        }

        mcp_log_info("  Tools (%zu):", servers[i].tools_count);
        for (size_t j = 0; j < servers[i].tools_count; j++) {
            mcp_log_info("    %s", servers[i].tools[j]);
        }
    }

    // Create a server manager
    kmcp_server_manager_t* manager = kmcp_server_manager_create();
    if (!manager) {
        mcp_log_error("Failed to create server manager");
        kmcp_registry_free_server_info_array(servers, count);
        kmcp_registry_close(registry);
        return 1;
    }

    // Add a server from the registry to the server manager
    if (count > 0) {
        result = kmcp_registry_add_server(registry, manager, servers[0].id);
        if (result != KMCP_SUCCESS) {
            mcp_log_error("Failed to add server to manager: %s", kmcp_error_message(result));
        } else {
            mcp_log_info("Added server %s to manager", servers[0].name);
        }
    }

    // Free the server information
    kmcp_registry_free_server_info_array(servers, count);

    // Search for servers
    result = kmcp_registry_search_servers(registry, "ai", &servers, &count);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to search servers: %s", kmcp_error_message(result));
    } else {
        mcp_log_info("Found %zu servers matching 'ai':", count);
        for (size_t i = 0; i < count; i++) {
            mcp_log_info("Server %zu: %s (%s)", i, servers[i].name, servers[i].url);
        }
        kmcp_registry_free_server_info_array(servers, count);
    }

    // Refresh the registry cache
    result = kmcp_registry_refresh_cache(registry);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to refresh registry cache: %s", kmcp_error_message(result));
    } else {
        mcp_log_info("Registry cache refreshed");
    }

    // Destroy the server manager
    kmcp_server_manager_destroy(manager);

    // Close the registry connection
    kmcp_registry_close(registry);

    // Close logging
    mcp_log_close();

    return 0;
}
```

## Example 9: Tool SDK Usage

```c
#include "kmcp_tool_sdk.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tool initialization callback
kmcp_error_t repeat_tool_init(void** user_data) {
    // No user data needed for this simple tool
    *user_data = NULL;
    return KMCP_SUCCESS;
}

// Tool cleanup callback
void repeat_tool_cleanup(void* user_data) {
    // Nothing to clean up
}

// Tool execution callback
kmcp_error_t repeat_tool_execute(void* user_data, const mcp_json_t* params, mcp_json_t** result) {
    // Get tool context
    kmcp_tool_context_t* context = kmcp_tool_get_context();
    if (!context) {
        return KMCP_ERROR_INVALID_CONTEXT;
    }

    // Get parameters
    const char* text = kmcp_tool_get_string_param(params, "text", NULL);
    if (!text) {
        *result = kmcp_tool_create_error_result("Missing 'text' parameter", KMCP_ERROR_INVALID_PARAMETER);
        return KMCP_SUCCESS;
    }

    int count = kmcp_tool_get_int_param(params, "count", 1);
    if (count < 1) {
        *result = kmcp_tool_create_error_result("'count' must be at least 1", KMCP_ERROR_INVALID_PARAMETER);
        return KMCP_SUCCESS;
    }

    // Log the request
    kmcp_tool_log(context, 2, "Repeating text '%s' %d times", text, count);

    // Process the parameters
    size_t text_len = strlen(text);
    char* output = malloc(text_len * count + 1);
    if (!output) {
        *result = kmcp_tool_create_error_result("Memory allocation failed", KMCP_ERROR_MEMORY);
        return KMCP_SUCCESS;
    }

    output[0] = '\0';
    for (int i = 0; i < count; i++) {
        // Send progress updates
        kmcp_tool_send_progress(context, (float)i / count, "Repeating text...");

        // Check for cancellation
        if (kmcp_tool_is_cancelled(context)) {
            free(output);
            *result = kmcp_tool_create_error_result("Operation cancelled", KMCP_ERROR_CANCELLED);
            return KMCP_SUCCESS;
        }

        strcat(output, text);
    }

    // Create result
    mcp_json_t* data = mcp_json_object();
    mcp_json_object_set_string(data, "output", output);
    mcp_json_object_set_int(data, "length", strlen(output));
    *result = kmcp_tool_create_data_result(data);

    // Free temporary data
    free(output);
    mcp_json_free(data);

    // Log the completion
    kmcp_tool_log(context, 2, "Completed repeating text");

    return KMCP_SUCCESS;
}

// Define tool metadata
kmcp_tool_metadata_t repeat_tool_metadata = {
    .name = "repeat",
    .version = "1.0.0",
    .description = "Repeats a text string a specified number of times",
    .author = "Your Name",
    .website = "https://example.com",
    .license = "MIT",
    .tags = (const char*[]){"text", "utility"},
    .tags_count = 2,
    .category = KMCP_TOOL_CATEGORY_UTILITY,
    .capabilities = KMCP_TOOL_CAP_CANCELLABLE,
    .dependencies = NULL,
    .dependencies_count = 0
};

// Define tool callbacks
kmcp_tool_callbacks_t repeat_tool_callbacks = {
    .init = repeat_tool_init,
    .cleanup = repeat_tool_cleanup,
    .execute = repeat_tool_execute,
    .cancel = NULL  // No custom cancellation needed
};

// Tool registration function
kmcp_error_t register_repeat_tool(void) {
    return kmcp_tool_register(&repeat_tool_metadata, &repeat_tool_callbacks);
}

// Tool unregistration function
kmcp_error_t unregister_repeat_tool(void) {
    return kmcp_tool_unregister("repeat");
}

// Tool entry point
KMCP_TOOL_EXPORT kmcp_error_t kmcp_tool_init(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);
    mcp_log_info("Initializing repeat tool");

    // Register the tool
    kmcp_error_t result = register_repeat_tool();
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to register repeat tool: %s", kmcp_error_message(result));
        return result;
    }

    mcp_log_info("Repeat tool registered successfully");
    return KMCP_SUCCESS;
}

// Tool exit point
KMCP_TOOL_EXPORT void kmcp_tool_cleanup(void) {
    mcp_log_info("Cleaning up repeat tool");

    // Unregister the tool
    kmcp_error_t result = unregister_repeat_tool();
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to unregister repeat tool: %s", kmcp_error_message(result));
    }

    // Close logging
    mcp_log_close();
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
