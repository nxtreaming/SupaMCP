# KMCP Architecture Overview

## Overall Architecture

The KMCP module uses a layered architecture design, which includes the following layers:

1. **Client Layer**: Provides high-level APIs, serving as the main interface for applications to interact with KMCP
2. **Server Management Layer**: Responsible for managing multiple MCP servers, including server startup, shutdown, and status monitoring
3. **Communication Layer**: Responsible for communication with MCP servers, including HTTP client and other transport protocols
4. **Configuration Layer**: Responsible for parsing and validating configuration files
5. **Tool Access Control Layer**: Responsible for controlling access permissions to MCP tools
6. **Process Management Layer**: Responsible for starting and managing local MCP server processes

## Core Components

### Client (kmcp_client)

The client component is the main interface for applications to interact with KMCP. It encapsulates server management, tool invocation, and resource access functionality. The client component is responsible for:

- Initializing and configuring the KMCP environment
- Managing connections to servers
- Providing high-level APIs for tool invocation and resource access
- Handling errors and exceptional conditions

### Server Manager (kmcp_server_manager)

The server manager is responsible for managing multiple MCP servers, including:

- Maintaining the server list
- Starting and stopping local server processes
- Monitoring server status
- Selecting appropriate servers based on tool names

### HTTP Client (kmcp_http_client)

The HTTP client is responsible for HTTP communication with MCP servers, including:

- Establishing and maintaining HTTP connections
- Sending HTTP requests
- Receiving and parsing HTTP responses
- Handling HTTP errors and timeouts

### Configuration Parser (kmcp_config_parser)

The configuration parser is responsible for parsing and validating configuration files, including:

- Reading JSON configuration files
- Parsing client configuration
- Parsing server configuration
- Parsing tool access control configuration

### Tool Access Control (kmcp_tool_access)

Tool access control is responsible for controlling access permissions to MCP tools, including:

- Maintaining lists of allowed and disallowed tools
- Checking tool access permissions
- Implementing default allow or default deny policies

### Process Management (kmcp_process)

Process management is responsible for starting and managing local MCP server processes, including:

- Creating and starting processes
- Monitoring process status
- Stopping and cleaning up processes

## Component Dependencies

```
kmcp_client
  ├── kmcp_server_manager
  │     ├── kmcp_process
  │     └── kmcp_http_client
  ├── kmcp_config_parser
  └── kmcp_tool_access
```

- `kmcp_client` depends on `kmcp_server_manager`, `kmcp_config_parser`, and `kmcp_tool_access`
- `kmcp_server_manager` depends on `kmcp_process` and `kmcp_http_client`
- All components depend on `kmcp_error` for error handling

## Design Principles

The KMCP module design follows these principles:

1. **Modularity**: Each component has clear responsibilities and can be developed and tested independently
2. **Extensibility**: The architecture allows for adding new features and components, such as new transport protocols or server types
3. **Error Handling**: Unified error handling mechanism ensures errors can be properly propagated and handled
4. **Resource Management**: Strict resource management ensures all resources are properly allocated and released
5. **Thread Safety**: Thread-safe design for critical components, supporting multi-threaded environments
6. **Cross-Platform**: Support for multiple operating systems, including Windows and POSIX systems
