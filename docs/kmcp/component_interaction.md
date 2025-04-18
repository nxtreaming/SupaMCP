# KMCP Component Interaction

This document describes how components in the KMCP module interact with each other, helping developers understand the relationships between components and data flow.

## Component Interaction Overview

Component interactions in the KMCP module follow these patterns:

1. **Client as Center**: The client component is the center of application interaction with KMCP, coordinating the work of other components
2. **Server Manager as Proxy**: The server manager acts as a proxy between the client and servers, responsible for selecting appropriate servers to handle requests
3. **Configuration Parser as Initialization Tool**: The configuration parser is used during initialization to provide configuration information to other components
4. **Tool Access Control as Gatekeeper**: Tool access control acts as a gatekeeper during request processing, controlling access permissions to tools

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
