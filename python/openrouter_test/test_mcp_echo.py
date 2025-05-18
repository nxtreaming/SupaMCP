#!/usr/bin/env python3
"""
MCP Echo Tool Test using OpenRouter.io

This script tests an MCP server's echo tool using OpenRouter.io's LLM API.
It connects to a locally running MCP server and instructs the LLM to use the echo tool.
"""

import os
import json
import time
import argparse
import requests
import sys
from dotenv import load_dotenv
from openai import OpenAI

# Load environment variables from .env file
load_dotenv()

# Get API key from environment variable
OPENROUTER_API_KEY = os.getenv("OPENROUTER_API_KEY")
if not OPENROUTER_API_KEY:
    raise ValueError("OPENROUTER_API_KEY environment variable is not set. Please create a .env file based on .env.example")

# Get MCP server URL from environment variable or use default
MCP_SERVER_URL = os.getenv("MCP_SERVER_URL", "http://127.0.0.1:8080/call_tool")

# We'll initialize the OpenAI client in the test function to ensure it uses the current MCP_SERVER_URL
client = None

def test_http_client(url, method="GET", headers=None, body=None, content_type=None, timeout=30, quiet=False, check_saved_file=True):
    """
    Test the HTTP client tool on the MCP server.

    Args:
        url: The URL to request
        method: HTTP method (GET, POST, etc.)
        headers: Additional HTTP headers
        body: Request body
        content_type: Content type for request body
        timeout: Request timeout in seconds
        quiet: Whether to suppress most output
        check_saved_file: Whether to check for saved response files in case of server error

    Returns:
        The response from the MCP server, or None if an error occurred
    """
    if not quiet:
        print(f"\n--- Direct HTTP Client Tool Test ---")
        print(f"Testing HTTP client tool with URL: {url}")
    else:
        print(f"Testing HTTP client with URL: {url}")

    # Prepare the JSON-RPC request
    arguments = {
        "url": url,
        "method": method
    }

    if headers:
        arguments["headers"] = headers
    if body:
        arguments["body"] = body
    if content_type:
        arguments["content_type"] = content_type
    if timeout:
        arguments["timeout"] = timeout

    jsonrpc_request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "call_tool",
        "params": {
            "name": "http_client",
            "arguments": arguments
        }
    }

    headers = {"Content-Type": "application/json"}

    try:
        # Send the request
        if not quiet:
            print(f"Sending request to MCP server at {MCP_SERVER_URL}...")

        response = requests.post(MCP_SERVER_URL, json=jsonrpc_request, headers=headers, timeout=timeout)

        # Print response status
        if not quiet:
            print(f"Status Code: {response.status_code}")

        # If successful, process the response
        if response.status_code == 200 and response.text.strip():
            if not quiet:
                print(f"Success!")

            try:
                result = response.json()

                if not quiet:
                    print(f"\n--- MCP Server Response ---")
                    print(json.dumps(result, indent=2))

                # Check if response contains a saved file path
                if "result" in result and "content" in result["result"]:
                    saved_file_path = None

                    # First check if any content item has a saved file path
                    for item in result["result"]["content"]:
                        if isinstance(item, dict):
                            # Check metadata for saved_to_file
                            if "metadata" in item and isinstance(item["metadata"], dict) and "saved_to_file" in item["metadata"]:
                                saved_file_path = item["metadata"]["saved_to_file"]
                                print(f"\nLarge response saved to file: {saved_file_path}")
                                print(f"You can view the complete response with: python test_mcp_echo.py --read-file {saved_file_path}")

                    # Process text content
                    for item in result["result"]["content"]:
                        if isinstance(item, dict) and "text" in item:
                            http_result = item["text"]

                            # Check if text contains file path information
                            if "FULL RESPONSE" in http_result and "SAVED TO FILE" in http_result and not quiet:
                                print("\nResponse contains file path information:")
                                # Extract the first line with file information
                                for line in http_result.split("\n"):
                                    if "SAVED TO FILE" in line:
                                        print(f"  {line}")
                                        break

                            if not quiet:
                                # If response is very large, print a preview
                                if len(http_result) > 1000:
                                    print(f"HTTP Response (preview of {len(http_result)} bytes):")
                                    print(f"{http_result[:500]}...")
                                    print("...")
                                    print(f"...{http_result[-500:]}")
                                else:
                                    print(f"HTTP Response: {http_result}")
                            else:
                                if saved_file_path:
                                    print(f"Received: '{http_result[:50]}...' (full response in file)")
                                else:
                                    print(f"Received {len(http_result)} bytes")

                return result
            except json.JSONDecodeError as e:
                if not quiet:
                    print(f"Error parsing JSON response: {e}")
                    print(f"Raw response content: {repr(response.text)}")
                else:
                    print("Error: Invalid response from server")
        else:
            if not quiet:
                print(f"Error: MCP server returned status code {response.status_code}")
                print(f"Response content: {response.text}")
            else:
                print(f"Error: Server returned status {response.status_code}")
    except Exception as e:
        if not quiet:
            print(f"Error calling MCP server: {e}")
        else:
            print(f"Error: {str(e)}")

        # Check if a response file was saved despite the error
        if check_saved_file:
            import glob
            import time

            # Get current date in format YYYYMMDD
            current_date = time.strftime("%Y%m%d")

            # Look for recently created response files
            response_files = glob.glob(f"http_response_{current_date}_*.html")
            if response_files:
                # Sort by modification time (newest first)
                response_files.sort(key=lambda x: os.path.getmtime(x), reverse=True)
                newest_file = response_files[0]

                # Check if file was created in the last minute
                file_mtime = os.path.getmtime(newest_file)
                if time.time() - file_mtime < 60:  # Within the last minute
                    print(f"\nFound a recently saved response file: {newest_file}")
                    print(f"The server may have crashed after saving the response.")
                    print(f"You can view this file with: python test_mcp_echo.py --read-file {newest_file}")

                    # Return a simple result with the file path
                    return {
                        "saved_file": newest_file,
                        "error": str(e),
                        "note": "Server crashed after saving response"
                    }

    return None

