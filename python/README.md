# KMCP Python Package

Python bindings and LangChain integration for the KMCP (Kernel MCP) library.

## Features

- Python bindings for KMCP core functionality
- LangChain integration for AI tool usage
- Async support with Python's asyncio
- Profile-based server management
- Tool access control
- Multi-server support
- Configuration management
- Event system for real-time notifications
- HTTP client for web requests
- Process management capabilities
- Service registry for discovery
- Easy-to-use high-level APIs

## Installation

```bash
# Install from source
git clone https://github.com/your-org/SupaMCPServer.git
cd SupaMCPServer/python
pip install -e .
```

## Usage

### Basic KMCP Usage

```python
from kmcp.kmcp_binding import kmcp

# Create a client with configuration
client = kmcp.create_client({
    "name": "my-client",
    "version": "1.0.0",
    "use_manager": True,
    "timeout_ms": 30000
})

# Call a tool
response = kmcp.call_tool(client, "echo", {
    "text": "Hello, KMCP!"
})
print(response)  # {"text": "Hello, KMCP!"}

# Close client when done
kmcp.close_client(client)
```

### Advanced Server Management

```python
# Create a server manager
manager = kmcp.create_server_manager()

# Add a server
kmcp.add_server(manager, {
    "name": "test-server",
    "url": "http://localhost:8080",
    "is_http": True,
    "api_key": "your-api-key"
})

# Connect to servers
kmcp.connect_servers(manager)

# Select a server for a tool
server_index = kmcp.select_tool_server(manager, "echo")
if server_index >= 0:
    # Get server connection
    connection = kmcp.get_server_connection(manager, server_index)
    
    # Call tool using the connection
    response = kmcp.call_tool(connection, "echo", {
        "text": "Hello from specific server!"
    })
    print(response)

# Clean up
kmcp.disconnect_servers(manager)
kmcp.destroy_server_manager(manager)
```

### Advanced Features

#### Event System
```python
def on_server_event(data, user_data):
    print(f"Server event: {data}")

# Register event handler
kmcp.register_event_handler("server_status", on_server_event)

# Trigger event
kmcp.trigger_event("server_status", "Server started")
```

#### HTTP Client
```python
# Make HTTP request
response = kmcp.http_request(
    method="GET",
    url="https://api.example.com/status",
    headers={"Authorization": "Bearer token123"}
)
print(f"Status code: {response['status_code']}")
```

#### Process Management
```python
# Create and manage processes
pid = kmcp.create_process(
    command="python",
    args=["-m", "http.server", "8000"],
    env={"PORT": "8000"}
)

# Wait for process
exit_code = kmcp.wait_process(pid, timeout_ms=5000)
```

#### Service Registry
```python
# Register service
kmcp.register_service("my_service", "http://localhost:8080")

# Look up service
endpoint = kmcp.lookup_service("my_service")
```

### LangChain Integration

```python
from kmcp.kmcp_langchain import KMCPAgent, WebBrowserTool
from langchain_openai import ChatOpenAI

# Create LLM
llm = ChatOpenAI(model="gpt-4")

# Create KMCP tools with specific server profile
tools = [
    WebBrowserTool(
        server_profile="default",
        client_config={
            "name": "web_browser_tool",
            "version": "1.0.0",
            "use_manager": True,
            "timeout_ms": 30000
        }
    )
]

# Create agent
agent = KMCPAgent(llm=llm, tools=tools)

# Run agent (supports both sync and async)
result = await agent.run("Search for the latest AI news")
print(result)
```

### Advanced Configuration

```python
from kmcp.kmcp_binding import kmcp

# Create client with custom configuration
client_config = {
    "name": "custom_client",
    "version": "1.0.0",
    "use_manager": True,
    "timeout_ms": 60000
}
client = kmcp.create_client(client_config)

# Set up multi-server profile
profile_manager = kmcp.create_profile_manager()
profile_manager.add_server("prod", "https://prod-server:8080")
profile_manager.add_server("dev", "http://localhost:8080")

# Use server capabilities
profile_manager.set_server_capability("prod", "high_performance", True)
profile_manager.set_server_capability("dev", "debug_mode", True)

# Activate profile based on needs
profile_manager.activate("dev" if debug_mode else "prod")
```

## API Reference

### Client Functions

- `create_client(config: dict = None) -> int`: Create a new KMCP client
- `create_client_from_file(config_file: str) -> int`: Create a client from config file
- `close_client(client: int)`: Close a client
- `call_tool(client: int, tool_name: str, request: dict) -> dict`: Call a tool

### Server Management Functions

- `create_server_manager() -> int`: Create a server manager
- `destroy_server_manager(manager: int)`: Destroy a server manager
- `add_server(manager: int, config: dict) -> int`: Add a server to the manager
- `connect_servers(manager: int) -> int`: Connect to all servers
- `disconnect_servers(manager: int) -> int`: Disconnect from all servers
- `select_tool_server(manager: int, tool_name: str) -> int`: Select server for a tool
- `get_server_connection(manager: int, index: int) -> int`: Get server connection
- `get_server_count(manager: int) -> int`: Get number of servers

## Development

### Building the Extension

```bash
# Install development dependencies
pip install -r requirements-dev.txt

# Build the extension
python setup.py build_ext --inplace
```

### Running Tests

```bash
pytest tests/
```

## Examples

Check out the `examples` directory for more detailed examples:

- `basic_usage.py`: Basic KMCP client usage
- `langchain_integration.py`: LangChain integration examples
- `advanced_features.py`: Advanced features demonstration

## License

Same as KMCP core library.
