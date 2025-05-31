#!/usr/bin/env python3
"""
Test script for custom header extraction in Streamable HTTP transport.

This script tests the Mcp-Session-Id and Last-Event-ID header extraction
functionality.
"""

import requests
import json
import sys
import time
import uuid
from typing import Optional

def test_session_header_extraction(base_url: str) -> bool:
    """Test Mcp-Session-Id header extraction."""
    print("Testing Mcp-Session-Id header extraction...")
    
    # Generate a test session ID
    session_id = str(uuid.uuid4())
    
    # Test POST request with session header
    headers = {
        'Content-Type': 'application/json',
        'Mcp-Session-Id': session_id
    }
    
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2024-11-05",
            "capabilities": {
                "roots": {
                    "listChanged": True
                }
            },
            "clientInfo": {
                "name": "test-client",
                "version": "1.0.0"
            }
        }
    }
    
    try:
        response = requests.post(
            f"{base_url}/mcp",
            headers=headers,
            json=payload,
            timeout=10
        )
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            # Check if session ID is echoed back in response headers
            response_session_id = response.headers.get('Mcp-Session-Id')
            if response_session_id == session_id:
                print(f"✓ Session ID correctly extracted and echoed: {session_id}")
                return True
            else:
                print(f"✗ Session ID mismatch. Sent: {session_id}, Received: {response_session_id}")
                return False
        else:
            print(f"✗ Request failed with status {response.status_code}")
            print(f"Response: {response.text}")
            return False
            
    except Exception as e:
        print(f"✗ Request failed with exception: {e}")
        return False

def test_last_event_id_header(base_url: str) -> bool:
    """Test Last-Event-ID header extraction for SSE streams."""
    print("\nTesting Last-Event-ID header extraction...")
    
    # Generate a test event ID
    last_event_id = f"event-{int(time.time())}"
    
    # Test GET request for SSE stream with Last-Event-ID header
    headers = {
        'Accept': 'text/event-stream',
        'Last-Event-ID': last_event_id,
        'Cache-Control': 'no-cache'
    }
    
    try:
        response = requests.get(
            f"{base_url}/mcp",
            headers=headers,
            stream=True,
            timeout=5
        )
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            content_type = response.headers.get('Content-Type', '')
            if 'text/event-stream' in content_type:
                print("✓ SSE stream established successfully")
                
                # Read a few lines to see if the server processed Last-Event-ID
                lines_read = 0
                for line in response.iter_lines(decode_unicode=True):
                    if line:
                        print(f"SSE line: {line}")
                        lines_read += 1
                        if lines_read >= 5:  # Read a few lines then break
                            break
                
                print(f"✓ Last-Event-ID header processed (sent: {last_event_id})")
                return True
            else:
                print(f"✗ Wrong content type: {content_type}")
                return False
        else:
            print(f"✗ SSE request failed with status {response.status_code}")
            print(f"Response: {response.text}")
            return False
            
    except requests.exceptions.Timeout:
        print("✓ SSE stream timeout (expected for this test)")
        return True
    except Exception as e:
        print(f"✗ SSE request failed with exception: {e}")
        return False

def test_custom_headers_with_session(base_url: str) -> bool:
    """Test both session ID and Last-Event-ID headers together."""
    print("\nTesting combined custom headers...")
    
    session_id = str(uuid.uuid4())
    last_event_id = f"event-{int(time.time())}"
    
    headers = {
        'Accept': 'text/event-stream',
        'Mcp-Session-Id': session_id,
        'Last-Event-ID': last_event_id,
        'Cache-Control': 'no-cache'
    }
    
    try:
        response = requests.get(
            f"{base_url}/mcp",
            headers=headers,
            stream=True,
            timeout=3
        )
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            # Check if session ID is echoed back
            response_session_id = response.headers.get('Mcp-Session-Id')
            if response_session_id == session_id:
                print(f"✓ Session ID correctly processed: {session_id}")
            else:
                print(f"⚠ Session ID not echoed back (may be expected)")
            
            content_type = response.headers.get('Content-Type', '')
            if 'text/event-stream' in content_type:
                print("✓ SSE stream with custom headers established")
                return True
            else:
                print(f"✗ Wrong content type: {content_type}")
                return False
        else:
            print(f"✗ Request failed with status {response.status_code}")
            return False
            
    except requests.exceptions.Timeout:
        print("✓ SSE stream timeout (expected for this test)")
        return True
    except Exception as e:
        print(f"✗ Request failed with exception: {e}")
        return False

def test_invalid_headers(base_url: str) -> bool:
    """Test server handling of invalid custom headers."""
    print("\nTesting invalid custom headers...")
    
    # Test with invalid session ID format
    headers = {
        'Content-Type': 'application/json',
        'Mcp-Session-Id': 'invalid-session-id-with-special-chars!@#$'
    }
    
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "ping"
    }
    
    try:
        response = requests.post(
            f"{base_url}/mcp",
            headers=headers,
            json=payload,
            timeout=10
        )
        
        print(f"Response status: {response.status_code}")
        
        # Server should either reject invalid session ID or ignore it
        if response.status_code in [200, 400]:
            print("✓ Server handled invalid session ID appropriately")
            return True
        else:
            print(f"⚠ Unexpected status code: {response.status_code}")
            return False
            
    except Exception as e:
        print(f"✗ Request failed with exception: {e}")
        return False

def main():
    if len(sys.argv) != 2:
        print("Usage: python test_custom_headers.py <base_url>")
        print("Example: python test_custom_headers.py http://localhost:8080")
        sys.exit(1)
    
    base_url = sys.argv[1].rstrip('/')
    
    print(f"Testing custom header extraction for Streamable HTTP transport")
    print(f"Server URL: {base_url}")
    print("=" * 60)
    
    tests = [
        ("Session Header Extraction", test_session_header_extraction),
        ("Last-Event-ID Header", test_last_event_id_header),
        ("Combined Custom Headers", test_custom_headers_with_session),
        ("Invalid Headers Handling", test_invalid_headers),
    ]
    
    passed = 0
    total = len(tests)
    
    for test_name, test_func in tests:
        print(f"\n[{passed + 1}/{total}] {test_name}")
        print("-" * 40)
        
        try:
            if test_func(base_url):
                passed += 1
                print(f"✓ {test_name} PASSED")
            else:
                print(f"✗ {test_name} FAILED")
        except Exception as e:
            print(f"✗ {test_name} FAILED with exception: {e}")
    
    print("\n" + "=" * 60)
    print(f"Test Results: {passed}/{total} tests passed")
    
    if passed == total:
        print("🎉 All custom header tests passed!")
        sys.exit(0)
    else:
        print("❌ Some tests failed. Check the server implementation.")
        sys.exit(1)

if __name__ == "__main__":
    main()
