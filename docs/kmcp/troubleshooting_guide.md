# KMCP Troubleshooting Guide

This guide provides solutions for common issues encountered when using KMCP.

## Common Error Codes

| Error Code | Description | Possible Causes | Solutions |
|------------|-------------|-----------------|-----------|
| `KMCP_ERROR_INVALID_PARAMETER` | Invalid parameter | Missing or invalid parameter | Check parameter values and ensure they are valid |
| `KMCP_ERROR_MEMORY` | Memory allocation failed | Insufficient memory | Free unused resources or increase available memory |
| `KMCP_ERROR_IO` | I/O error | File not found, permission denied, etc. | Check file paths, permissions, and disk space |
| `KMCP_ERROR_NETWORK` | Network error | Connection failed, timeout, etc. | Check network connectivity, server status, and firewall settings |
| `KMCP_ERROR_SERVER` | Server error | Server internal error | Check server logs and configuration |
| `KMCP_ERROR_TOOL` | Tool error | Tool execution failed | Check tool parameters and server logs |
| `KMCP_ERROR_NOT_FOUND` | Resource not found | Profile, server, or tool not found | Check resource names and existence |
| `KMCP_ERROR_ALREADY_EXISTS` | Resource already exists | Profile or server already exists | Use a different name or update the existing resource |
| `KMCP_ERROR_TIMEOUT` | Operation timed out | Server not responding, network congestion | Increase timeout, check server status, or retry later |
| `KMCP_ERROR_CANCELLED` | Operation cancelled | User cancelled the operation | No action needed |
| `KMCP_ERROR_INVALID_STATE` | Invalid state | Operation not allowed in current state | Check the state of the resource before performing the operation |
| `KMCP_ERROR_PERMISSION` | Permission denied | Insufficient permissions | Check user permissions and access control settings |
| `KMCP_ERROR_INVALID_CONTEXT` | Invalid context | Context not available or invalid | Ensure the operation is performed in the correct context |
| `KMCP_ERROR_UNSUPPORTED` | Unsupported operation | Operation not supported by the server or tool | Check server or tool capabilities |
| `KMCP_ERROR_UNKNOWN` | Unknown error | Unspecified error | Check logs for more information |

## Client Issues

### Connection Issues

**Problem**: Unable to connect to the server.

**Possible Causes**:
- Server is not running
- Incorrect server URL
- Network connectivity issues
- Firewall blocking the connection

**Solutions**:
1. Verify that the server is running:
   ```c
   // Check if the server is running
   bool is_running = kmcp_server_is_running(server);
   if (!is_running) {
       // Start the server
       kmcp_error_t result = kmcp_server_start(server);
       if (result != KMCP_SUCCESS) {
           printf("Failed to start server: %s\n", kmcp_error_message(result));
       }
   }
   ```

2. Check the server URL:
   ```c
   // Print server URL
   kmcp_server_config_t config;
   kmcp_server_manager_get_server(manager, "server-name", &config);
   printf("Server URL: %s\n", config.url);
   ```

3. Test network connectivity:
   ```c
   // Test network connectivity
   kmcp_http_client_t* client = kmcp_http_client_create(config.url);
   if (!client) {
       printf("Failed to create HTTP client\n");
   } else {
       char* response = NULL;
       char* content_type = NULL;
       kmcp_error_t result = kmcp_http_client_send(client, "GET", "/", NULL, NULL, 0, &response, &content_type);
       if (result != KMCP_SUCCESS) {
           printf("Failed to connect to server: %s\n", kmcp_error_message(result));
       } else {
           printf("Connected to server successfully\n");
           free(response);
           free(content_type);
       }
       kmcp_http_client_destroy(client);
   }
   ```

4. Check firewall settings and ensure that the server port is open.

### Tool Execution Issues

**Problem**: Tool execution fails.

**Possible Causes**:
- Tool not found on the server
- Invalid tool parameters
- Tool execution error
- Server error

**Solutions**:
1. Check if the tool exists on the server:
   ```c
   // Get available tools
   char** tools = NULL;
   size_t count = 0;
   kmcp_error_t result = kmcp_client_get_tools(client, &tools, &count);
   if (result != KMCP_SUCCESS) {
       printf("Failed to get tools: %s\n", kmcp_error_message(result));
   } else {
       bool found = false;
       for (size_t i = 0; i < count; i++) {
           if (strcmp(tools[i], "tool-name") == 0) {
               found = true;
               break;
           }
           free(tools[i]);
       }
       free(tools);
       
       if (!found) {
           printf("Tool 'tool-name' not found on the server\n");
       }
   }
   ```

