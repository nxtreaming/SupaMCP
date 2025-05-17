#!/usr/bin/env python3
"""
Direct MCP Server Connection Test

This script tests direct connection to an MCP server without using OpenRouter.
It tries different URL formats and request methods to find a working connection.
"""

import os
import json
import argparse
import requests
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# Get MCP server URL from environment variable or use default
MCP_SERVER_URL = os.getenv("MCP_SERVER_URL", "http://127.0.0.1:8080/call_tool")

def test_mcp_server(url, tool_name="echo", message="Test message"):
    """
    Test direct connection to MCP server

    Args:
        url: Base URL of the MCP server
        tool_name: Name of the tool to call
        message: Message to send to the echo tool
    """
    print(f"Testing MCP server at {url}")
    print(f"Tool: {tool_name}")
    print(f"Message: {message}")

    # Prepare arguments based on tool name
    if tool_name == "echo":
        arguments = {"text": message}
    elif tool_name == "reverse":
        arguments = {"text": message}
    else:
        arguments = {}

    # Prepare the JSON-RPC request
    jsonrpc_request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "call_tool",
        "params": {
            "name": tool_name,
            "arguments": arguments
        }
    }

    # Only test the /call_tool endpoint
    # If the URL already ends with /call_tool, use it as is
    # Otherwise, append /call_tool to the URL
    if url.endswith("/call_tool"):
        test_url = url
    else:
        test_url = f"{url}/call_tool"

    print(f"Using endpoint: {test_url}")

    headers = {"Content-Type": "application/json"}

    # Test the single endpoint
    try:
        print(f"\n--- Testing endpoint: {test_url} ---")
        print(f"Request: {json.dumps(jsonrpc_request, indent=2)}")

        # Send the request (explicitly using POST method)
        print("Sending POST request...")
        response = requests.post(test_url, json=jsonrpc_request, headers=headers, timeout=5)

        # Print response
        print(f"Status Code: {response.status_code}")
        print(f"Headers: {dict(response.headers)}")
        print(f"Content: {response.text}")

        # Check if successful
        if response.status_code == 200 and response.text.strip():
            print(f"Success with endpoint: {test_url}")
            try:
                result = response.json()
                print(f"Parsed JSON: {json.dumps(result, indent=2)}")
                return True
            except json.JSONDecodeError:
                print("Response is not valid JSON")
        else:
            print(f"Request failed with status code: {response.status_code}")
    except Exception as e:
        print(f"Error with endpoint {test_url}: {e}")

    return False

def test_ping(url):
    """Test ping method"""
    print(f"\n--- Testing ping method ---")

    # Prepare the JSON-RPC request
    jsonrpc_request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "ping",
        "params": {}
    }

    # Only test the /call_tool endpoint
    # If the URL already ends with /call_tool, use it as is
    # Otherwise, append /call_tool to the URL
    if url.endswith("/call_tool"):
        test_url = url
    else:
        test_url = f"{url}/call_tool"

    print(f"Using endpoint: {test_url}")

    headers = {"Content-Type": "application/json"}

    # Test the single endpoint
    try:
        print(f"\n--- Testing endpoint: {test_url} ---")

        # Send the request (explicitly using POST method)
        print("Sending POST request...")
        response = requests.post(test_url, json=jsonrpc_request, headers=headers, timeout=5)

        # Print response
        print(f"Status Code: {response.status_code}")
        print(f"Content: {response.text}")

        # Check if successful
        if response.status_code == 200 and response.text.strip():
            print(f"Success with endpoint: {test_url}")
            try:
                result = response.json()
                print(f"Parsed JSON: {json.dumps(result, indent=2)}")
                return True
            except json.JSONDecodeError:
                print("Response is not valid JSON")
        else:
            print(f"Request failed with status code: {response.status_code}")
    except Exception as e:
        print(f"Error with endpoint {test_url}: {e}")

    return False

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description="Test direct connection to MCP server's /call_tool endpoint")
    parser.add_argument("--url", default=MCP_SERVER_URL,
                        help=f"Base MCP server URL (default: {MCP_SERVER_URL})")
    parser.add_argument("--tool", default="echo",
                        help="Tool to call (default: echo)")
    parser.add_argument("--message", default="Hello, MCP Server!",
                        help="Message to send (default: 'Hello, MCP Server!')")
    parser.add_argument("--ping", action="store_true",
                        help="Test ping method instead of tool call")

    args = parser.parse_args()

    print("=== MCP Server Direct Connection Test ===")
    print("This script will test the /call_tool endpoint of your MCP server")
    print(f"Base URL: {args.url}")

    if args.ping:
        success = test_ping(args.url)
    else:
        success = test_mcp_server(args.url, args.tool, args.message)

    if success:
        print("\n✅ Test completed successfully!")
    else:
        print("\n❌ Test failed!")

if __name__ == "__main__":
    main()
