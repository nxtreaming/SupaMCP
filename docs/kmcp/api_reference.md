# KMCP API Reference

## Table of Contents

- [Client API](#client-api)
- [Server Management API](#server-management-api)
- [Configuration Parsing API](#configuration-parsing-api)
- [Tool Access Control API](#tool-access-control-api)
- [HTTP Client API](#http-client-api)
- [Process Management API](#process-management-api)
- [Profile Management API](#profile-management-api)
- [Registry API](#registry-api)
- [Tool SDK API](#tool-sdk-api)
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

## Profile Management API

### Data Structures

#### kmcp_profile_t

Profile structure representing a profile instance.

#### kmcp_profile_manager_t

Profile manager structure representing a profile manager instance.

### Functions

#### kmcp_profile_manager_create

```c
kmcp_profile_manager_t* kmcp_profile_manager_create(void);
```

Create a profile manager.

**Return Value**:
- Returns profile manager pointer on success, NULL on failure

#### kmcp_profile_manager_close

```c
void kmcp_profile_manager_close(kmcp_profile_manager_t* manager);
```

Close a profile manager and free resources.

**Parameters**:
- `manager`: Profile manager (must not be NULL)

#### kmcp_profile_create

```c
kmcp_error_t kmcp_profile_create(kmcp_profile_manager_t* manager, const char* name);
```

Create a new profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `name`: Profile name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_delete

```c
kmcp_error_t kmcp_profile_delete(kmcp_profile_manager_t* manager, const char* name);
```

Delete a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `name`: Profile name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_rename

```c
kmcp_error_t kmcp_profile_rename(kmcp_profile_manager_t* manager, const char* old_name, const char* new_name);
```

Rename a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `old_name`: Old profile name (must not be NULL)
- `new_name`: New profile name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_activate

```c
kmcp_error_t kmcp_profile_activate(kmcp_profile_manager_t* manager, const char* name);
```

Activate a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `name`: Profile name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_deactivate

```c
kmcp_error_t kmcp_profile_deactivate(kmcp_profile_manager_t* manager, const char* name);
```

Deactivate a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `name`: Profile name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_get_active

```c
const char* kmcp_profile_get_active(kmcp_profile_manager_t* manager);
```

Get the active profile name.

**Parameters**:
- `manager`: Profile manager (must not be NULL)

**Return Value**:
- Returns the active profile name, or NULL if no profile is active

#### kmcp_profile_exists

```c
bool kmcp_profile_exists(kmcp_profile_manager_t* manager, const char* name);
```

Check if a profile exists.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `name`: Profile name (must not be NULL)

**Return Value**:
- Returns true if the profile exists, false otherwise

#### kmcp_profile_get_count

```c
size_t kmcp_profile_get_count(kmcp_profile_manager_t* manager);
```

Get the number of profiles.

**Parameters**:
- `manager`: Profile manager (must not be NULL)

**Return Value**:
- Returns the number of profiles, or 0 on error

#### kmcp_profile_get_names

```c
kmcp_error_t kmcp_profile_get_names(kmcp_profile_manager_t* manager, char*** names, size_t* count);
```

Get a list of profile names.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `names`: Pointer to an array of strings to store profile names (must not be NULL)
- `count`: Pointer to store the number of profiles (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

**Note**: The caller is responsible for freeing each string in the names array and the names array itself.

#### kmcp_profile_add_server

```c
kmcp_error_t kmcp_profile_add_server(kmcp_profile_manager_t* manager, const char* profile_name, kmcp_server_config_t* config);
```

Add a server to a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `profile_name`: Profile name (must not be NULL)
- `config`: Server configuration (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_remove_server

```c
kmcp_error_t kmcp_profile_remove_server(kmcp_profile_manager_t* manager, const char* profile_name, const char* server_name);
```

Remove a server from a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `profile_name`: Profile name (must not be NULL)
- `server_name`: Server name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_copy_server

```c
kmcp_error_t kmcp_profile_copy_server(
    kmcp_profile_manager_t* manager,
    const char* source_profile,
    const char* source_server,
    const char* target_profile,
    const char* target_server
);
```

Copy a server from one profile to another.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `source_profile`: Source profile name (must not be NULL)
- `source_server`: Source server name (must not be NULL)
- `target_profile`: Target profile name (must not be NULL)
- `target_server`: Target server name (can be NULL to use source_server)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_move_server

```c
kmcp_error_t kmcp_profile_move_server(
    kmcp_profile_manager_t* manager,
    const char* source_profile,
    const char* source_server,
    const char* target_profile,
    const char* target_server
);
```

Move a server from one profile to another.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `source_profile`: Source profile name (must not be NULL)
- `source_server`: Source server name (must not be NULL)
- `target_profile`: Target profile name (must not be NULL)
- `target_server`: Target server name (can be NULL to use source_server)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_get_server_manager

```c
kmcp_server_manager_t* kmcp_profile_get_server_manager(kmcp_profile_manager_t* manager, const char* profile_name);
```

Get the server manager for a profile.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `profile_name`: Profile name (must not be NULL)

**Return Value**:
- Returns the server manager for the profile, or NULL on error

#### kmcp_profile_save

```c
kmcp_error_t kmcp_profile_save(kmcp_profile_manager_t* manager, const char* file_path);
```

Save profiles to a file.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `file_path`: File path (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_load

```c
kmcp_error_t kmcp_profile_load(kmcp_profile_manager_t* manager, const char* file_path);
```

Load profiles from a file.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `file_path`: File path (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_export

```c
kmcp_error_t kmcp_profile_export(kmcp_profile_manager_t* manager, const char* profile_name, const char* file_path);
```

Export a profile to a file.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `profile_name`: Profile name (must not be NULL)
- `file_path`: File path (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_profile_import

```c
kmcp_error_t kmcp_profile_import(kmcp_profile_manager_t* manager, const char* file_path, const char* profile_name);
```

Import a profile from a file.

**Parameters**:
- `manager`: Profile manager (must not be NULL)
- `file_path`: File path (must not be NULL)
- `profile_name`: Profile name (can be NULL to use the name from the file)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

## Registry API

### Data Structures

#### kmcp_registry_t

Registry structure representing a registry connection.

#### kmcp_server_info_t

```c
typedef struct kmcp_server_info {
    char* id;                       /**< Server ID */
    char* name;                     /**< Server name */
    char* url;                      /**< Server URL */
    char* description;              /**< Server description */
    char* version;                  /**< Server version */
    char** capabilities;            /**< Server capabilities */
    size_t capabilities_count;      /**< Number of capabilities */
    char** tools;                   /**< Supported tools */
    size_t tools_count;             /**< Number of tools */
    char** resources;               /**< Supported resources */
    size_t resources_count;         /**< Number of resources */
    bool is_public;                 /**< Whether the server is public */
    time_t last_seen;               /**< Last time the server was seen */
} kmcp_server_info_t;
```

#### kmcp_registry_config_t

```c
typedef struct kmcp_registry_config {
    const char* registry_url;       /**< Registry URL (must not be NULL) */
    const char* api_key;            /**< API key (can be NULL) */
    int cache_ttl_seconds;          /**< Cache time-to-live in seconds (0 for default) */
    int connect_timeout_ms;         /**< Connection timeout in milliseconds (0 for default) */
    int request_timeout_ms;         /**< Request timeout in milliseconds (0 for default) */
    int max_retries;                /**< Maximum number of retries (0 for default) */
} kmcp_registry_config_t;
```

### Functions

#### kmcp_registry_create

```c
kmcp_registry_t* kmcp_registry_create(const char* registry_url);
```

Create a registry connection.

**Parameters**:
- `registry_url`: Registry URL (must not be NULL)

**Return Value**:
- Returns registry connection pointer on success, NULL on failure

**Note**: The caller is responsible for freeing the registry using kmcp_registry_close()

#### kmcp_registry_create_with_config

```c
kmcp_registry_t* kmcp_registry_create_with_config(const kmcp_registry_config_t* config);
```

Create a registry connection with custom configuration.

**Parameters**:
- `config`: Registry configuration (must not be NULL)

**Return Value**:
- Returns registry connection pointer on success, NULL on failure

**Note**: The caller is responsible for freeing the registry using kmcp_registry_close()

#### kmcp_registry_close

```c
void kmcp_registry_close(kmcp_registry_t* registry);
```

Close a registry connection.

**Parameters**:
- `registry`: Registry connection (can be NULL)

#### kmcp_registry_get_servers

```c
kmcp_error_t kmcp_registry_get_servers(kmcp_registry_t* registry, kmcp_server_info_t** servers, size_t* count);
```

Get available servers from the registry.

**Parameters**:
- `registry`: Registry connection (must not be NULL)
- `servers`: Pointer to an array of server information structures (output parameter)
- `count`: Pointer to the number of servers (output parameter)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

**Note**: The caller is responsible for freeing the server information array using kmcp_registry_free_server_info()

#### kmcp_registry_search_servers

```c
kmcp_error_t kmcp_registry_search_servers(kmcp_registry_t* registry, const char* query, kmcp_server_info_t** servers, size_t* count);
```

Search for servers in the registry.

**Parameters**:
- `registry`: Registry connection (must not be NULL)
- `query`: Search query (must not be NULL)
- `servers`: Pointer to an array of server information structures (output parameter)
- `count`: Pointer to the number of servers (output parameter)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

**Note**: The caller is responsible for freeing the server information array using kmcp_registry_free_server_info()

#### kmcp_registry_get_server_info

```c
kmcp_error_t kmcp_registry_get_server_info(kmcp_registry_t* registry, const char* server_id, kmcp_server_info_t** server_info);
```

Get detailed information about a server from the registry.

**Parameters**:
- `registry`: Registry connection (must not be NULL)
- `server_id`: Server ID (must not be NULL)
- `server_info`: Pointer to a server information structure (output parameter)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

**Note**: The caller is responsible for freeing the server information using kmcp_registry_free_server_info()

#### kmcp_registry_add_server

```c
kmcp_error_t kmcp_registry_add_server(kmcp_registry_t* registry, kmcp_server_manager_t* manager, const char* server_id);
```

Add a server from the registry to a server manager.

**Parameters**:
- `registry`: Registry connection (must not be NULL)
- `manager`: Server manager (must not be NULL)
- `server_id`: Server ID (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_registry_add_server_by_url

```c
kmcp_error_t kmcp_registry_add_server_by_url(kmcp_registry_t* registry, kmcp_server_manager_t* manager, const char* url);
```

Add a server from the registry to a server manager by URL.

**Parameters**:
- `registry`: Registry connection (must not be NULL)
- `manager`: Server manager (must not be NULL)
- `url`: Server URL (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_registry_refresh_cache

```c
kmcp_error_t kmcp_registry_refresh_cache(kmcp_registry_t* registry);
```

Refresh the registry cache.

**Parameters**:
- `registry`: Registry connection (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_registry_free_server_info

```c
void kmcp_registry_free_server_info(kmcp_server_info_t* server_info);
```

Free server information.

**Parameters**:
- `server_info`: Server information (can be NULL)

#### kmcp_registry_free_server_info_array

```c
void kmcp_registry_free_server_info_array(kmcp_server_info_t* servers, size_t count);
```

Free an array of server information structures.

**Parameters**:
- `servers`: Array of server information structures (can be NULL)
- `count`: Number of servers in the array

## Tool SDK API

### Data Structures

#### kmcp_tool_context_t

Tool context structure representing a tool execution context.

#### kmcp_tool_capability_t

```c
typedef enum {
    KMCP_TOOL_CAP_NONE           = 0,      /**< No special capabilities */
    KMCP_TOOL_CAP_STREAMING      = 1 << 0, /**< Tool supports streaming responses */
    KMCP_TOOL_CAP_BINARY         = 1 << 1, /**< Tool supports binary data */
    KMCP_TOOL_CAP_ASYNC          = 1 << 2, /**< Tool supports asynchronous operation */
    KMCP_TOOL_CAP_CANCELLABLE    = 1 << 3, /**< Tool operations can be cancelled */
    KMCP_TOOL_CAP_BATCH          = 1 << 4, /**< Tool supports batch operations */
    KMCP_TOOL_CAP_STATEFUL       = 1 << 5, /**< Tool maintains state between calls */
    KMCP_TOOL_CAP_RESOURCE_HEAVY = 1 << 6, /**< Tool requires significant resources */
    KMCP_TOOL_CAP_PRIVILEGED     = 1 << 7  /**< Tool requires elevated privileges */
} kmcp_tool_capability_t;
```

#### kmcp_tool_category_t

```c
typedef enum {
    KMCP_TOOL_CATEGORY_GENERAL,    /**< General purpose tool */
    KMCP_TOOL_CATEGORY_SYSTEM,     /**< System management tool */
    KMCP_TOOL_CATEGORY_NETWORK,    /**< Network-related tool */
    KMCP_TOOL_CATEGORY_SECURITY,   /**< Security-related tool */
    KMCP_TOOL_CATEGORY_DEVELOPMENT,/**< Development tool */
    KMCP_TOOL_CATEGORY_MEDIA,      /**< Media processing tool */
    KMCP_TOOL_CATEGORY_AI,         /**< AI/ML tool */
    KMCP_TOOL_CATEGORY_DATABASE,   /**< Database tool */
    KMCP_TOOL_CATEGORY_UTILITY,    /**< Utility tool */
    KMCP_TOOL_CATEGORY_CUSTOM      /**< Custom category */
} kmcp_tool_category_t;
```

#### kmcp_tool_metadata_t

```c
typedef struct {
    const char* name;              /**< Tool name (required) */
    const char* version;           /**< Tool version (required) */
    const char* description;       /**< Tool description (optional) */
    const char* author;            /**< Tool author (optional) */
    const char* website;           /**< Tool website (optional) */
    const char* license;           /**< Tool license (optional) */
    const char** tags;             /**< Tool tags (optional) */
    size_t tags_count;             /**< Number of tags */
    kmcp_tool_category_t category; /**< Tool category */
    unsigned int capabilities;     /**< Tool capabilities (bitfield of kmcp_tool_capability_t) */
    const char** dependencies;     /**< Tool dependencies (optional) */
    size_t dependencies_count;     /**< Number of dependencies */
} kmcp_tool_metadata_t;
```

#### kmcp_tool_callbacks_t

```c
typedef struct {
    kmcp_tool_init_fn init;       /**< Initialization callback (required) */
    kmcp_tool_cleanup_fn cleanup; /**< Cleanup callback (required) */
    kmcp_tool_execute_fn execute; /**< Execute callback (required) */
    kmcp_tool_cancel_fn cancel;   /**< Cancel callback (optional) */
} kmcp_tool_callbacks_t;
```

### Functions

#### kmcp_tool_register

```c
kmcp_error_t kmcp_tool_register(
    const kmcp_tool_metadata_t* metadata,
    const kmcp_tool_callbacks_t* callbacks
);
```

Register a tool with KMCP.

**Parameters**:
- `metadata`: Tool metadata (must not be NULL)
- `callbacks`: Tool callbacks (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_tool_unregister

```c
kmcp_error_t kmcp_tool_unregister(const char* tool_name);
```

Unregister a tool from KMCP.

**Parameters**:
- `tool_name`: Tool name (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_tool_get_context

```c
kmcp_tool_context_t* kmcp_tool_get_context(void);
```

Get tool context.

**Return Value**:
- Returns the tool context, or NULL if not called from a tool callback

#### kmcp_tool_set_user_data

```c
kmcp_error_t kmcp_tool_set_user_data(kmcp_tool_context_t* context, void* user_data);
```

Set tool user data.

**Parameters**:
- `context`: Tool context (must not be NULL)
- `user_data`: User data pointer

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_tool_get_user_data

```c
kmcp_error_t kmcp_tool_get_user_data(kmcp_tool_context_t* context, void** user_data);
```

Get tool user data.

**Parameters**:
- `context`: Tool context (must not be NULL)
- `user_data`: Pointer to store the user data (output parameter)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_tool_log

```c
void kmcp_tool_log(kmcp_tool_context_t* context, int level, const char* format, ...);
```

Log a message from a tool.

**Parameters**:
- `context`: Tool context (must not be NULL)
- `level`: Log level (0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=fatal)
- `format`: Format string (printf-style)
- `...`: Arguments for the format string

#### kmcp_tool_send_progress

```c
kmcp_error_t kmcp_tool_send_progress(
    kmcp_tool_context_t* context,
    float progress,
    const char* message
);
```

Send progress update from a tool.

**Parameters**:
- `context`: Tool context (must not be NULL)
- `progress`: Progress value (0.0 to 1.0)
- `message`: Progress message (optional, can be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_tool_send_partial_result

```c
kmcp_error_t kmcp_tool_send_partial_result(
    kmcp_tool_context_t* context,
    const mcp_json_t* partial_result
);
```

Send a partial result from a tool.

**Parameters**:
- `context`: Tool context (must not be NULL)
- `partial_result`: Partial result as a JSON object (must not be NULL)

**Return Value**:
- Returns KMCP_SUCCESS on success, or an error code on failure

#### kmcp_tool_is_cancelled

```c
bool kmcp_tool_is_cancelled(kmcp_tool_context_t* context);
```

Check if a tool operation has been cancelled.

**Parameters**:
- `context`: Tool context (must not be NULL)

**Return Value**:
- Returns true if the operation has been cancelled, false otherwise

#### kmcp_tool_get_string_param

```c
const char* kmcp_tool_get_string_param(
    const mcp_json_t* params,
    const char* key,
    const char* default_value
);
```

Get tool parameter as string.

**Parameters**:
- `params`: Tool parameters as a JSON object (must not be NULL)
- `key`: Parameter key (must not be NULL)
- `default_value`: Default value to return if the parameter is not found or not a string

**Return Value**:
- Returns the parameter value, or default_value if not found or not a string

#### kmcp_tool_get_int_param

```c
int kmcp_tool_get_int_param(
    const mcp_json_t* params,
    const char* key,
    int default_value
);
```

Get tool parameter as integer.

**Parameters**:
- `params`: Tool parameters as a JSON object (must not be NULL)
- `key`: Parameter key (must not be NULL)
- `default_value`: Default value to return if the parameter is not found or not an integer

**Return Value**:
- Returns the parameter value, or default_value if not found or not an integer

#### kmcp_tool_get_bool_param

```c
bool kmcp_tool_get_bool_param(
    const mcp_json_t* params,
    const char* key,
    bool default_value
);
```

Get tool parameter as boolean.

**Parameters**:
- `params`: Tool parameters as a JSON object (must not be NULL)
- `key`: Parameter key (must not be NULL)
- `default_value`: Default value to return if the parameter is not found or not a boolean

**Return Value**:
- Returns the parameter value, or default_value if not found or not a boolean

#### kmcp_tool_get_number_param

```c
double kmcp_tool_get_number_param(
    const mcp_json_t* params,
    const char* key,
    double default_value
);
```

Get tool parameter as number.

**Parameters**:
- `params`: Tool parameters as a JSON object (must not be NULL)
- `key`: Parameter key (must not be NULL)
- `default_value`: Default value to return if the parameter is not found or not a number

**Return Value**:
- Returns the parameter value, or default_value if not found or not a number

#### kmcp_tool_get_object_param

```c
const mcp_json_t* kmcp_tool_get_object_param(
    const mcp_json_t* params,
    const char* key
);
```

Get tool parameter as object.

**Parameters**:
- `params`: Tool parameters as a JSON object (must not be NULL)
- `key`: Parameter key (must not be NULL)

**Return Value**:
- Returns the parameter value, or NULL if not found or not an object

#### kmcp_tool_get_array_param

```c
const mcp_json_t* kmcp_tool_get_array_param(
    const mcp_json_t* params,
    const char* key
);
```

Get tool parameter as array.

**Parameters**:
- `params`: Tool parameters as a JSON object (must not be NULL)
- `key`: Parameter key (must not be NULL)

**Return Value**:
- Returns the parameter value, or NULL if not found or not an array

#### kmcp_tool_create_success_result

```c
mcp_json_t* kmcp_tool_create_success_result(const char* message);
```

Create a success result.

**Parameters**:
- `message`: Success message (optional, can be NULL)

**Return Value**:
- Returns a new JSON object representing a success result

#### kmcp_tool_create_error_result

```c
mcp_json_t* kmcp_tool_create_error_result(const char* message, int error_code);
```

Create an error result.

**Parameters**:
- `message`: Error message (must not be NULL)
- `error_code`: Error code

**Return Value**:
- Returns a new JSON object representing an error result

#### kmcp_tool_create_data_result

```c
mcp_json_t* kmcp_tool_create_data_result(const mcp_json_t* data);
```

Create a data result.

**Parameters**:
- `data`: Result data as a JSON object (must not be NULL)

**Return Value**:
- Returns a new JSON object representing a data result
