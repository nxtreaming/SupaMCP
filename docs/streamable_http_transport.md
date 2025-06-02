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

### Basic Server Configuration

```c
#include "mcp_sthttp_transport.h"

mcp_sthttp_config_t config = MCP_STHTTP_CONFIG_DEFAULT;
config.host = "127.0.0.1";
config.port = 8080;
config.mcp_endpoint = "/mcp";

mcp_transport_t* transport = mcp_transport_sthttp_create(&config);
```

### Basic Client Configuration

```c
#include "mcp_sthttp_client_transport.h"

mcp_sthttp_client_config_t config = MCP_STHTTP_CLIENT_CONFIG_DEFAULT;
config.host = "127.0.0.1";
config.port = 8080;
config.enable_sessions = true;
config.enable_sse_streams = true;

mcp_transport_t* client = mcp_transport_sthttp_client_create(&config);
```

### Advanced Server Configuration

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

### Advanced Client Configuration

```c
mcp_sthttp_client_config_t config = {
    .host = "127.0.0.1",
    .port = 8080,
    .use_ssl = false,
    .mcp_endpoint = "/mcp",
    .user_agent = "MyMCP-Client/1.0",
    .api_key = NULL,

    // Timeouts
    .connect_timeout_ms = 10000,
    .request_timeout_ms = 30000,
    .sse_reconnect_delay_ms = 5000,
    .max_reconnect_attempts = 10,

    // Features
    .enable_sessions = true,
    .enable_sse_streams = true,
    .auto_reconnect_sse = true,

    // SSL settings (when use_ssl = true)
    .verify_ssl = true,
    .ca_cert_path = NULL,
    .cert_path = NULL,
    .key_path = NULL
};
```

## API Reference

### Core Functions

```c
// Create server transport
mcp_transport_t* mcp_transport_sthttp_create(const mcp_sthttp_config_t* config);

// Create client transport
mcp_transport_t* mcp_transport_sthttp_client_create(const mcp_sthttp_client_config_t* config);

// Send message with session context (server)
int mcp_transport_sthttp_send_with_session(
    mcp_transport_t* transport,
    const void* data,
    size_t size,
    const char* session_id
);
```

### Server Utility Functions

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

### Client Functions

```c
// Connection state enumeration
typedef enum {
    MCP_CLIENT_STATE_DISCONNECTED,      // Not connected
    MCP_CLIENT_STATE_CONNECTING,        // Connecting to server
    MCP_CLIENT_STATE_CONNECTED,         // Connected and ready
    MCP_CLIENT_STATE_SSE_CONNECTING,    // Establishing SSE stream
    MCP_CLIENT_STATE_SSE_CONNECTED,     // SSE stream active
    MCP_CLIENT_STATE_RECONNECTING,      // Reconnecting after failure
    MCP_CLIENT_STATE_ERROR              // Error state
} mcp_client_connection_state_t;

// Connection statistics structure
typedef struct {
    uint64_t requests_sent;
    uint64_t responses_received;
    uint64_t sse_events_received;
    uint64_t connection_errors;
    uint64_t reconnect_attempts;
    time_t last_request_time;
    time_t last_sse_event_time;
} mcp_client_connection_stats_t;

// State change callback
typedef void (*mcp_client_state_callback_t)(
    mcp_transport_t* transport,
    mcp_client_connection_state_t old_state,
    mcp_client_connection_state_t new_state,
    void* user_data
);

// SSE event callback
typedef void (*mcp_client_sse_event_callback_t)(
    mcp_transport_t* transport,
    const char* event_id,
    const char* event_type,
    const char* data,
    void* user_data
);

// Set connection state callback
int mcp_sthttp_client_set_state_callback(
    mcp_transport_t* transport,
    mcp_client_state_callback_t callback,
    void* user_data
);

// Set SSE event callback
int mcp_sthttp_client_set_sse_callback(
    mcp_transport_t* transport,
    mcp_client_sse_event_callback_t callback,
    void* user_data
);

// Get connection statistics
int mcp_sthttp_client_get_stats(mcp_transport_t* transport, mcp_client_connection_stats_t* stats);

// Get current session ID
const char* mcp_sthttp_client_get_session_id(mcp_transport_t* transport);

// Force SSE reconnection
int mcp_sthttp_client_reconnect_sse(mcp_transport_t* transport);
```

## Usage Examples

### Server Example

