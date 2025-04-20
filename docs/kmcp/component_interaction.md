# KMCP Component Interaction

This document describes how components in the KMCP module interact with each other, helping developers understand the relationships between components and data flow.

## Component Interaction Overview

Component interactions in the KMCP module follow these patterns:

1. **Client as Center**: The client component is the center of application interaction with KMCP, coordinating the work of other components
2. **Server Manager as Proxy**: The server manager acts as a proxy between the client and servers, responsible for selecting appropriate servers to handle requests
3. **Configuration Parser as Initialization Tool**: The configuration parser is used during initialization to provide configuration information to other components
4. **Tool Access Control as Gatekeeper**: Tool access control acts as a gatekeeper during request processing, controlling access permissions to tools
5. **Profile Manager as Configuration Hub**: The profile manager acts as a hub for managing multiple server configurations, allowing users to switch between different environments
6. **Registry as Server Discovery Service**: The registry provides server discovery services, allowing clients to find and connect to available servers
7. **Tool SDK as Extension Framework**: The tool SDK provides a framework for developing and integrating custom tools with KMCP

## Main Interaction Flows

### Client Initialization Flow

1. Application calls `kmcp_client_create` or `kmcp_client_create_from_file` to create a client
2. If using a configuration file, the client creates a configuration parser (`kmcp_config_parser_create`)
3. Client gets client configuration from the configuration parser (`kmcp_config_parser_get_client`)
4. Client creates a server manager (`kmcp_server_manager_create`)
5. Client gets server configurations from the configuration parser and adds them to the server manager (`kmcp_config_parser_get_servers`)
6. Client creates tool access control (`kmcp_tool_access_create`)
7. Client gets tool access control configuration from the configuration parser (`kmcp_config_parser_get_access`)
8. Client closes the configuration parser (`kmcp_config_parser_close`)

### Tool Invocation Flow

1. Application calls `kmcp_client_call_tool` to request a tool invocation
2. Client checks tool access permissions (`kmcp_tool_access_check`)
3. If the tool is allowed, client selects an appropriate server through the server manager
4. Server manager selects a server based on the tool name
5. If a local server is selected, server manager ensures the server process is started (`kmcp_process_start`)
6. Client creates an HTTP client (`kmcp_http_client_create`) to connect to the selected server
7. Client calls the tool through the HTTP client (`kmcp_http_client_call_tool`)
8. HTTP client sends the request and receives the response
9. Client processes the response and returns the result to the application

### Resource Retrieval Flow

1. Application calls `kmcp_client_get_resource` to request a resource
2. Client selects an appropriate server through the server manager
3. Server manager selects a server based on the resource URI
4. If a local server is selected, server manager ensures the server process is started (`kmcp_process_start`)
5. Client creates an HTTP client (`kmcp_http_client_create`) to connect to the selected server
6. Client gets the resource through the HTTP client (`kmcp_http_client_get_resource`)
7. HTTP client sends the request and receives the response
8. Client processes the response and returns the resource content to the application

### Server Management Flow

1. Server manager maintains a list of servers, including local and remote servers
2. For local servers, server manager creates processes (`kmcp_process_create`)
3. When a local server is needed, server manager starts the process (`kmcp_process_start`)
4. Server manager periodically checks process status (`kmcp_process_is_running`)
5. When the client is closed, server manager terminates all local server processes (`kmcp_process_terminate`)

### Profile Management Flow

1. Application creates a profile manager (`kmcp_profile_manager_create`)
2. Application creates profiles (`kmcp_profile_create`)
3. Application adds servers to profiles (`kmcp_profile_add_server`)
4. Application activates a profile (`kmcp_profile_activate`)
5. Application gets the server manager from the active profile (`kmcp_profile_get_server_manager`)
6. Application uses the server manager for client operations
7. Application can switch between profiles by activating different profiles
8. Application can save profiles to a file (`kmcp_profile_save`) and load them later (`kmcp_profile_load`)

### Registry Interaction Flow

1. Application creates a registry connection (`kmcp_registry_create` or `kmcp_registry_create_with_config`)
2. Application gets available servers from the registry (`kmcp_registry_get_servers`)
3. Application searches for servers in the registry (`kmcp_registry_search_servers`)
4. Application gets detailed information about servers (`kmcp_registry_get_server_info`)
5. Application adds servers from the registry to a server manager (`kmcp_registry_add_server`)
6. Application can refresh the registry cache (`kmcp_registry_refresh_cache`)
7. Application frees server information when no longer needed (`kmcp_registry_free_server_info`)

### Tool SDK Flow

1. Tool developer defines tool metadata (`kmcp_tool_metadata_t`)
2. Tool developer implements tool callbacks (`kmcp_tool_callbacks_t`)
3. Tool developer registers the tool with KMCP (`kmcp_tool_register`)
4. When a client calls the tool, KMCP calls the tool's execute callback
5. Tool gets parameters from the JSON object using helper functions (`kmcp_tool_get_string_param`, etc.)
6. Tool creates a result using helper functions (`kmcp_tool_create_success_result`, etc.)
7. Tool can send progress updates (`kmcp_tool_send_progress`) and partial results (`kmcp_tool_send_partial_result`)
8. Tool can check if the operation has been cancelled (`kmcp_tool_is_cancelled`)
9. When the tool is no longer needed, it is unregistered (`kmcp_tool_unregister`)

## Component Data Flow

### Client and Server Manager

- Client sends tool invocation and resource retrieval requests to the server manager
- Server manager returns selected server information to the client

### Server Manager and Process Management

- Server manager sends process creation, startup, and termination requests to process management
- Process management returns process status information to the server manager

### Client and HTTP Client

- Client sends HTTP requests to the HTTP client
- HTTP client returns HTTP responses to the client

### Client and Tool Access Control

- Client sends tool names to tool access control for checking
- Tool access control returns access permission results to the client

### Client and Configuration Parser

- Client requests the configuration parser to parse configuration files
- Configuration parser returns parsing results to the client

### Client and Profile Manager

- Client gets server manager from the profile manager
- Profile manager provides server configurations to the client

### Profile Manager and Server Manager

- Profile manager creates and manages server managers for each profile
- Server manager provides server access to the profile manager

### Client and Registry

- Client discovers servers through the registry
- Registry provides server information to the client

### Registry and Server Manager

- Registry adds servers to the server manager
- Server manager integrates registry-provided servers

### Tool SDK and KMCP

- Tool SDK registers tools with KMCP
- KMCP calls tool callbacks when tools are invoked
- Tool SDK provides parameter access and result creation helpers

## Error Handling

All components use a unified error handling mechanism:

1. Functions return error codes of type `kmcp_error_t`
2. Applications can use `kmcp_error_message` to get error messages
3. Error information is recorded through the logging system
4. Errors propagate from lower-level components to higher-level components, ultimately returning to the application

## Resource Management

Resource management in the KMCP module follows these principles:

1. Functions that create resources are responsible for allocating memory
2. Functions that destroy resources are responsible for freeing memory
3. Resource ownership is clear, avoiding memory leaks and double frees
4. Function documentation clearly states resource ownership and release responsibilities
