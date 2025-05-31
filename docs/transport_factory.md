# Transport Factory

The Transport Factory provides a unified interface for creating different types of MCP transports. This simplifies transport creation and provides a consistent API across all transport types.

## Overview

The transport factory centralizes the creation logic for all supported transport types:

- **STDIO Transport** - Standard input/output communication
- **TCP Transport** - TCP server and client transports
- **WebSocket Transport** - WebSocket server, client, and connection pool
- **HTTP Transport** - Traditional HTTP+SSE transport
- **HTTP Streamable Transport** - New MCP 2025-03-26 Streamable HTTP transport

## Usage

### Basic Usage

```c
#include "mcp_transport_factory.h"

// Create transport configuration
mcp_transport_config_t config = {0};
// ... configure based on transport type

// Create transport
mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_HTTP_STREAMABLE, &config);
if (transport == NULL) {
    // Handle error
}

// Use transport with server
mcp_server_start(server, transport);

// Cleanup
mcp_transport_destroy(transport);
```

### Supported Transport Types

#### MCP_TRANSPORT_STDIO
Standard input/output transport (no configuration needed):

```c
mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_STDIO, NULL);
```

#### MCP_TRANSPORT_TCP
TCP server transport:

```c
mcp_transport_config_t config = {0};
config.tcp.host = "127.0.0.1";
config.tcp.port = 8080;
config.tcp.idle_timeout_ms = 30000;

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP, &config);
```

#### MCP_TRANSPORT_TCP_CLIENT
TCP client transport:

```c
mcp_transport_config_t config = {0};
config.tcp.host = "127.0.0.1";
config.tcp.port = 8080;

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP_CLIENT, &config);
```

#### MCP_TRANSPORT_WS_SERVER
WebSocket server transport:

```c
mcp_transport_config_t config = {0};
config.ws.host = "127.0.0.1";
config.ws.port = 8080;
config.ws.path = "/ws";
config.ws.use_ssl = 0;

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_WS_SERVER, &config);
```

#### MCP_TRANSPORT_WS_CLIENT
WebSocket client transport:

```c
mcp_transport_config_t config = {0};
config.ws.host = "127.0.0.1";
config.ws.port = 8080;
config.ws.path = "/ws";
config.ws.use_ssl = 0;

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_WS_CLIENT, &config);
```

#### MCP_TRANSPORT_HTTP_SERVER
Traditional HTTP+SSE transport:

```c
mcp_transport_config_t config = {0};
config.http.host = "127.0.0.1";
config.http.port = 8080;
config.http.use_ssl = 0;
config.http.doc_root = "./www";

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_HTTP_SERVER, &config);
```

#### MCP_TRANSPORT_HTTP_CLIENT
HTTP client transport:

```c
mcp_transport_config_t config = {0};
config.http_client.host = "127.0.0.1";
config.http_client.port = 8080;
config.http_client.use_ssl = 0;
config.http_client.api_key = "your-api-key";

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_HTTP_CLIENT, &config);
```

#### MCP_TRANSPORT_HTTP_STREAMABLE
New Streamable HTTP transport (MCP 2025-03-26):

```c
mcp_transport_config_t config = {0};
config.http_streamable.host = "127.0.0.1";
config.http_streamable.port = 8080;
config.http_streamable.use_ssl = 0;
config.http_streamable.mcp_endpoint = "/mcp";
config.http_streamable.enable_sessions = 1;
config.http_streamable.session_timeout_seconds = 3600;
config.http_streamable.validate_origin = 1;
config.http_streamable.allowed_origins = "http://localhost:*,https://localhost:*";
config.http_streamable.enable_cors = 1;
config.http_streamable.cors_allow_origin = "*";
config.http_streamable.cors_allow_methods = "GET, POST, OPTIONS, DELETE";
config.http_streamable.cors_allow_headers = "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID";
config.http_streamable.cors_max_age = 86400;
config.http_streamable.enable_sse_resumability = 1;
config.http_streamable.max_stored_events = 1000;
config.http_streamable.send_heartbeats = 1;
config.http_streamable.heartbeat_interval_ms = 30000;
config.http_streamable.enable_legacy_endpoints = 1;

mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_HTTP_STREAMABLE, &config);
```

