#!/usr/bin/env python3
"""
HTTP Protocol Test Script for SupaMCP

This script tests the HTTP protocol implementation in SupaMCP, including:
- Tool calls
- Tool discovery
- SSE events
- CORS support
"""

import requests
import json
import time
import sys
import os
import threading
import sseclient
import argparse

# Default server URL
DEFAULT_SERVER_URL = "http://127.0.0.1:8280"

class HttpProtocolTester:
    def __init__(self, server_url=DEFAULT_SERVER_URL):
        self.server_url = server_url
        self.sse_events = []
        self.sse_thread = None
        self.sse_running = False
        
    def test_root_endpoint(self):
        """Test the root endpoint"""
        print("\n=== Testing Root Endpoint ===")
        response = requests.get(f"{self.server_url}/")
        print(f"Status Code: {response.status_code}")
        print(f"Content Type: {response.headers.get('Content-Type')}")
        if response.status_code == 200:
            print("Root endpoint test: PASSED")
            return True
        else:
            print("Root endpoint test: FAILED")
            return False
    
    def test_tool_discovery(self):
        """Test the tool discovery API"""
        print("\n=== Testing Tool Discovery API ===")
        response = requests.get(f"{self.server_url}/tools")
        print(f"Status Code: {response.status_code}")
        print(f"Content Type: {response.headers.get('Content-Type')}")
        
        if response.status_code == 200:
            try:
                tools = response.json()
                print(f"Available Tools: {json.dumps(tools, indent=2)}")
                if "tools" in tools and len(tools["tools"]) > 0:
                    print("Tool discovery test: PASSED")
                    return True
                else:
                    print("Tool discovery test: FAILED (No tools found)")
                    return False
            except json.JSONDecodeError:
                print("Tool discovery test: FAILED (Invalid JSON)")
                return False
        else:
            print("Tool discovery test: FAILED")
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
        response = requests.options(f"{self.server_url}/call_tool", headers=headers)
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
        
        response = requests.post(
            f"{self.server_url}/call_tool", 
            data=json.dumps(payload),
            headers=headers
        )
        
        print(f"Status Code: {response.status_code}")
        print(f"Content Type: {response.headers.get('Content-Type')}")
        
        if response.status_code == 200:
            try:
                result = response.json()
                print(f"Response: {json.dumps(result, indent=2)}")
                if "result" in result and result["result"] == "Hello, SupaMCP!":
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
            response = requests.get(f"{self.server_url}/events", headers=headers, stream=True)
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
        
        response = requests.post(
            f"{self.server_url}/call_tool", 
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
    
    def run_all_tests(self):
        """Run all tests and return the number of failed tests"""
        tests = [
            self.test_root_endpoint,
            self.test_tool_discovery,
            self.test_cors_support,
            self.test_echo_tool,
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
    parser.add_argument('--url', default=DEFAULT_SERVER_URL, help=f'Server URL (default: {DEFAULT_SERVER_URL})')
    args = parser.parse_args()
    
    tester = HttpProtocolTester(args.url)
    failed_tests = tester.run_all_tests()
    
    sys.exit(failed_tests)

if __name__ == "__main__":
    main()