def read_saved_response_file(file_path, max_lines=None):
    """
    Read a saved response file and return its contents.

    Args:
        file_path: Path to the saved response file
        max_lines: Maximum number of lines to read (None for all)

    Returns:
        The contents of the file, or an error message if the file cannot be read
    """
    try:
        if not os.path.exists(file_path):
            return f"Error: File not found: {file_path}"

        with open(file_path, 'r', encoding='utf-8') as f:
            if max_lines is None:
                return f.read()
            else:
                lines = []
                for i, line in enumerate(f):
                    if i >= max_lines:
                        lines.append("...\n[Output truncated]")
                        break
                    lines.append(line)
                return ''.join(lines)
    except Exception as e:
        return f"Error reading file: {str(e)}"

def initialize_client():
    """Initialize the OpenAI client with the current MCP_SERVER_URL"""
    global client
    client = OpenAI(
        base_url="https://openrouter.ai/api/v1",
        api_key=OPENROUTER_API_KEY,
        default_headers={
            "HTTP-Referer": "https://github.com/yourusername/SupaMCPServer",  # Replace with your actual repo
            "X-Title": "MCP Echo Tool Test",
            "MCP-Server-URL": MCP_SERVER_URL,  # Custom header to specify MCP server URL
            "X-MCP-Server-URL": MCP_SERVER_URL  # Alternative header format
        }
    )
    print(f"Initialized OpenAI client with MCP Server URL: {MCP_SERVER_URL}")

