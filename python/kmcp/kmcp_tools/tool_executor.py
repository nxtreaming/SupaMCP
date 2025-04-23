"""KMCP tool executor."""

import json
from typing import Any, Dict, List, Optional
from langchain.callbacks.manager import CallbackManagerForToolRun

from ..kmcp_langchain import KMCPTool

class ToolExecutor(KMCPTool):
    """KMCP tool executor for LangChain."""
    
    name: str = "tool_executor"
    description: str = "Execute tools across KMCP servers"
    
    def _run(
        self,
        tool_id: str,
        params: Dict[str, Any],
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run a tool synchronously.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of tool execution
        """
        return super()._run(tool_id, params, run_manager, **kwargs)
        
    async def _arun(
        self,
        tool_id: str,
        params: Dict[str, Any],
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run a tool asynchronously.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of tool execution
        """
        return await super()._arun(tool_id, params, run_manager, **kwargs)
        
    def execute_tool(self, tool_id: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Execute a tool.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            
        Returns:
            Dict containing the tool result and any error information
        """
        try:
            result = self._run(tool_id, params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def list_available_tools(self) -> List[str]:
        """List available tools.
        
        Returns:
            List of available tool IDs
        """
        try:
            result = self._run("list_tools", {})
            response = json.loads(result)
            return response.get("tools", [])
        except Exception:
            return []
            
    def get_tool_info(self, tool_id: str) -> Dict[str, Any]:
        """Get information about a tool.
        
        Args:
            tool_id: ID of the tool to get info for
            
        Returns:
            Dict containing tool information and any error information
        """
        try:
            result = self._run("tool_info", {"tool_id": tool_id})
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
