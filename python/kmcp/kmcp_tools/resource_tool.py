"""KMCP resource tool."""

import json
from typing import Any, Dict, Optional

from ..kmcp_binding import kmcp

class ResourceTool:
    """KMCP resource tool."""
    
    def __init__(self):
        self.client = kmcp.create_client({
            "name": "resource_tool",
            "version": "1.0.0",
            "use_manager": True,
            "timeout_ms": 30000
        })
        
    def __del__(self):
        """Clean up resources."""
        if hasattr(self, 'client'):
            kmcp.close_client(self.client)
            
    def get_resource(self, uri: str) -> Dict[str, Any]:
        """Get a resource by its URI.
        
        Args:
            uri: URI of the resource to get
            
        Returns:
            Dict containing the resource data and any error information
        """
        request = {
            "uri": uri
        }
        
        try:
            result = kmcp.call_tool(self.client, "resource_get", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def put_resource(self, uri: str, data: Any) -> Dict[str, Any]:
        """Put a resource at the given URI.
        
        Args:
            uri: URI where to put the resource
            data: Resource data to store
            
        Returns:
            Dict containing the put status and any error information
        """
        request = {
            "uri": uri,
            "data": data
        }
        
        try:
            result = kmcp.call_tool(self.client, "resource_put", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def delete_resource(self, uri: str) -> Dict[str, Any]:
        """Delete a resource by its URI.
        
        Args:
            uri: URI of the resource to delete
            
        Returns:
            Dict containing the delete status and any error information
        """
        request = {
            "uri": uri
        }
        
        try:
            result = kmcp.call_tool(self.client, "resource_delete", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def list_resources(self, prefix: Optional[str] = None) -> Dict[str, Any]:
        """List resources under a prefix.
        
        Args:
            prefix: Optional prefix to filter resources
            
        Returns:
            Dict containing the list of resources and any error information
        """
        request = {
            "prefix": prefix or "/"
        }
        
        try:
            result = kmcp.call_tool(self.client, "resource_list", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
