#!/usr/bin/env python3
"""
Test script for MCP Streamable HTTP Transport

This script tests the Streamable HTTP transport implementation
according to MCP 2025-03-26 specification.
"""

import json
import requests
import time
import sys
from typing import Optional, Dict, Any

class MCPStreamableHTTPClient:
    """Simple client for testing MCP Streamable HTTP transport"""
    
    def __init__(self, base_url: str, mcp_endpoint: str = "/mcp"):
        self.base_url = base_url.rstrip('/')
        self.mcp_endpoint = mcp_endpoint
        self.session_id: Optional[str] = None
        self.session = requests.Session()
        
    def initialize(self) -> Dict[str, Any]:
        """Initialize MCP session"""
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-03-26",
                "capabilities": {
                    "tools": {}
                },
                "clientInfo": {
                    "name": "test-client",
                    "version": "1.0.0"
                }
            }
        }
        
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream"
        }
        
        response = self.session.post(
            f"{self.base_url}{self.mcp_endpoint}",
            json=request,
            headers=headers
        )
        
        # Check for session ID in response headers
        session_id = response.headers.get("Mcp-Session-Id")
        if session_id:
            self.session_id = session_id
            print(f"Session established: {session_id}")
        
        return response.json()
    
    def call_tool(self, name: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
        """Call a tool"""
        request = {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/call",
            "params": {
                "name": name,
                "arguments": arguments
            }
        }
        
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream"
        }
        
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        
        response = self.session.post(
            f"{self.base_url}{self.mcp_endpoint}",
            json=request,
            headers=headers
        )
        
        return response.json()
    
    def list_tools(self) -> Dict[str, Any]:
        """List available tools"""
        request = {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/list",
            "params": {}
        }
        
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json"
        }
        
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        
        response = self.session.post(
            f"{self.base_url}{self.mcp_endpoint}",
            json=request,
            headers=headers
        )
        
        return response.json()
    
    def open_sse_stream(self):
        """Open SSE stream for server notifications"""
        headers = {
            "Accept": "text/event-stream",
            "Cache-Control": "no-cache"
        }
        
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        
        response = self.session.get(
            f"{self.base_url}{self.mcp_endpoint}",
            headers=headers,
            stream=True
        )
        
        return response
    
    def terminate_session(self) -> bool:
        """Terminate the current session"""
        if not self.session_id:
            return False
        
        headers = {
            "Mcp-Session-Id": self.session_id
        }
        
        response = self.session.delete(
            f"{self.base_url}{self.mcp_endpoint}",
            headers=headers
        )
        
        if response.status_code == 204:
            print(f"Session terminated: {self.session_id}")
            self.session_id = None
            return True
        
        return False

def test_basic_functionality(base_url: str):
    """Test basic MCP functionality"""
    print("=== Testing Basic Functionality ===")
    
    client = MCPStreamableHTTPClient(base_url)
    
    # Test initialization
    print("1. Testing initialization...")
    try:
        init_response = client.initialize()
        print(f"   ✓ Initialization successful")
        print(f"   Response: {json.dumps(init_response, indent=2)}")
    except Exception as e:
        print(f"   ✗ Initialization failed: {e}")
        return False
    
    # Test tool listing
    print("\n2. Testing tool listing...")
    try:
        tools_response = client.list_tools()
        print(f"   ✓ Tool listing successful")
        print(f"   Response: {json.dumps(tools_response, indent=2)}")
    except Exception as e:
        print(f"   ✗ Tool listing failed: {e}")
    
    # Test echo tool
    print("\n3. Testing echo tool...")
    try:
        echo_response = client.call_tool("echo", {"text": "Hello, Streamable HTTP!"})
        print(f"   ✓ Echo tool successful")
        print(f"   Response: {json.dumps(echo_response, indent=2)}")
    except Exception as e:
        print(f"   ✗ Echo tool failed: {e}")
    
    # Test reverse tool
    print("\n4. Testing reverse tool...")
    try:
        reverse_response = client.call_tool("reverse", {"text": "Hello, World!"})
        print(f"   ✓ Reverse tool successful")
        print(f"   Response: {json.dumps(reverse_response, indent=2)}")
    except Exception as e:
        print(f"   ✗ Reverse tool failed: {e}")
    
    # Test session termination
    print("\n5. Testing session termination...")
    try:
        if client.terminate_session():
            print(f"   ✓ Session termination successful")
        else:
            print(f"   ✗ Session termination failed")
    except Exception as e:
        print(f"   ✗ Session termination failed: {e}")
    
    return True