2. Check tool parameters:
   ```c
   // Create valid tool parameters
   mcp_json_t* params = mcp_json_object();
   mcp_json_object_set_string(params, "param1", "value1");
   mcp_json_object_set_int(params, "param2", 42);
   
   // Convert to JSON string
   char* params_json = mcp_json_to_string(params);
   mcp_json_free(params);
   
   // Call the tool
   char* result_json = NULL;
   kmcp_error_t result = kmcp_client_call_tool(client, "tool-name", params_json, &result_json);
   free(params_json);
   
   if (result != KMCP_SUCCESS) {
       printf("Failed to call tool: %s\n", kmcp_error_message(result));
   } else {
       // Parse the result
       mcp_json_t* result_obj = mcp_json_parse(result_json);
       free(result_json);
       
       // Check for error
       const char* error = mcp_json_object_get_string(result_obj, "error");
       if (error) {
           printf("Tool execution error: %s\n", error);
       }
       
       mcp_json_free(result_obj);
   }
   ```

3. Check server logs for more information about the error.

### Resource Access Issues

**Problem**: Unable to access resources.

**Possible Causes**:
- Resource not found
- Insufficient permissions
- Network connectivity issues

**Solutions**:
1. Check if the resource exists:
   ```c
   // Get available resources
   char** resources = NULL;
   size_t count = 0;
   kmcp_error_t result = kmcp_client_get_resources(client, &resources, &count);
   if (result != KMCP_SUCCESS) {
       printf("Failed to get resources: %s\n", kmcp_error_message(result));
   } else {
       bool found = false;
       for (size_t i = 0; i < count; i++) {
           if (strcmp(resources[i], "resource-uri") == 0) {
               found = true;
               break;
           }
           free(resources[i]);
       }
       free(resources);
       
       if (!found) {
           printf("Resource 'resource-uri' not found on the server\n");
       }
   }
   ```

2. Check permissions:
   ```c
   // Get resource
   char* content = NULL;
   char* content_type = NULL;
   kmcp_error_t result = kmcp_client_get_resource(client, "resource-uri", &content, &content_type);
   if (result == KMCP_ERROR_PERMISSION) {
       printf("Permission denied for resource 'resource-uri'\n");
   } else if (result != KMCP_SUCCESS) {
       printf("Failed to get resource: %s\n", kmcp_error_message(result));
   } else {
       printf("Resource content type: %s\n", content_type);
       printf("Resource content: %s\n", content);
       free(content);
       free(content_type);
   }
   ```

3. Check network connectivity as described in the Connection Issues section.

## Server Issues

### Server Startup Issues

**Problem**: Server fails to start.

**Possible Causes**:
- Port already in use
- Insufficient permissions
- Configuration errors
- Missing dependencies

**Solutions**:
1. Check if the port is already in use:
   ```c
   // Check if the port is in use
   kmcp_server_config_t config;
   memset(&config, 0, sizeof(config));
   config.name = strdup("test-server");
   config.command = strdup("mcp_server");
   config.args_count = 3;
   config.args = (char**)malloc(config.args_count * sizeof(char*));
   config.args[0] = strdup("--tcp");
   config.args[1] = strdup("--port");
   config.args[2] = strdup("8080");
   
   kmcp_server_manager_t* manager = kmcp_server_manager_create();
   kmcp_error_t result = kmcp_server_manager_add_server(manager, &config);
   
   // Free config
   free(config.name);
   free(config.command);
   for (size_t i = 0; i < config.args_count; i++) {
       free(config.args[i]);
   }
   free(config.args);
   
   if (result != KMCP_SUCCESS) {
       printf("Failed to add server: %s\n", kmcp_error_message(result));
   } else {
       result = kmcp_server_start(manager, "test-server");
       if (result == KMCP_ERROR_IO) {
           printf("Port may be already in use\n");
       } else if (result != KMCP_SUCCESS) {
           printf("Failed to start server: %s\n", kmcp_error_message(result));
       }
   }
   
   kmcp_server_manager_destroy(manager);
   ```

2. Check permissions:
   - Ensure that the user has permission to bind to the port
   - On Unix-like systems, ports below 1024 require root privileges

3. Check configuration:
   - Verify that the server configuration is correct
   - Check command-line arguments and environment variables

4. Check dependencies:
   - Ensure that all required dependencies are installed
   - Check for missing DLLs or shared libraries

