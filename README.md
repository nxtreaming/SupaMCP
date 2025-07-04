# SupaMCP

A cross-platform implementation of the Model Context Protocol (MCP) server/client in C. This server can be used to provide resources and tools to MCP clients.

## Features

- Cross-platform support (Windows, macOS, Linux)
- Multiple transport options (stdio, TCP, HTTP, WebSocket, **Streamable HTTP**, **MQTT**)
- Resource and tool support
- **MCP Gateway Mode:** Can act as a gateway to route requests to downstream MCP servers.
- **Streamable HTTP Transport:** Full MCP 2025-03-26 protocol support with unified endpoints, session management, and streaming responses
- HTTP protocol with Server-Sent Events (SSE) support
- CORS support for web integration
- Extensible architecture

## Requirements

- CMake 3.10 or higher
- C99 compatible compiler
- pthread library (on Unix-like systems)

## Project Structure

The project is organized into three main components:

1. **Common Library (mcpcore)**: Contains shared functionality used by both the server and client.
   - JSON parsing and manipulation
   - JSON-RPC message formatting and parsing
   - Transport layer (stdio, TCP, HTTP, WebSocket)
   - Server-Sent Events (SSE) support
   - Common data types and utilities
   - Connection Pooling

2. **Server (mcp_server)**: Implements the MCP server functionality.
   - Resource handling (local)
   - Tool handling (local)
   - **Gateway Routing:** Routes requests to configured backends based on resource prefixes or tool names.
   - Server configuration and management

3. **Client (mcp_client)**: Implements the MCP client functionality.
   - Connection to MCP servers
   - Resource and tool discovery
   - Resource reading and tool calling

This modular structure ensures clean separation of concerns and avoids code duplication between the server and client components.

## Building

### Linux/macOS

```bash
# Create a build directory
mkdir build
cd build

# Configure and build
cmake ..
make

# Run tests
make test

# Install
sudo make install
```

### Windows

```cmd
# Create a build directory
mkdir build
cd build

# Configure and build
cmake ..
cmake --build . --config Release

# Run tests
ctest -C Release

# Install
cmake --install . --config Release
```

The build process will create:

1. A static library `libmcpcore.a` (or `mcpcore.lib` on Windows)
2. The server executable `mcp_server`
3. The client executable `mcp_client`

### Visual C++ Studio 2022 Compatibility

The code has been specifically optimized for compatibility with Visual C++ Studio 2022:

1. **Empty Struct Fix**: Visual C++ requires that all structs and unions have at least one member. Empty structs in the code have been modified to include a dummy field to satisfy this requirement.

2. **strdup() Deprecation**: The `strdup()` function is deprecated in Visual C++. A macro has been added to redefine `strdup` to `_strdup` on Windows platforms to avoid deprecation warnings.

3. **Unused Parameter Warnings**: Functions with unused parameters have been modified to include `(void)parameter_name;` statements to explicitly indicate that the parameters are intentionally not used, avoiding C4100 warnings.

4. **Nameless Struct/Union Warnings**: The C4201 warning about nameless struct/union has been disabled using `#pragma warning(disable: 4201)` for Visual C++ compatibility.

5. **SOCKET to int Conversion Warnings**: On Windows, the socket type (SOCKET) is defined as UINT_PTR, which is 64-bit on 64-bit Windows, while int is 32-bit. This could lead to data loss when converting from SOCKET to int. The code has been modified to use the appropriate SOCKET type on Windows and int on other platforms, and to use INVALID_SOCKET instead of -1 for invalid socket checks on Windows.

### Memory Management Improvements

The code includes robust memory management to prevent leaks and handle allocation failures:

1. **NULL Pointer Checks**: All functions that operate on pointers check for NULL before dereferencing.

2. **Memory Allocation Failure Handling**: All memory allocation functions (malloc, strdup, etc.) are checked for failure, and appropriate cleanup is performed if allocation fails.

3. **Initialization Before Use**: All allocated structures have their fields initialized to NULL or appropriate default values before use, ensuring safe cleanup in case of partial initialization failures.

4. **Proper Resource Cleanup**: All resources are properly freed when they are no longer needed, including in error cases.

5. **Consistent Error Handling**: Error handling is consistent throughout the codebase, with appropriate return values and cleanup operations.

6. **Leveraging C Standard Guarantees**: The code takes advantage of the C standard's guarantee that `free(NULL)` is a safe no-op operation, which allows for simpler and cleaner code without unnecessary NULL checks before freeing memory.

## Usage

### Server Usage

```bash
# Run with stdio transport (default)
mcp_server

# Run with TCP transport
mcp_server --tcp --host 127.0.0.1 --port 8080

# Run with HTTP transport
mcp_server --http --host 127.0.0.1 --port 8080

# Run with Streamable HTTP transport (MCP 2025-03-26)
./build/examples/http_streamable_server 8080

# Run with MQTT transport
mcp_server --mqtt --host 127.0.0.1 --port 1883

# Run with logging to file
mcp_server --log-file /path/to/log/file.log

# Run with specific log level
mcp_server --log-level debug

# Run as daemon (Unix-like systems only)
mcp_server --daemon --tcp --log-file /path/to/log/file.log

# Show help
mcp_server --help
```