## Configuration Structure

The `mcp_transport_config_t` is a union containing configuration options for all transport types. Only the fields relevant to the chosen transport type should be used.

### HTTP Streamable Configuration Fields

| Field | Type | Description |
|-------|------|-------------|
| `host` | `const char*` | Hostname or IP address to bind to |
| `port` | `uint16_t` | Port number |
| `use_ssl` | `int` | Whether to use SSL/TLS (1 for true, 0 for false) |
| `cert_path` | `const char*` | Path to SSL certificate (if use_ssl is true) |
| `key_path` | `const char*` | Path to SSL private key (if use_ssl is true) |
| `doc_root` | `const char*` | Document root for serving static files (optional) |
| `timeout_ms` | `uint32_t` | Connection timeout in milliseconds (0 to disable) |
| `mcp_endpoint` | `const char*` | MCP endpoint path (default: "/mcp") |
| `enable_sessions` | `int` | Whether to enable session management |
| `session_timeout_seconds` | `uint32_t` | Session timeout in seconds (0 for default) |
| `validate_origin` | `int` | Whether to validate Origin header |
| `allowed_origins` | `const char*` | Comma-separated list of allowed origins |
| `enable_cors` | `int` | Whether to enable CORS |
| `cors_allow_origin` | `const char*` | Allowed origins for CORS |
| `cors_allow_methods` | `const char*` | Allowed methods for CORS |
| `cors_allow_headers` | `const char*` | Allowed headers for CORS |
| `cors_max_age` | `int` | Max age for CORS preflight requests in seconds |
| `enable_sse_resumability` | `int` | Whether to enable SSE stream resumability |
| `max_stored_events` | `uint32_t` | Maximum number of events to store for resumability |
| `send_heartbeats` | `int` | Whether to send SSE heartbeats |
| `heartbeat_interval_ms` | `uint32_t` | Heartbeat interval in milliseconds |
| `enable_legacy_endpoints` | `int` | Whether to enable legacy HTTP+SSE endpoints |

## Complete Example

See `examples/factory_streamable_server.c` for a complete example of using the transport factory to create a Streamable HTTP transport server.

```bash
# Build the example
cmake --build build --config Release --target factory_streamable_server

# Run the example
./build/examples/factory_streamable_server 8080
```

## Benefits

1. **Unified Interface** - Single function to create any transport type
2. **Simplified Configuration** - Consistent configuration structure
3. **Type Safety** - Compile-time checking of transport types
4. **Maintainability** - Centralized creation logic
5. **Extensibility** - Easy to add new transport types

## Error Handling

The factory function returns `NULL` on failure. Common failure reasons:

- Invalid configuration (NULL config when required)
- Memory allocation failure
- Transport-specific initialization failure
- Unsupported transport type

Always check the return value and handle errors appropriately:

```c
mcp_transport_t* transport = mcp_transport_factory_create(type, &config);
if (transport == NULL) {
    fprintf(stderr, "Failed to create transport\n");
    return -1;
}
```

## Migration

If you're currently using direct transport creation functions, you can easily migrate to the factory:

**Before:**
```c
mcp_http_streamable_config_t config = MCP_HTTP_STREAMABLE_CONFIG_DEFAULT;
config.port = 8080;
mcp_transport_t* transport = mcp_transport_http_streamable_create(&config);
```

**After:**
```c
mcp_transport_config_t config = {0};
config.http_streamable.port = 8080;
// ... set other fields as needed
mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_HTTP_STREAMABLE, &config);
```

The factory approach provides better consistency and makes it easier to switch between transport types during development and testing.
