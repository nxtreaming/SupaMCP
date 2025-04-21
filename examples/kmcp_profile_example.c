/**
 * @file kmcp_profile_example.c
 * @brief Example application for KMCP profile manager
 */

#include "kmcp.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(x) Sleep((x) * 1000)
#else
#include <unistd.h>
#endif

/**
 * @brief Print a separator line
 */
static void print_separator(void) {
    printf("\n----------------------------------------\n");
}

/**
 * @brief Print profile information
 */
static void print_profile_info(kmcp_profile_manager_t* manager, const char* profile_name) {
    printf("Profile: %s\n", profile_name);
    
    // Check if profile is active
    const char* active_profile = kmcp_profile_get_active(manager);
    if (active_profile && strcmp(active_profile, profile_name) == 0) {
        printf("  Status: Active\n");
    } else {
        printf("  Status: Inactive\n");
    }
    
    // Get server manager for profile
    kmcp_server_manager_t* server_manager = kmcp_profile_get_server_manager(manager, profile_name);
    if (!server_manager) {
        printf("  Failed to get server manager\n");
        return;
    }
    
    // Get server count
    size_t server_count = kmcp_server_get_count(server_manager);
    printf("  Servers: %zu\n", server_count);
    
    // Print server information
    for (size_t i = 0; i < server_count; i++) {
        kmcp_server_config_t* config = NULL;
        kmcp_error_t result = kmcp_server_get_config_by_index(server_manager, i, &config);
        if (result != KMCP_SUCCESS || !config) {
            printf("    Failed to get server configuration at index %zu\n", i);
            continue;
        }
        
        printf("    Server: %s\n", config->name);
        if (config->is_http) {
            printf("      Type: HTTP\n");
            printf("      URL: %s\n", config->url ? config->url : "");
            if (config->api_key) {
                printf("      API Key: %s\n", config->api_key);
            }
        } else {
            printf("      Type: Local Process\n");
            printf("      Command: %s\n", config->command ? config->command : "");
            
            if (config->args && config->args_count > 0) {
                printf("      Arguments:\n");
                for (size_t j = 0; j < config->args_count; j++) {
                    if (config->args[j]) {
                        printf("        %s\n", config->args[j]);
                    }
                }
            }
            
            if (config->env && config->env_count > 0) {
                printf("      Environment Variables:\n");
                for (size_t j = 0; j < config->env_count; j++) {
                    if (config->env[j]) {
                        printf("        %s\n", config->env[j]);
                    }
                }
            }
        }
        
        kmcp_server_config_free(config);
    }
}

/**
 * @brief Print all profiles
 */
static void print_all_profiles(kmcp_profile_manager_t* manager) {
    // Get profile count
    size_t profile_count = kmcp_profile_get_count(manager);
    printf("Total profiles: %zu\n", profile_count);
    
    // Get profile names
    char** profile_names = NULL;
    size_t count = 0;
    kmcp_error_t result = kmcp_profile_get_names(manager, &profile_names, &count);
    if (result != KMCP_SUCCESS || !profile_names) {
        printf("Failed to get profile names\n");
        return;
    }
    
    // Print profile information
    for (size_t i = 0; i < count; i++) {
        print_separator();
        print_profile_info(manager, profile_names[i]);
        
        // Free profile name
        free(profile_names[i]);
    }
    
    // Free profile names array
    free(profile_names);
}

/**
 * @brief Example 1: Create and manage profiles
 */
