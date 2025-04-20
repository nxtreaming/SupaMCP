# KMCP Profile Management Guide

This guide explains how to use the KMCP profile management functionality to manage multiple server configurations.

## Overview

KMCP profiles allow you to create and manage named collections of server configurations. This is useful for:

- Managing multiple server environments (development, testing, production)
- Switching between different server configurations
- Sharing server configurations between team members
- Creating backups of server configurations

## Basic Concepts

- **Profile**: A named collection of server configurations
- **Active Profile**: The currently active profile that is used for server operations
- **Server Configuration**: Configuration for an MCP server, including command, arguments, and environment variables for local servers, or URL for remote servers

## Using Profiles

### Creating a Profile

```c
#include "kmcp.h"
#include "kmcp_profile_manager.h"

// Create a profile manager
kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
if (!manager) {
    // Handle error
    return 1;
}

// Create a profile
kmcp_error_t result = kmcp_profile_create(manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_profile_manager_close(manager);
    return 1;
}

// Add a server to the profile
kmcp_server_config_t server_config;
memset(&server_config, 0, sizeof(server_config));
server_config.name = strdup("local");
server_config.command = strdup("mcp_server");
server_config.args_count = 3;
server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
server_config.args[0] = strdup("--tcp");
server_config.args[1] = strdup("--port");
server_config.args[2] = strdup("8080");

result = kmcp_profile_add_server(manager, "development", &server_config);

// Free server configuration
free(server_config.name);
free(server_config.command);
for (size_t i = 0; i < server_config.args_count; i++) {
    free(server_config.args[i]);
}
free(server_config.args);

if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_profile_manager_close(manager);
    return 1;
}

// Activate the profile
result = kmcp_profile_activate(manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_profile_manager_close(manager);
    return 1;
}

// Use the profile...

// Close the profile manager
kmcp_profile_manager_close(manager);
```

### Managing Profiles

#### Creating Multiple Profiles

```c
// Create multiple profiles
kmcp_profile_create(manager, "development");
kmcp_profile_create(manager, "testing");
kmcp_profile_create(manager, "production");
```

#### Checking if a Profile Exists

```c
// Check if a profile exists
bool exists = kmcp_profile_exists(manager, "development");
if (exists) {
    // Profile exists
} else {
    // Profile does not exist
}
```

#### Getting the Number of Profiles

```c
// Get the number of profiles
size_t count = kmcp_profile_get_count(manager);
printf("Number of profiles: %zu\n", count);
```

#### Getting Profile Names

```c
// Get profile names
char** names = NULL;
size_t count = 0;
kmcp_error_t result = kmcp_profile_get_names(manager, &names, &count);
if (result == KMCP_SUCCESS) {
    for (size_t i = 0; i < count; i++) {
        printf("Profile %zu: %s\n", i, names[i]);
        free(names[i]);
    }
    free(names);
}
```

#### Renaming a Profile

