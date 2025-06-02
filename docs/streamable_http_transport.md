# Streamable HTTP Transport

This document describes the Streamable HTTP transport implementation in SupaMCP, which implements the MCP 2025-03-26 protocol specification.

## Overview

The Streamable HTTP transport is the latest transport mechanism for the Model Context Protocol (MCP), replacing the HTTP+SSE transport from the 2024-11-05 specification.

**Status**: ✅ **Server and Client implementations are complete and functional**

It provides:

- **Unified MCP Endpoint**: A single endpoint that handles both POST and GET requests
- **Session Management**: Optional session support with `Mcp-Session-Id` headers
- **Streaming Responses**: POST requests can return either JSON or SSE streams
- **Resumable Streams**: SSE streams can be resumed using `Last-Event-ID` headers
- **Enhanced Security**: Origin validation and improved CORS support
- **Backwards Compatibility**: Optional support for legacy HTTP+SSE endpoints

## Key Features

### 1. Unified MCP Endpoint

Unlike the previous HTTP+SSE transport that used separate endpoints (`/call_tool`, `/events`), the Streamable HTTP transport uses a single configurable endpoint (default: `/mcp`) that supports:

- **POST**: Send JSON-RPC requests, receive JSON or SSE responses
- **GET**: Open SSE streams for server-to-client communication
- **DELETE**: Terminate sessions (when session management is enabled)
- **OPTIONS**: CORS preflight requests

### 2. Session Management

Sessions provide stateful communication between clients and servers:

```c
// Enable sessions in configuration
mcp_sthttp_config_t config = MCP_STHTTP_CONFIG_DEFAULT;
config.enable_sessions = true;
config.session_timeout_seconds = 3600; // 1 hour timeout
```

Session workflow:

1. Client sends `initialize` request to MCP endpoint
2. Server responds with `Mcp-Session-Id` header if sessions are enabled
3. Client includes `Mcp-Session-Id` header in subsequent requests
4. Client can terminate session with DELETE request

### 3. Streaming Responses

POST requests can return different response types based on the `Accept` header:

- `Accept: application/json` → JSON response
- `Accept: text/event-stream` → SSE stream response
- `Accept: application/json, text/event-stream` → Server chooses format

### 4. Stream Resumability

SSE streams can be resumed after disconnection:

```http
GET /mcp HTTP/1.1
Accept: text/event-stream
Last-Event-ID: 12345
```

The server will replay events after the specified event ID.

## Configuration

### Basic Configuration

```c
#include "mcp_http_streamable_transport.h"

mcp_http_streamable_config_t config = MCP_HTTP_STREAMABLE_CONFIG_DEFAULT;
config.host = "127.0.0.1";
config.port = 8080;
config.mcp_endpoint = "/mcp";

mcp_transport_t* transport = mcp_transport_sthttp_create(&config);
```

### Advanced Configuration

```c
mcp_sthttp_config_t config = {
    .host = "127.0.0.1",
    .port = 8080,
    .use_ssl = false,
    .mcp_endpoint = "/mcp",
    
    // Session management
    .enable_sessions = true,
    .session_timeout_seconds = 3600,
    
    // Security
    .validate_origin = true,
    .allowed_origins = "http://localhost:*,https://localhost:*",
    
    // CORS
    .enable_cors = true,
    .cors_allow_origin = "*",
    .cors_allow_methods = "GET, POST, OPTIONS, DELETE",
    .cors_allow_headers = "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID",
    
    // SSE settings
    .enable_sse_resumability = true,
    .max_stored_events = 1000,
    .send_heartbeats = true,
    .heartbeat_interval_ms = 30000,
    
    // Backwards compatibility
    .enable_legacy_endpoints = true
};
```

## API Reference

### Core Functions

```c
// Create server transport
mcp_transport_t* mcp_transport_sthttp_create(const mcp_sthttp_config_t* config);

// Create client transport
mcp_transport_t* mcp_transport_sthttp_client_create(
    const char* host,
    uint16_t port,
    const char* mcp_endpoint,
    bool use_ssl,
    const char* api_key
);

// Send message with session context
int mcp_transport_sthttp_send_with_session(
    mcp_transport_t* transport, 
    const void* data, 
    size_t size,
    const char* session_id
);
```

### Utility Functions

```c
// Get MCP endpoint path
const char* mcp_transport_sthttp_get_endpoint(mcp_transport_t* transport);

// Check if sessions are enabled
bool mcp_transport_sthttp_has_sessions(mcp_transport_t* transport);

// Get active session count
size_t mcp_transport_sthttp_get_session_count(mcp_transport_t* transport);

// Terminate a specific session
bool mcp_transport_sthttp_terminate_session(mcp_transport_t* transport, const char* session_id);
```

## Usage Examples

### Server Example