```c
#include "mcp_server.h"
#include "mcp_sthttp_transport.h"
#include "mcp_sys_utils.h"

static volatile bool g_running = true;

int main() {
    // Create transport configuration
    mcp_sthttp_config_t config = MCP_STHTTP_CONFIG_DEFAULT;
    config.host = "127.0.0.1";
    config.port = 8080;
    config.mcp_endpoint = "/mcp";
    config.enable_sessions = true;
    config.enable_legacy_endpoints = true;

    // Create transport
    mcp_transport_t* transport = mcp_transport_sthttp_create(&config);
    if (transport == NULL) {
        fprintf(stderr, "Failed to create transport\n");
        return 1;
    }

    // Create server configuration
    mcp_server_config_t server_config = {
        .name = "SupaMCP Streamable HTTP Server",
        .version = "1.0.0"
    };

    mcp_server_capabilities_t capabilities = {
        .tools_supported = true,
        .resources_supported = false
    };

    // Create server
    mcp_server_t* server = mcp_server_create(&server_config, &capabilities);
    if (server == NULL) {
        fprintf(stderr, "Failed to create server\n");
        mcp_transport_destroy(transport);
        return 1;
    }

    // Register tools and handlers here...

    // Start server
    if (mcp_server_start(server, transport) != 0) {
        fprintf(stderr, "Failed to start server\n");
        mcp_server_destroy(server);
        mcp_transport_destroy(transport);
        return 1;
    }

    printf("Server started on http://%s:%d%s\n",
           config.host, config.port, config.mcp_endpoint);

    // Wait for server to finish
    while (g_running) {
        mcp_sleep_ms(1000);
    }

    // Cleanup
    mcp_server_destroy(server);
    mcp_transport_destroy(transport);
    return 0;
}
```

### Client Example (C)

```c
#include "mcp_sthttp_client_transport.h"
#include "mcp_sys_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile bool g_running = true;
static int g_request_id = 0;

// Connection state change callback
static void state_callback(mcp_transport_t* transport,
                          mcp_client_connection_state_t old_state,
                          mcp_client_connection_state_t new_state,
                          void* user_data) {
    const char* state_names[] = {
        "DISCONNECTED", "CONNECTING", "CONNECTED",
        "SSE_CONNECTING", "SSE_CONNECTED", "RECONNECTING", "ERROR"
    };

    printf("Connection state changed: %s -> %s\n",
           state_names[old_state], state_names[new_state]);
}

// SSE event callback
static void sse_event_callback(mcp_transport_t* transport,
                              const char* event_id,
                              const char* event_type,
                              const char* data,
                              void* user_data) {
    printf("SSE Event received:\n");
    if (event_id) printf("  ID: %s\n", event_id);
    if (event_type) printf("  Type: %s\n", event_type);
    if (data) printf("  Data: %s\n", data);
    printf("\n");
}

// Message response callback
static char* message_callback(void* user_data, const void* data, size_t size, int* error_code) {
    char* message = (char*)malloc(size + 1);
    if (message) {
        memcpy(message, data, size);
        message[size] = '\0';
        printf("Response received (%zu bytes):\n%s\n\n", size, message);
        free(message);
    }
    return NULL; // No response needed
}

// Error callback
static void error_callback(void* user_data, int error_code) {
    printf("Transport error: %d\n", error_code);
}

int main() {
    // Create client configuration
    mcp_sthttp_client_config_t config = MCP_STHTTP_CLIENT_CONFIG_DEFAULT;
    config.host = "127.0.0.1";
    config.port = 8080;
    config.enable_sessions = true;
    config.enable_sse_streams = true;
    config.auto_reconnect_sse = true;

    // Create client transport
    mcp_transport_t* client = mcp_transport_sthttp_client_create(&config);
    if (client == NULL) {
        fprintf(stderr, "Failed to create client transport\n");
        return 1;
    }

    // Set callbacks
    mcp_sthttp_client_set_state_callback(client, state_callback, NULL);
    mcp_sthttp_client_set_sse_callback(client, sse_event_callback, NULL);

    // Start client with callbacks
    if (mcp_transport_start(client, message_callback, NULL, error_callback) != 0) {
        fprintf(stderr, "Failed to start client transport\n");
        mcp_transport_destroy(client);
        return 1;
    }

    printf("Client started successfully!\n");

    // Wait for connection to establish
    mcp_sleep_ms(1000);

    // Send tool list request
    char tools_request[256];
    snprintf(tools_request, sizeof(tools_request),
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": %d,"
        "\"method\": \"list_tools\""
        "}", ++g_request_id);

    printf("Sending tools list request...\n");
    mcp_transport_send(client, tools_request, strlen(tools_request));

    // Send tool call request
    mcp_sleep_ms(1000);
    char tool_request[512];
    snprintf(tool_request, sizeof(tool_request),
        "{"
        "\"jsonrpc\": \"2.0\","
        "\"id\": %d,"
        "\"method\": \"call_tool\","
        "\"params\": {"
            "\"name\": \"echo\","
            "\"arguments\": {"
                "\"text\": \"Hello from client!\""
            "}"
        "}"
        "}", ++g_request_id);

    printf("Sending tool call request...\n");
    mcp_transport_send(client, tool_request, strlen(tool_request));

    // Print session ID and statistics
    printf("Session ID: %s\n", mcp_sthttp_client_get_session_id(client));

    mcp_client_connection_stats_t stats;
    if (mcp_sthttp_client_get_stats(client, &stats) == 0) {
        printf("Statistics: Requests=%llu, Responses=%llu, SSE Events=%llu\n",
               (unsigned long long)stats.requests_sent,
               (unsigned long long)stats.responses_received,
               (unsigned long long)stats.sse_events_received);
    }

    // Keep running for a while
    mcp_sleep_ms(5000);

    // Cleanup
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

Use the provided example programs to verify functionality:

### Basic Testing

```bash
# Build the examples
cd build
make

