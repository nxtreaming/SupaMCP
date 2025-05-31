# MCP Streamable HTTP Transport Test

This directory contains a Python test script for the MCP Streamable HTTP Transport implementation.

## Files

- `test_streamable_http.py` - Main test script for Streamable HTTP transport
- `requirements_streamable.txt` - Python dependencies for the test script
- `README_streamable.md` - This file

## Prerequisites

1. Python 3.7 or higher
2. pip package manager

## Installation

Install the required Python packages:

```bash
pip install -r requirements_streamable.txt
```

## Usage

### Start the Server

First, build and start the SupaMCP server with Streamable HTTP transport:

```bash
# Build the project
cmake --build build --config Release

# Start the streamable HTTP server
./build/examples/http_streamable_server 8080
```

### Run the Tests

In a separate terminal, run the test script:

```bash
# Test against default server (localhost:8080)
python test_streamable_http.py

# Test against custom server
python test_streamable_http.py http://localhost:9000
```

## Test Coverage

The test script covers the following functionality:

### Basic Functionality
1. **Initialization** - Tests MCP session initialization
2. **Tool Listing** - Tests the `tools/list` method
3. **Tool Calling** - Tests calling the `echo` and `reverse` tools
4. **Session Management** - Tests session creation and termination

### Legacy Endpoints (Backwards Compatibility)
1. **Legacy /call_tool** - Tests the legacy tool calling endpoint
2. **Legacy /tools** - Tests the legacy tool discovery endpoint

### CORS Support
1. **OPTIONS Request** - Tests CORS preflight requests
2. **CORS Headers** - Verifies proper CORS header responses

## Expected Output

When running against a working server, you should see output like:

```
Testing MCP Streamable HTTP Transport at: http://127.0.0.1:8080
============================================================
Waiting for server to be ready...
Server is ready!

=== Testing Basic Functionality ===
1. Testing initialization...
Session established: a1b2c3d4e5f6...
   ✓ Initialization successful
   Response: {
     "jsonrpc": "2.0",
     "id": 1,
     "result": {
       "protocolVersion": "2025-03-26",
       "capabilities": {
         "tools": {}
       },
       "serverInfo": {
         "name": "SupaMCP Streamable HTTP Server",
         "version": "1.0.0"
       }
     }
   }

2. Testing tool listing...
   ✓ Tool listing successful
   Response: {
     "jsonrpc": "2.0",
     "id": 3,
     "result": {
       "tools": [
         {
           "name": "echo",
           "description": "Echo the input text",
           "inputSchema": {
             "type": "object",
             "properties": {
               "text": {
                 "type": "string",
                 "description": "Text to echo"
               }
             },
             "required": ["text"]
           }
         },
         {
           "name": "reverse",
           "description": "Reverse the input text",
           "inputSchema": {
             "type": "object",
             "properties": {
               "text": {
                 "type": "string",
                 "description": "Text to reverse"
               }
             },
             "required": ["text"]
           }
         }
       ]
     }
   }

3. Testing echo tool...
   ✓ Echo tool successful
   Response: {
     "jsonrpc": "2.0",
     "id": 2,
     "result": {
       "content": [
         {
           "type": "text",
           "text": "Hello, Streamable HTTP!"
         }
       ]
     }
   }

4. Testing reverse tool...
   ✓ Reverse tool successful
   Response: {
     "jsonrpc": "2.0",
     "id": 2,
     "result": {
       "content": [
         {
           "type": "text",
           "text": "!dlroW ,olleH"
         }
       ]
     }
   }

5. Testing session termination...
Session terminated: a1b2c3d4e5f6...
   ✓ Session termination successful

=== Testing Legacy Endpoints ===
1. Testing legacy /call_tool endpoint...
   ✓ Legacy /call_tool successful
   Response: {
     "jsonrpc": "2.0",
     "id": 1,
     "result": {
       "content": [
         {
           "type": "text",
           "text": "Legacy endpoint test"
         }
       ]
     }
   }

2. Testing legacy /tools endpoint...
   ✓ Legacy /tools successful
   Response: {"tools":[]}

=== Testing CORS ===
1. Testing OPTIONS request...
   ✓ OPTIONS request successful
   CORS headers:
     Access-Control-Allow-Origin: *
     Access-Control-Allow-Methods: GET, POST, OPTIONS, DELETE
     Access-Control-Allow-Headers: Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID
     Access-Control-Max-Age: 86400

============================================================
Testing completed!
```

## Troubleshooting

### Server Not Responding
- Make sure the server is built and running
- Check that the port matches (default is 8080)
- Verify no firewall is blocking the connection

### Import Errors
- Make sure you've installed the requirements: `pip install -r requirements_streamable.txt`
- Check your Python version (3.7+ required)

### Test Failures
- Check server logs for error messages
- Verify the server supports the MCP 2025-03-26 protocol
- Make sure all required tools are registered on the server

## Customization

You can modify the test script to:

- Test additional tools by adding them to the test functions
- Test different MCP endpoints by changing the `mcp_endpoint` parameter
- Add SSE stream testing (currently not implemented)
- Test with different session configurations

## Notes

- The test script currently focuses on JSON-RPC over HTTP
- SSE (Server-Sent Events) testing is not yet fully implemented
- Some advanced features like stream resumability are not tested
- The script is designed for testing during development, not production use
