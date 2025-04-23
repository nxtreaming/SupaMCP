"""KMCP web browser tool."""

import json
from typing import Any, Dict, Optional
from langchain.callbacks.manager import CallbackManagerForToolRun

from ..kmcp_langchain import KMCPTool

class WebBrowserTool(KMCPTool):
    """KMCP web browser tool for LangChain."""
    
    name: str = "web_browser"
    description: str = "Control a web browser to visit URLs and interact with web pages"
    
    def _run(
        self,
        url: str,
        action: str = "visit",
        selector: Optional[str] = None,
        value: Optional[str] = None,
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run the web browser tool synchronously.
        
        Args:
            url: URL to visit or interact with
            action: Action to perform (visit, click, input, etc)
            selector: CSS selector for element to interact with
            value: Value to input if action is input
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of browser action
        """
        params = {
            "url": url,
            "action": action,
            "selector": selector,
            "value": value
        }
        
        return super()._run("web_browser", params, run_manager, **kwargs)
        
    async def _arun(
        self,
        url: str,
        action: str = "visit",
        selector: Optional[str] = None,
        value: Optional[str] = None,
        run_manager: Optional[CallbackManagerForToolRun] = None,
        **kwargs: Any
    ) -> str:
        """Run the web browser tool asynchronously.
        
        Args:
            url: URL to visit or interact with
            action: Action to perform (visit, click, input, etc)
            selector: CSS selector for element to interact with
            value: Value to input if action is input
            run_manager: Callback manager for the tool run
            **kwargs: Additional arguments
            
        Returns:
            str: Result of browser action
        """
        params = {
            "url": url,
            "action": action,
            "selector": selector,
            "value": value
        }
        
        return await super()._arun("web_browser", params, run_manager, **kwargs)
        
    def preview_url(self, url: str, name: Optional[str] = None) -> Dict[str, Any]:
        """Preview a URL in the browser.
        
        Args:
            url: URL to preview
            name: Optional name for the preview
            
        Returns:
            Dict containing the preview status and any error information
        """
        params = {
            "url": url,
            "action": "preview",
            "name": name or "Browser Preview"
        }
        
        try:
            result = self._run("browser_preview", params)
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
        params = {
            "preview_id": preview_id
        }
        
        try:
            result = self._run("browser_close", params)
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
        params = {
            "preview_id": preview_id
        }
        
        try:
            result = self._run("browser_status", params)
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
        params = {
            "preview_id": preview_id
        }
        
        try:
            result = self._run("browser_logs", params)
            return json.loads(result)
        except Exception as e:
            return {"error": str(e)}
