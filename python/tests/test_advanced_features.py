"""Test KMCP advanced features."""

import json
import pytest
from unittest.mock import patch, MagicMock
from kmcp.kmcp_binding import kmcp

def test_event_system():
    """Test event system functionality."""
    # Test data
    test_event = "test_event"
    test_data = "test_data"
    test_user_data = {"context": "test"}
    
    # Mock callback
    callback_called = False
    def test_callback(data, user_data):
        nonlocal callback_called
        callback_called = True
        assert data == test_data
        assert user_data == test_user_data
    
    # Register handler
    result = kmcp.register_event_handler(test_event, test_callback, test_user_data)
    assert result == 0  # KMCP_SUCCESS
    
    # Trigger event
    result = kmcp.trigger_event(test_event, test_data)
    assert result == 0  # KMCP_SUCCESS
    assert callback_called

@patch('kmcp.kmcp_binding.KMCPBinding.http_request')
def test_http_client(mock_http_request):
    """Test HTTP client functionality."""
    # Mock response
    mock_response = {
        'status_code': 200,
        'headers': {'Content-Type': 'application/json'},
        'body': b'{"status": "ok"}'
    }
    mock_http_request.return_value = mock_response
    
    # Test GET request
    response = kmcp.http_request(
        method="GET",
        url="https://api.example.com/test",
        headers={"Authorization": "Bearer test"}
    )
    
    assert response == mock_response
    assert response['status_code'] == 200
    assert 'Content-Type' in response['headers']
    
    # Test POST request with body
    response = kmcp.http_request(
        method="POST",
        url="https://api.example.com/test",
        headers={"Content-Type": "application/json"},
        body=json.dumps({"test": "data"}).encode('utf-8')
    )
    
    assert response == mock_response

@patch('kmcp.kmcp_binding.KMCPBinding.create_process')
@patch('kmcp.kmcp_binding.KMCPBinding.wait_process')
def test_process_management(mock_wait_process, mock_create_process):
    """Test process management functionality."""
    # Mock process creation
    test_pid = 12345
    mock_create_process.return_value = test_pid
    
    # Mock process wait
    mock_wait_process.return_value = 0  # Success exit code
    
    # Test process creation
    pid = kmcp.create_process(
        command="python",
        args=["-c", "print('test')"],
        working_dir="/tmp",
        env={"TEST": "value"}
    )
    
    assert pid == test_pid
    
    # Test process wait
    exit_code = kmcp.wait_process(pid, timeout_ms=1000)
    assert exit_code == 0
    
    # Verify mock calls
    mock_create_process.assert_called_once()
    mock_wait_process.assert_called_once_with(test_pid, 1000)

def test_registry():
    """Test service registry functionality."""
    # Test service registration
    service_name = "test_service"
    service_endpoint = "http://localhost:8080"
    
    result = kmcp.register_service(service_name, service_endpoint)
    assert result == 0  # KMCP_SUCCESS
    
    # Test service lookup
    endpoint = kmcp.lookup_service(service_name)
    assert endpoint == service_endpoint
    
    # Test service unregistration
    result = kmcp.unregister_service(service_name)
    assert result == 0  # KMCP_SUCCESS
    
    # Verify service is gone
    endpoint = kmcp.lookup_service(service_name)
    assert endpoint is None

@patch('kmcp.kmcp_binding.KMCPBinding.get_server_status')
@patch('kmcp.kmcp_binding.KMCPBinding.get_server_info')
def test_server_connection(mock_get_info, mock_get_status):
    """Test server connection functionality."""
    # Mock server status
    mock_get_status.return_value = 1  # Connected
    
    # Mock server info
    mock_info = {
        "version": "1.0.0",
        "uptime": 3600,
        "connections": 10
    }
    mock_get_info.return_value = mock_info
    
    # Test server status
    server_handle = 12345
    status = kmcp.get_server_status(server_handle)
    assert status == 1
    
    # Test server info
    info = kmcp.get_server_info(server_handle)
    assert info == mock_info
    assert info["version"] == "1.0.0"

def test_tool_sdk():
    """Test tool SDK functionality."""
    # Test tool creation
    tool_config = {
        "name": "test_tool",
        "version": "1.0.0",
        "description": "Test tool",
        "capabilities": ["test"]
    }
    
    tool = kmcp.create_tool(tool_config)
    assert tool is not None
    
    # Clean up
    kmcp.destroy_tool(tool)

def test_error_handling():
    """Test error handling in advanced features."""
    # Test invalid event type
    with pytest.raises(RuntimeError):
        kmcp.trigger_event("invalid_event", "test")
    
    # Test invalid HTTP request
    with pytest.raises(RuntimeError):
        kmcp.http_request("INVALID", "not_a_url")
    
    # Test invalid process creation
    with pytest.raises(RuntimeError):
        kmcp.create_process("nonexistent_command")
    
    # Test invalid service lookup
    result = kmcp.lookup_service("nonexistent_service")
    assert result is None

if __name__ == "__main__":
    pytest.main([__file__])