```c
#include "mcp_server.h"
#include "mcp_sthttp_transport.h"

int main() {
    // Create transport
    mcp_sthttp_config_t config = MCP_STHTTP_CONFIG_DEFAULT;
    config.port = 8080;
    mcp_transport_t* transport = mcp_transport_sthttp_create(&config);
    
    // Create server
    mcp_server_config_t server_config = {
        .name = "My MCP Server",
        .version = "1.0.0"
    };
    
    mcp_server_capabilities_t capabilities = { .tools = true };
    mcp_server_t* server = mcp_server_create(&server_config, &capabilities);
    
    // Start server
    mcp_server_start_with_transport(server, transport);
    mcp_server_wait(server);
    
    // Cleanup
    mcp_server_destroy(server);
    mcp_transport_destroy(transport);
    return 0;
}
```

### Client Example (C)

```c
#include "mcp_transport.h"
#include "mcp_sthttp_client_transport.h"

// Message callback
static char* message_callback(void* user_data, const void* data, size_t size, int* error_code) {
    char* message = (char*)malloc(size + 1);
    if (message) {
        memcpy(message, data, size);
        message[size] = '\0';
        printf("Response: %s\n", message);
        free(message);
    }
    return NULL; // No response needed
}

// Error callback
static void error_callback(void* user_data, int error_code) {
    printf("Transport error: %d\n", error_code);
}

int main() {
    // Create client transport
    mcp_transport_t* client = mcp_transport_sthttp_client_create(
        "localhost", 8080, "/mcp", false, NULL
    );

    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    // Start client with callbacks
    if (mcp_transport_start(client, message_callback, NULL, error_callback) != 0) {
        fprintf(stderr, "Failed to start client\n");
        mcp_transport_destroy(client);
        return 1;
    }

    // Send initialize request
    const char* init_request =
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": 1,"
        "\"method\": \"initialize\","
        "\"params\": {"
            "\"protocolVersion\": \"2025-03-26\","
            "\"capabilities\": {\"tools\": {}},"
            "\"clientInfo\": {\"name\": \"test-client\", \"version\": \"1.0.0\"}"
        "}"
        "}";

    mcp_transport_send(client, init_request, strlen(init_request));

    // Keep running to receive responses
    sleep(5);

    // Cleanup
    mcp_transport_stop(client);
    mcp_transport_destroy(client);
    return 0;
}
```

### Client Example (Python)

```python
import requests
import json

class MCPClient:
    def __init__(self, base_url, mcp_endpoint="/mcp"):
        self.base_url = base_url
        self.mcp_endpoint = mcp_endpoint
        self.session_id = None
        self.session = requests.Session()
    
    def initialize(self):
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-03-26",
                "capabilities": {"tools": {}},
                "clientInfo": {"name": "test-client", "version": "1.0.0"}
            }
        }
        
        response = self.session.post(
            f"{self.base_url}{self.mcp_endpoint}",
            json=request,
            headers={"Accept": "application/json"}
        )
        
        # Extract session ID
        self.session_id = response.headers.get("Mcp-Session-Id")
        return response.json()
    
    def call_tool(self, name, arguments):
        request = {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments}
        }
        
        headers = {"Content-Type": "application/json"}
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        
        response = self.session.post(
            f"{self.base_url}{self.mcp_endpoint}",
            json=request,
            headers=headers
        )
        
        return response.json()

# Usage
client = MCPClient("http://localhost:8080")
client.initialize()
result = client.call_tool("echo", {"text": "Hello, World!"})
print(result)
```

## Security Considerations

1. **Origin Validation**: Enable `validate_origin` and configure `allowed_origins` for production
2. **HTTPS**: Use SSL/TLS in production environments
3. **Session Security**: Session IDs are cryptographically secure UUIDs
4. **CORS**: Configure CORS headers appropriately for your use case

## Backwards Compatibility

The transport supports legacy HTTP+SSE endpoints when `enable_legacy_endpoints` is true:

- `/call_tool` → Redirects to MCP endpoint
- `/events` → Opens SSE stream
- `/tools` → Returns tool discovery response

## Testing

Use the provided test tools to verify functionality:

```bash
# Start the server
./build/examples/http_streamable_server 8080

# Test with C client
./build/examples/http_streamable_client

# Test with Python script
python3 examples/test_streamable_http.py http://localhost:8080
```

## Limitations

1. **SSL Support**: HTTPS/SSL support in client transport needs implementation
2. **Header Extraction**: Custom header extraction needs improvement
3. **Connection Pooling**: Client connection pooling not yet implemented
4. **Advanced SSE Features**: Some advanced SSE features are still being implemented

## Migration from HTTP+SSE Transport

To migrate from the old HTTP+SSE transport:

1. Replace `mcp_transport_http_create()` with `mcp_transport_sthttp_create()`
2. Update client code to use the unified MCP endpoint
3. Add session management if desired
4. Update CORS configuration for new headers
5. Test with both new and legacy clients during transition
