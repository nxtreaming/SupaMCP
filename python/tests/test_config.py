"""Test KMCP configuration management."""

import os
import json
import pytest
from unittest.mock import patch, mock_open
from kmcp.kmcp_binding import kmcp

@pytest.fixture
def mock_config():
    """Fixture for configuration testing."""
    return {
        "version": "1.0.0",
        "debug_mode": True,
        "profiles": {
            "default": {
                "servers": [
                    {
                        "name": "local",
                        "url": "http://localhost:8080",
                        "capabilities": ["text"]
                    }
                ]
            },
            "prod": {
                "servers": [
                    {
                        "name": "prod1",
                        "url": "https://prod1:8080",
                        "capabilities": ["text", "image"]
                    },
                    {
                        "name": "prod2",
                        "url": "https://prod2:8080",
                        "capabilities": ["code"]
                    }
                ]
            }
        },
        "aliases": {
            "local": "default.servers.0",
            "primary": "prod.servers.0",
            "secondary": "prod.servers.1"
        }
    }

def test_config_loading(mock_config):
    """Test configuration loading."""
    # Mock config file
    mock_file = mock_open(read_data=json.dumps(mock_config))
    
    with patch('builtins.open', mock_file):
        client = kmcp.create_client_from_file("config.json")
        assert client is not None
        
        # Verify debug mode
        assert kmcp.get_debug_mode(client) is True
        
        # Clean up
        kmcp.close_client(client)

def test_profile_management(mock_config):
    """Test profile management."""
    profile_manager = kmcp.create_profile_manager()
    
    try:
        # Load profiles from config
        for profile_name, profile_data in mock_config["profiles"].items():
            for server in profile_data["servers"]:
                kmcp.add_server(profile_manager, server, profile=profile_name)
        
        # Test profile activation
        kmcp.activate_profile(profile_manager, "prod")
        active = kmcp.get_active_profile(profile_manager)
        assert active == "prod"
        
        # Test server enumeration
        servers = kmcp.list_servers(profile_manager, "prod")
        assert len(servers) == 2
        assert servers[0]["name"] == "prod1"
        assert servers[1]["name"] == "prod2"
        
    finally:
        kmcp.close_profile_manager(profile_manager)

def test_alias_resolution(mock_config):
    """Test alias resolution."""
    # Create client with aliases
    client = kmcp.create_client({
        "aliases": mock_config["aliases"]
    })
    
    try:
        # Test alias resolution
        local_server = kmcp.resolve_alias(client, "local")
        assert local_server["name"] == "local"
        assert local_server["url"] == "http://localhost:8080"
        
        primary_server = kmcp.resolve_alias(client, "primary")
        assert primary_server["name"] == "prod1"
        assert primary_server["url"] == "https://prod1:8080"
        
        # Test invalid alias
        with pytest.raises(RuntimeError):
            kmcp.resolve_alias(client, "nonexistent")
            
    finally:
        kmcp.close_client(client)

def test_debug_mode():
    """Test debug mode functionality."""
    # Create client with debug mode
    client = kmcp.create_client({
        "debug_mode": True
    })
    
    try:
        # Verify debug mode is enabled
        assert kmcp.get_debug_mode(client) is True
        
        # Test debug logging
        with patch('kmcp.kmcp_binding.KMCPBinding.log_debug') as mock_log:
            kmcp.log_debug(client, "Test debug message")
            mock_log.assert_called_once_with(client, "Test debug message")
            
    finally:
        kmcp.close_client(client)

def test_multi_server_config():
    """Test multi-server configuration."""
    # Create client with multiple servers
    client = kmcp.create_client({
        "servers": [
            {
                "name": "server1",
                "url": "http://server1:8080",
                "weight": 10
            },
            {
                "name": "server2",
                "url": "http://server2:8080",
                "weight": 20
            }
        ]
    })
    
    try:
        # Test server selection based on weight
        selected = kmcp.select_server(client)
        assert selected is not None
        assert selected["name"] in ["server1", "server2"]
        
        # Test server health check
        health = kmcp.check_server_health(client, selected["name"])
        assert isinstance(health, dict)
        assert "status" in health
        
    finally:
        kmcp.close_client(client)

def test_config_validation():
    """Test configuration validation."""
    # Test invalid version
    with pytest.raises(RuntimeError):
        kmcp.create_client({
            "version": "invalid"
        })
    
    # Test invalid profile
    with pytest.raises(RuntimeError):
        kmcp.create_client({
            "profiles": {
                "default": "invalid"  # Should be dict
            }
        })
    
    # Test invalid alias
    with pytest.raises(RuntimeError):
        kmcp.create_client({
            "aliases": {
                "invalid": "nonexistent.path"
            }
        })

if __name__ == "__main__":
    pytest.main([__file__])
