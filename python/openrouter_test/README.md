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

## Troubleshooting

- **API Key Error**: Make sure your OpenRouter API key is correctly set in the `.env` file
- **Connection Error**: Ensure your MCP server is running and accessible at the URL specified in the `.env` file
- **Tool Not Found**: Verify that your MCP server has an echo tool registered

## Available OpenRouter Models

Some popular models you can use with the `--model` flag:

- `anthropic/claude-3-opus:beta`
- `anthropic/claude-3-sonnet`
- `openai/gpt-4-turbo`
- `openai/gpt-3.5-turbo`
- `google/gemini-pro`
- `meta-llama/llama-3-70b-instruct`

For a complete list, see the [OpenRouter documentation](https://openrouter.ai/docs).
