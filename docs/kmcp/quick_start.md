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
  }
}
```

Replace `path/to/mcp_server` with the actual path to the MCP server executable.

### 2. Include Header Files

Include the KMCP header files in your C code:

```c
#include "kmcp.h"
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