### Server Crash Issues

**Problem**: Server crashes during operation.

**Possible Causes**:
- Memory leaks
- Segmentation faults
- Unhandled exceptions
- Resource exhaustion

**Solutions**:
1. Check server logs for error messages.

2. Monitor server resource usage:
   - CPU usage
   - Memory usage
   - Disk usage
   - Network usage

3. Use debugging tools:
   - Debugger (GDB, WinDbg, etc.)
   - Memory profiler (Valgrind, etc.)
   - Crash dump analysis

4. Implement proper error handling in server code.

## Profile Management Issues

### Profile Creation Issues

**Problem**: Unable to create a profile.

**Possible Causes**:
- Profile already exists
- Invalid profile name
- Insufficient permissions
- Memory allocation failure

**Solutions**:
1. Check if the profile already exists:
   ```c
   // Check if the profile exists
   bool exists = kmcp_profile_exists(manager, "profile-name");
   if (exists) {
       printf("Profile 'profile-name' already exists\n");
   } else {
       // Create the profile
       kmcp_error_t result = kmcp_profile_create(manager, "profile-name");
       if (result != KMCP_SUCCESS) {
           printf("Failed to create profile: %s\n", kmcp_error_message(result));
       }
   }
   ```

2. Check profile name:
   - Ensure that the profile name is valid
   - Avoid special characters and spaces

3. Check permissions:
   - Ensure that the user has permission to create profiles
   - Check file system permissions for profile storage

4. Check memory allocation:
   - Ensure that there is sufficient memory available
   - Free unused resources

### Profile Loading Issues

**Problem**: Unable to load profiles from a file.

**Possible Causes**:
- File not found
- Invalid file format
- Insufficient permissions
- Corrupted file

**Solutions**:
1. Check if the file exists:
   ```c
   // Check if the file exists
   FILE* file = fopen("profiles.json", "r");
   if (!file) {
       printf("File 'profiles.json' not found\n");
   } else {
       fclose(file);
       
       // Load profiles
       kmcp_error_t result = kmcp_profile_load(manager, "profiles.json");
       if (result != KMCP_SUCCESS) {
           printf("Failed to load profiles: %s\n", kmcp_error_message(result));
       }
   }
   ```

2. Check file format:
   - Ensure that the file is a valid JSON file
   - Check for syntax errors

3. Check permissions:
   - Ensure that the user has permission to read the file
   - Check file system permissions

4. Check for file corruption:
   - Verify file integrity
   - Try loading a backup file

## Registry Issues

### Registry Connection Issues

**Problem**: Unable to connect to the registry.

**Possible Causes**:
- Registry server is not running
- Incorrect registry URL
- Network connectivity issues
- Firewall blocking the connection

**Solutions**:
1. Check if the registry server is running.

2. Check the registry URL:
   ```c
   // Print registry URL
   kmcp_registry_config_t config;
   config.registry_url = "https://registry.example.com";
   printf("Registry URL: %s\n", config.registry_url);
   ```

3. Test network connectivity:
   ```c
   // Test network connectivity
   kmcp_registry_t* registry = kmcp_registry_create("https://registry.example.com");
   if (!registry) {
       printf("Failed to create registry connection\n");
   } else {
       kmcp_server_info_t* servers = NULL;
       size_t count = 0;
       kmcp_error_t result = kmcp_registry_get_servers(registry, &servers, &count);
       if (result != KMCP_SUCCESS) {
           printf("Failed to connect to registry: %s\n", kmcp_error_message(result));
       } else {
           printf("Connected to registry successfully\n");
           kmcp_registry_free_server_info_array(servers, count);
       }
       kmcp_registry_close(registry);
   }
   ```

4. Check firewall settings and ensure that the registry port is open.

### Server Discovery Issues

**Problem**: Unable to discover servers from the registry.

**Possible Causes**:
- No servers registered in the registry
- Search query too restrictive
- Registry cache outdated
- Network connectivity issues

**Solutions**:
1. Check if there are any servers in the registry:
   ```c
   // Get all servers
   kmcp_server_info_t* servers = NULL;
   size_t count = 0;
   kmcp_error_t result = kmcp_registry_get_servers(registry, &servers, &count);
   if (result != KMCP_SUCCESS) {
       printf("Failed to get servers: %s\n", kmcp_error_message(result));
   } else {
       printf("Found %zu servers\n", count);
       kmcp_registry_free_server_info_array(servers, count);
   }
   ```