# Start the HTTP Streamable server
./examples/http_streamable_server.exe 8080

# In another terminal, run the client
./examples/http_streamable_client.exe 127.0.0.1 8080
```

### Advanced Testing

```bash
# Start server with custom configuration
./examples/http_streamable_server.exe 8080 127.0.0.1 /mcp

# Test with different client configurations
./examples/http_streamable_client.exe 127.0.0.1 8080

# Test SSE connection manually
./examples/test_sse_manual.exe 127.0.0.1 8080
```

### Expected Output

**Server Output:**

```text
Starting MCP Streamable HTTP Server...
Host: 127.0.0.1
Port: 8080
MCP Endpoint: /mcp
Sessions: enabled
Legacy endpoints: enabled

Server started successfully!
MCP endpoint: http://127.0.0.1:8080/mcp
Legacy endpoints:
  - http://127.0.0.1:8080/call_tool
  - http://127.0.0.1:8080/events
  - http://127.0.0.1:8080/tools
Session management: enabled
Session count: 0

Press Ctrl+C to stop the server.
```

**Client Output:**

```text
Starting MCP Streamable HTTP Client...
Server: 127.0.0.1:8080

Connection state changed: DISCONNECTED -> CONNECTING
Connection state changed: CONNECTING -> CONNECTED
Connection state changed: CONNECTED -> SSE_CONNECTING
Connection state changed: SSE_CONNECTING -> SSE_CONNECTED
Client started successfully!

Sending ping request with ID 1...
Response received (XX bytes):
{"jsonrpc":"2.0","id":1,"result":"pong"}

Sending tools list request with ID 2...
Response received (XX bytes):
{"jsonrpc":"2.0","id":2,"result":{"tools":[...]}}

Session ID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
Statistics: Requests=4, Responses=4, SSE Events=0, Errors=0
```

## Limitations

1. **SSL Support**: HTTPS/SSL support in client transport is partially implemented but needs testing
2. **Connection Pooling**: Client connection pooling not yet implemented
3. **Advanced SSE Features**: Some advanced SSE features are still being implemented
4. **Error Recovery**: Advanced error recovery mechanisms could be improved

## Migration from HTTP+SSE Transport

To migrate from the old HTTP+SSE transport:

1. **Server Side:**
   - Replace `mcp_transport_http_create()` with `mcp_transport_sthttp_create()`
   - Update configuration structure from `mcp_http_config_t` to `mcp_sthttp_config_t`
   - Enable legacy endpoints during transition: `config.enable_legacy_endpoints = true`

2. **Client Side:**
   - Replace `mcp_transport_http_client_create()` with `mcp_transport_sthttp_client_create()`
   - Update configuration structure to `mcp_sthttp_client_config_t`
   - Add state and SSE event callbacks for better monitoring
   - Use the unified MCP endpoint instead of separate endpoints

3. **Configuration Updates:**
   - Add session management if desired: `config.enable_sessions = true`
   - Update CORS configuration for new headers (`Mcp-Session-Id`, `Last-Event-ID`)
   - Configure SSE resumability and heartbeats

4. **Testing:**
   - Test with both new and legacy clients during transition
   - Verify SSE connection establishment and event handling
   - Monitor connection statistics and error rates

## Troubleshooting

### Common Issues

1. **SSE Connection Fails:**
   - Check that `enable_sse_streams = true` in client config
   - Verify server has `enable_sse_resumability = true`
   - Check firewall and network connectivity

2. **Session Not Created:**
   - Ensure `enable_sessions = true` on both server and client
   - Check that initialize request is sent first
   - Verify `Mcp-Session-Id` header handling

3. **Tool Calls Fail:**
   - Use correct method names: `list_tools`, `call_tool`
   - Check JSON-RPC format and parameter structure
   - Monitor server logs for error details

4. **Connection Timeouts:**
   - Adjust `connect_timeout_ms` and `request_timeout_ms`
   - Check network latency and server load
   - Consider enabling auto-reconnection features
