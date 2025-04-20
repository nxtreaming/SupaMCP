# KMCP Registry Guide

This guide explains how to use the KMCP registry functionality to discover and use MCP servers.

## Overview

The KMCP registry is a central repository of MCP servers that allows clients to discover and connect to servers. The registry provides the following features:

- Server discovery: Find available MCP servers
- Server search: Search for servers by name, capabilities, or other criteria
- Server information: Get detailed information about servers
- Server integration: Add servers from the registry to a server manager

## Basic Concepts

- **Registry**: A central repository of MCP servers
- **Server Information**: Metadata about an MCP server, including ID, name, URL, capabilities, and supported tools
- **Server Discovery**: The process of finding available MCP servers
- **Server Integration**: The process of adding a server from the registry to a server manager

## Using the Registry

### Connecting to a Registry

```c
#include "kmcp.h"
#include "kmcp_registry.h"

// Connect to a registry with default configuration
kmcp_registry_t* registry = kmcp_registry_create("https://registry.example.com");
if (!registry) {
    // Handle error
    return 1;
}

// Connect to a registry with custom configuration
kmcp_registry_config_t config;
config.registry_url = "https://registry.example.com";
config.api_key = "your-api-key";  // Optional
config.cache_ttl_seconds = 300;    // 5 minutes
config.connect_timeout_ms = 5000;  // 5 seconds
config.request_timeout_ms = 10000; // 10 seconds
config.max_retries = 3;

kmcp_registry_t* registry_custom = kmcp_registry_create_with_config(&config);
if (!registry_custom) {
    // Handle error
    return 1;
}

// Use the registry...

// Close the registry connections
kmcp_registry_close(registry);
kmcp_registry_close(registry_custom);
```

### Discovering Servers

#### Getting All Servers

```c
// Get all servers from the registry
kmcp_server_info_t* servers = NULL;
size_t count = 0;
kmcp_error_t result = kmcp_registry_get_servers(registry, &servers, &count);
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_registry_close(registry);
    return 1;
}

// Process the servers
for (size_t i = 0; i < count; i++) {
    printf("Server %zu: %s (%s)\n", i, servers[i].name, servers[i].url);
    
    // Print capabilities
    printf("  Capabilities (%zu):\n", servers[i].capabilities_count);
    for (size_t j = 0; j < servers[i].capabilities_count; j++) {
        printf("    %s\n", servers[i].capabilities[j]);
    }
    
    // Print tools
    printf("  Tools (%zu):\n", servers[i].tools_count);
    for (size_t j = 0; j < servers[i].tools_count; j++) {
        printf("    %s\n", servers[i].tools[j]);
    }
    
    // Print resources
    printf("  Resources (%zu):\n", servers[i].resources_count);
    for (size_t j = 0; j < servers[i].resources_count; j++) {
        printf("    %s\n", servers[i].resources[j]);
    }
}

// Free the server information
kmcp_registry_free_server_info_array(servers, count);
```

#### Searching for Servers

```c
// Search for servers by query
kmcp_server_info_t* servers = NULL;
size_t count = 0;
kmcp_error_t result = kmcp_registry_search_servers(registry, "ai", &servers, &count);
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_registry_close(registry);
    return 1;
}

// Process the servers
for (size_t i = 0; i < count; i++) {
    printf("Server %zu: %s (%s)\n", i, servers[i].name, servers[i].url);
}

// Free the server information
kmcp_registry_free_server_info_array(servers, count);
```

#### Getting Detailed Server Information

```c
// Get detailed information about a server
kmcp_server_info_t* server_info = NULL;
kmcp_error_t result = kmcp_registry_get_server_info(registry, "server-id", &server_info);
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_registry_close(registry);
    return 1;
}

// Process the server information
printf("Server: %s (%s)\n", server_info->name, server_info->url);
printf("Description: %s\n", server_info->description);
printf("Version: %s\n", server_info->version);
printf("Public: %s\n", server_info->is_public ? "Yes" : "No");
printf("Last seen: %s\n", ctime(&server_info->last_seen));

// Free the server information
kmcp_registry_free_server_info(server_info);
```

### Integrating Servers

#### Adding a Server to a Server Manager