static void example_create_manage_profiles(void) {
    printf("Example 1: Create and manage profiles\n");
    print_separator();
    
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return;
    }
    
    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "development");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create development profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    result = kmcp_profile_create(manager, "production");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create production profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    result = kmcp_profile_create(manager, "testing");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create testing profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Create server configurations
    kmcp_server_config_t dev_server;
    memset(&dev_server, 0, sizeof(dev_server));
    dev_server.name = "dev_server";
    dev_server.is_http = true;
    dev_server.url = "http://localhost:8080";
    
    kmcp_server_config_t prod_server;
    memset(&prod_server, 0, sizeof(prod_server));
    prod_server.name = "prod_server";
    prod_server.is_http = true;
    prod_server.url = "https://api.example.com";
    prod_server.api_key = "api_key_123456";
    
    kmcp_server_config_t test_server;
    memset(&test_server, 0, sizeof(test_server));
    test_server.name = "test_server";
    test_server.is_http = false;
    test_server.command = "mcp_server";
    
    // Add servers to profiles
    result = kmcp_profile_add_server(manager, "development", &dev_server);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to development profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    result = kmcp_profile_add_server(manager, "production", &prod_server);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to production profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    result = kmcp_profile_add_server(manager, "testing", &test_server);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to testing profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print all profiles
    print_all_profiles(manager);
    
    // Activate development profile
    print_separator();
    printf("Activating development profile...\n");
    result = kmcp_profile_activate(manager, "development");
    if (result != KMCP_SUCCESS) {
        printf("Failed to activate development profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print active profile
    const char* active_profile = kmcp_profile_get_active(manager);
    printf("Active profile: %s\n", active_profile ? active_profile : "None");
    
    // Copy server from development to testing
    print_separator();
    printf("Copying server from development to testing...\n");
    result = kmcp_profile_copy_server(manager, "development", "dev_server", "testing", "dev_server_copy");
    if (result != KMCP_SUCCESS) {
        printf("Failed to copy server: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print testing profile
    print_profile_info(manager, "testing");
    
    // Move server from production to development
    print_separator();
    printf("Moving server from production to development...\n");
    result = kmcp_profile_move_server(manager, "production", "prod_server", "development", "prod_server_moved");
    if (result != KMCP_SUCCESS) {
        printf("Failed to move server: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print development and production profiles
    print_profile_info(manager, "development");
    print_separator();
    print_profile_info(manager, "production");
    
    // Save profiles to file
    print_separator();
    printf("Saving profiles to file...\n");
    result = kmcp_profile_save(manager, "profiles.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to save profiles: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Close profile manager
    kmcp_profile_manager_close(manager);
    printf("Profiles saved to profiles.json\n");
}

/**
 * @brief Example 2: Load profiles from file
 */
static void example_load_profiles(void) {
    printf("Example 2: Load profiles from file\n");
    print_separator();
    
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return;
    }
    
    // Load profiles from file
    printf("Loading profiles from file...\n");
    kmcp_error_t result = kmcp_profile_load(manager, "profiles.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to load profiles: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print all profiles
    print_all_profiles(manager);
    
    // Export development profile
    print_separator();
    printf("Exporting development profile...\n");
    result = kmcp_profile_export(manager, "development", "development_profile.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to export profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Delete development profile
    print_separator();
    printf("Deleting development profile...\n");
    result = kmcp_profile_delete(manager, "development");
    if (result != KMCP_SUCCESS) {
        printf("Failed to delete profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print all profiles
    print_all_profiles(manager);
    
    // Import development profile with a new name
    print_separator();
    printf("Importing development profile with a new name...\n");
    result = kmcp_profile_import(manager, "development_profile.json", "development_imported");
    if (result != KMCP_SUCCESS) {
        printf("Failed to import profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print all profiles
    print_all_profiles(manager);
    
    // Rename profile
    print_separator();
    printf("Renaming development_imported to development...\n");
    result = kmcp_profile_rename(manager, "development_imported", "development");
    if (result != KMCP_SUCCESS) {
        printf("Failed to rename profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }
    
    // Print all profiles
    print_all_profiles(manager);
    
    // Close profile manager
    kmcp_profile_manager_close(manager);
    
    // Clean up files
    remove("development_profile.json");
    printf("Cleaned up temporary files\n");
}

/**
 * @brief Main function
 */
int main(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);
    
    // Run examples
    example_create_manage_profiles();
    print_separator();
    sleep(1); // Small delay between examples
    example_load_profiles();
    
    // Clean up
    remove("profiles.json");
    
    // Close logging
    mcp_log_close();
    
    return 0;
}
