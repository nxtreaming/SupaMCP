"""KMCP tool executor."""

import json
from typing import Any, Dict

from ..kmcp_binding import kmcp

class ToolExecutor:
    """KMCP tool executor."""
    
    def __init__(self):
        self.client = kmcp.create_client({
            "name": "tool_executor",
            "version": "1.0.0",
            "use_manager": True,
            "timeout_ms": 30000
        })
        
    def __del__(self):
        """Clean up resources."""
        if hasattr(self, 'client'):
            kmcp.close_client(self.client)
            
    def execute_tool(self, tool_id: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Execute a tool with the given parameters.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            
        Returns:
            Dict containing the tool execution result and any error information
        """
        try:
            result = kmcp.call_tool(self.client, tool_id, params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def execute_tool_async(self, tool_id: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Execute a tool asynchronously with the given parameters.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            
        Returns:
            Dict containing the tool execution ID and any error information
        """
        params["async"] = True
        
        try:
            result = kmcp.call_tool(self.client, tool_id, params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def get_tool_status(self, tool_id: str, execution_id: str) -> Dict[str, Any]:
        """Get the status of an asynchronous tool execution.
        
        Args:
            tool_id: ID of the tool that was executed
            execution_id: ID of the tool execution to check
            
        Returns:
            Dict containing the tool execution status and any error information
        """
        request = {
            "tool_id": tool_id,
            "execution_id": execution_id
        }
        
        try:
            result = kmcp.call_tool(self.client, "tool_status", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def cancel_tool_execution(self, tool_id: str, execution_id: str) -> Dict[str, Any]:
        """Cancel an asynchronous tool execution.
        
        Args:
            tool_id: ID of the tool that was executed
            execution_id: ID of the tool execution to cancel
            
        Returns:
            Dict containing the cancellation status and any error information
        """
        request = {
            "tool_id": tool_id,
            "execution_id": execution_id
        }
        
        try:
            result = kmcp.call_tool(self.client, "tool_cancel", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