```c
// Rename a profile
kmcp_error_t result = kmcp_profile_rename(manager, "development", "dev");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Deleting a Profile

```c
// Delete a profile
kmcp_error_t result = kmcp_profile_delete(manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

### Working with Servers in Profiles

#### Adding a Server to a Profile

```c
// Add a local server to a profile
kmcp_server_config_t local_server;
memset(&local_server, 0, sizeof(local_server));
local_server.name = strdup("local");
local_server.command = strdup("mcp_server");
local_server.args_count = 3;
local_server.args = (char**)malloc(local_server.args_count * sizeof(char*));
local_server.args[0] = strdup("--tcp");
local_server.args[1] = strdup("--port");
local_server.args[2] = strdup("8080");

kmcp_error_t result = kmcp_profile_add_server(manager, "development", &local_server);

// Free server configuration
free(local_server.name);
free(local_server.command);
for (size_t i = 0; i < local_server.args_count; i++) {
    free(local_server.args[i]);
}
free(local_server.args);

// Add a remote server to a profile
kmcp_server_config_t remote_server;
memset(&remote_server, 0, sizeof(remote_server));
remote_server.name = strdup("remote");
remote_server.url = strdup("http://example.com:8080");

result = kmcp_profile_add_server(manager, "development", &remote_server);

// Free server configuration
free(remote_server.name);
free(remote_server.url);
```

#### Removing a Server from a Profile

```c
// Remove a server from a profile
kmcp_error_t result = kmcp_profile_remove_server(manager, "development", "local");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Copying a Server Between Profiles

```c
// Copy a server from one profile to another
kmcp_error_t result = kmcp_profile_copy_server(
    manager,
    "development",  // Source profile
    "local",        // Source server
    "testing",      // Target profile
    "local-copy"    // Target server (new name)
);
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Moving a Server Between Profiles

```c
// Move a server from one profile to another
kmcp_error_t result = kmcp_profile_move_server(
    manager,
    "development",  // Source profile
    "local",        // Source server
    "testing",      // Target profile
    NULL            // Target server (use same name)
);
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

### Profile Activation and Deactivation

#### Activating a Profile

```c
// Activate a profile
kmcp_error_t result = kmcp_profile_activate(manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Deactivating a Profile

```c
// Deactivate a profile
kmcp_error_t result = kmcp_profile_deactivate(manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Getting the Active Profile

```c
// Get the active profile
const char* active_profile = kmcp_profile_get_active(manager);
if (active_profile) {
    printf("Active profile: %s\n", active_profile);
} else {
    printf("No active profile\n");
}
```

### Profile Persistence

#### Saving Profiles to a File

```c
// Save profiles to a file
kmcp_error_t result = kmcp_profile_save(manager, "profiles.json");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Loading Profiles from a File

```c
// Load profiles from a file
kmcp_error_t result = kmcp_profile_load(manager, "profiles.json");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Exporting a Profile to a File

```c
// Export a profile to a file
kmcp_error_t result = kmcp_profile_export(manager, "development", "development.json");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

#### Importing a Profile from a File

```c
// Import a profile from a file
kmcp_error_t result = kmcp_profile_import(manager, "development.json", "imported-dev");
if (result != KMCP_SUCCESS) {
    // Handle error
}
```

### Integration with KMCP Client

The KMCP client can be configured to use a profile manager for server selection:

```c
#include "kmcp.h"
#include "kmcp_profile_manager.h"

// Create a profile manager
kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
if (!manager) {
    // Handle error
    return 1;
}

// Create and configure profiles...

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
    kmcp_profile_manager_close(manager);
    return 1;
}

// Free client configuration
free(config.name);
free(config.version);

// Get the server manager from the client
kmcp_server_manager_t* server_manager = kmcp_client_get_manager(client);
if (!server_manager) {
    // Handle error
    kmcp_client_close(client);
    kmcp_profile_manager_close(manager);
    return 1;
}

// Activate a profile
kmcp_error_t result = kmcp_profile_activate(manager, "development");
if (result != KMCP_SUCCESS) {
    // Handle error
    kmcp_client_close(client);
    kmcp_profile_manager_close(manager);
    return 1;
}

// Get the server manager from the active profile
kmcp_server_manager_t* profile_server_manager = kmcp_profile_get_server_manager(manager, "development");
if (!profile_server_manager) {
    // Handle error
    kmcp_client_close(client);
    kmcp_profile_manager_close(manager);
    return 1;
}

// Use the client to call tools...

// Close the client and profile manager
kmcp_client_close(client);
kmcp_profile_manager_close(manager);
```

## Best Practices

1. **Use Descriptive Profile Names**: Use descriptive names for profiles to make it clear what each profile is for.
2. **Create Separate Profiles for Different Environments**: Create separate profiles for development, testing, and production environments.
3. **Use Profile Export/Import for Sharing**: Use profile export and import to share profiles between team members.
4. **Regularly Save Profiles**: Regularly save profiles to a file to avoid losing configuration changes.
5. **Check for Profile Existence**: Always check if a profile exists before trying to use it.
6. **Handle Errors**: Always check for errors when calling profile management functions.
7. **Free Resources**: Always free resources when they are no longer needed.
