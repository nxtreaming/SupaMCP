"""LangChain integration for KMCP."""

from typing import Any, Dict, List, Optional
from langchain.tools import BaseTool

class KMCPTool(BaseTool):
    """Base class for KMCP tools that integrate with LangChain."""
    
    name: str = ""
    description: str = ""
    
    def __init__(self, **kwargs):
        """Initialize the tool."""
        super().__init__(**kwargs)
        
    def _run(self, *args: Any, **kwargs: Any) -> Any:
        """Use the tool synchronously."""
        raise NotImplementedError("KMCPTool must implement _run")
        
    async def _arun(self, *args: Any, **kwargs: Any) -> Any:
        """Use the tool asynchronously."""
        raise NotImplementedError("KMCPTool must implement _arun")
