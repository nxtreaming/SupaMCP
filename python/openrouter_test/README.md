# MCP Echo Tool Test with OpenRouter.io

This Python script tests your MCP server's echo tool using OpenRouter.io's LLM API. It connects to a locally running MCP server and instructs the LLM to use the echo tool.

## Prerequisites

- Python 3.8 or higher
- A running MCP server with an echo tool (default: `http://127.0.0.1:8080/call_tool`)
- An OpenRouter.io API key

## Setup

1. Install the required dependencies:

   ```bash
   pip install -r requirements.txt
   ```

2. Create a `.env` file based on the `.env.example` template:

   ```bash
   cp .env.example .env
   ```

3. Edit the `.env` file and add your OpenRouter API key:

   ```ini
   OPENROUTER_API_KEY=your_openrouter_api_key_here
   MCP_SERVER_URL=http://127.0.0.1:8080/call_tool
   ```

## Usage

Run the script with a message to echo:

```bash
python test_mcp_echo.py "Hello, MCP Server!"
```

### Command-line Options

- `message`: The message to echo (default: "Hello from OpenRouter LLM!")
- `--model`: The OpenRouter model to use (default: "anthropic/claude-3-opus:beta")
- `--verbose` or `-v`: Print verbose output including the full API response
- `--quiet` or `-q`: Minimize output, only show essential information (while still showing full LLM responses)
- `--simulate` or `-s`: Simulate tool calls without using a real MCP server
- `--mcp-url`: Specify a custom MCP server URL (default: `http://127.0.0.1:8080/call_tool`)
- `--read-file` or `-r`: Read a saved response file instead of making a new request
- `--latest-file` or `-l`: Read the most recently created response file
- `--max-lines`: Maximum number of lines to read from the saved response file
- `--http-client`: Test the HTTP client tool instead of the echo tool
- `--http-url`: URL to request when testing the HTTP client tool
- `--http-method`: HTTP method to use (GET, POST, etc.) (default: "GET")
- `--http-headers`: HTTP headers in JSON format
- `--http-body`: HTTP request body
- `--http-content-type`: Content type for HTTP request body
- `--http-timeout`: HTTP request timeout in seconds (default: 30)

Examples:

```bash
# Use a different model
python test_mcp_echo.py --model "openai/gpt-4-turbo"

# Print verbose output
python test_mcp_echo.py -v

# Use quiet mode (minimal output)
python test_mcp_echo.py -q

# Specify a custom message and model
python test_mcp_echo.py "Test message" --model "anthropic/claude-3-sonnet"

# Simulate tool calls without a real MCP server
python test_mcp_echo.py --simulate

# Use a custom MCP server URL
python test_mcp_echo.py --mcp-url "http://example.com:8080/call_tool"

# Combine multiple options
python test_mcp_echo.py "Hello world" --model "openai/gpt-4-turbo" -q --mcp-url "http://192.168.1.100:8080/call_tool"

# Test the HTTP client tool with a GET request
python test_mcp_echo.py --http-client --http-url "https://www.example.com"

# Test the HTTP client tool with a POST request and custom headers
python test_mcp_echo.py --http-client --http-url "https://api.example.com/data" --http-method "POST" --http-headers '{"Authorization": "Bearer token123", "Content-Type": "application/json"}' --http-body '{"key": "value"}'

# Read a saved response file
python test_mcp_echo.py --read-file "http_response_20250518_163800.html"

# Read the most recently created response file
python test_mcp_echo.py --latest-file

# Read only the first 100 lines of a response file
python test_mcp_echo.py --read-file "http_response_20250518_163800.html" --max-lines 100
```

## How It Works

1. The script connects to OpenRouter.io's API using your API key
2. It defines the echo tool with its parameters (using the correct `message` parameter name)
3. It instructs the LLM to use the echo tool with your message
4. The LLM attempts to call the echo tool on your MCP server
5. The script displays the results and the LLM's explanation

## Important Note About Parameters

Based on the MCP server implementation, the echo tool expects a parameter named `text`. The correct JSON-RPC format is:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "call_tool",
  "params": {
    "name": "echo",
    "arguments": {
      "text": "Hello, world!"
    }
  }
}
```

## MCP Server Endpoint

The MCP server expects **POST** requests at the `/call_tool` endpoint. The correct URL and method are:

```http
POST http://127.0.0.1:8080/call_tool
```

Important notes:

- You **must** use the POST method
- You **must** set the Content-Type header to "application/json"
- Other endpoints like `/api`, `/rpc`, or `/api/call_tool` will return 404 errors
- Using GET or other methods will not work

The script has been configured to use this correct parameter format.

## Advanced Testing

For more advanced testing with multiple tools, use the `advanced_test.py` script:

```bash
python advanced_test.py "Please use the echo tool to send the message 'Hello, world!' and explain what happened."
```

This script supports the same options as `test_mcp_echo.py` but allows you to specify multiple tools:

```bash
python advanced_test.py "Use the reverse tool to reverse this text" --tools reverse
```

Or multiple tools:

```bash
python advanced_test.py "First echo 'Hello' and then reverse it" --tools echo reverse
```

You can also use the `--quiet` option to minimize output:

```bash
python advanced_test.py "Please echo 'Hello, world!'" -q
```

## Reading Response Files

The script now supports reading saved response files. This is useful for examining large HTTP responses that were saved to files.

To read a saved response file:

```bash
python test_mcp_echo.py --read-file "http_response_20250518_163800.html"
```

You can also read the most recently created response file:

```bash
python test_mcp_echo.py --latest-file
```

To limit the number of lines displayed:

```bash
python test_mcp_echo.py --read-file "http_response_20250518_163800.html" --max-lines 100
```

## Testing the HTTP Client Tool

The script now supports testing the HTTP client tool directly, without going through the OpenRouter LLM API. This is useful for testing your MCP server's HTTP client implementation.

To test the HTTP client tool:

```bash
python test_mcp_echo.py --http-client --http-url "https://www.example.com"
```

The script will send a JSON-RPC request to your MCP server, calling the HTTP client tool with the specified URL. The response will be displayed in the console.

You can customize the HTTP request with the following options:

- `--http-method`: HTTP method to use (GET, POST, PUT, DELETE, etc.)
- `--http-headers`: HTTP headers in JSON format
- `--http-body`: Request body
- `--http-content-type`: Content type for request body
- `--http-timeout`: Request timeout in seconds

Example of a POST request with custom headers and body:

```bash
python test_mcp_echo.py --http-client --http-url "https://api.example.com/data" \
  --http-method "POST" \
  --http-headers '{"Authorization": "Bearer token123"}' \
  --http-body '{"key": "value"}' \
  --http-content-type "application/json"
```

## Troubleshooting

- **API Key Error**: Make sure your OpenRouter API key is correctly set in the `.env` file
- **Connection Error**: Ensure your MCP server is running and accessible at the URL specified in the `.env` file
- **Tool Not Found**: Verify that your MCP server has an echo tool registered
- **HTTP Client Error**: If testing the HTTP client tool fails, check that your MCP server has the HTTP client tool registered and properly configured

## Available OpenRouter Models

Some popular models you can use with the `--model` flag:

- `anthropic/claude-3-opus:beta`
- `anthropic/claude-3-sonnet`
- `openai/gpt-4-turbo`
- `openai/gpt-3.5-turbo`
- `google/gemini-pro`
- `meta-llama/llama-3-70b-instruct`

For a complete list, see the [OpenRouter documentation](https://openrouter.ai/docs).
