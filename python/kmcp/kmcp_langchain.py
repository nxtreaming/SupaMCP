"""LangChain integration for KMCP."""

from typing import Any, Dict, List, Optional
from langchain.tools import BaseTool
from langchain.callbacks.manager import CallbackManagerForToolRun

class KMCPTool(BaseTool):
    """Base class for KMCP tools that integrate with LangChain."""
    
    name: str
    description: str
    
    def __init__(self, client_config: Optional[Dict[str, Any]] = None) -> None:
        """Initialize the KMCP tool.
        
        Args:
            client_config: Optional configuration for the KMCP client
        """
        super().__init__()
        self._client_config = client_config or {
            "name": "langchain_tool",
            "version": "1.0.0",
            "use_manager": True,
            "timeout_ms": 30000
        }
        
    def _run(
        self,
        tool_id: str,
        params: Dict[str, Any],
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run the tool synchronously.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of tool execution
        """
        from .kmcp_binding import kmcp
        
        try:
            # Create a client for tool execution
            client = kmcp.create_client(self._client_config)
            try:
                result = kmcp.call_tool(client, tool_id, params)
                if run_manager:
                    run_manager.on_tool_end(result)
                return result
            finally:
                kmcp.close_client(client)
                
        except Exception as e:
            if run_manager:
                run_manager.on_tool_error(str(e))
            raise
            
    async def _arun(
        self,
        tool_id: str,
        params: Dict[str, Any],
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run the tool asynchronously.
        
        Args:
            tool_id: ID of the tool to execute
            params: Tool parameters
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of tool execution
        """
        # For now, we'll just call the sync version
        # In the future, we can implement true async execution
        result = self._run(tool_id, params, run_manager, **kwargs)
        return result
