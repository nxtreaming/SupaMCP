"""Advanced KMCP features example."""

import asyncio
from kmcp.kmcp_binding import kmcp

async def main():
    # Event system example
    def on_server_event(data, user_data):
        print(f"Server event received: {data}")
        print(f"User data: {user_data}")
    
    # Register event handler
    kmcp.register_event_handler(
        "server_status",
        on_server_event,
        user_data={"context": "example"}
    )
    
    # HTTP client example
    response = kmcp.http_request(
        method="GET",
        url="https://api.example.com/status",
        headers={
            "Authorization": "Bearer token123",
            "Accept": "application/json"
        }
    )
    print(f"HTTP Response: {response}")
    
    # Process management example
    process_id = kmcp.create_process(
        command="python",
        args=["-c", "print('Hello from subprocess')"],
        env={"PYTHONPATH": "/custom/path"}
    )
    
    # Wait for process with timeout
    exit_code = kmcp.wait_process(process_id, timeout_ms=5000)
    print(f"Process exit code: {exit_code}")
    
    # Registry example
    kmcp.register_service("example_service", "http://localhost:8080")
    endpoint = kmcp.lookup_service("example_service")
    print(f"Service endpoint: {endpoint}")
    
    # Server connection example
    client = kmcp.create_client({
        "name": "example_client",
        "version": "1.0.0"
    })
    
    status = kmcp.get_server_status(client)
    print(f"Server status: {status}")
    
    info = kmcp.get_server_info(client)
    print(f"Server info: {info}")
    
    # Tool creation example
    tool_config = {
        "name": "example_tool",
        "version": "1.0.0",
        "description": "An example tool",
        "capabilities": ["text_processing"],
        "parameters": {
            "input": {
                "type": "string",
                "description": "Input text to process"
            }
        }
    }
    
    tool = kmcp.create_tool(tool_config)
    print(f"Created tool instance: {tool}")
    
    # Clean up
    kmcp.destroy_tool(tool)
    kmcp.unregister_service("example_service")

if __name__ == "__main__":
    asyncio.run(main())
