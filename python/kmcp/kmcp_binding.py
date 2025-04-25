"""KMCP Python bindings using ctypes."""

import os
import sys
import ctypes
import json
import logging
from typing import Optional

# Constants for log levels
MCP_LOG_LEVEL_TRACE = 0  # Fine-grained debugging information
MCP_LOG_LEVEL_DEBUG = 1  # Detailed debugging information
MCP_LOG_LEVEL_INFO = 2   # Informational messages about normal operation
MCP_LOG_LEVEL_WARN = 3   # Warning conditions that might indicate potential problems
MCP_LOG_LEVEL_ERROR = 4  # Error conditions that prevent normal operation
MCP_LOG_LEVEL_FATAL = 5  # Severe errors causing program termination

# Constants for arena initialization
MCP_ARENA_DEFAULT_SIZE = 0  # Use default arena size

# Constants for client configuration
DEFAULT_CLIENT_NAME = "default-client"
DEFAULT_CLIENT_VERSION = "1.0.0"
DEFAULT_REQUEST_TIMEOUT_MS = 30000  # 30 seconds

# Remove libc import since we'll use kmcp_free instead

# choose the correct libc
# if sys.platform == 'win32':
#     libc = ctypes.cdll.msvcrt
# elif sys.platform == 'darwin':
#     libc = ctypes.CDLL('libc.dylib')
# else:  # Linux and other Unix-like systems
#     libc = ctypes.CDLL('libc.so.6')

