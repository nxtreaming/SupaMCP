# KMCP API Reference

## Table of Contents

- [Client API](#client-api)
- [Server Management API](#server-management-api)
- [Configuration Parsing API](#configuration-parsing-api)
- [Tool Access Control API](#tool-access-control-api)
- [HTTP Client API](#http-client-api)
- [Process Management API](#process-management-api)
- [Error Handling API](#error-handling-api)
- [Version Information API](#version-information-api)

## Client API

### Data Structures

#### kmcp_client_t

Client structure representing a KMCP client instance.

#### kmcp_client_config_t

```c
typedef struct {
    char* name;                    /**< Client name */
    char* version;                 /**< Client version */
    bool use_manager;              /**< Whether to use server manager */
    uint32_t timeout_ms;           /**< Request timeout in milliseconds */
} kmcp_client_config_t;
```

### Functions

#### kmcp_client_create

```c
kmcp_client_t* kmcp_client_create(kmcp_client_config_t* config);
```

Create a new KMCP client.

**Parameters**:
- `config`: Client configuration, cannot be NULL

**Return Value**:
- Returns client pointer on success, NULL on failure

#### kmcp_client_create_from_file

```c
kmcp_client_t* kmcp_client_create_from_file(const char* config_file);
```

Create a new KMCP client from a configuration file.

**Parameters**:
- `config_file`: Configuration file path, cannot be NULL

**Return Value**:
- Returns client pointer on success, NULL on failure

#### kmcp_client_close

```c
void kmcp_client_close(kmcp_client_t* client);
```

Close and free a KMCP client.

**Parameters**:
- `client`: Client pointer, can be NULL

#### kmcp_client_get_manager

```c
kmcp_server_manager_t* kmcp_client_get_manager(kmcp_client_t* client);
```

Get the server manager from a client.

**Parameters**:
- `client`: Client pointer, cannot be NULL

**Return Value**:
- Returns server manager pointer on success, NULL on failure

#### kmcp_client_call_tool

```c
int kmcp_client_call_tool(kmcp_client_t* client, const char* tool_name, const char* params_json, char** result_json);
```

Call an MCP tool.

**Parameters**:
- `client`: Client pointer, cannot be NULL
- `tool_name`: Tool name, cannot be NULL
- `params_json`: Parameter JSON string, cannot be NULL
- `result_json`: Pointer to store the result JSON string, memory is allocated by the function, caller is responsible for freeing

**Return Value**:
- Returns 0 on success, error code on failure

#### kmcp_client_get_resource

```c
int kmcp_client_get_resource(kmcp_client_t* client, const char* uri, char** content, char** content_type);
```

Get an MCP resource.

**Parameters**:
- `client`: Client pointer, cannot be NULL
- `uri`: Resource URI, cannot be NULL
- `content`: Pointer to store the resource content, memory is allocated by the function, caller is responsible for freeing
- `content_type`: Pointer to store the content type, memory is allocated by the function, caller is responsible for freeing

**Return Value**:
- Returns 0 on success, error code on failure

## Server Management API

### Data Structures

#### kmcp_server_manager_t

Server manager structure representing a server manager instance.

#### kmcp_server_config_t

```c
typedef struct {
    char* name;                    /**< Server name */
    char* command;                 /**< Server command (local server) */
    char** args;                   /**< Command arguments (local server) */
    size_t args_count;             /**< Argument count (local server) */
    char** env;                    /**< Environment variables (local server) */
    size_t env_count;              /**< Environment variable count (local server) */
    char* url;                     /**< Server URL (HTTP server) */
} kmcp_server_config_t;
```

### Functions

#### kmcp_server_manager_create

```c
kmcp_server_manager_t* kmcp_server_manager_create(void);
```

Create a new server manager.

**Return Value**:
- Returns server manager pointer on success, NULL on failure

#### kmcp_server_manager_destroy

```c
void kmcp_server_manager_destroy(kmcp_server_manager_t* manager);
```

Destroy a server manager.

**Parameters**:
- `manager`: Server manager pointer, can be NULL

#### kmcp_server_manager_add_server

```c
kmcp_error_t kmcp_server_manager_add_server(kmcp_server_manager_t* manager, kmcp_server_config_t* config);
```

Add a server to the manager.

**Parameters**:
- `manager`: Server manager pointer, cannot be NULL
- `config`: Server configuration, cannot be NULL

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_server_manager_get_count

```c
size_t kmcp_server_manager_get_count(kmcp_server_manager_t* manager);
```

Get the number of servers.

**Parameters**:
- `manager`: Server manager pointer, cannot be NULL

**Return Value**:
- Number of servers

#### kmcp_server_manager_get_server

```c
kmcp_error_t kmcp_server_manager_get_server(kmcp_server_manager_t* manager, const char* name, kmcp_server_config_t** config);
```

Get a server configuration by name.

**Parameters**:
- `manager`: Server manager pointer, cannot be NULL
- `name`: Server name, cannot be NULL
- `config`: Pointer to store the server configuration, memory is allocated by the function, caller is responsible for freeing

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

## Configuration Parsing API

### Data Structures

#### kmcp_config_parser_t

Configuration parser structure representing a configuration parser instance.

### Functions

#### kmcp_config_parser_create

```c
kmcp_config_parser_t* kmcp_config_parser_create(const char* file_path);
```

Create a new configuration parser.

**Parameters**:
- `file_path`: Configuration file path, cannot be NULL

**Return Value**:
- Returns configuration parser pointer on success, NULL on failure

#### kmcp_config_parser_close

```c
void kmcp_config_parser_close(kmcp_config_parser_t* parser);
```

Close and free a configuration parser.

**Parameters**:
- `parser`: Configuration parser pointer, can be NULL

#### kmcp_config_parser_get_client

```c
kmcp_error_t kmcp_config_parser_get_client(kmcp_config_parser_t* parser, kmcp_client_config_t* config);
```

Get client configuration.

**Parameters**:
- `parser`: Configuration parser pointer, cannot be NULL
- `config`: Pointer to store client configuration, cannot be NULL

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_config_parser_get_servers

```c
kmcp_error_t kmcp_config_parser_get_servers(kmcp_config_parser_t* parser, kmcp_server_manager_t* manager);
```

Get server configurations and add them to the server manager.

**Parameters**:
- `parser`: Configuration parser pointer, cannot be NULL
- `manager`: Server manager pointer, cannot be NULL

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_config_parser_get_access

```c
kmcp_error_t kmcp_config_parser_get_access(kmcp_config_parser_t* parser, kmcp_tool_access_t* access);
```

Get tool access control configuration.

**Parameters**:
- `parser`: Configuration parser pointer, cannot be NULL
- `access`: Tool access control pointer, cannot be NULL

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

## Tool Access Control API

### Data Structures

#### kmcp_tool_access_t

Tool access control structure representing a tool access control instance.

### Functions

#### kmcp_tool_access_create

```c
kmcp_tool_access_t* kmcp_tool_access_create(bool default_allow);
```

Create a new tool access control.

**Parameters**:
- `default_allow`: Default allow policy, true means default allow, false means default deny

**Return Value**:
- Returns tool access control pointer on success, NULL on failure

#### kmcp_tool_access_destroy

```c
void kmcp_tool_access_destroy(kmcp_tool_access_t* access);
```

Destroy a tool access control.

**Parameters**:
- `access`: Tool access control pointer, can be NULL

#### kmcp_tool_access_add

```c
kmcp_error_t kmcp_tool_access_add(kmcp_tool_access_t* access, const char* tool_name, bool allow);
```

Add a tool to the access control list.

**Parameters**:
- `access`: Tool access control pointer, cannot be NULL
- `tool_name`: Tool name, cannot be NULL
- `allow`: Whether to allow access, true means allow, false means deny

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_tool_access_check

```c
bool kmcp_tool_access_check(kmcp_tool_access_t* access, const char* tool_name);
```

Check if a tool is allowed to access.

**Parameters**:
- `access`: Tool access control pointer, cannot be NULL
- `tool_name`: Tool name, cannot be NULL

**Return Value**:
- Returns true if access is allowed, false if access is denied

#### kmcp_tool_access_set_default_policy

```c
kmcp_error_t kmcp_tool_access_set_default_policy(kmcp_tool_access_t* access, bool default_allow);
```

Set the default access policy.

**Parameters**:
- `access`: Tool access control pointer, cannot be NULL
- `default_allow`: Default allow policy, true means default allow, false means default deny

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

## HTTP Client API

### Data Structures

#### kmcp_http_client_t

HTTP client structure representing an HTTP client instance.

### Functions

#### kmcp_http_client_create

```c
kmcp_http_client_t* kmcp_http_client_create(const char* url);
```

Create a new HTTP client.

**Parameters**:
- `url`: Server URL, cannot be NULL

**Return Value**:
- Returns HTTP client pointer on success, NULL on failure

#### kmcp_http_client_destroy

```c
void kmcp_http_client_destroy(kmcp_http_client_t* client);
```

Destroy an HTTP client.

**Parameters**:
- `client`: HTTP client pointer, can be NULL

#### kmcp_http_client_send

```c
kmcp_error_t kmcp_http_client_send(kmcp_http_client_t* client, const char* method, const char* path, const char* content_type, const char* body, char** response_body, char** response_content_type);
```

Send an HTTP request.

**Parameters**:
- `client`: HTTP client pointer, cannot be NULL
- `method`: HTTP method, cannot be NULL
- `path`: Request path, cannot be NULL
- `content_type`: Content type, can be NULL
- `body`: Request body, can be NULL
- `response_body`: Pointer to store the response body, memory is allocated by the function, caller is responsible for freeing
- `response_content_type`: Pointer to store the response content type, memory is allocated by the function, caller is responsible for freeing

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_http_client_call_tool

```c
kmcp_error_t kmcp_http_client_call_tool(kmcp_http_client_t* client, const char* tool_name, const char* params_json, char** result_json);
```

Call an MCP tool.

**Parameters**:
- `client`: HTTP client pointer, cannot be NULL
- `tool_name`: Tool name, cannot be NULL
- `params_json`: Parameter JSON string, cannot be NULL
- `result_json`: Pointer to store the result JSON string, memory is allocated by the function, caller is responsible for freeing

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_http_client_get_resource

```c
kmcp_error_t kmcp_http_client_get_resource(kmcp_http_client_t* client, const char* uri, char** content, char** content_type);
```

Get an MCP resource.

**Parameters**:
- `client`: HTTP client pointer, cannot be NULL
- `uri`: Resource URI, cannot be NULL
- `content`: Pointer to store the resource content, memory is allocated by the function, caller is responsible for freeing
- `content_type`: Pointer to store the content type, memory is allocated by the function, caller is responsible for freeing

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

## Process Management API

### Data Structures

#### kmcp_process_t

Process structure representing a process instance.

### Functions

#### kmcp_process_create

```c
kmcp_process_t* kmcp_process_create(const char* command, char** args, size_t args_count, char** env, size_t env_count);
```

Create a new process.

**Parameters**:
- `command`: Command path, cannot be NULL
- `args`: Command arguments array, can be NULL
- `args_count`: Argument count
- `env`: Environment variables array, can be NULL
- `env_count`: Environment variable count

**Return Value**:
- Returns process pointer on success, NULL on failure

#### kmcp_process_destroy

```c
void kmcp_process_destroy(kmcp_process_t* process);
```

Destroy a process.

**Parameters**:
- `process`: Process pointer, can be NULL

#### kmcp_process_start

```c
kmcp_error_t kmcp_process_start(kmcp_process_t* process);
```

Start a process.

**Parameters**:
- `process`: Process pointer, cannot be NULL

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

#### kmcp_process_is_running

```c
bool kmcp_process_is_running(kmcp_process_t* process);
```

Check if a process is running.

**Parameters**:
- `process`: Process pointer, cannot be NULL

**Return Value**:
- Returns true if the process is running, false otherwise

#### kmcp_process_terminate

```c
kmcp_error_t kmcp_process_terminate(kmcp_process_t* process);
```

Terminate a process.

**Parameters**:
- `process`: Process pointer, cannot be NULL

**Return Value**:
- Returns KMCP_SUCCESS on success, error code on failure

## Error Handling API

### Data Structures

#### kmcp_error_t

```c
typedef enum {
    KMCP_SUCCESS = 0,                  /**< Operation successful */
    KMCP_ERROR_INVALID_PARAMETER = -1, /**< Invalid parameter */
    KMCP_ERROR_MEMORY_ALLOCATION = -2, /**< Memory allocation failed */
    KMCP_ERROR_FILE_NOT_FOUND = -3,    /**< File not found */
    KMCP_ERROR_PARSE_FAILED = -4,      /**< Parsing failed */
    KMCP_ERROR_CONNECTION_FAILED = -5, /**< Connection failed */
    KMCP_ERROR_TIMEOUT = -6,           /**< Operation timed out */
    KMCP_ERROR_NOT_IMPLEMENTED = -7,   /**< Feature not implemented */
    KMCP_ERROR_PERMISSION_DENIED = -8, /**< Permission denied */
    KMCP_ERROR_PROCESS_FAILED = -9,    /**< Process operation failed */
    KMCP_ERROR_SERVER_NOT_FOUND = -10, /**< Server not found */
    KMCP_ERROR_TOOL_NOT_FOUND = -11,   /**< Tool not found */
    KMCP_ERROR_RESOURCE_NOT_FOUND = -12, /**< Resource not found */
    KMCP_ERROR_INTERNAL = -99          /**< Internal error */
} kmcp_error_t;
```

### Functions

#### kmcp_error_message

```c
const char* kmcp_error_message(kmcp_error_t error_code);
```

Get the error message corresponding to an error code.

**Parameters**:
- `error_code`: Error code

**Return Value**:
- Error message string

## Version Information API

### Functions

#### kmcp_get_version

```c
const char* kmcp_get_version(void);
```

Get the KMCP version information.

**Return Value**:
- KMCP version string

#### kmcp_get_build_info

```c
const char* kmcp_get_build_info(void);
```

Get the KMCP build information.

**Return Value**:
- KMCP build information string
