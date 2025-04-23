# KMCP Python Package

Python bindings and LangChain integration for the KMCP (Kernel MCP) library.

## Features

- Python bindings for KMCP core functionality
- LangChain integration for AI tool usage
- Async support
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
import kmcp

# Create a client
client = kmcp.kmcp_client_create()

# Create a profile manager
profile_manager = kmcp.kmcp_profile_manager_create()

# Add a server to a profile
profile_manager.add_server("default", "http://localhost:8080")

# Activate the profile
profile_manager.activate("default")
```

### LangChain Integration

```python
from kmcp_langchain import KMCPAgent, WebBrowserTool
from langchain_openai import ChatOpenAI

# Create LLM
llm = ChatOpenAI(model="gpt-4")

# Create KMCP tools
tools = [
    WebBrowserTool(server_profile="default")
]

# Create agent
agent = KMCPAgent(llm=llm, tools=tools)

# Run agent
result = await agent.run("Search for the latest AI news")
print(result)
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
