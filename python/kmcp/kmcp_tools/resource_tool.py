"""KMCP resource tool."""

import json
from typing import Any, Dict, Optional
from langchain.callbacks.manager import CallbackManagerForToolRun

from ..kmcp_langchain import KMCPTool

class ResourceTool(KMCPTool):
    """KMCP resource tool for LangChain."""
    
    name: str = "resource_tool"
    description: str = "Manage resources across KMCP servers"
    
    def _run(
        self,
        uri: str,
        action: str = "get",
        data: Optional[Any] = None,
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run the resource tool synchronously.
        
        Args:
            uri: Resource URI to operate on
            action: Action to perform (get, put, delete, list)
            data: Data to put if action is put
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of resource operation
        """
        params = {
            "uri": uri,
            "action": action,
            "data": data
        }
        
        return super()._run("resource_tool", params, run_manager, **kwargs)
        
    async def _arun(
        self,
        uri: str,
        action: str = "get",
        data: Optional[Any] = None,
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run the resource tool asynchronously.
        
        Args:
            uri: Resource URI to operate on
            action: Action to perform (get, put, delete, list)
            data: Data to put if action is put
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of resource operation
        """
        params = {
            "uri": uri,
            "action": action,
            "data": data
        }
        
        return await super()._arun("resource_tool", params, run_manager, **kwargs)
        
    def get_resource(self, uri: str) -> Dict[str, Any]:
        """Get a resource.
        
        Args:
            uri: Resource URI to get
            
        Returns:
            Dict containing the resource data and any error information
        """
        params = {
            "uri": uri,
            "action": "get"
        }
        
        try:
            result = self._run("resource_get", params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def put_resource(self, uri: str, data: Any) -> Dict[str, Any]:
        """Put a resource.
        
        Args:
            uri: Resource URI to put
            data: Data to put
            
        Returns:
            Dict containing the put status and any error information
        """
        params = {
            "uri": uri,
            "action": "put",
            "data": data
        }
        
        try:
            result = self._run("resource_put", params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def delete_resource(self, uri: str) -> Dict[str, Any]:
        """Delete a resource.
        
        Args:
            uri: Resource URI to delete
            
        Returns:
            Dict containing the delete status and any error information
        """
        params = {
            "uri": uri,
            "action": "delete"
        }
        
        try:
            result = self._run("resource_delete", params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def list_resources(self, uri: str) -> Dict[str, Any]:
        """List resources.
        
        Args:
            uri: Resource URI to list
            
        Returns:
            Dict containing the list of resources and any error information
        """
        params = {
            "uri": uri,
            "action": "list"
        }
        
        try:
            result = self._run("resource_list", params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