2. Try a less restrictive search query:
   ```c
   // Search for servers with a less restrictive query
   kmcp_server_info_t* servers = NULL;
   size_t count = 0;
   kmcp_error_t result = kmcp_registry_search_servers(registry, "", &servers, &count);
   if (result != KMCP_SUCCESS) {
       printf("Failed to search servers: %s\n", kmcp_error_message(result));
   } else {
       printf("Found %zu servers\n", count);
       kmcp_registry_free_server_info_array(servers, count);
   }
   ```

3. Refresh the registry cache:
   ```c
   // Refresh the registry cache
   kmcp_error_t result = kmcp_registry_refresh_cache(registry);
   if (result != KMCP_SUCCESS) {
       printf("Failed to refresh registry cache: %s\n", kmcp_error_message(result));
   }
   ```

4. Check network connectivity as described in the Connection Issues section.

## Tool SDK Issues

### Tool Registration Issues

**Problem**: Unable to register a tool.

**Possible Causes**:
- Tool already registered
- Invalid tool metadata
- Invalid tool callbacks
- Memory allocation failure

**Solutions**:
1. Check if the tool is already registered:
   ```c
   // Try to unregister the tool first
   kmcp_tool_unregister("tool-name");
   
   // Register the tool
   kmcp_error_t result = kmcp_tool_register(&metadata, &callbacks);
   if (result != KMCP_SUCCESS) {
       printf("Failed to register tool: %s\n", kmcp_error_message(result));
   }
   ```

2. Check tool metadata:
   - Ensure that all required fields are set
   - Check for invalid values

3. Check tool callbacks:
   - Ensure that all required callbacks are set
   - Check for NULL function pointers

4. Check memory allocation:
   - Ensure that there is sufficient memory available
   - Free unused resources

### Tool Execution Issues

**Problem**: Tool execution fails.

**Possible Causes**:
- Invalid parameters
- Memory allocation failure
- Unhandled exceptions
- Resource exhaustion

**Solutions**:
1. Validate parameters:
   ```c
   // Validate parameters
   const char* text = kmcp_tool_get_string_param(params, "text", NULL);
   if (!text) {
       *result = kmcp_tool_create_error_result("Missing 'text' parameter", KMCP_ERROR_INVALID_PARAMETER);
       return KMCP_SUCCESS;
   }
   
   int count = kmcp_tool_get_int_param(params, "count", 1);
   if (count < 1) {
       *result = kmcp_tool_create_error_result("'count' must be at least 1", KMCP_ERROR_INVALID_PARAMETER);
       return KMCP_SUCCESS;
   }
   ```

2. Handle memory allocation failures:
   ```c
   // Allocate memory
   char* output = malloc(strlen(text) * count + 1);
   if (!output) {
       *result = kmcp_tool_create_error_result("Memory allocation failed", KMCP_ERROR_MEMORY);
       return KMCP_SUCCESS;
   }
   ```

3. Implement proper error handling:
   ```c
   // Try to perform an operation
   if (some_operation() != 0) {
       *result = kmcp_tool_create_error_result("Operation failed", KMCP_ERROR_UNKNOWN);
       return KMCP_SUCCESS;
   }
   ```

4. Check for resource exhaustion:
   - Monitor memory usage
   - Monitor CPU usage
   - Monitor disk usage
   - Monitor network usage

## General Troubleshooting Tips

1. **Check Logs**: Always check logs for error messages and warnings.

2. **Enable Verbose Logging**: Enable verbose logging to get more detailed information:
   ```c
   // Enable verbose logging
   kmcp_set_log_level(KMCP_LOG_LEVEL_DEBUG);
   ```

3. **Check Error Codes**: Always check error codes returned by functions:
   ```c
   // Check error code
   kmcp_error_t result = some_function();
   if (result != KMCP_SUCCESS) {
       printf("Function failed: %s\n", kmcp_error_message(result));
   }
   ```

4. **Use Debugging Tools**: Use debugging tools to diagnose issues:
   - Debugger (GDB, WinDbg, etc.)
   - Memory profiler (Valgrind, etc.)
   - Network analyzer (Wireshark, etc.)

5. **Check System Resources**: Check system resources to ensure that there are sufficient resources available:
   - Memory
   - CPU
   - Disk space
   - Network bandwidth

6. **Update Software**: Ensure that you are using the latest version of KMCP and its dependencies.

7. **Check Documentation**: Refer to the documentation for correct usage and configuration.

8. **Seek Help**: If you cannot resolve the issue, seek help from the KMCP community or support team.
