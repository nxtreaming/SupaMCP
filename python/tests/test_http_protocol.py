#!/usr/bin/env python3
"""
HTTP Protocol Test for SupaMCP

This script tests the HTTP protocol implementation in SupaMCP by:
1. Starting an mcp_server with HTTP transport
2. Testing HTTP endpoints (tool calls, SSE, CORS)
3. Verifying the responses

This test uses the mcp_server.exe executable directly, not http_server.exe.
"""

import os
import sys
import subprocess
import time
import json
import threading
import argparse
import requests
import sseclient
import signal
import platform
from pathlib import Path

# Default server settings
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8280

class MCPServerRunner:
    """Class to manage the MCP server process"""
    
    def __init__(self, server_path=None, host=DEFAULT_HOST, port=DEFAULT_PORT, enable_cors=True):
        self.server_path = server_path or self._find_server_executable()
        self.host = host
        self.port = port
        self.enable_cors = enable_cors
        self.process = None
        
    def _find_server_executable(self):
        """Find the mcp_server executable"""
        # Start from the script directory and look for the executable
        script_dir = Path(__file__).parent.absolute()
        repo_root = script_dir.parent.parent
        
        # Check for Windows executable
        if platform.system() == "Windows":
            paths_to_check = [
                repo_root / "bin" / "mcp_server.exe",
                repo_root / "build" / "bin" / "mcp_server.exe",
                repo_root / "build" / "Debug" / "mcp_server.exe",
                repo_root / "build" / "Release" / "mcp_server.exe"
            ]
        else:
            # Linux/macOS paths
            paths_to_check = [
                repo_root / "bin" / "mcp_server",
                repo_root / "build" / "bin" / "mcp_server",
                repo_root / "build" / "mcp_server"
            ]
            
        for path in paths_to_check:
            if path.exists():
                return str(path)
                
        raise FileNotFoundError("Could not find mcp_server executable")
    
    def start(self):
        """Start the MCP server with HTTP transport"""
        if self.process:
            print("Server is already running")
            return
            
        # Build command to start server with HTTP transport
        cmd = [
            self.server_path,
            "--http",
            "--host", self.host,
            "--port", str(self.port),
            "--log-level", "debug"
        ]
        
        print(f"Starting MCP server: {' '.join(cmd)}")
        
        try:
            # Start the server process
            self.process = subprocess.Popen(cmd)
            print(f"MCP server started with PID {self.process.pid}")
            
            # Wait a moment for the server to start
            time.sleep(2)
            
            # Check if the process is still running
            if self.process.poll() is not None:
                print(f"Server process exited with code {self.process.returncode}")
                self.process = None
                return False
                
            return True
        except Exception as e:
            print(f"Error starting MCP server: {e}")
            self.process = None
            return False
    
    def stop(self):
        """Stop the MCP server"""
        if not self.process:
            print("Server is not running")
            return
            
        print("Stopping MCP server...")
        
        try:
            # Send termination signal
            if platform.system() == "Windows":
                self.process.terminate()
            else:
                self.process.send_signal(signal.SIGTERM)
                
            # Wait for the process to exit
            try:
                self.process.wait(timeout=5)
                print("Server stopped")
            except subprocess.TimeoutExpired:
                print("Server did not exit gracefully, forcing termination")
                self.process.kill()
                
            self.process = None
        except Exception as e:
            print(f"Error stopping server: {e}")
    
    def __enter__(self):
        """Context manager entry"""
        self.start()
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.stop()


