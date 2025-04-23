"""KMCP web browser tool."""

import json
from typing import Any, Dict, Optional

from ..kmcp_binding import kmcp

class WebBrowserTool:
    """KMCP web browser tool."""
    
    def __init__(self):
        self.client = kmcp.create_client({
            "name": "web_browser_tool",
            "version": "1.0.0",
            "use_manager": True,
            "timeout_ms": 30000
        })
        
    def __del__(self):
        """Clean up resources."""
        if hasattr(self, 'client'):
            kmcp.close_client(self.client)
            
    def preview_url(self, url: str, name: Optional[str] = None) -> Dict[str, Any]:
        """Preview a URL in the browser.
        
        Args:
            url: URL to preview
            name: Optional name for the preview
            
        Returns:
            Dict containing the preview status and any error information
        """
        request = {
            "url": url,
            "name": name or "Browser Preview"
        }
        
        try:
            result = kmcp.call_tool(self.client, "browser_preview", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def close_preview(self, preview_id: str) -> Dict[str, Any]:
        """Close a browser preview.
        
        Args:
            preview_id: ID of the preview to close
            
        Returns:
            Dict containing the close status and any error information
        """
        request = {
            "preview_id": preview_id
        }
        
        try:
            result = kmcp.call_tool(self.client, "browser_close", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def get_preview_status(self, preview_id: str) -> Dict[str, Any]:
        """Get the status of a browser preview.
        
        Args:
            preview_id: ID of the preview to check
            
        Returns:
            Dict containing the preview status and any error information
        """
        request = {
            "preview_id": preview_id
        }
        
        try:
            result = kmcp.call_tool(self.client, "browser_status", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
            
    def get_preview_logs(self, preview_id: str) -> Dict[str, Any]:
        """Get the console logs from a browser preview.
        
        Args:
            preview_id: ID of the preview to get logs from
            
        Returns:
            Dict containing the console logs and any error information
        """
        request = {
            "preview_id": preview_id
        }
        
        try:
            result = kmcp.call_tool(self.client, "browser_logs", request)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
