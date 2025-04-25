"""KMCP Python bindings using ctypes."""

import os
import sys
import ctypes
import json
from typing import Optional

# 根据平台选择正确的libc库
if sys.platform == 'win32':
    libc = ctypes.cdll.msvcrt
elif sys.platform == 'darwin':
    libc = ctypes.CDLL('libc.dylib')
else:  # Linux and other Unix-like systems
    libc = ctypes.CDLL('libc.so.6')

# Explicitly specify libc.free parameter and return types
libc.free.argtypes = [ctypes.c_void_p]
libc.free.restype = None

# Structure definitions for future use
class KMCPEventHandler(ctypes.Structure):
    """KMCP event handler structure."""
    _fields_ = [
        ("on_event", ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)),
        ("user_data", ctypes.c_void_p)
    ]

class KMCPHttpRequest(ctypes.Structure):
    """HTTP request structure."""
    _fields_ = [
        ("method", ctypes.c_char_p),
        ("url", ctypes.c_char_p),
        ("headers", ctypes.POINTER(ctypes.c_char_p)),
        ("headers_count", ctypes.c_size_t),
        ("body", ctypes.c_char_p),
        ("body_length", ctypes.c_size_t),
        ("timeout_ms", ctypes.c_uint32)
    ]

class KMCPHttpResponse(ctypes.Structure):
    """HTTP response structure."""
    _fields_ = [
        ("status_code", ctypes.c_int),
        ("headers", ctypes.POINTER(ctypes.c_char_p)),
        ("headers_count", ctypes.c_size_t),
        ("body", ctypes.c_char_p),
        ("body_length", ctypes.c_size_t)
    ]

class KMCPProcessInfo(ctypes.Structure):
    """Process information structure."""
    _fields_ = [
        ("pid", ctypes.c_int32),
        ("command", ctypes.c_char_p),
        ("args", ctypes.POINTER(ctypes.c_char_p)),
        ("args_count", ctypes.c_size_t),
        ("working_dir", ctypes.c_char_p),
        ("env", ctypes.POINTER(ctypes.c_char_p)),
        ("env_count", ctypes.c_size_t)
    ]

