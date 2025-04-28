# KMCP Quick Start Guide

This guide will help you quickly get started with the KMCP module, including installation, configuration, and basic usage.

## Installation

The KMCP module is part of the SupaMCP project. You can install it using the following steps:

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/SupaMCPServer.git
cd SupaMCPServer
```

### 2. Build the Project

Use CMake to build the project:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Configuration

### 1. Create a Configuration File

Create a configuration file named `config.json` with the following content:

```json
{
  "clientConfig": {
    "clientName": "my-client",
    "clientVersion": "1.0.0",
    "useServerManager": true,
    "requestTimeoutMs": 30000
  },
  "mcpServers": {
    "local": {
      "command": "path/to/mcp_server",
      "args": ["--tcp", "--port", "8080"]
    }
  },
  "toolAccessControl": {
    "defaultAllow": true
  },
  "profiles": {
    "activeProfile": "development",
    "profileList": {
      "development": {
        "servers": {
          "local-dev": {
            "command": "path/to/mcp_server",
            "args": ["--tcp", "--port", "8080"]
          }
        }
      }
    }
  },
  "registry": {
    "registryUrl": "https://registry.example.com"
  },
  "toolSDK": {
    "toolsDirectory": "./tools",
    "autoLoadTools": true
  }
}
```

Replace `path/to/mcp_server` with the actual path to the MCP server executable.

### 2. Include Header Files

Include the KMCP header files in your C code:

```c
#include "kmcp.h"
#include "kmcp_registry.h"        // For registry integration
#include "kmcp_tool_sdk.h"        // For tool development
```

### 3. Link Libraries

Link the KMCP library in your CMake file:

```cmake
target_link_libraries(your_app kmcp)
```

## Basic Usage

### 1. Initialize the Client

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client from configuration file
    kmcp_client_t* client = kmcp_client_create_from_file("config.json");
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }

    printf("Client created successfully\n");

    // Use the client...

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```

### 2. Call a Tool

```c
// Call a tool
const char* tool_name = "echo";
const char* params_json = "{\"text\":\"Hello, World!\"}";
char* result_json = NULL;

kmcp_error_t result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
if (result != KMCP_SUCCESS) {
    printf("Failed to call tool: %s\n", kmcp_error_message(result));
    kmcp_client_close(client);
    return 1;
}

// Process the result
printf("Tool call result: %s\n", result_json);

// Free the result
free(result_json);
```

### 3. Get a Resource

```c
// Get a resource
const char* uri = "example://hello";
char* content = NULL;
char* content_type = NULL;

kmcp_error_t result = kmcp_client_get_resource(client, uri, &content, &content_type);
if (result != KMCP_SUCCESS) {
    printf("Failed to get resource: %s\n", kmcp_error_message(result));
    kmcp_client_close(client);
    return 1;
}

// Process the resource
printf("Resource content: %s\n", content);
printf("Content type: %s\n", content_type);

// Free the resource
free(content);
free(content_type);
```

### 4. Use Server Manager Directly

```c
// Create a server manager
kmcp_server_manager_t* manager = kmcp_server_create();
if (!manager) {
    printf("Failed to create server manager\n");
    return 1;
}

// Add a server to the manager
kmcp_server_config_t server_config;
memset(&server_config, 0, sizeof(server_config));
server_config.name = "local-dev";
server_config.command = "path/to/mcp_server";
server_config.is_http = false;
server_config.args_count = 3;
server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
server_config.args[0] = strdup("--tcp");
server_config.args[1] = strdup("--port");
server_config.args[2] = strdup("8080");

kmcp_error_t result = kmcp_server_add(manager, &server_config);

// Free server configuration arguments
for (size_t i = 0; i < server_config.args_count; i++) {
    free(server_config.args[i]);
}
free(server_config.args);

if (result != KMCP_SUCCESS) {
    printf("Failed to add server to manager: %s\n", kmcp_error_message(result));
    kmcp_server_destroy(manager);
    return 1;
}

// Connect to servers
result = kmcp_server_connect(manager);
if (result != KMCP_SUCCESS) {
    printf("Failed to connect to servers: %s\n", kmcp_error_message(result));
    kmcp_server_destroy(manager);
    return 1;
}

// Use the server manager...

// Destroy the server manager
kmcp_server_destroy(manager);
```

### 5. Use Registry Integration

