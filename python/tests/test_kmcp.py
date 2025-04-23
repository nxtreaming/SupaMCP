"""Test KMCP Python bindings."""

import json
import pytest
from unittest.mock import patch
from kmcp import kmcp, WebBrowserTool, ResourceTool, ToolExecutorTool

def test_version():
    """Test version info."""
    version = kmcp.get_version()
    assert version is not None
    assert isinstance(version, str)
    
    build_info = kmcp.get_build_info()
    assert build_info is not None
    assert isinstance(build_info, str)
    
def test_client():
    """Test client creation and cleanup."""
    client = kmcp.create_client()
    assert client is not None
    kmcp.close_client(client)
    
def test_profile_manager():
    """Test profile manager creation and cleanup."""
    manager = kmcp.create_profile_manager()
    assert manager is not None
    kmcp.close_profile_manager(manager)
    
def test_tool_access():
    """Test tool access control."""
    access = kmcp.create_tool_access()
    assert access is not None
    
    result = kmcp.add_tool_access(access, "test_tool", True)
    assert result == 0  # KMCP_SUCCESS
    
    allowed = kmcp.check_tool_access(access, "test_tool")
    assert allowed is True
    
    kmcp.destroy_tool_access(access)
    
def test_server_manager():
    """Test server manager creation and cleanup."""
    manager = kmcp.create_server_manager()
    assert manager is not None
    
    # Test adding a server
    config = {
        "name": "test_server",
        "url": "http://localhost:8080",
        "is_http": True
    }
    result = kmcp.add_server(manager, config)
    assert result == 0  # KMCP_SUCCESS
    
    # Test connecting to servers
    result = kmcp.connect_servers(manager)
    assert result == 0  # KMCP_SUCCESS
    
    # Test selecting a tool server
    server_index = kmcp.select_tool_server(manager, "test_tool")
    assert server_index >= -1  # -1 means no server found, which is valid
    
    kmcp.destroy_server_manager(manager)
    
@patch('kmcp.kmcp_binding.KMCPBinding.call_tool')
def test_web_browser_tool(mock_call_tool):
    """Test web browser tool."""
    # Mock successful response
    mock_call_tool.return_value = json.dumps({
        "preview_id": "test_preview",
        "status": "success"
    })
    
    tool = WebBrowserTool()
    
    # Test preview URL
    result = tool.preview_url("http://example.com", "Example")
    assert isinstance(result, dict)
    assert "error" not in result
    assert result["preview_id"] == "test_preview"
    
@patch('kmcp.kmcp_binding.KMCPBinding.call_tool')
def test_resource_tool(mock_call_tool):
    """Test resource tool."""
    # Mock successful response
    mock_call_tool.return_value = json.dumps({
        "uri": "test://example",
        "data": "test_data",
        "status": "success"
    })
    
    tool = ResourceTool()
    
    # Test get resource
    result = tool.get_resource("test://example")
    assert isinstance(result, dict)
    assert "error" not in result
    assert result["data"] == "test_data"
    
@patch('kmcp.kmcp_binding.KMCPBinding.call_tool')
def test_tool_executor(mock_call_tool):
    """Test tool executor."""
    # Mock successful response
    mock_call_tool.return_value = json.dumps({
        "tool_id": "test_tool",
        "status": "success",
        "result": "test_result"
    })
    
    tool = ToolExecutorTool()
    
    # Test execute tool
    result = tool.execute_tool("test_tool", {"param": "value"})
    assert isinstance(result, dict)
    assert "error" not in result
    assert result["result"] == "test_result"

if __name__ == "__main__":
    pytest.main([__file__])