class KMCPBinding:
    """KMCP library binding using ctypes."""

    def __init__(self):
        """Initialize KMCP binding."""
        # Add DLL directories to PATH on Windows
        lib_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'build', 'lib'))

        if os.path.exists(lib_dir):
            os.add_dll_directory(lib_dir)

        # Load the libraries
        try:
            # First try to load mcp_common
            try:
                self.mcp_common = ctypes.CDLL(os.path.join(lib_dir, 'mcp_common.dll'))
            except OSError as e:
                raise OSError(f"Could not load mcp_common library: {e}")

            # Then load kmcp
            try:
                self.lib = ctypes.CDLL(os.path.join(lib_dir, 'kmcp.dll'))
            except OSError as e:
                raise OSError(f"Could not load KMCP library: {e}")

            # Set up function prototypes
            self._setup_mcp_common_functions()

            # Initialize logging
            result = self.mcp_common.mcp_log_init(None, 1)  # MCP_LOG_LEVEL_INFO = 2
            if result != 0:
                raise RuntimeError(f"Failed to initialize logging with error code {result}")

            # Initialize thread-local arena
            result = self.mcp_common.mcp_arena_init_current_thread(0)
            if result != 0:
                self.mcp_common.mcp_log_close()
                raise RuntimeError("Failed to initialize thread-local arena")

        except Exception as e:
            raise RuntimeError(f"Failed to initialize KMCP binding: {e}")

        # Set up function prototypes
        self._setup_functions()

    def __del__(self):
        """Clean up KMCP binding."""
        if hasattr(self, 'mcp_common'):
            # Clean up thread-local arena
            self.mcp_common.mcp_arena_destroy_current_thread()
            # Clean up logging
            self.mcp_common.mcp_log_close()

    def _setup_mcp_common_functions(self):
        """Set up mcp_common function prototypes."""
        # Logging functions
        self.mcp_common.mcp_log_init.restype = ctypes.c_int
        self.mcp_common.mcp_log_init.argtypes = [ctypes.c_char_p, ctypes.c_int]

        self.mcp_common.mcp_log_close.restype = None
        self.mcp_common.mcp_log_close.argtypes = []

        # Arena functions
        self.mcp_common.mcp_arena_init_current_thread.restype = ctypes.c_int
        self.mcp_common.mcp_arena_init_current_thread.argtypes = [ctypes.c_size_t]

        self.mcp_common.mcp_arena_destroy_current_thread.restype = None
        self.mcp_common.mcp_arena_destroy_current_thread.argtypes = []

    def _setup_functions(self):
        """Set up function prototypes."""
        # Version functions
        self.lib.kmcp_get_version.restype = ctypes.c_char_p
        self.lib.kmcp_get_version.argtypes = []

        self.lib.kmcp_get_build_info.restype = ctypes.c_char_p
        self.lib.kmcp_get_build_info.argtypes = []

        # Client functions
        self.lib.kmcp_client_create.argtypes = [ctypes.POINTER(self.ClientConfig)]
        self.lib.kmcp_client_create.restype = ctypes.c_void_p

        self.lib.kmcp_client_close.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_client_close.restype = None

        self.lib.kmcp_client_get_manager.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_client_get_manager.restype = ctypes.c_void_p

        self.lib.kmcp_client_create_from_file.argtypes = [ctypes.c_char_p]
        self.lib.kmcp_client_create_from_file.restype = ctypes.c_void_p

        self.lib.kmcp_client_call_tool.argtypes = [
            ctypes.c_void_p,                # client
            ctypes.c_char_p,                # tool_name
            ctypes.c_char_p,                # params_json
            ctypes.POINTER(ctypes.c_void_p)  # result_json (char**)
        ]
        self.lib.kmcp_client_call_tool.restype = ctypes.c_int

        self.lib.kmcp_client_get_resource.argtypes = [
            ctypes.c_void_p,                # client
            ctypes.c_char_p,                # resource_uri
            ctypes.POINTER(ctypes.c_void_p),  # content (char**)
            ctypes.POINTER(ctypes.c_void_p)   # content_type (char**)
        ]
        self.lib.kmcp_client_get_resource.restype = ctypes.c_int

        # Define kmcp_free function
        self.lib.kmcp_free.argtypes = [ctypes.c_void_p]  # void*
        self.lib.kmcp_free.restype = None  # void

        # Server functions
        self.lib.kmcp_server_create.restype = ctypes.c_void_p
        self.lib.kmcp_server_create.argtypes = []

        self.lib.kmcp_server_destroy.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_server_destroy.restype = None

        self.lib.kmcp_server_add.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(self.ServerConfig)
        ]
        self.lib.kmcp_server_add.restype = ctypes.c_int

        self.lib.kmcp_server_connect.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_server_connect.restype = ctypes.c_int

        self.lib.kmcp_server_disconnect.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_server_disconnect.restype = None

        self.lib.kmcp_server_select_tool.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p
        ]
        self.lib.kmcp_server_select_tool.restype = ctypes.c_int

        self.lib.kmcp_server_select_resource.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p
        ]
        self.lib.kmcp_server_select_resource.restype = ctypes.c_int

        self.lib.kmcp_server_get_connection.argtypes = [
            ctypes.c_void_p,
            ctypes.c_size_t
        ]
        self.lib.kmcp_server_get_connection.restype = ctypes.c_void_p

        self.lib.kmcp_server_get_count.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_server_get_count.restype = ctypes.c_size_t

        self.lib.kmcp_server_reconnect.argtypes = [
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.c_uint32
        ]
        self.lib.kmcp_server_reconnect.restype = ctypes.c_int

    # Helper methods
    class ClientConfig(ctypes.Structure):
        """Client configuration structure."""
        _fields_ = [
            ("name", ctypes.c_char_p),
            ("version", ctypes.c_char_p),
            ("use_manager", ctypes.c_bool),
            ("timeout_ms", ctypes.c_uint32)
        ]

    class ServerConfig(ctypes.Structure):
        """Server configuration structure."""
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

    def create_client(self, config: Optional[dict] = None) -> int:
        """Create a client.

        Args:
            config: Optional configuration dictionary. If None, default values are used.

        Returns:
            Client handle as an integer
        """
        if config is None:
            config = {
                "clientConfig": {
                    "clientName": "default-client",
                    "clientVersion": "1.0.0",
                    "useServerManager": True,
                    "requestTimeoutMs": 30000
                }
            }

        # Extract client config
        client_config = config.get("clientConfig", {})

        # Create client configuration
        client = self.ClientConfig()
        client.name = client_config.get("clientName", "default-client").encode()
        client.version = client_config.get("clientVersion", "1.0.0").encode()
        client.use_manager = client_config.get("useServerManager", True)
        client.timeout_ms = client_config.get("requestTimeoutMs", 30000)

        # Create client
        handle = self.lib.kmcp_client_create(ctypes.byref(client))
        if not handle:
            raise RuntimeError("Failed to create client")
        return handle

    def create_client_from_file(self, config_file: str) -> int:
        """Create a client from a configuration file.

        Args:
            config_file: Path to configuration file

        Returns:
            Client handle as an integer
        """
        client = self.lib.kmcp_client_create_from_file(config_file.encode())
        if not client:
            raise RuntimeError("Failed to create client from file")

        return client

    def get_server_manager(self, client: int) -> int:
        """Get server manager from client.

        Args:
            client: Client handle

        Returns:
            Server manager handle as an integer
        """
        manager = self.lib.kmcp_client_get_manager(client)
        if not manager:
            raise RuntimeError("Failed to get server manager")

        return manager

    def close_client(self, client: int) -> None:
        """Close a client."""
        print(f"close_client called with client={client}, type={type(client)}")
        if client and client != 0:
            try:
                # Convert to void pointer with explicit check
                client_ptr = ctypes.c_void_p(client)
                print(f"Created client_ptr={client_ptr}, value={client_ptr.value}")
                if client_ptr:
                    print(f"Calling kmcp_client_close with client_ptr={client_ptr}")
                    self.lib.kmcp_client_close(client_ptr)
                    print(f"kmcp_client_close call completed successfully")
            except Exception as e:
                print(f"Warning: Error closing client: {e}")

    def call_tool(self, client: int, tool_name: str, request: dict) -> dict:
        """Call a tool."""
        # Convert request to JSON
        request_json = json.dumps(request).encode()

        # Create a pointer to receive the result string
        result_json_ptr = ctypes.c_void_p()

        # Call tool
        result = self.lib.kmcp_client_call_tool(
            ctypes.c_void_p(client),  # Convert to void pointer
            tool_name.encode(),
            request_json,
            ctypes.byref(result_json_ptr)
        )
        if result != 0:
            raise RuntimeError(f"Failed to call tool {tool_name} with error code {result}")

        # Parse response
        if not result_json_ptr:
            return {"error": "No response from tool"}

        # Convert void* to char* and decode
        result_json_str = ctypes.cast(result_json_ptr, ctypes.c_char_p).value
        if not result_json_str:
            return {"error": "Empty response from tool"}

        try:
            response_text = result_json_str.decode()
            response = json.loads(response_text)
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            # Free memory before raising exception
            if result_json_ptr.value:
                self.lib.kmcp_free(result_json_ptr)
            raise RuntimeError(f"Failed to parse tool response: {e}")

        # Free memory using kmcp_free
        if result_json_ptr.value:
            self.lib.kmcp_free(result_json_ptr)

        return response

    def get_resource(self, client: int, resource_uri: str) -> tuple:
        """Get a resource.

        Args:
            client: Client handle
            resource_uri: Resource URI

        Returns:
            Tuple of (content, content_type)

        Raises:
            RuntimeError: If the resource cannot be retrieved
        """
        # Create buffers to store the result
        content_ptr = ctypes.c_void_p()
        content_type_ptr = ctypes.c_void_p()

        # Call get_resource
        result = self.lib.kmcp_client_get_resource(
            ctypes.c_void_p(client),  # Convert to void pointer
            resource_uri.encode(),
            ctypes.byref(content_ptr),
            ctypes.byref(content_type_ptr)
        )

        if result != 0:
            raise RuntimeError(f"Failed to get resource {resource_uri} with error code {result}")

        # Convert void* to char* and decode
        content_str = ctypes.cast(content_ptr, ctypes.c_char_p).value if content_ptr else None
        content_type_str = ctypes.cast(content_type_ptr, ctypes.c_char_p).value if content_type_ptr else None

        # Get the values
        content_value = content_str.decode() if content_str else ""
        content_type_value = content_type_str.decode() if content_type_str else ""

        # Free memory using kmcp_free
        if content_ptr.value:
            self.lib.kmcp_free(content_ptr)
        if content_type_ptr.value:
            self.lib.kmcp_free(content_type_ptr)

        return content_value, content_type_value

    def create_server_manager(self) -> int:
        """Create a server manager instance."""
        manager = self.lib.kmcp_server_create()
        print(f"Created server manager: {manager}")
        return manager

    def destroy_server_manager(self, manager: int):
        """Destroy a server manager instance."""
        self.lib.kmcp_server_destroy(manager)

    def add_server(self, manager: int, config: dict) -> int:
        """Add a server configuration to the manager."""
        server_config = self._create_server_config(config)
        return self.lib.kmcp_server_add(manager, ctypes.byref(server_config))

    def connect_servers(self, manager: int) -> int:
        """Connect to all servers in the manager."""
        return self.lib.kmcp_server_connect(manager)

    def disconnect_servers(self, manager: int) -> int:
        """Disconnect from all servers in the manager."""
        return self.lib.kmcp_server_disconnect(manager)

    def select_tool_server(self, manager: int, tool_name: str) -> int:
        """Select a server for a tool."""
        return self.lib.kmcp_server_select_tool(manager, tool_name.encode())

    def select_resource_server(self, manager: int, resource_uri: str) -> int:
        """Select a server for a resource."""
        return self.lib.kmcp_server_select_resource(manager, resource_uri.encode())

    def get_server_connection(self, manager: int, index: int) -> int:
        """Get a server connection."""
        return self.lib.kmcp_server_get_connection(manager, index)

    def get_server_count(self, manager: int) -> int:
        """Get the number of servers in the manager."""
        return self.lib.kmcp_server_get_count(manager)

    def reconnect_server(self, manager: int, server_index: int, max_attempts: int, retry_interval_ms: int) -> int:
        """Reconnect to a server."""
        return self.lib.kmcp_server_reconnect(manager, server_index, max_attempts, retry_interval_ms)

    def get_version(self) -> str:
        """Get KMCP version."""
        version = self.lib.kmcp_get_version()
        return version.decode() if version else ""

    def get_build_info(self) -> str:
        """Get KMCP build information."""
        build_info = self.lib.kmcp_get_build_info()
        return build_info.decode() if build_info else ""

    def _create_server_config(self, config: dict) -> ServerConfig:
        """Create a server configuration structure."""
        print(f"Creating server config: name={config.get('name')}, url={config.get('url')}, is_http={config.get('is_http')}")

        # Convert args to array if present
        args = config.get("args", [])
        args_array = (ctypes.c_char_p * len(args))()
        for i, arg in enumerate(args):
            args_array[i] = arg.encode() if arg is not None else b""

        # Convert env to array if present
        env = config.get("env", [])
        env_array = (ctypes.c_char_p * len(env))()
        for i, var in enumerate(env):
            env_array[i] = var.encode() if var is not None else b""

        # Handle None values for string fields
        def encode_or_empty(value):
            if value is None:
                return b""
            return value.encode() if isinstance(value, str) else b""

        server_config = self.ServerConfig(
            name=encode_or_empty(config.get("name")),
            command=encode_or_empty(config.get("command")),
            args=args_array,
            args_count=len(args),
            url=encode_or_empty(config.get("url")),
            api_key=encode_or_empty(config.get("api_key")),
            env=env_array,
            env_count=len(env),
            is_http=config.get("is_http", False)
        )

        print(f"Config pointer: {server_config}")
        return server_config

# Create a global instance
kmcp = KMCPBinding()