def test_direct_mcp_server(url, message="Test message"):
    """
    Test the MCP server directly using requests, without going through OpenRouter.

    Args:
        url: The MCP server URL
        message: The message to echo
    """
    print(f"\n--- Direct MCP Server Test ---")
    print(f"Testing direct connection to MCP server at {url}")

    # Prepare the JSON-RPC request
    jsonrpc_request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "call_tool",
        "params": {
            "name": "echo",
            "arguments": {
                "text": message
            }
        }
    }

    # Try different URL formats
    urls_to_try = [
        url,  # Original URL (e.g., http://127.0.0.1:8080)
        f"{url}/",  # With trailing slash
        f"{url}/call_tool",  # With /call_tool endpoint
        f"{url}/api",  # With /api endpoint
        f"{url}/rpc"  # With /rpc endpoint
    ]

    headers = {"Content-Type": "application/json"}

    # Try each URL
    for test_url in urls_to_try:
        try:
            print(f"\n--- Trying URL: {test_url} ---")

            # Send the request
            response = requests.post(test_url, json=jsonrpc_request, headers=headers, timeout=5)

            # Print response
            print(f"Status Code: {response.status_code}")
            print(f"Headers: {dict(response.headers)}")
            print(f"Content: {response.text}")

            # If successful, break
            if response.status_code == 200 and response.text.strip():
                print(f"Success with URL: {test_url}")
                try:
                    result = response.json()
                    print(f"Parsed JSON: {json.dumps(result, indent=2)}")

                    # Check if response contains a saved file path
                    if "result" in result and "content" in result["result"]:
                        for item in result["result"]["content"]:
                            if isinstance(item, dict):
                                if "metadata" in item and isinstance(item["metadata"], dict) and "saved_to_file" in item["metadata"]:
                                    file_path = item["metadata"]["saved_to_file"]
                                    print(f"\nLarge response saved to file: {file_path}")
                                    print(f"You can view the complete response in this file.")

                                # Check if text contains file path information
                                if "text" in item and isinstance(item["text"], str) and "FULL RESPONSE" in item["text"] and "SAVED TO FILE" in item["text"]:
                                    print("\nResponse contains file path information:")
                                    # Extract the first line with file information
                                    for line in item["text"].split("\n"):
                                        if "SAVED TO FILE" in line:
                                            print(f"  {line}")
                                            break

                    return test_url  # Return the successful URL
                except json.JSONDecodeError:
                    print("Response is not valid JSON")
        except Exception as e:
            print(f"Error with URL {test_url}: {e}")

    print("All URLs failed")
    return None

