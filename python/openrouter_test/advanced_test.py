#!/usr/bin/env python3
"""
Advanced MCP Tool Testing with OpenRouter.io

This script provides more advanced testing capabilities for MCP servers using OpenRouter.io's LLM API.
It can test multiple tools and handle more complex scenarios.
"""

import os
import json
import time
import argparse
import requests
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

def initialize_client():
    """Initialize the OpenAI client with the current MCP_SERVER_URL"""
    global client
    client = OpenAI(
        base_url="https://openrouter.ai/api/v1",
        api_key=OPENROUTER_API_KEY,
        default_headers={
            "HTTP-Referer": "https://github.com/yourusername/SupaMCPServer",  # Replace with your actual repo
            "X-Title": "MCP Tools Test",
            "MCP-Server-URL": MCP_SERVER_URL,  # Custom header to specify MCP server URL
            "X-MCP-Server-URL": MCP_SERVER_URL  # Alternative header format
        }
    )
    print(f"Initialized OpenAI client with MCP Server URL: {MCP_SERVER_URL}")

# Define available tools
AVAILABLE_TOOLS = {
    "echo": {
        "type": "function",
        "function": {
            "name": "echo",
            "description": "Echoes back the provided text",
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
    # Add more tools as needed
}

def test_direct_mcp_server(url, tool_name="echo", arguments=None):
    """
    Test the MCP server directly using requests, without going through OpenRouter.

    Args:
        url: The MCP server URL
        tool_name: The name of the tool to call
        arguments: The arguments to pass to the tool
    """
    if arguments is None:
        if tool_name == "echo":
            arguments = {"text": "Test message"}
        else:
            arguments = {}

    print(f"\n--- Direct MCP Server Test ---")
    print(f"Testing direct connection to MCP server at {url}")
    print(f"Tool: {tool_name}")
    print(f"Arguments: {json.dumps(arguments, indent=2)}")

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
                    return test_url  # Return the successful URL
                except json.JSONDecodeError:
                    print("Response is not valid JSON")
        except Exception as e:
            print(f"Error with URL {test_url}: {e}")

    print("All URLs failed")
    return None

def test_mcp_tools(prompt, tools=None, model="anthropic/claude-3-opus:beta", verbose=False, use_real_mcp=True, quiet=False):
    """
    Test MCP tools on the server using an LLM from OpenRouter.

    Args:
        prompt: The prompt to send to the LLM
        tools: List of tool names to make available (default: ["echo"])
        model: The model to use from OpenRouter
        verbose: Whether to print detailed information
        use_real_mcp: Whether to use a real MCP server or simulate the tool calls
        quiet: Whether to suppress most output and only show essential information

    Returns:
        The response from the LLM
    """
    # We need to access the global variables
    global MCP_SERVER_URL
    global client

    # Make sure client is initialized
    if client is None:
        initialize_client()

    # Test direct connection to MCP server first
    if use_real_mcp and tools and len(tools) > 0:
        if not quiet:
            # Test the first tool in the list
            tool_name = tools[0]
            test_args = {"text": "Test message"} if tool_name == "echo" else {}
            working_url = test_direct_mcp_server(MCP_SERVER_URL, tool_name, test_args)
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
    if tools is None:
        tools = ["echo"]

    # Validate and collect the requested tools
    tool_definitions = []
    for tool_name in tools:
        if tool_name in AVAILABLE_TOOLS:
            # Create a copy of the tool definition
            tool_def = AVAILABLE_TOOLS[tool_name].copy()

            # Add MCP server flag if using real MCP server
            if use_real_mcp:
                tool_def["mcp_server"] = True

            tool_definitions.append(tool_def)
        else:
            print(f"Warning: Tool '{tool_name}' is not defined. Skipping.")

    if not tool_definitions:
        raise ValueError("No valid tools specified")

    if not quiet:
        print(f"Testing MCP tools: {', '.join(tools)}")
        print(f"Using model: {model}")
        print(f"MCP Server URL: {MCP_SERVER_URL}")

        if use_real_mcp:
            print("Using REAL MCP server for tool calls")
        else:
            print("Using SIMULATED tool calls (no real MCP server)")

        # Print the tool definitions if verbose
        if verbose:
            print("\nTool Definitions:")
            for tool in tool_definitions:
                print(json.dumps(tool, indent=2))
    else:
        print(f"Testing with model: {model}...")

    try:
        # Call the OpenRouter API with tool definitions
        response = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": prompt}],
            tools=tool_definitions,
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
                function_name = tool_call.function.name
                args = json.loads(tool_call.function.arguments)
                args_str = ", ".join([f"{k}='{v}'" for k, v in args.items()])
                if not quiet:
                    print(f"Tool Call: {function_name}({args_str})")
                else:
                    # For echo tool, print the text being sent
                    if function_name == "echo" and "text" in args:
                        print(f"Sending: '{args['text']}'")
                    else:
                        print(f"Calling {function_name}...")

                # Handle tool calls based on the tool name
                if function_name in AVAILABLE_TOOLS:
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
                                    "name": function_name,
                                    "arguments": args
                                }
                            }

                            # Send the request to the MCP server
                            headers = {"Content-Type": "application/json"}
                            mcp_response = requests.post(MCP_SERVER_URL, json=jsonrpc_request, headers=headers, timeout=10)

                            # Process the response
                            if mcp_response.status_code == 200:
                                result = mcp_response.json()
                                if not quiet:
                                    print("\n--- MCP Server Response ---")
                                    print(json.dumps(result, indent=2))

                                # Extract the content from the result
                                if "result" in result and "content" in result["result"]:
                                    for item in result["result"]["content"]:
                                        if "text" in item:
                                            tool_result = item["text"]
                                            if not quiet:
                                                print(f"Tool Result: {tool_result}")
                                            else:
                                                print(f"Received: '{tool_result}'")
                                            tool_call_results.append({
                                                "tool_call_id": tool_call.id,
                                                "role": "tool",
                                                "name": function_name,
                                                "content": tool_result
                                            })
                            else:
                                if not quiet:
                                    print(f"Error: MCP server returned status code {mcp_response.status_code}")
                                    print(mcp_response.text)
                                else:
                                    print(f"Error: Server returned status {mcp_response.status_code}")
                        except Exception as e:
                            if not quiet:
                                print(f"Error calling MCP server: {e}")
                            else:
                                print(f"Error: {str(e)}")
                    else:
                        # Simulate tool call
                        if function_name == "echo" and "text" in args:
                            simulated_result = f"Echo: {args['text']}"
                        else:
                            simulated_result = f"Simulated result for {function_name}({args_str})"

                        if not quiet:
                            print(f"Simulated Result: {simulated_result}")
                        else:
                            if function_name == "echo" and "text" in args:
                                print(f"Received (simulated): '{args['text']}'")
                            else:
                                print(f"Received simulated response")

                        tool_call_results.append({
                            "tool_call_id": tool_call.id,
                            "role": "tool",
                            "name": function_name,
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
        if not quiet:
            print(f"Error: {e}")
        else:
            print(f"Error: {str(e)}")
        return None

def main():
    """Main function to parse arguments and run the test"""
    global MCP_SERVER_URL

    parser = argparse.ArgumentParser(description="Test MCP tools using OpenRouter LLM")
    parser.add_argument("prompt", nargs="?",
                        default="Please use the echo tool to send the message 'Hello from OpenRouter LLM!' and explain what happened.",
                        help="Prompt to send to the LLM")
    parser.add_argument("--tools", nargs="+", default=["echo"],
                        help="List of tools to make available (default: echo)")
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

    args = parser.parse_args()

    # Verbose and quiet are mutually exclusive
    if args.verbose and args.quiet:
        print("Error: --verbose and --quiet cannot be used together")
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
    test_mcp_tools(args.prompt, args.tools, args.model, args.verbose, not args.simulate, args.quiet)
    elapsed_time = time.time() - start_time

    if not args.quiet:
        print(f"\nTest completed in {elapsed_time:.2f} seconds")
    else:
        print(f"Done in {elapsed_time:.2f}s")

if __name__ == "__main__":
    main()
