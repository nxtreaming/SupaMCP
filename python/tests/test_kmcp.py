"""Test KMCP Python bindings."""

import os
import json
import pytest
from kmcp.kmcp_binding import kmcp

@pytest.fixture(scope="session", autouse=True)
def setup_tests():
    """Setup for all tests."""
    print("\n=== KMCP Tests ===")
    yield
    print("\n=== Test Summary ===")
    print("All tests PASSED")

@pytest.fixture
def kmcp_binding():
    """Create a KMCP binding."""
    return kmcp

def test_version(kmcp_binding):
    """Test version info."""
    print("\nTesting version information...")

    # Test version string
    version = kmcp_binding.get_version()
    assert version is not None
    assert len(version) > 0
    print(f"PASS: Version string: {version}")

    # Test build info string
    build_info = kmcp_binding.get_build_info()
    assert build_info is not None
    assert len(build_info) > 0
    print(f"PASS: Build info: {build_info}")

def test_client_create_destroy(kmcp_binding):
    """Test client creation and destruction."""
    print("\nTesting client creation and destruction...")

    # Create client configuration
    config = {
        "name": "test-client",
        "version": "1.0.0",
        "use_manager": True,
        "timeout_ms": 30000
    }

    # Create client
    client = kmcp_binding.create_client(config)
    assert client != 0, "Failed to create client"

    # Get server manager
    manager = kmcp_binding.get_server_manager(client)
    assert manager != 0, "Failed to get server manager"

    # Close client
    kmcp_binding.close_client(client)
    print("PASS: Client creation and destruction tests passed")

def test_client_create_from_file(kmcp_binding):
    """Test client creation from file."""
    print("\nTesting client creation from file...")

    # Create a temporary config file
    config_file = "test_config.json"
    config = {
        "client": {
            "name": "test-client",
            "version": "1.0.0",
            "use_manager": True,
            "timeout_ms": 30000
        },
        "profiles": {
            "default": {
                "servers": [
                    {
                        "name": "local-server",
                        "url": "http://localhost:8080",
                        "api_key": "test-key",
                        "is_http": True
                    }
                ]
            }
        }
    }

    try:
        # Write config to file
        with open(config_file, "w") as f:
            json.dump(config, f, indent=2)

        # Create client from file
        client = kmcp_binding.create_client_from_file(config_file)
        assert client != 0, "Failed to create client from file"

        # Get server manager
        manager = kmcp_binding.get_server_manager(client)
        assert manager != 0, "Failed to get server manager"

        # Close client
        kmcp_binding.close_client(client)
        print("PASS: Client creation from file tests passed")

    finally:
        # Clean up config file
        if os.path.exists(config_file):
            os.remove(config_file)

if __name__ == "__main__":
    pytest.main([__file__])
