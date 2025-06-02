# MCP Server Testing Tools

This directory contains various testing and debugging tools for the MCP (Model Context Protocol) server implementation.

## HTTP Streamable Transport Testing

### test_sse_manual.exe
**Purpose**: Manual SSE (Server-Sent Events) connection testing tool

**Description**: 
This tool creates a raw TCP connection to test the HTTP Streamable transport's SSE functionality. It's useful for debugging connection issues, header parsing problems, and protocol compliance.

**Usage**:
```bash
# Start the HTTP Streamable server first
./http_streamable_server.exe

# In another terminal, run the SSE test
./test_sse_manual.exe
```

**What it tests**:
- TCP connection establishment
- HTTP GET request for SSE endpoint
- Response header parsing (especially Content-Type)
- SSE protocol compliance
- Case-insensitive header handling

**Sample Output**:
```
Connected to server
Sending SSE request:
GET /mcp HTTP/1.1
Host: 127.0.0.1:8080
Accept: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

Received response (612 bytes):
=== RAW RESPONSE ===
HTTP/1.1 200 OK\r\n
content-type: text/event-stream\r\n
...
✅ Found Content-Type header: content-type: text/event-stream
✅ Found text/event-stream in response
```

### http_streamable_client.exe
**Purpose**: Full-featured HTTP Streamable client testing

**Description**:
Complete client implementation that tests all aspects of the HTTP Streamable transport including JSON-RPC communication, SSE streams, and session management.

**Usage**:
```bash
# Start the HTTP Streamable server first
./http_streamable_server.exe

# In another terminal, run the client
./http_streamable_client.exe
```

**What it tests**:
- SSE connection establishment
- JSON-RPC method calls (ping, list_tools, call_tool)
- Response parsing and error detection
- Connection statistics and monitoring
- Session management (if enabled)

**Sample Output**:
```
Starting MCP Streamable HTTP Client...
Server: 127.0.0.1:8080

Connection state changed: DISCONNECTED -> CONNECTING
Connection state changed: CONNECTING -> SSE_CONNECTED
SSE Event received:
  Type: connection
  Data: {"type":"connection","session_id":"null","timestamp":1748838257}

✅ Success response detected!
Statistics: Requests=4, Responses=4, SSE Events=1, Errors=0
```

### http_streamable_server.exe
**Purpose**: HTTP Streamable transport server

**Description**:
The main server implementation that provides HTTP Streamable transport with SSE support, JSON-RPC handling, and tool execution.

**Features**:
- HTTP/1.1 with SSE support
- JSON-RPC 2.0 protocol
- Built-in tools (echo, reverse)
- Session management
- CORS support
- Performance metrics

## Testing Workflow

### 1. Basic Functionality Test
```bash
# Terminal 1: Start server
./http_streamable_server.exe

# Terminal 2: Test SSE connection
./test_sse_manual.exe

# Terminal 3: Test full client functionality
./http_streamable_client.exe
```

### 2. Debugging Connection Issues
If you encounter SSE connection problems:

1. **Use test_sse_manual.exe** to check raw HTTP response
2. **Check Content-Type header** case sensitivity
3. **Verify server response format** matches SSE specification
4. **Check for CORS issues** if testing from browser

### 3. Performance Testing
```bash
# Run multiple clients simultaneously
./http_streamable_client.exe &
./http_streamable_client.exe &
./http_streamable_client.exe &
```

## Common Issues and Solutions

### SSE Connection Fails
**Symptoms**: `Invalid SSE content type: none`
**Solution**: Check that server sends `content-type: text/event-stream` header

### Method Not Found Errors
**Symptoms**: `{"error":{"code":-32601,"message":"Method not found"}}`
**Solution**: Verify method names match server expectations:
- Use `ping` instead of `initialize`
- Use `list_tools` instead of `tools/list`
- Use `call_tool` instead of `tools/call`

### Connection Refused
**Symptoms**: `Connection failed`
**Solution**: 
- Ensure server is running on correct port (default: 8080)
- Check firewall settings
- Verify server startup logs for errors

### Session ID Issues
**Symptoms**: `Session ID: (null)`
**Solution**: This is normal for basic testing. Session creation requires specific configuration.

## Build Instructions

To build the testing tools:

```bash
# From project root
cd build
cmake --build . --target test_sse_manual --config Debug
cmake --build . --target http_streamable_client --config Debug
cmake --build . --target http_streamable_server --config Debug
```

## Troubleshooting

### Windows-Specific Issues
- Ensure Winsock2 is properly initialized
- Check for Windows Defender blocking connections
- Use `netstat -an | findstr 8080` to verify server is listening

### Debug Logging
Set log level to DEBUG for detailed output:
```c
mcp_log_set_level(MCP_LOG_LEVEL_DEBUG);
```

### Network Analysis
Use Wireshark or similar tools to capture network traffic for detailed protocol analysis.

## Contributing

When adding new testing tools:
1. Add the executable to `examples/CMakeLists.txt`
2. Include appropriate platform-specific libraries
3. Document the tool's purpose in this README
4. Provide usage examples and expected output
