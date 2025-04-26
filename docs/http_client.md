# HTTP Client Transport

This document describes the HTTP client transport implementation in SupaMCP, including Server-Sent Events (SSE) support.

## Overview

The HTTP client transport allows MCP clients to communicate with MCP servers using the HTTP protocol. It supports:

- JSON-RPC requests over HTTP POST
- Server-Sent Events (SSE) for asynchronous notifications
- API key authentication
- SSL/TLS encryption (optional)

## Usage

To use the HTTP client transport, specify the `--http` flag when starting the MCP client:

```bash
mcp_client --http --host 127.0.0.1 --port 8080
```

## Implementation Details

The HTTP client transport is implemented in `src/transport/mcp_http_client_transport.c`. It consists of the following main components:

### 1. Transport Creation

The transport is created using one of the following functions:

```c
mcp_transport_t* mcp_transport_http_client_create(const char* host, uint16_t port);
mcp_transport_t* mcp_transport_http_client_create_with_config(const mcp_http_client_config_t* config);
```

The configuration structure allows specifying additional options:

```c
typedef struct {
    const char* host;         // Host to connect to
    uint16_t port;            // Port to connect to
    bool use_ssl;             // Whether to use SSL
    const char* cert_path;    // Path to SSL certificate
    const char* key_path;     // Path to SSL private key
    uint32_t timeout_ms;      // Connection timeout in milliseconds
    const char* api_key;      // API key for authentication
} mcp_http_client_config_t;
```

### 2. Request Handling

The HTTP client transport sends JSON-RPC requests using HTTP POST to the `/call_tool` endpoint. The implementation handles:

- Binary length prefix format used by MCP client
- JSON-RPC request formatting
- HTTP request creation and sending
- HTTP response parsing
- Error handling

### 3. Server-Sent Events (SSE)

The HTTP client transport includes support for Server-Sent Events (SSE), which allows the server to push notifications to the client. The SSE implementation:

- Connects to the `/events` endpoint
- Parses SSE events (id, event, data)
- Processes events and calls the message callback
- Handles reconnection if the connection is lost

## Protocol Differences

The HTTP client transport differs from other transports (like TCP) in how it handles the request-response cycle:

- **TCP Transport**: Sends a request and then waits for a response in a separate receive operation.
- **HTTP Transport**: Sends a request and receives the response in the same operation, due to the synchronous nature of HTTP.

This difference required special handling in the MCP client to avoid waiting for responses that have already been processed.

## SSE Testing

To test the SSE functionality:

1. Start an MCP server with HTTP transport:
   ```bash
   mcp_server --http --host 127.0.0.1 --port 8080
   ```

2. Start an MCP client with HTTP transport:
   ```bash
   mcp_client --http --host 127.0.0.1 --port 8080
   ```

3. Trigger events on the server (e.g., by calling a long-running tool).

4. Observe the client logs for SSE events.

## Error Handling

The HTTP client transport includes robust error handling for:

- Connection failures
- HTTP request/response errors
- SSL/TLS errors
- JSON parsing errors
- SSE connection and parsing errors

## Future Improvements

Potential future improvements to the HTTP client transport include:

- Better SSL/TLS support
- HTTP/2 support
- Connection pooling
- Compression support
- More robust SSE reconnection logic
- Support for WebSockets as an alternative to SSE