def test_echo_tool(message_to_echo, model="anthropic/claude-3-opus:beta", verbose=False, use_real_mcp=True, quiet=False):
    """
    Test the echo tool on the MCP server using an LLM from OpenRouter.

    Args:
        message_to_echo: The message to echo
        model: The model to use from OpenRouter
        verbose: Whether to print detailed information
        use_real_mcp: Whether to use a real MCP server or simulate the tool call
        quiet: Whether to suppress most output and only show essential information

    Returns:
        The response from the LLM
    """
    # We need to access the global variables
    global MCP_SERVER_URL
    global client

    # Make sure client is initialized
    if client is None:
        if not quiet:
            initialize_client()
        else:
            # Initialize client quietly
            client = OpenAI(
                base_url="https://openrouter.ai/api/v1",
                api_key=OPENROUTER_API_KEY,
                default_headers={
                    "HTTP-Referer": "https://github.com/nxtreaming/SupaMCPServer",
                    "X-Title": "MCP Echo Tool Test",
                    "MCP-Server-URL": MCP_SERVER_URL,
                    "X-MCP-Server-URL": MCP_SERVER_URL
                }
            )

    # Test direct connection to MCP server first
    if use_real_mcp:
        if not quiet:
            working_url = test_direct_mcp_server(MCP_SERVER_URL, message_to_echo)
            if working_url:
                MCP_SERVER_URL = working_url
                print(f"Using working URL for MCP server: {MCP_SERVER_URL}")
                # Re-initialize client with new URL
                initialize_client()
            else:
                print("WARNING: Direct connection to MCP server failed. LLM tool calls may not work.")
        else:
            # Skip direct connection test in quiet mode
            pass

    if not quiet:
        print(f"Testing MCP echo tool with message: '{message_to_echo}'")
        print(f"Using model: {model}")
        print(f"MCP Server URL: {MCP_SERVER_URL}")
    else:
        print(f"Testing with model: {model}...")

    # Define the echo tool with explicit MCP server information
    echo_tool = {
        "type": "function",
        "function": {
            "name": "echo",
            "description": "Echoes back the provided text. This tool is provided by an external MCP server.",
            "parameters": {
                "type": "object",
                "properties": {
                    "text": {
                        "type": "string",
                        "description": "The text to echo back"
                    }
                },
                "required": ["text"]
            }
        }
    }

    # Add MCP server flag if using real MCP server
    if use_real_mcp:
        echo_tool["mcp_server"] = True
        if not quiet:
            print("Using REAL MCP server for tool calls")
    else:
        if not quiet:
            print("Using SIMULATED tool calls (no real MCP server)")

    # Print the tool definition if verbose and not quiet
    if verbose and not quiet:
        print("\nTool Definition:")
        print(json.dumps(echo_tool, indent=2))

    # Create the prompt for the LLM with explicit instructions
    prompt = f"""You have access to an 'echo' tool that is provided by an external MCP server.
This tool can echo back any text you send to it through a real server connection.

IMPORTANT: This is NOT a hypothetical exercise. You must actually call the external tool with the provided message.

Please use the echo tool to send the following message: '{message_to_echo}'

After receiving the response from the external MCP server, explain what happened, including:
1. What message you sent to the echo tool
2. What response you received from the MCP server
3. Whether the tool call was successful
"""

    try:
        # Call the OpenRouter API with tool definition
        response = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": prompt}],
            tools=[echo_tool],
            tool_choice="auto"
        )

        # Process and display the response
        if verbose and not quiet:
            print("\nFull API Response:")
            print(json.dumps(response.model_dump(), indent=2))

        # Extract and display the content
        content = response.choices[0].message.content
        tool_calls = response.choices[0].message.tool_calls

        if not quiet:
            print("\n--- LLM Response ---")
        tool_call_results = []

        if tool_calls:
            for tool_call in tool_calls:
                if tool_call.function.name == "echo":
                    args = json.loads(tool_call.function.arguments)
                    text = args.get('text', '')
                    if not quiet:
                        print(f"Tool Call: echo(text='{text}')")
                    else:
                        print(f"Sending: '{text}'")

                    # Simulate or perform actual tool call
                    if use_real_mcp:
                        try:
                            # Perform actual tool call to MCP server
                            if not quiet:
                                print(f"\nCalling MCP server at {MCP_SERVER_URL}...")
                            else:
                                print("Calling MCP server...")

                            # Use requests library to call the MCP server directly

                            # Prepare the JSON-RPC request
                            jsonrpc_request = {
                                "jsonrpc": "2.0",
                                "id": 1,
                                "method": "call_tool",
                                "params": {
                                    "name": "echo",
                                    "arguments": {
                                        "text": text
                                    }
                                }
                            }

                            headers = {"Content-Type": "application/json"}

                            try:
                                # Send the request directly to the MCP server
                                response = requests.post(MCP_SERVER_URL, json=jsonrpc_request, headers=headers, timeout=5)

                                # Process the response
                                if response.status_code == 200 and response.text.strip():
                                    try:
                                        result = response.json()
                                        if not quiet:
                                            print("\n--- MCP Server Response ---")
                                            print(json.dumps(result, indent=2))

                                        # Extract the content from the result
                                        if "result" in result and "content" in result["result"]:
                                            saved_file_path = None

                                            # First check if any content item has a saved file path
                                            for item in result["result"]["content"]:
                                                if isinstance(item, dict):
                                                    # Check metadata for saved_to_file
                                                    if "metadata" in item and isinstance(item["metadata"], dict) and "saved_to_file" in item["metadata"]:
                                                        saved_file_path = item["metadata"]["saved_to_file"]
                                                        if not quiet:
                                                            print(f"\nLarge response saved to file: {saved_file_path}")
                                                            print(f"You can view the complete response in this file.")
                                                        else:
                                                            print(f"Response saved to: {saved_file_path}")

                                            # Process text content
                                            for item in result["result"]["content"]:
                                                if isinstance(item, dict) and "text" in item:
                                                    tool_result = item["text"]

                                                    # Check if text contains file path information
                                                    if "FULL RESPONSE" in tool_result and "SAVED TO FILE" in tool_result and not quiet:
                                                        print("\nResponse contains file path information:")
                                                        # Extract the first line with file information
                                                        for line in tool_result.split("\n"):
                                                            if "SAVED TO FILE" in line:
                                                                print(f"  {line}")
                                                                break

                                                    if not quiet:
                                                        # If response is very large, print a preview
                                                        if len(tool_result) > 1000:
                                                            print(f"Echo Result (preview of {len(tool_result)} bytes):")
                                                            print(f"{tool_result[:500]}...")
                                                            print("...")
                                                            print(f"...{tool_result[-500:]}")
                                                        else:
                                                            print(f"Echo Result: {tool_result}")
                                                    else:
                                                        if saved_file_path:
                                                            print(f"Received: '{tool_result[:50]}...' (full response in file)")
                                                        else:
                                                            print(f"Received: '{tool_result}'")

                                                    tool_call_results.append({
                                                        "tool_call_id": tool_call.id,
                                                        "role": "tool",
                                                        "name": "echo",
                                                        "content": tool_result
                                                    })
                                    except json.JSONDecodeError as e:
                                        if not quiet:
                                            print(f"Error parsing JSON response: {e}")
                                            print(f"Raw response content: {repr(response.text)}")
                                        else:
                                            print("Error: Invalid response from server")
                                else:
                                    if not quiet:
                                        print(f"Error: MCP server returned status code {response.status_code}")
                                        print(f"Response content: {response.text}")
                                    else:
                                        print(f"Error: Server returned status {response.status_code}")
                            except Exception as e:
                                if not quiet:
                                    print(f"Error calling MCP server: {e}")
                                else:
                                    print(f"Error: {str(e)}")
                        except Exception as e:
                            print(f"Error calling MCP server: {e}")
                    else:
                        # Simulate tool call
                        simulated_result = f"Echo: {text}"
                        if not quiet:
                            print(f"Simulated Result: {simulated_result}")
                        else:
                            print(f"Received (simulated): '{text}'")
                        tool_call_results.append({
                            "tool_call_id": tool_call.id,
                            "role": "tool",
                            "name": "echo",
                            "content": simulated_result
                        })

        if not quiet:
            print("\n--- LLM Explanation ---")
            print(content)

        # If we have tool call results, send a follow-up request to get the LLM's response to the tool results
        if tool_call_results and use_real_mcp:
            if not quiet:
                print("\n--- Sending tool results back to LLM ---")
            else:
                print("Getting LLM response...")

            # Create a new messages array with the original prompt and tool results
            messages = [
                {"role": "user", "content": prompt},
                {"role": "assistant", "content": content, "tool_calls": [tc.model_dump() for tc in tool_calls]},
            ]

            # Add tool results
            for result in tool_call_results:
                messages.append(result)

            try:
                # Send the follow-up request
                follow_up_response = client.chat.completions.create(
                    model=model,
                    messages=messages
                )

                # Display the follow-up response
                follow_up_content = follow_up_response.choices[0].message.content
                if not quiet:
                    print("\n--- LLM Response to Tool Results ---")
                    print(follow_up_content)
                else:
                    print("\nLLM Response:")
                    # Print the full response, even in quiet mode
                    print(follow_up_content)

                if verbose and not quiet:
                    print("\nFull Follow-up Response:")
                    print(json.dumps(follow_up_response.model_dump(), indent=2))

                # Return the follow-up response
                return follow_up_response
            except Exception as e:
                if not quiet:
                    print(f"Error getting follow-up response: {e}")
                else:
                    print(f"Error: {str(e)}")

        return response

    except Exception as e:
        print(f"Error: {e}")
        return None