```c
// Create a registry connection
kmcp_registry_t* registry = kmcp_registry_create("https://registry.example.com");
if (!registry) {
    printf("Failed to create registry connection\n");
    return 1;
}

// Get servers from the registry
kmcp_server_info_t* servers = NULL;
size_t count = 0;
kmcp_error_t result = kmcp_registry_get_servers(registry, &servers, &count);
if (result != KMCP_SUCCESS) {
    printf("Failed to get servers: %s\n", kmcp_error_message(result));
    kmcp_registry_close(registry);
    return 1;
}

// Process the servers
printf("Found %zu servers:\n", count);
for (size_t i = 0; i < count; i++) {
    printf("Server %zu: %s (%s)\n", i, servers[i].name, servers[i].url);
}

// Add a server from the registry to a server manager
kmcp_server_manager_t* manager = kmcp_server_manager_create();
if (!manager) {
    printf("Failed to create server manager\n");
    kmcp_registry_free_server_info_array(servers, count);
    kmcp_registry_close(registry);
    return 1;
}

if (count > 0) {
    result = kmcp_registry_add_server(registry, manager, servers[0].id);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to manager: %s\n", kmcp_error_message(result));
    } else {
        printf("Added server %s to manager\n", servers[0].name);
    }
}

// Free the server information
kmcp_registry_free_server_info_array(servers, count);

// Use the server manager...

// Destroy the server manager
kmcp_server_manager_destroy(manager);

// Close the registry connection
kmcp_registry_close(registry);
```

### 6. Develop a Custom Tool

```c
// Tool initialization callback
kmcp_error_t my_tool_init(void** user_data) {
    // No user data needed for this simple tool
    *user_data = NULL;
    return KMCP_SUCCESS;
}

// Tool cleanup callback
void my_tool_cleanup(void* user_data) {
    // Nothing to clean up
}

// Tool execution callback
kmcp_error_t my_tool_execute(void* user_data, const mcp_json_t* params, mcp_json_t** result) {
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

    // Create result
    mcp_json_t* data = mcp_json_object();
    mcp_json_object_set_string(data, "output", text);
    *result = kmcp_tool_create_data_result(data);

    // Free temporary data
    mcp_json_free(data);

    return KMCP_SUCCESS;
}

// Define tool metadata
kmcp_tool_metadata_t my_tool_metadata = {
    .name = "my-tool",
    .version = "1.0.0",
    .description = "My custom tool",
    .author = "Your Name",
    .website = "https://example.com",
    .license = "MIT",
    .tags = (const char*[]){"example", "custom"},
    .tags_count = 2,
    .category = KMCP_TOOL_CATEGORY_UTILITY,
    .capabilities = KMCP_TOOL_CAP_NONE,
    .dependencies = NULL,
    .dependencies_count = 0
};

// Define tool callbacks
kmcp_tool_callbacks_t my_tool_callbacks = {
    .init = my_tool_init,
    .cleanup = my_tool_cleanup,
    .execute = my_tool_execute,
    .cancel = NULL  // No custom cancellation needed
};

// Register the tool
kmcp_error_t result = kmcp_tool_register(&my_tool_metadata, &my_tool_callbacks);
if (result != KMCP_SUCCESS) {
    printf("Failed to register tool: %s\n", kmcp_error_message(result));
    return 1;
}

// Use the tool...

// Unregister the tool
result = kmcp_tool_unregister("my-tool");
if (result != KMCP_SUCCESS) {
    printf("Failed to unregister tool: %s\n", kmcp_error_message(result));
    return 1;
}
```

## Complete Example

Here's a complete example showing how to use the KMCP module:

```c
#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client from configuration file
    kmcp_client_t* client = kmcp_client_create_from_file("config.json");
    if (!client) {
        printf("Failed to create client\n");
        return 1;
    }

    printf("Client created successfully\n");

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        printf("Failed to get server manager\n");
        kmcp_client_close(client);
        return 1;
    }

    // Display server count
    size_t server_count = kmcp_server_manager_get_count(manager);
    printf("Server count: %zu\n", server_count);

    // Call a tool
    const char* tool_name = "echo";
    const char* params_json = "{\"text\":\"Hello, World!\"}";
    char* result_json = NULL;

    kmcp_error_t result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
    if (result != KMCP_SUCCESS) {
        printf("Failed to call tool: %s\n", kmcp_error_message(result));
        kmcp_client_close(client);
        return 1;
    }

    // Process the result
    printf("Tool call result: %s\n", result_json);

    // Free the result
    free(result_json);

    // Get a resource
    const char* uri = "example://hello";
    char* content = NULL;
    char* content_type = NULL;

    result = kmcp_client_get_resource(client, uri, &content, &content_type);
    if (result != KMCP_SUCCESS) {
        printf("Failed to get resource: %s\n", kmcp_error_message(result));
        kmcp_client_close(client);
        return 1;
    }

    // Process the resource
    printf("Resource content: %s\n", content);
    printf("Content type: %s\n", content_type);

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

## Next Steps

- Read the [API Reference](api_reference.md) for more API details
- Check the [Configuration Guide](configuration_guide.md) for more configuration options
- Refer to the [Usage Examples](usage_examples.md) for more usage scenarios
- Explore [Registry Integration](registry_guide.md) for discovering and connecting to MCP servers
- Develop custom tools with the [Tool SDK Guide](tool_sdk_guide.md)
- Troubleshoot issues with the [Troubleshooting Guide](troubleshooting_guide.md)
