# KMCP Module Documentation

KMCP (Kernel MCP) is a lightweight MCP client library that provides a high-level interface for communicating with MCP servers. It supports multi-server management, tool access control, and configuration parsing, making it easy for applications to integrate with MCP servers.

## Documentation Contents

- [Overview](kmcp/README.md)
- [Quick Start Guide](kmcp/quick_start.md)
- [Architecture Overview](kmcp/architecture.md)
- [API Reference](kmcp/api_reference.md)
- [Component Interaction](kmcp/component_interaction.md)
- [Usage Examples](kmcp/usage_examples.md)
- [Configuration Guide](kmcp/configuration_guide.md)

## Key Features

- **Multi-server Management**: Support for managing multiple MCP servers, including local process servers and remote HTTP servers
- **Tool Access Control**: Fine-grained tool access control to restrict access to specific tools
- **Configuration Parsing**: Support for loading client and server configurations from JSON configuration files
- **HTTP Client**: HTTP client for communicating with MCP servers
- **Process Management**: Support for starting and managing local MCP server processes

## Getting Started

To start using the KMCP module, please refer to the [Quick Start Guide](kmcp/quick_start.md).

## API Overview

The KMCP module provides the following main APIs:

- **Client API**: Create and manage KMCP clients
- **Server Management API**: Manage MCP servers
- **Configuration Parsing API**: Parse configuration files
- **Tool Access Control API**: Control access to MCP tools
- **HTTP Client API**: Communicate with MCP servers
- **Process Management API**: Start and manage local MCP server processes

For detailed API documentation, please refer to the [API Reference](kmcp/api_reference.md).

## Configuration

The KMCP module uses JSON format configuration files, which include client configuration, server configuration, and tool access control configuration. For detailed configuration instructions, please refer to the [Configuration Guide](kmcp/configuration_guide.md).

## Example

Here's a simple example showing how to use the KMCP module:

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
    free(result_json);

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 0;
}
```

For more examples, please refer to the [Usage Examples](kmcp/usage_examples.md).