# Explicitly specify libc.free parameter and return types
# libc.free.argtypes = [ctypes.c_void_p]
# libc.free.restype = None

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

    def __init__(self) -> None:
        """Initialize KMCP binding.

        Raises:
            OSError: If the KMCP or MCP Common libraries cannot be loaded
            RuntimeError: If initialization fails
        """
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

            # Set up function prototypes including kmcp_free
            self._setup_functions()

            # Initialize logging
            result = self.mcp_common.mcp_log_init(None, MCP_LOG_LEVEL_INFO)
            if result != 0:
                raise RuntimeError(f"Failed to initialize logging with error code {result}")

            # Initialize thread-local arena
            result = self.mcp_common.mcp_arena_init_current_thread(MCP_ARENA_DEFAULT_SIZE)
            if result != 0:
                self.mcp_common.mcp_log_close()
                raise RuntimeError("Failed to initialize thread-local arena")

        except Exception as e:
            raise RuntimeError(f"Failed to initialize KMCP binding: {e}")

        # Set up function prototypes
        self._setup_mcp_common_functions()

    def __del__(self) -> None:
        """Clean up KMCP binding.

        This method is called when the object is about to be destroyed.
        It cleans up resources allocated by the KMCP binding.
        """
        if hasattr(self, 'mcp_common'):
            # Clean up thread-local arena
            self.mcp_common.mcp_arena_destroy_current_thread()
            # Clean up logging
            self.mcp_common.mcp_log_close()

    def _setup_mcp_common_functions(self) -> None:
        """Set up mcp_common function prototypes.

        This method sets up the function prototypes for the MCP Common library.
        """
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

    def _setup_functions(self) -> None:
        """Set up function prototypes.

        This method sets up the function prototypes for the KMCP library.
        """
        # Memory management
        self.lib.kmcp_free.argtypes = [ctypes.c_void_p]
        self.lib.kmcp_free.restype = None

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

    def create_client(self, config: Optional[dict] = None) -> ctypes.c_void_p:
        """Create a client.

        Args:
            config: Optional configuration dictionary. If None, default values are used.

        Returns:
            Client handle as a c_void_p
        """
        if config is None:
            config = {
                "clientConfig": {
                    "clientName": DEFAULT_CLIENT_NAME,
                    "clientVersion": DEFAULT_CLIENT_VERSION,
                    "useServerManager": True,
                    "requestTimeoutMs": DEFAULT_REQUEST_TIMEOUT_MS
                }
            }

        # Extract client config
        client_config = config.get("clientConfig", {})

        # Create client configuration
        client = self.ClientConfig()
        client.name = client_config.get("clientName", DEFAULT_CLIENT_NAME).encode()
        client.version = client_config.get("clientVersion", DEFAULT_CLIENT_VERSION).encode()
        client.use_manager = client_config.get("useServerManager", True)
        client.timeout_ms = client_config.get("requestTimeoutMs", DEFAULT_REQUEST_TIMEOUT_MS)

        # Create client
        handle = self.lib.kmcp_client_create(ctypes.byref(client))
        if not handle:
            raise RuntimeError("Failed to create client")
        return ctypes.c_void_p(handle)

    def create_client_from_file(self, config_file: str) -> ctypes.c_void_p:
        """Create a client from a configuration file.

        Args:
            config_file: Path to configuration file

        Returns:
            Client handle as a c_void_p
        """
        client = self.lib.kmcp_client_create_from_file(config_file.encode())
        if not client:
            raise RuntimeError("Failed to create client from file")

        return ctypes.c_void_p(client)

    def get_server_manager(self, client: ctypes.c_void_p) -> ctypes.c_void_p:
        """Get server manager from client.

        Args:
            client: Client handle as a c_void_p

        Returns:
            Server manager handle as a c_void_p
        """
        manager = self.lib.kmcp_client_get_manager(client)
        if not manager:
            raise RuntimeError("Failed to get server manager")

        return ctypes.c_void_p(manager)

    def close_client(self, client: ctypes.c_void_p) -> None:
        """Close a client.

        Args:
            client: Client handle as a c_void_p

        Raises:
            RuntimeError: If the client cannot be closed
        """
        if client and client.value:
            try:
                self.lib.kmcp_client_close(client)
            except Exception as e:
                # Log the error and raise an exception
                logging.error(f"Error closing client: {e}")
                raise RuntimeError(f"Failed to close client: {e}")

    def call_tool(self, client: ctypes.c_void_p, tool_name: str, request: dict) -> dict:
        """Call a tool.

        Args:
            client: Client handle as a c_void_p
            tool_name: Name of the tool to call
            request: Request data as a dictionary

        Returns:
            Response data as a dictionary

        Raises:
            RuntimeError: If the tool call fails or the response cannot be parsed
        """
        # Convert request to JSON
        request_json = json.dumps(request).encode()

        # Create a pointer to receive the result string
        result_json_ptr = ctypes.c_void_p()

        # Call tool
        result = self.lib.kmcp_client_call_tool(
            client,
            tool_name.encode(),
            request_json,
            ctypes.byref(result_json_ptr)
        )
        if result != 0:
            raise RuntimeError(f"Failed to call tool {tool_name} with error code {result}")

        # Parse response
        if not result_json_ptr:
            raise RuntimeError("No response from tool")

        # Convert void* to char* and decode
        result_json_str = ctypes.cast(result_json_ptr, ctypes.c_char_p).value
        if not result_json_str:
            self._free_memory(result_json_ptr)
            raise RuntimeError("Empty response from tool")

        try:
            response_text = result_json_str.decode()
            response = json.loads(response_text)
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            # Free memory before raising exception
            self._free_memory(result_json_ptr)
            raise RuntimeError(f"Failed to parse tool response: {e}")

        # Free memory using kmcp_free
        self._free_memory(result_json_ptr)

        return response

    def get_resource(self, client: ctypes.c_void_p, resource_uri: str) -> tuple[str, str]:
        """Get a resource.

        Args:
            client: Client handle as a c_void_p
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
            client,
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
        self._free_memory(content_ptr)
        self._free_memory(content_type_ptr)

        return content_value, content_type_value

    def create_server_manager(self) -> ctypes.c_void_p:
        """Create a server manager instance.

        Returns:
            Server manager handle as a c_void_p

        Raises:
            RuntimeError: If the server manager cannot be created
        """
        manager = self.lib.kmcp_server_create()
        if not manager:
            raise RuntimeError("Failed to create server manager")
        return ctypes.c_void_p(manager)

    def destroy_server_manager(self, manager: ctypes.c_void_p):
        """Destroy a server manager instance.

        Args:
            manager: Server manager handle as a c_void_p
        """
        self.lib.kmcp_server_destroy(manager)

    def add_server(self, manager: ctypes.c_void_p, config: dict) -> int:
        """Add a server configuration to the manager.

        Args:
            manager: Server manager handle as a c_void_p
            config: Server configuration dictionary

        Returns:
            Server index as an integer

        Raises:
            RuntimeError: If the server cannot be added
        """
        server_config = self._create_server_config(config)
        result = self.lib.kmcp_server_add(manager, ctypes.byref(server_config))
        if result < 0:
            raise RuntimeError(f"Failed to add server with error code {result}")
        return result

    def connect_servers(self, manager: ctypes.c_void_p) -> int:
        """Connect to all servers in the manager.

        Args:
            manager: Server manager handle as a c_void_p

        Returns:
            Number of connected servers

        Raises:
            RuntimeError: If the servers cannot be connected
        """
        result = self.lib.kmcp_server_connect(manager)
        if result < 0:
            raise RuntimeError(f"Failed to connect servers with error code {result}")
        return result

    def disconnect_servers(self, manager: ctypes.c_void_p) -> int:
        """Disconnect from all servers in the manager.

        Args:
            manager: Server manager handle as a c_void_p

        Returns:
            0 on success
        """
        return self.lib.kmcp_server_disconnect(manager)

    def select_tool_server(self, manager: ctypes.c_void_p, tool_name: str) -> int:
        """Select a server for a tool.

        Args:
            manager: Server manager handle as a c_void_p
            tool_name: Name of the tool

        Returns:
            Server index as an integer

        Raises:
            RuntimeError: If no server can be selected for the tool
        """
        result = self.lib.kmcp_server_select_tool(manager, tool_name.encode())
        if result < 0:
            raise RuntimeError(f"Failed to select server for tool {tool_name} with error code {result}")
        return result

    def select_resource_server(self, manager: ctypes.c_void_p, resource_uri: str) -> int:
        """Select a server for a resource.

        Args:
            manager: Server manager handle as a c_void_p
            resource_uri: URI of the resource

        Returns:
            Server index as an integer

        Raises:
            RuntimeError: If no server can be selected for the resource
        """
        result = self.lib.kmcp_server_select_resource(manager, resource_uri.encode())
        if result < 0:
            raise RuntimeError(f"Failed to select server for resource {resource_uri} with error code {result}")
        return result

    def get_server_connection(self, manager: ctypes.c_void_p, index: int) -> ctypes.c_void_p:
        """Get a server connection.

        Args:
            manager: Server manager handle as a c_void_p
            index: Server index

        Returns:
            Connection handle as a c_void_p

        Raises:
            RuntimeError: If the connection cannot be retrieved
        """
        result = self.lib.kmcp_server_get_connection(manager, index)
        if not result:
            raise RuntimeError(f"Failed to get server connection for index {index}")
        return ctypes.c_void_p(result)

    def get_server_count(self, manager: ctypes.c_void_p) -> int:
        """Get the number of servers in the manager.

        Args:
            manager: Server manager handle as a c_void_p

        Returns:
            Number of servers in the manager
        """
        return self.lib.kmcp_server_get_count(manager)

    def reconnect_server(self, manager: ctypes.c_void_p, server_index: int, max_attempts: int, retry_interval_ms: int) -> int:
        """Reconnect to a server.

        Args:
            manager: Server manager handle as a c_void_p
            server_index: Server index
            max_attempts: Maximum number of reconnection attempts
            retry_interval_ms: Interval between reconnection attempts in milliseconds

        Returns:
            0 on success, negative value on failure

        Raises:
            RuntimeError: If the server cannot be reconnected
        """
        result = self.lib.kmcp_server_reconnect(manager, server_index, max_attempts, retry_interval_ms)
        if result < 0:
            raise RuntimeError(f"Failed to reconnect server {server_index} with error code {result}")
        return result

    @property
    def version(self) -> str:
        """Get KMCP version."""
        version = self.lib.kmcp_get_version()
        return version.decode('utf-8') if version else ""

    @property
    def build_info(self) -> str:
        """Get KMCP build information."""
        info = self.lib.kmcp_get_build_info()
        return info.decode('utf-8') if info else ""

    def _free_memory(self, ptr: ctypes.c_void_p) -> None:
        """Free memory allocated by KMCP.

        Args:
            ptr: Pointer to memory to free
        """
        if ptr and ptr.value:
            self.lib.kmcp_free(ptr)

    def _create_server_config(self, config: dict) -> 'KMCPBinding.ServerConfig':
        """Create a server configuration structure.

        Args:
            config: Server configuration dictionary

        Returns:
            Server configuration structure
        """
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
        def encode_or_empty(value: Optional[str]) -> bytes:
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

        return server_config

# Create a global instance
kmcp = KMCPBinding()