```c
// Create a server manager
kmcp_server_manager_t* manager = kmcp_server_manager_create();
if (!manager) {
    // Handle error
    kmcp_registry_close(registry);
    return 1;
}

// Add a server from the registry to the server manager by ID
kmcp_error_t result = kmcp_registry_add_server(registry, manager, "server-id");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_server_manager_destroy(manager);
    kmcp_registry_close(registry);
    return 1;
}

// Add a server from the registry to the server manager by URL
result = kmcp_registry_add_server_by_url(registry, manager, "https://server.example.com");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_server_manager_destroy(manager);
    kmcp_registry_close(registry);
    return 1;
}

// Use the server manager...

// Destroy the server manager
kmcp_server_manager_destroy(manager);
```

### Refreshing the Cache

```c
// Refresh the registry cache
kmcp_error_t result = kmcp_registry_refresh_cache(registry);
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_registry_close(registry);
    return 1;
}
```

## Integration with KMCP Client

The KMCP client can be configured to use a registry for server discovery:

```c
#include "kmcp.h"
#include "kmcp_registry.h"

// Connect to a registry
kmcp_registry_t* registry = kmcp_registry_create("https://registry.example.com");
if (!registry) {
    // Handle error
    return 1;
}

// Create a client configuration
kmcp_client_config_t config;
config.name = strdup("my-client");
config.version = strdup("1.0.0");
config.use_manager = true;
config.timeout_ms = 30000;

// Create a client
kmcp_client_t* client = kmcp_client_create(&config);
if (!client) {
    // Handle error
    free(config.name);
    free(config.version);
    kmcp_registry_close(registry);
    return 1;
}

// Free client configuration
free(config.name);
free(config.version);

// Get the server manager from the client
kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
if (!manager) {
    // Handle error
    kmcp_client_close(client);
    kmcp_registry_close(registry);
    return 1;
}

// Add a server from the registry to the server manager
kmcp_error_t result = kmcp_registry_add_server(registry, manager, "server-id");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_client_close(client);
    kmcp_registry_close(registry);
    return 1;
}

// Use the client to call tools...

// Close the client and registry
kmcp_client_close(client);
kmcp_registry_close(registry);
```

## Integration with Profile Management

The registry can be used with profile management to create profiles with servers from the registry:

```c
#include "kmcp.h"
#include "kmcp_registry.h"
#include "kmcp_profile_manager.h"

// Connect to a registry
kmcp_registry_t* registry = kmcp_registry_create("https://registry.example.com");
if (!registry) {
    // Handle error
    return 1;
}

// Create a profile manager
kmcp_profile_manager_t* profile_manager = kmcp_profile_manager_create();
if (!profile_manager) {
    // Handle error
    kmcp_registry_close(registry);
    return 1;
}

// Create a profile
kmcp_error_t result = kmcp_profile_create(profile_manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_profile_manager_close(profile_manager);
    kmcp_registry_close(registry);
    return 1;
}

// Get the server manager for the profile
kmcp_server_manager_t* manager = kmcp_profile_get_server_manager(profile_manager, "development");
if (!manager) {
    // Handle error
    kmcp_profile_manager_close(profile_manager);
    kmcp_registry_close(registry);
    return 1;
}

// Add a server from the registry to the profile's server manager
result = kmcp_registry_add_server(registry, manager, "server-id");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_profile_manager_close(profile_manager);
    kmcp_registry_close(registry);
    return 1;
}

// Use the profile...

// Close the profile manager and registry
kmcp_profile_manager_close(profile_manager);
kmcp_registry_close(registry);
```

## Best Practices

1. **Use Cache TTL**: Set an appropriate cache TTL to balance between performance and freshness of data.
2. **Handle Connection Errors**: Always handle connection errors when working with the registry.
3. **Refresh Cache When Needed**: Refresh the cache when you need the most up-to-date information.
4. **Free Server Information**: Always free server information when it's no longer needed.
5. **Use Search for Large Registries**: Use search instead of getting all servers for large registries.
6. **Check Server Capabilities**: Check server capabilities before adding a server to ensure it supports the features you need.
7. **Use Server IDs**: Use server IDs instead of URLs when possible, as they are more stable.
