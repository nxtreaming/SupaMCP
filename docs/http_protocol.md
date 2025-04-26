# SupaMCP HTTP Protocol Implementation

This document describes the HTTP protocol implementation in SupaMCP.

## Overview

The HTTP protocol implementation in SupaMCP provides a way to interact with the MCP server over HTTP. It supports:

- Tool calls via HTTP POST requests
- Server-Sent Events (SSE) for real-time event notifications
- Tool discovery API
- Static file serving
- CORS support for cross-origin requests

## Endpoints

### `/call_tool` - Tool Call Endpoint

This endpoint accepts POST requests with JSON-RPC 2.0 formatted payloads to call tools.

**Example Request:**

```http
POST /call_tool HTTP/1.1
Host: localhost:8280
Content-Type: application/json

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "call_tool",
  "params": {
    "name": "echo",
    "arguments": {
      "text": "Hello, SupaMCP!"
    }
  }
}
```

**Example Response:**

```http
HTTP/1.1 200 OK
Content-Type: application/json
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
Access-Control-Allow-Credentials: true

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": "Hello, SupaMCP!"
}
```

### `/tools` - Tool Discovery API

This endpoint returns information about available tools.

**Example Request:**

```http
GET /tools HTTP/1.1
Host: localhost:8280
```

**Example Response:**

```http
HTTP/1.1 200 OK
Content-Type: application/json
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
Access-Control-Allow-Credentials: true

{
  "tools": [
    {
      "name": "echo",
      "description": "Echoes back the input text",
      "parameters": {
        "text": {
          "type": "string",
          "description": "Text to echo",
          "required": true
        }
      }
    },
    {
      "name": "reverse",
      "description": "Reverses the input text",
      "parameters": {
        "text": {
          "type": "string",
          "description": "Text to reverse",
          "required": true
        }
      }
    }
  ]
}
```

### `/events` - Server-Sent Events (SSE) Endpoint

This endpoint provides a Server-Sent Events (SSE) stream for real-time event notifications.

**Example Request:**

```http
GET /events HTTP/1.1
Host: localhost:8280
Accept: text/event-stream
```

**Example Response:**

```http
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
Access-Control-Max-Age: 86400
Access-Control-Allow-Credentials: true

data: connected

id: 1
event: echo
data: {"text":"Hello, SupaMCP!"}

id: 2
event: reverse
data: {"text":"!PCMapuS ,olleH"}

: heartbeat
```

## CORS Support

The HTTP protocol implementation supports Cross-Origin Resource Sharing (CORS) to allow web applications hosted on different domains to interact with the MCP server.

CORS headers are added to all responses, and OPTIONS requests (preflight requests) are handled properly.

## Configuration

The HTTP protocol implementation can be configured with the following options:

- `host` - Host to bind to (e.g., "0.0.0.0" for all interfaces)
- `port` - Port to listen on
- `use_ssl` - Whether to use HTTPS
- `cert_path` - Path to SSL certificate file (for HTTPS)
- `key_path` - Path to SSL private key file (for HTTPS)
- `doc_root` - Document root for serving static files
- `timeout_ms` - Connection timeout in milliseconds
- `enable_cors` - Whether to enable CORS
- `cors_allow_origin` - Allowed origins for CORS (e.g., "*" for all)
- `cors_allow_methods` - Allowed methods for CORS (e.g., "GET, POST, OPTIONS")
- `cors_allow_headers` - Allowed headers for CORS
- `cors_max_age` - Max age for CORS preflight requests in seconds

## Testing

The HTTP protocol implementation can be tested using the provided test scripts:

- `tests/start_http_server.py` - Starts the HTTP server for testing
- `tests/http_protocol_test.py` - Tests the HTTP protocol implementation

To run the tests:

1. Start the HTTP server:

```bash
python tests/start_http_server.py
```

2. Run the tests:

```bash
python tests/http_protocol_test.py
```

## Implementation Details

The HTTP protocol implementation is based on libwebsockets and provides the following features:

- HTTP server with support for GET, POST, and OPTIONS methods
- JSON-RPC 2.0 support for tool calls
- Server-Sent Events (SSE) for real-time event notifications
- Static file serving with proper MIME type detection
- CORS support for cross-origin requests
- Tool discovery API
- Efficient event storage using a circular buffer

The implementation is designed to be lightweight, efficient, and compatible with web browsers and other HTTP clients.
