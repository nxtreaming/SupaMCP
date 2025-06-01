# MCP Streamable HTTP Transport

This document provides a quick start guide for the new MCP Streamable HTTP Transport implementation in SupaMCP.

## Overview

The Streamable HTTP Transport implements the MCP 2025-03-26 protocol specification, providing:

- **Unified MCP Endpoint**: Single endpoint (`/mcp`) for all MCP operations
- **Session Management**: Optional stateful communication with session IDs
- **Streaming Responses**: Support for both JSON and SSE responses
- **Enhanced Security**: Origin validation, CORS support, and secure session management
- **Backwards Compatibility**: Optional support for legacy HTTP+SSE endpoints

## Quick Start

### 1. Build the Example

```bash
# Create and configure build directory
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the streamable server example
cmake --build . --config Release --target http_streamable_server
```

### 2. Run the Server

```bash
# Windows
.\build\examples\Release\http_streamable_server.exe 8080

# Linux/macOS
./build/examples/http_streamable_server 8080
```

The server will start on `http://localhost:8080` with the MCP endpoint at `/mcp`.

### 3. Test the Implementation

**Install Python dependencies:**
```bash
cd python
pip install -r requirements_streamable.txt
```

**Run the test script:**
```bash
python test_streamable_http.py http://localhost:8080
```

## API Endpoints

### Primary MCP Endpoint

- **URL**: `/mcp` (configurable)
- **Methods**: POST, GET, DELETE, OPTIONS

#### POST - Send JSON-RPC Requests
```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2025-03-26",
      "capabilities": {"tools": {}},
      "clientInfo": {"name": "test-client", "version": "1.0.0"}
    }
  }'
```

#### GET - Open SSE Stream
```bash
curl -X GET http://localhost:8080/mcp \
  -H "Accept: text/event-stream" \
  -H "Cache-Control: no-cache"
```

#### DELETE - Terminate Session
```bash
curl -X DELETE http://localhost:8080/mcp \
  -H "Mcp-Session-Id: your-session-id"
```

#### OPTIONS - CORS Preflight
```bash
curl -X OPTIONS http://localhost:8080/mcp \
  -H "Origin: http://localhost:3000" \
  -H "Access-Control-Request-Method: POST"
```

### Legacy Endpoints (Optional)

When `enable_legacy_endpoints` is true:

- **POST** `/call_tool` - Legacy tool calling
- **GET** `/events` - Legacy SSE stream
- **GET** `/tools` - Legacy tool discovery

## Configuration

The server can be configured through the `mcp_http_streamable_config_t` structure:

```c
mcp_http_streamable_config_t config = {
    .host = "127.0.0.1",
    .port = 8080,
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

## Session Management

Sessions provide stateful communication:

1. **Session Creation**: Server responds with `Mcp-Session-Id` header on initialization
2. **Session Usage**: Client includes `Mcp-Session-Id` header in subsequent requests
3. **Session Termination**: Client sends DELETE request to terminate session
4. **Session Timeout**: Sessions automatically expire after configured timeout

## Testing Tools

### Python Test Client

The `python/test_streamable_http.py` script provides comprehensive testing:

```bash
# Basic functionality test
python test_streamable_http.py

# Test against custom server
python test_streamable_http.py http://localhost:9000

# Test specific functionality
python -c "
from test_streamable_http import *
test_basic_functionality('http://localhost:8080')
"
```

### Manual Testing with curl

**Initialize session:**
```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{"tools":{}},"clientInfo":{"name":"curl-client","version":"1.0.0"}}}' \
  -v
```

**Call a tool:**
```bash
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: YOUR_SESSION_ID" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"echo","arguments":{"text":"Hello, World!"}}}'
```

## Troubleshooting

### Build Issues

1. **Missing libwebsockets**: Ensure libwebsockets is properly installed
2. **Link errors**: Check that all required libraries are linked
3. **Header not found**: Verify include paths are correct

### Runtime Issues

1. **Port already in use**: Change the port number or stop conflicting services
2. **Permission denied**: On Linux/macOS, ports below 1024 require root privileges
3. **CORS errors**: Check origin validation and CORS configuration

### Common Error Messages

- **"Failed to create Streamable HTTP transport"**: Check configuration parameters
- **"Session not found"**: Verify session ID is correct and session hasn't expired
- **"Origin not allowed"**: Add your origin to the `allowed_origins` configuration

## Development

### Adding New Features

1. **New endpoints**: Add handlers in `mcp_http_streamable_callbacks.c`
2. **Session data**: Extend `http_streamable_session_data_t` structure
3. **Configuration**: Add options to `mcp_http_streamable_config_t`

### Debugging

Enable debug logging:
```c
mcp_log_set_level(MCP_LOG_LEVEL_DEBUG);
```

Monitor network traffic:
```bash
# Linux
sudo tcpdump -i lo port 8080

# Windows
netsh trace start capture=yes tracefile=trace.etl provider=Microsoft-Windows-TCPIP
```

## Documentation

- **Full Documentation**: `docs/streamable_http_transport.md`
- **API Reference**: See header files in `include/`
- **Examples**: Check `examples/http_streamable_server.c`

## Support

For issues and questions:

1. Check the troubleshooting section above
2. Review the full documentation
3. Examine the example implementation
4. Enable debug logging for detailed error information
