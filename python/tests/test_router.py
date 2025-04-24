"""Test KMCP router functionality."""

import pytest
from unittest.mock import patch, MagicMock
from kmcp.kmcp_binding import kmcp

@pytest.fixture
def mock_router():
    """Fixture for router testing."""
    router = kmcp.create_server_manager()
    
    # Add test servers
    servers = [
        {
            "name": "server1",
            "url": "http://localhost:8080",
            "capabilities": ["text", "image"],
            "is_http": True
        },
        {
            "name": "server2",
            "url": "http://localhost:8081",
            "capabilities": ["code", "text"],
            "is_http": True
        }
    ]
    
    for server in servers:
        kmcp.add_server(router, server)
    
    yield router
    kmcp.destroy_server_manager(router)

def test_capability_routing(mock_router):
    """Test routing based on server capabilities."""
    # Test text capability (should find server1 or server2)
    server = kmcp.select_tool_server(mock_router, "text_tool")
    assert server is not None
    
    # Test image capability (should find server1)
    server = kmcp.select_tool_server(mock_router, "image_tool")
    assert server is not None
    
    # Test code capability (should find server2)
    server = kmcp.select_tool_server(mock_router, "code_tool")
    assert server is not None
    
    # Test nonexistent capability (should return None)
    server = kmcp.select_tool_server(mock_router, "nonexistent_tool")
    assert server is None

@patch('kmcp.kmcp_binding.KMCPBinding.execute_tool')
def test_shared_session(mock_execute_tool, mock_router):
    """Test shared session management."""
    # Mock successful tool execution
    mock_execute_tool.return_value = {"status": "success"}
    
    # Execute tools in the same session
    session_id = "test_session"
    
    result1 = kmcp.execute_tool(mock_router, "text_tool", {
        "session_id": session_id,
        "input": "test"
    })
    assert result1["status"] == "success"
    
    result2 = kmcp.execute_tool(mock_router, "code_tool", {
        "session_id": session_id,
        "input": "test"
    })
    assert result2["status"] == "success"
    
    # Verify session was shared
    mock_execute_tool.assert_called_with(mock_router, "code_tool", {
        "session_id": session_id,
        "input": "test"
    })

def test_server_aggregation(mock_router):
    """Test server aggregation functionality."""
    # Get aggregated server info
    info = kmcp.get_server_info(mock_router)
    
    assert isinstance(info, dict)
    assert "servers" in info
    assert len(info["servers"]) == 2
    
    # Verify capabilities are merged
    capabilities = set()
    for server in info["servers"]:
        capabilities.update(server.get("capabilities", []))
    
    assert "text" in capabilities
    assert "image" in capabilities
    assert "code" in capabilities

def test_profile_based_routing(mock_router):
    """Test profile-based routing."""
    # Create profile manager
    profile_manager = kmcp.create_profile_manager()
    
    try:
        # Set up profiles
        kmcp.add_server(profile_manager, {
            "name": "prod",
            "url": "http://prod:8080",
            "capabilities": ["text", "image"]
        })
        
        kmcp.add_server(profile_manager, {
            "name": "dev",
            "url": "http://dev:8080",
            "capabilities": ["text", "debug"]
        })
        
        # Test prod profile
        kmcp.activate_profile(profile_manager, "prod")
        server = kmcp.select_tool_server(mock_router, "image_tool")
        assert server is not None
        
        # Test dev profile
        kmcp.activate_profile(profile_manager, "dev")
        server = kmcp.select_tool_server(mock_router, "debug_tool")
        assert server is not None
        
    finally:
        kmcp.close_profile_manager(profile_manager)

def test_error_handling(mock_router):
    """Test router error handling."""
    # Test invalid server addition
    with pytest.raises(RuntimeError):
        kmcp.add_server(mock_router, {
            "name": "invalid",
            # Missing required fields
        })
    
    # Test invalid tool execution
    with pytest.raises(RuntimeError):
        kmcp.execute_tool(mock_router, "nonexistent_tool", {})
    
    # Test invalid capability query
    server = kmcp.select_tool_server(mock_router, "nonexistent_capability")
    assert server is None

if __name__ == "__main__":
    pytest.main([__file__])