class HttpProtocolTester:
    """Class to test the HTTP protocol implementation"""
    
    def __init__(self, host=DEFAULT_HOST, port=DEFAULT_PORT):
        self.base_url = f"http://{host}:{port}"
        self.sse_events = []
        self.sse_thread = None
        self.sse_running = False
        
    def test_root_endpoint(self):
        """Test the root endpoint"""
        print("\n=== Testing Root Endpoint ===")
        try:
            response = requests.get(f"{self.base_url}/")
            print(f"Status Code: {response.status_code}")
            print(f"Content Type: {response.headers.get('Content-Type')}")
            
            if response.status_code == 200:
                print("Root endpoint test: PASSED")
                return True
            else:
                print("Root endpoint test: FAILED")
                return False
        except requests.RequestException as e:
            print(f"Request error: {e}")
            print("Root endpoint test: FAILED")
            return False
    
    def test_echo_tool(self):
        """Test the echo tool"""
        print("\n=== Testing Echo Tool ===")
        
        # Prepare the request
        payload = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "call_tool",
            "params": {
                "name": "echo",
                "arguments": {
                    "text": "Hello, SupaMCP!"
                }
            }
        }
        
        headers = {
            "Content-Type": "application/json"
        }
        
        try:
            response = requests.post(
                f"{self.base_url}/call_tool", 
                data=json.dumps(payload),
                headers=headers
            )
            
            print(f"Status Code: {response.status_code}")
            print(f"Content Type: {response.headers.get('Content-Type')}")
            
            if response.status_code == 200:
                try:
                    result = response.json()
                    print(f"Response: {json.dumps(result, indent=2)}")
                    
                    # Check if the response contains the expected result
                    if "result" in result and result.get("id") == 1:
                        print("Echo tool test: PASSED")
                        return True
                    else:
                        print("Echo tool test: FAILED (Unexpected response)")
                        return False
                except json.JSONDecodeError:
                    print("Echo tool test: FAILED (Invalid JSON)")
                    return False
            else:
                print("Echo tool test: FAILED")
                return False
        except requests.RequestException as e:
            print(f"Request error: {e}")
            print("Echo tool test: FAILED")
            return False
    
    def test_reverse_tool(self):
        """Test the reverse tool"""
        print("\n=== Testing Reverse Tool ===")
        
        # Prepare the request
        payload = {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "call_tool",
            "params": {
                "name": "reverse",
                "arguments": {
                    "text": "Hello, SupaMCP!"
                }
            }
        }
        
        headers = {
            "Content-Type": "application/json"
        }
        
        try:
            response = requests.post(
                f"{self.base_url}/call_tool", 
                data=json.dumps(payload),
                headers=headers
            )
            
            print(f"Status Code: {response.status_code}")
            print(f"Content Type: {response.headers.get('Content-Type')}")
            
            if response.status_code == 200:
                try:
                    result = response.json()
                    print(f"Response: {json.dumps(result, indent=2)}")
                    
                    # Check if the response contains the expected result
                    if "result" in result and result.get("id") == 2:
                        print("Reverse tool test: PASSED")
                        return True
                    else:
                        print("Reverse tool test: FAILED (Unexpected response)")
                        return False
                except json.JSONDecodeError:
                    print("Reverse tool test: FAILED (Invalid JSON)")
                    return False
            else:
                print("Reverse tool test: FAILED")
                return False
        except requests.RequestException as e:
            print(f"Request error: {e}")
            print("Reverse tool test: FAILED")
            return False
    
    def test_cors_support(self):
        """Test CORS support"""
        print("\n=== Testing CORS Support ===")
        
        # Test preflight request
        headers = {
            "Origin": "http://example.com",
            "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": "Content-Type"
        }
        
        try:
            response = requests.options(f"{self.base_url}/call_tool", headers=headers)
            print(f"Preflight Status Code: {response.status_code}")
            print(f"Access-Control-Allow-Origin: {response.headers.get('Access-Control-Allow-Origin')}")
            print(f"Access-Control-Allow-Methods: {response.headers.get('Access-Control-Allow-Methods')}")
            print(f"Access-Control-Allow-Headers: {response.headers.get('Access-Control-Allow-Headers')}")
            
            if (response.status_code == 200 and 
                response.headers.get('Access-Control-Allow-Origin') and
                response.headers.get('Access-Control-Allow-Methods') and
                response.headers.get('Access-Control-Allow-Headers')):
                print("CORS preflight test: PASSED")
                return True
            else:
                print("CORS preflight test: FAILED")
                return False
        except requests.RequestException as e:
            print(f"Request error: {e}")
            print("CORS preflight test: FAILED")
            return False
    
    def start_sse_listener(self):
        """Start listening for SSE events in a separate thread"""
        self.sse_running = True
        self.sse_thread = threading.Thread(target=self._sse_listener_thread)
        self.sse_thread.daemon = True
        self.sse_thread.start()
        
        # Wait a moment for the connection to establish
        time.sleep(1)
    
    def _sse_listener_thread(self):
        """SSE listener thread function"""
        try:
            headers = {'Accept': 'text/event-stream'}
            response = requests.get(f"{self.base_url}/events", headers=headers, stream=True)
            client = sseclient.SSEClient(response)
            
            for event in client.events():
                if not self.sse_running:
                    break
                
                event_data = {
                    'id': event.id,
                    'event': event.event,
                    'data': event.data
                }
                self.sse_events.append(event_data)
                print(f"Received SSE event: {event_data}")
        except Exception as e:
            print(f"SSE listener error: {e}")
    
    def stop_sse_listener(self):
        """Stop the SSE listener thread"""
        self.sse_running = False
        if self.sse_thread:
            self.sse_thread.join(timeout=2)
    
    def test_sse_events(self):
        """Test SSE events by triggering an echo tool call and checking for events"""
        print("\n=== Testing SSE Events ===")
        
        # Clear previous events
        self.sse_events = []
        
        # Start SSE listener
        self.start_sse_listener()
        
        # Trigger an echo tool call
        payload = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "call_tool",
            "params": {
                "name": "echo",
                "arguments": {
                    "text": "Testing SSE Events"
                }
            }
        }
        
        headers = {
            "Content-Type": "application/json"
        }
        
        try:
            response = requests.post(
                f"{self.base_url}/call_tool", 
                data=json.dumps(payload),
                headers=headers
            )
            
            # Wait for events
            time.sleep(2)
            
            # Stop SSE listener
            self.stop_sse_listener()
            
            # Check if we received any events
            if len(self.sse_events) > 0:
                print(f"Received {len(self.sse_events)} SSE events")
                print("SSE events test: PASSED")
                return True
            else:
                print("No SSE events received")
                print("SSE events test: FAILED")
                return False
        except requests.RequestException as e:
            print(f"Request error: {e}")
            self.stop_sse_listener()
            print("SSE events test: FAILED")
            return False
    
    def run_all_tests(self):
        """Run all tests and return the number of failed tests"""
        tests = [
            self.test_root_endpoint,
            self.test_echo_tool,
            self.test_reverse_tool,
            self.test_cors_support,
            self.test_sse_events
        ]
        
        passed = 0
        failed = 0
        
        for test in tests:
            if test():
                passed += 1
            else:
                failed += 1
        
        print(f"\n=== Test Results: {passed} passed, {failed} failed ===")
        return failed


def main():
    parser = argparse.ArgumentParser(description='Test the HTTP protocol implementation in SupaMCP')
    parser.add_argument('--host', default=DEFAULT_HOST, help=f'Server host (default: {DEFAULT_HOST})')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help=f'Server port (default: {DEFAULT_PORT})')
    parser.add_argument('--server-path', help='Path to mcp_server executable')
    parser.add_argument('--no-server', action='store_true', help='Do not start the server (assume it is already running)')
    args = parser.parse_args()
    
    # Run tests with or without starting the server
    if args.no_server:
        # Just run the tests against an existing server
        tester = HttpProtocolTester(args.host, args.port)
        failed_tests = tester.run_all_tests()
        sys.exit(failed_tests)
    else:
        # Start the server, run tests, then stop the server
        try:
            with MCPServerRunner(args.server_path, args.host, args.port) as server:
                if server.process is None:
                    print("Failed to start MCP server")
                    sys.exit(1)
                
                # Run the tests
                tester = HttpProtocolTester(args.host, args.port)
                failed_tests = tester.run_all_tests()
                sys.exit(failed_tests)
        except FileNotFoundError as e:
            print(f"Error: {e}")
            sys.exit(1)


if __name__ == "__main__":
    main()
