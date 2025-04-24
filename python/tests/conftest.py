"""Pytest configuration for KMCP tests."""

import os
import sys
import pytest

def pytest_configure(config):
    """Configure pytest."""
    # Add project root to Python path
    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    sys.path.insert(0, project_root)
    
    # Set test environment variables
    os.environ['KMCP_TEST_MODE'] = '1'
    os.environ['KMCP_LOG_LEVEL'] = 'DEBUG'

@pytest.fixture
def mock_client():
    """Fixture for mocked KMCP client."""
    from kmcp.kmcp_binding import kmcp
    client = kmcp.create_client({
        "name": "test_client",
        "version": "1.0.0",
        "use_manager": True
    })
    yield client
    kmcp.close_client(client)

@pytest.fixture
def mock_server():
    """Fixture for mocked KMCP server."""
    from kmcp.kmcp_binding import kmcp
    server = kmcp.create_server_manager()
    yield server
    kmcp.destroy_server_manager(server)

@pytest.fixture
def mock_event_handler():
    """Fixture for mocked event handler."""
    events = []
    def handler(data, user_data):
        events.append((data, user_data))
    return handler, events