def test_legacy_endpoints(base_url: str):
    """Test legacy endpoint compatibility"""
    print("\n=== Testing Legacy Endpoints ===")
    
    session = requests.Session()
    
    # Test legacy /call_tool endpoint
    print("1. Testing legacy /call_tool endpoint...")
    try:
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {
                "name": "echo",
                "arguments": {"text": "Legacy endpoint test"}
            }
        }
        
        response = session.post(
            f"{base_url}/call_tool",
            json=request,
            headers={"Content-Type": "application/json"}
        )
        
        if response.status_code == 200:
            print(f"   ✓ Legacy /call_tool successful")
            print(f"   Response: {json.dumps(response.json(), indent=2)}")
        else:
            print(f"   ✗ Legacy /call_tool failed: {response.status_code}")
    except Exception as e:
        print(f"   ✗ Legacy /call_tool failed: {e}")
    
    # Test legacy /tools endpoint
    print("\n2. Testing legacy /tools endpoint...")
    try:
        response = session.get(f"{base_url}/tools")
        
        if response.status_code == 200:
            print(f"   ✓ Legacy /tools successful")
            print(f"   Response: {response.text}")
        else:
            print(f"   ✗ Legacy /tools failed: {response.status_code}")
    except Exception as e:
        print(f"   ✗ Legacy /tools failed: {e}")

def test_cors(base_url: str):
    """Test CORS functionality"""
    print("\n=== Testing CORS ===")
    
    session = requests.Session()
    
    # Test OPTIONS request
    print("1. Testing OPTIONS request...")
    try:
        response = session.options(
            f"{base_url}/mcp",
            headers={
                "Origin": "http://localhost:3000",
                "Access-Control-Request-Method": "POST",
                "Access-Control-Request-Headers": "Content-Type, Mcp-Session-Id"
            }
        )
        
        if response.status_code == 200:
            print(f"   ✓ OPTIONS request successful")
            print(f"   CORS headers:")
            for header, value in response.headers.items():
                if header.lower().startswith('access-control'):
                    print(f"     {header}: {value}")
        else:
            print(f"   ✗ OPTIONS request failed: {response.status_code}")
    except Exception as e:
        print(f"   ✗ OPTIONS request failed: {e}")

def main():
    if len(sys.argv) > 1:
        base_url = sys.argv[1]
    else:
        base_url = "http://127.0.0.1:8080"
    
    print(f"Testing MCP Streamable HTTP Transport at: {base_url}")
    print("=" * 60)
    
    # Wait for server to be ready
    print("Waiting for server to be ready...")
    for i in range(10):
        try:
            response = requests.get(f"{base_url}/", timeout=2)
            if response.status_code == 200:
                print("Server is ready!")
                break
        except:
            pass
        time.sleep(1)
    else:
        print("Server is not responding. Please make sure the server is running.")
        return 1
    
    # Run tests
    try:
        test_basic_functionality(base_url)
        test_legacy_endpoints(base_url)
        test_cors(base_url)
        
        print("\n" + "=" * 60)
        print("Testing completed!")
        
    except KeyboardInterrupt:
        print("\nTesting interrupted by user.")
        return 1
    except Exception as e:
        print(f"\nTesting failed with error: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