### Server Command-line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--tcp` | Use TCP transport | - |
| `--http` | Use HTTP transport with SSE support | - |
| `--mqtt` | Use MQTT transport | - |
| `--stdio` | Use stdio transport (default) | - |
| `--host HOST` | Host to bind to | 127.0.0.1 |
| `--port PORT` | Port to bind to | 8080 |
| `--log-file FILE` | Log to file | - |
| `--log-level LEVEL` | Set log level (error, warn, info, debug) | info |
| `--api-key KEY` | Require API key for authentication | - |
| `--daemon` | Run as daemon (Unix-like systems only) | - |
| `--gateway` | Enable MCP Gateway mode (requires `gateway_config.json`) | - |
| `--help` | Show help message | - |

### Gateway Mode

When run with the `--gateway` flag, the server acts as an MCP Gateway. It loads backend server configurations from `gateway_config.json` (located in the same directory as the executable).

The `gateway_config.json` file should be a JSON array of backend objects, each with the following structure:

```json
[
  {
    "name": "unique_backend_name",
    "address": "tcp://host:port", // Currently only tcp:// supported for backends
    "routing": {
      "resource_prefixes": ["prefix1://", "prefix2://"], // Optional: URIs starting with these are routed
      "tool_names": ["tool1", "tool2"]                 // Optional: These tools are routed
    },
    "timeout_ms": 5000 // Optional: Request timeout for this backend (milliseconds)
  }
  // ... more backends
]
```

- Requests for `read_resource` matching a `resource_prefix` will be forwarded.
- Requests for `call_tool` matching a `tool_name` will be forwarded.
- All other requests (or requests not matching any routing rule) will be handled locally by the gateway server itself (using its own example resources/tools or registered handlers).

### Client Usage

The project also includes an MCP client that can connect to an MCP server and interact with it. The client can be used to list resources and tools, read resources, and call tools.

```bash
# List resources using stdio transport (default)
mcp_client --list-resources

# List resources using TCP transport
mcp_client --tcp --host 127.0.0.1 --port 8080 --list-resources

# List resources using HTTP transport
mcp_client --http --host 127.0.0.1 --port 8080 --list-resources

# List resources using MQTT transport
mcp_client --mqtt --host 127.0.0.1 --port 1883 --list-resources

# List resource templates
mcp_client --list-templates

# List tools
mcp_client --list-tools

# Read a resource
mcp_client --read-resource example://hello

# Call a tool
mcp_client --call-tool echo '{"text":"Hello, world!"}'

# Show help
mcp_client --help
```

### Client Command-line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--tcp` | Use TCP transport | - |
| `--http` | Use HTTP transport | - |
| `--mqtt` | Use MQTT transport | - |
| `--stdio` | Use stdio transport (default) | - |
| `--host HOST` | Host to connect to | 127.0.0.1 |
| `--port PORT` | Port to connect to | 8080 |
| `--help` | Show help message | - |

### Client Commands

| Command | Description |
|---------|-------------|
| `--list-resources` | List available resources |
| `--list-templates` | List available resource templates |
| `--list-tools` | List available tools |
| `--read-resource URI` | Read a resource |
| `--call-tool NAME ARGS` | Call a tool |

### Logging System

The server includes a comprehensive logging system with the following features:

- Four log levels: ERROR, WARN, INFO, DEBUG
- Console logging
- File logging with automatic directory creation
- Timestamp and log level prefixes
- Log filtering based on log level

When running in daemon mode, logging to a file is required as the standard output and error streams are closed.

### Example Resources

The server provides the following example resources:

- `example://hello`: Returns a simple "Hello, world!" message
- `example://info`: Returns information about the server

### Example Tools

The server provides the following example tools:

- `echo`: Echoes back the input text
- `reverse`: Reverses the input text

## Documentation

Additional documentation is available in the `docs` directory:

- [MQTT Transport](docs/mqtt_transport.md): Complete documentation for the MQTT transport implementation, including configuration, usage examples, and troubleshooting.
- [Streamable HTTP Transport](docs/streamable_http_transport.md): Complete documentation for the MCP 2025-03-26 Streamable HTTP Transport implementation, including server and client usage.
- [HTTP Protocol Implementation Optimization](docs/http_protocol_optimization.md): Details about the optimizations made to the HTTP protocol implementation in SupaMCP and how to test these optimizations.
- [HTTP Protocol](docs/http_protocol.md): Comprehensive documentation of the HTTP protocol implementation in SupaMCP.
- [HTTP Client Transport](docs/http_client.md): Documentation of the HTTP client transport implementation in SupaMCP, including SSE support.

## Protocol

The Model Context Protocol (MCP) is a JSON-RPC based protocol for communication between AI models and external systems. It allows models to access resources and call tools provided by MCP servers.

### Message Format

MCP messages are JSON objects with the following structure:

#### Request

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "method_name",
  "params": {
    // Method-specific parameters
  }
}
```

#### Response

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    // Method-specific result
  }
}
```

or in case of an error:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32000,
    "message": "Error message"
  }
}
```

### Methods

The server supports the following methods:

- `list_resources`: List available resources
- `list_resource_templates`: List available resource templates
- `read_resource`: Read a resource
- `list_tools`: List available tools
- `call_tool`: Call a tool

## Extending

### Adding Resources

To add a new resource, implement a resource handler function and register it with the server:

```c
static int my_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count
) {
    // Handle resource request
    // ...
    return 0;
}

// Register the handler
mcp_server_set_resource_handler(server, my_resource_handler, user_data);
```

### Adding Tools

To add a new tool, implement a tool handler function and register it with the server:

```c
static int my_tool_handler(
    mcp_server_t* server,
    const char* name,
    const char* arguments,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count,
    bool* is_error
) {
    // Handle tool call
    // ...
    return 0;
}

// Register the handler
mcp_server_set_tool_handler(server, my_tool_handler, user_data);
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.
