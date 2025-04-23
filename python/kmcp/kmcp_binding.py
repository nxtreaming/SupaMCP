"""KMCP Python bindings using ctypes."""

import os
import sys
import ctypes
import json
from typing import Optional

class KMCPBinding:
    """KMCP library binding using ctypes."""
    
    def __init__(self):
        # Add DLL directories to PATH on Windows
        lib_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'build', 'lib'))
        release_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'Release'))
        
        if sys.platform == 'win32':
            # Add both directories to DLL search path
            os.add_dll_directory(lib_dir)
            os.add_dll_directory(release_dir)
            
            # Try loading from Release directory first
            try:
                # Load dependencies first
                self.mcp_common = ctypes.WinDLL(os.path.join(release_dir, 'mcp_common.dll'))
                # Then load KMCP
                self.lib = ctypes.WinDLL(os.path.join(release_dir, 'kmcp.dll'))
            except OSError:
                try:
                    # Try lib directory as fallback
                    self.mcp_common = ctypes.WinDLL(os.path.join(lib_dir, 'mcp_common.dll'))
                    self.lib = ctypes.WinDLL(os.path.join(lib_dir, 'kmcp.dll'))
                except OSError as e:
                    raise OSError(f"Could not load KMCP libraries: {e}")
                
        elif sys.platform == 'darwin':
            self.mcp_common = ctypes.CDLL(os.path.join(lib_dir, 'libmcp_common.dylib'))
            self.lib = ctypes.CDLL(os.path.join(lib_dir, 'libkmcp.dylib'))
        else:
            self.mcp_common = ctypes.CDLL(os.path.join(lib_dir, 'libmcp_common.so'))
            self.lib = ctypes.CDLL(os.path.join(lib_dir, 'libkmcp.so'))
            
        # Set function prototypes
        self._setup_functions()
        
    def _setup_functions(self):
        """Set up C function prototypes."""
        # Version info
        self.lib.kmcp_get_version.restype = ctypes.c_char_p
        self.lib.kmcp_get_version.argtypes = []
        
        self.lib.kmcp_get_build_info.restype = ctypes.c_char_p
        self.lib.kmcp_get_build_info.argtypes = []
        
        # Client functions
        self.lib.kmcp_client_create.restype = ctypes.c_void_p
        self.lib.kmcp_client_create.argtypes = [ctypes.c_void_p]  # config
        
        self.lib.kmcp_client_create_from_file.restype = ctypes.c_void_p
        self.lib.kmcp_client_create_from_file.argtypes = [ctypes.c_char_p]  # config_file
        
        self.lib.kmcp_client_close.restype = None
        self.lib.kmcp_client_close.argtypes = [ctypes.c_void_p]
        
        self.lib.kmcp_client_call_tool.restype = ctypes.c_int
        self.lib.kmcp_client_call_tool.argtypes = [
            ctypes.c_void_p,      # client
            ctypes.c_char_p,      # tool_name
            ctypes.c_char_p,      # params_json
            ctypes.POINTER(ctypes.c_char_p)  # result_json
        ]
        
        # Profile manager functions
        self.lib.kmcp_profile_manager_create.restype = ctypes.c_void_p
        self.lib.kmcp_profile_manager_create.argtypes = []
        
        self.lib.kmcp_profile_manager_close.restype = None
        self.lib.kmcp_profile_manager_close.argtypes = [ctypes.c_void_p]
        
        # Tool access functions
        self.lib.kmcp_tool_access_create.restype = ctypes.c_void_p
        self.lib.kmcp_tool_access_create.argtypes = [ctypes.c_bool]
        
        self.lib.kmcp_tool_access_destroy.restype = None
        self.lib.kmcp_tool_access_destroy.argtypes = [ctypes.c_void_p]
        
        # Tool access control functions
        self.lib.kmcp_tool_access_check.restype = ctypes.c_bool
        self.lib.kmcp_tool_access_check.argtypes = [
            ctypes.c_void_p,  # access
            ctypes.c_char_p   # tool_name
        ]
        
        self.lib.kmcp_tool_access_add.restype = ctypes.c_int  # kmcp_error_t
        self.lib.kmcp_tool_access_add.argtypes = [
            ctypes.c_void_p,  # access
            ctypes.c_char_p,  # tool_name
            ctypes.c_bool     # allow
        ]
        
        self.lib.kmcp_tool_access_set_default_policy.restype = ctypes.c_int  # kmcp_error_t
        self.lib.kmcp_tool_access_set_default_policy.argtypes = [
            ctypes.c_void_p,  # access
            ctypes.c_bool     # default_allow
        ]
        
        # Server manager functions
        self.lib.kmcp_server_create.restype = ctypes.c_void_p
        self.lib.kmcp_server_create.argtypes = []
        
        self.lib.kmcp_server_destroy.restype = None
        self.lib.kmcp_server_destroy.argtypes = [ctypes.c_void_p]
        
        self.lib.kmcp_server_add.restype = ctypes.c_int  # kmcp_error_t
        self.lib.kmcp_server_add.argtypes = [
            ctypes.c_void_p,        # manager
            ctypes.POINTER(ctypes.c_void_p)  # config
        ]
        
        self.lib.kmcp_server_connect.restype = ctypes.c_int  # kmcp_error_t
        self.lib.kmcp_server_connect.argtypes = [ctypes.c_void_p]  # manager
        
        self.lib.kmcp_server_disconnect.restype = ctypes.c_int  # kmcp_error_t
        self.lib.kmcp_server_disconnect.argtypes = [ctypes.c_void_p]  # manager
        
        self.lib.kmcp_server_select_tool.restype = ctypes.c_int
        self.lib.kmcp_server_select_tool.argtypes = [
            ctypes.c_void_p,  # manager
            ctypes.c_char_p   # tool_name
        ]
        
    def get_version(self) -> str:
        """Get KMCP version."""
        return self.lib.kmcp_get_version().decode('utf-8')
        
    def get_build_info(self) -> str:
        """Get KMCP build info."""
        return self.lib.kmcp_get_build_info().decode('utf-8')
        
    def create_client(self, config: Optional[dict] = None) -> int:
        """Create a KMCP client."""
        if config:
            # Convert Python dict to kmcp_client_config_t
            client_config = self._create_client_config(config)
            return self.lib.kmcp_client_create(ctypes.byref(client_config))
        else:
            # Create with default config
            default_config = self._create_client_config({
                "name": "default",
                "version": "1.0.0",
                "use_manager": True,
                "timeout_ms": 30000
            })
            return self.lib.kmcp_client_create(ctypes.byref(default_config))
        
    def create_client_from_file(self, config_file: str) -> int:
        """Create a KMCP client from a configuration file."""
        return self.lib.kmcp_client_create_from_file(config_file.encode('utf-8'))
        
    def close_client(self, client: int) -> None:
        """Close a KMCP client."""
        self.lib.kmcp_client_close(client)
        
    def call_tool(self, client: int, tool_name: str, params: dict) -> str:
        """Call a tool on a client."""
        params_json = json.dumps(params).encode('utf-8')
        result_json = ctypes.c_char_p()
        
        error = self.lib.kmcp_client_call_tool(
            client,
            tool_name.encode('utf-8'),
            params_json,
            ctypes.byref(result_json)
        )
        
        if error != 0:  # KMCP_SUCCESS
            raise RuntimeError(f"Tool call failed with error {error}")
            
        result = result_json.value.decode('utf-8') if result_json.value else ""
        ctypes.pythonapi.PyMem_Free(result_json)
        return result
        
    def create_profile_manager(self) -> int:
        """Create a KMCP profile manager."""
        return self.lib.kmcp_profile_manager_create()
        
    def close_profile_manager(self, manager: int) -> None:
        """Close a KMCP profile manager."""
        self.lib.kmcp_profile_manager_close(manager)
        
    def create_tool_access(self, default_allow: bool = True) -> int:
        """Create a KMCP tool access control."""
        return self.lib.kmcp_tool_access_create(default_allow)
        
    def destroy_tool_access(self, access: int) -> None:
        """Destroy a KMCP tool access control."""
        self.lib.kmcp_tool_access_destroy(access)
        
    def check_tool_access(self, access: int, tool_name: str) -> bool:
        """Check if a tool is allowed to access."""
        return self.lib.kmcp_tool_access_check(access, tool_name.encode('utf-8'))
        
    def add_tool_access(self, access: int, tool_name: str, allow: bool) -> int:
        """Add a tool to the access control list."""
        return self.lib.kmcp_tool_access_add(access, tool_name.encode('utf-8'), allow)
        
    def set_tool_access_default_policy(self, access: int, default_allow: bool) -> int:
        """Set the default allow policy for tool access."""
        return self.lib.kmcp_tool_access_set_default_policy(access, default_allow)
        
    def create_server_manager(self) -> int:
        """Create a server manager."""
        return self.lib.kmcp_server_create()
        
    def destroy_server_manager(self, manager: int) -> None:
        """Destroy a server manager."""
        self.lib.kmcp_server_destroy(manager)
        
    def add_server(self, manager: int, config: dict) -> int:
        """Add a server configuration to the manager."""
        # Convert Python dict to kmcp_server_config_t
        server_config = self._create_server_config(config)
        config_ptr = ctypes.cast(ctypes.pointer(server_config), ctypes.POINTER(ctypes.c_void_p))
        return self.lib.kmcp_server_add(manager, config_ptr)
        
    def connect_servers(self, manager: int) -> int:
        """Connect to all servers in the manager."""
        return self.lib.kmcp_server_connect(manager)
        
    def disconnect_servers(self, manager: int) -> int:
        """Disconnect from all servers in the manager."""
        return self.lib.kmcp_server_disconnect(manager)
        
    def select_tool_server(self, manager: int, tool_name: str) -> int:
        """Select a server for a tool."""
        return self.lib.kmcp_server_select_tool(manager, tool_name.encode('utf-8'))
        
    def execute_tool(self, server: int, tool_id: str, request: dict) -> str:
        """Execute a tool on a server."""
        # Create a client for tool execution
        client = self.create_client()
        try:
            return self.call_tool(client, tool_id, request)
        finally:
            self.close_client(client)
        
    def _create_server_config(self, config: dict) -> ctypes.Structure:
        """Create a kmcp_server_config_t structure from a Python dict."""
        class ServerConfig(ctypes.Structure):
            _fields_ = [
                ("name", ctypes.c_char_p),
                ("command", ctypes.c_char_p),
                ("args", ctypes.POINTER(ctypes.c_char_p)),
                ("args_count", ctypes.c_size_t),
                ("url", ctypes.c_char_p),
                ("api_key", ctypes.c_char_p),
                ("env", ctypes.POINTER(ctypes.c_char_p)),
                ("env_count", ctypes.c_size_t),
                ("is_http", ctypes.c_bool)
            ]
            
        # Create arrays for args and env if they exist
        args = config.get("args", [])
        env = config.get("env", [])
        
        # Convert strings to bytes
        args_array = (ctypes.c_char_p * len(args))(*[arg.encode('utf-8') for arg in args])
        env_array = (ctypes.c_char_p * len(env))(*[e.encode('utf-8') for e in env])
        
        return ServerConfig(
            name=config.get("name", "").encode('utf-8'),
            command=config.get("command", "").encode('utf-8'),
            args=args_array,
            args_count=len(args),
            url=config.get("url", "").encode('utf-8'),
            api_key=config.get("api_key", "").encode('utf-8'),
            env=env_array,
            env_count=len(env),
            is_http=config.get("is_http", False)
        )
        
    def _create_client_config(self, config: dict) -> ctypes.Structure:
        """Create a kmcp_client_config_t structure from a Python dict."""
        class ClientConfig(ctypes.Structure):
            _fields_ = [
                ("name", ctypes.c_char_p),
                ("version", ctypes.c_char_p),
                ("use_manager", ctypes.c_bool),
                ("timeout_ms", ctypes.c_uint32)
            ]
            
        return ClientConfig(
            name=config.get("name", "").encode('utf-8'),
            version=config.get("version", "").encode('utf-8'),
            use_manager=config.get("use_manager", True),
            timeout_ms=config.get("timeout_ms", 30000)
        )

# Create a global instance
kmcp = KMCPBinding()