def main():
    """Main function to parse arguments and run the test"""
    global MCP_SERVER_URL

    parser = argparse.ArgumentParser(description="Test MCP echo tool using OpenRouter LLM")
    parser.add_argument("message", nargs="?", default="Hello from OpenRouter LLM!",
                        help="Message to echo (default: 'Hello from OpenRouter LLM!')")
    parser.add_argument("--model", default="anthropic/claude-3-opus:beta",
                        help="OpenRouter model to use (default: anthropic/claude-3-opus:beta)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print verbose output including full API response")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Minimize output, only show essential information")
    parser.add_argument("--simulate", "-s", action="store_true",
                        help="Simulate tool calls without using a real MCP server")
    parser.add_argument("--mcp-url",
                        help=f"MCP server URL (default: {MCP_SERVER_URL})")
    parser.add_argument("--read-file", "-r",
                        help="Read a saved response file instead of making a new request")
    parser.add_argument("--latest-file", "-l", action="store_true",
                        help="Read the most recently created response file")
    parser.add_argument("--max-lines", type=int, default=None,
                        help="Maximum number of lines to read from the saved response file")
    parser.add_argument("--http-client", action="store_true",
                        help="Test the HTTP client tool instead of the echo tool")
    parser.add_argument("--http-url",
                        help="URL to request when testing the HTTP client tool")
    parser.add_argument("--http-method", default="GET",
                        help="HTTP method to use (GET, POST, etc.)")
    parser.add_argument("--http-headers",
                        help="HTTP headers in JSON format")
    parser.add_argument("--http-body",
                        help="HTTP request body")
    parser.add_argument("--http-content-type",
                        help="Content type for HTTP request body")
    parser.add_argument("--http-timeout", type=int, default=30,
                        help="HTTP request timeout in seconds")

    args = parser.parse_args()

    # Verbose and quiet are mutually exclusive
    if args.verbose and args.quiet:
        print("Error: --verbose and --quiet cannot be used together")
        return

    # Check if we should read a saved response file
    if args.read_file or args.latest_file:
        file_path = args.read_file

        # If --latest-file is specified, find the most recent response file
        if args.latest_file:
            import glob
            import time

            # Look for all response files
            response_files = glob.glob("http_response_*.html")
            if not response_files:
                print("Error: No response files found")
                return

            # Sort by modification time (newest first)
            response_files.sort(key=lambda x: os.path.getmtime(x), reverse=True)
            file_path = response_files[0]
            print(f"Found latest response file: {file_path}")

        print(f"Reading saved response file: {file_path}")
        file_content = read_saved_response_file(file_path, args.max_lines)
        print("\n--- File Content ---")
        print(file_content)
        return

    # Check if we should test the HTTP client tool
    if args.http_client:
        if not args.http_url:
            print("Error: --http-url is required when using --http-client")
            return

        # Parse HTTP headers if provided
        headers = None
        if args.http_headers:
            try:
                headers = json.loads(args.http_headers)
            except json.JSONDecodeError:
                print("Error: --http-headers must be valid JSON")
                return

        # Run the HTTP client test
        import time
        start_time = time.time()
        result = test_http_client(
            url=args.http_url,
            method=args.http_method,
            headers=headers,
            body=args.http_body,
            content_type=args.http_content_type,
            timeout=args.http_timeout,
            quiet=args.quiet
        )
        elapsed_time = time.time() - start_time

        # Check if we got a result with a saved file path
        if result and isinstance(result, dict) and "saved_file" in result:
            saved_file = result["saved_file"]
            print(f"\nAutomatically displaying content of saved file: {saved_file}")

            # Ask user if they want to view the file
            view_file = input("Do you want to view the file content? (y/n): ").strip().lower()
            if view_file == 'y' or view_file == 'yes':
                # Determine how many lines to show
                max_lines = args.max_lines
                if max_lines is None:
                    try:
                        max_lines_input = input("How many lines to display? (Enter for all): ").strip()
                        if max_lines_input:
                            max_lines = int(max_lines_input)
                    except ValueError:
                        print("Invalid input, showing all lines")

                # Read and display the file
                file_content = read_saved_response_file(saved_file, max_lines)
                print("\n--- File Content ---")
                print(file_content)

        if not args.quiet:
            print(f"\nHTTP client test completed in {elapsed_time:.2f} seconds")
        else:
            print(f"Done in {elapsed_time:.2f}s")
        return

    # Override MCP server URL if specified
    if args.mcp_url:
        MCP_SERVER_URL = args.mcp_url
        if not args.quiet:
            print(f"Using custom MCP server URL: {MCP_SERVER_URL}")
        # Re-initialize client with new URL
        if not args.quiet:
            initialize_client()

    # Run the test
    start_time = time.time()
    response = test_echo_tool(args.message, args.model, args.verbose, not args.simulate, args.quiet)
    elapsed_time = time.time() - start_time

    # Check if the response contains information about a saved file
    if response and hasattr(response, 'choices') and response.choices:
        content = response.choices[0].message.content
        if "saved to file" in content.lower():
            print("\nThe response mentions a saved file. You can view it using:")
            # Try to extract the file path
            import re
            file_match = re.search(r'saved to file:?\s*([^\s\n]+)', content, re.IGNORECASE)
            if file_match:
                file_path = file_match.group(1)
                print(f"  python test_mcp_echo.py --read-file {file_path}")

    if not args.quiet:
        print(f"\nTest completed in {elapsed_time:.2f} seconds")
    else:
        print(f"Done in {elapsed_time:.2f}s")

if __name__ == "__main__":
    main()
