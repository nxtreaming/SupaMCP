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

# Create a client
client = kmcp.create_client()

# Create a profile manager
profile_manager = kmcp.create_profile_manager()

# Add a server to a profile
profile_manager.add_server("default", "http://localhost:8080")

# Activate the profile
profile_manager.activate("default")

# Create tool access manager
tool_access = kmcp.create_tool_access()

# Check tool access
if tool_access.check_access("web_browser"):
    result = kmcp.call_tool(client, "web_browser", {"url": "https://example.com"})
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

## License

Same as KMCP core library.
