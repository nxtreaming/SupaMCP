/**
 * @file kmcp_comprehensive_example.c
 * @brief Comprehensive example application for KMCP
 *
 * This example demonstrates the following KMCP features:
 * - Multi-server management
 * - Profile management
 * - Tool access control
 * - Server registry integration
 * - Configuration management
 * - Third-party tool integration
 */

#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp.h"
#include "kmcp_client.h"
#include "kmcp_server_manager.h"
#include "kmcp_profile_manager.h"
#include "kmcp_registry.h"
#include "kmcp_tool_sdk.h"
#include "kmcp_config_parser.h"
#include "kmcp_tool_access.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

// No forward declarations needed

#ifdef _WIN32
#include <windows.h>
#define sleep(x) Sleep((x) * 1000)
#else
#include <unistd.h>
#include <limits.h>
#define MAX_PATH PATH_MAX
#endif

/**
 * @brief Print a separator line
 */
static void print_separator(void) {
    printf("\n----------------------------------------\n");
}

/**
 * @brief Print section header
 */
static void print_section(const char* title) {
    printf("\n========================================\n");
    printf("  %s\n", title);
    printf("========================================\n");
}

/**
 * @brief Example configuration file content
 */
const char* example_config =
"{\n"
"  \"clientConfig\": {\n"
"    \"clientName\": \"kmcp-comprehensive-example\",\n"
"    \"clientVersion\": \"1.0.0\",\n"
"    \"useServerManager\": true,\n"
"    \"requestTimeoutMs\": 30000\n"
"  },\n"
"  \"mcpServers\": {\n"
"    \"local\": {\n"
"      \"command\": \"mcp_server\",\n"
"      \"args\": [\"--tcp\", \"--port\", \"8080\", \"--log-level\", \"debug\"],\n"
"      \"env\": {\n"
"        \"MCP_DEBUG\": \"1\"\n"
"      }\n"
"    },\n"
"    \"remote\": {\n"
"      \"url\": \"http://localhost:8080\"\n"
"    }\n"
"  },\n"
"  \"toolAccessControl\": {\n"
"    \"defaultAllow\": true,\n"
"    \"disallowedTools\": [\"file_write\", \"execute_command\"]\n"
"  },\n"
"  \"profiles\": {\n"
"    \"default\": {\n"
"      \"servers\": [\"local\"],\n"
"      \"active\": true,\n"
"      \"description\": \"Default profile with local server\"\n"
"    },\n"
"    \"remote\": {\n"
"      \"servers\": [\"remote\"],\n"
"      \"active\": false,\n"
"      \"description\": \"Remote server profile\"\n"
"    }\n"
"  }\n"
"}";

/**
 * @brief Create example configuration file
 *
 * @param file_path Configuration file path
 * @return int Returns 0 on success, non-zero error code on failure
 */
static int create_example_config(const char* file_path) {
    // Open file in text mode for JSON
    FILE* file = fopen(file_path, "w");
    if (!file) {
        mcp_log_error("Failed to create config file: %s", file_path);
        return -1;
    }

    // Write the configuration
    size_t written = fprintf(file, "%s", example_config);

    // Flush and close the file
    fflush(file);
    fclose(file);

    // Verify the file was written correctly
    if (written != strlen(example_config)) {
        mcp_log_error("Failed to write complete config file: %s (wrote %zu of %zu bytes)",
                     file_path, written, strlen(example_config));
        return -1;
    }

    // Verify the file exists and can be read
    file = fopen(file_path, "r");
    if (!file) {
        mcp_log_error("Failed to verify config file: %s", file_path);
        return -1;
    }
    fclose(file);

    return 0;
}

/**
 * @brief Example 1: Basic KMCP client usage
 */
static void example_basic_client(const char* config_file) {
    print_section("Basic KMCP Client Usage");

    // Create client
    printf("Creating KMCP client from config file: %s\n", config_file);
    kmcp_client_t* client = kmcp_client_create_from_file(config_file);
    if (!client) {
        printf("Failed to create client\n");
        return;
    }

    printf("KMCP client created successfully\n");

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    if (!manager) {
        printf("Failed to get server manager\n");
        kmcp_client_close(client);
        return;
    }

    // Display server count
    size_t server_count = kmcp_server_get_count(manager);
    printf("Server count: %zu\n", server_count);

    // List servers
    printf("\nServer list:\n");
    for (size_t i = 0; i < server_count; i++) {
        kmcp_server_config_t* config = NULL;
        kmcp_error_t result = kmcp_server_manager_get_config_by_index(manager, i, &config);
        if (result != KMCP_SUCCESS || !config) {
            printf("  Failed to get server configuration at index %zu\n", i);
            continue;
        }

        printf("  Server: %s\n", config->name);
        if (config->is_http) {
            printf("    Type: HTTP\n");
            printf("    URL: %s\n", config->url ? config->url : "");
        } else {
            printf("    Type: Local Process\n");
            printf("    Command: %s\n", config->command ? config->command : "");
        }

        kmcp_server_manager_config_free(config);
    }

    // Try to call a tool
    print_separator();
    printf("Calling 'echo' tool...\n");
    char* result = NULL;
    int ret = kmcp_client_call_tool(client, "echo", "{\"text\":\"Hello, World!\"}", &result);
    if (ret == 0 && result) {
        printf("Tool call result: %s\n", result);
        free(result);
    } else {
        printf("Failed to call tool, this is expected if no real server is running\n");
    }

    // Close client
    printf("\nClosing KMCP client...\n");
    kmcp_client_close(client);
    printf("KMCP client closed\n");
}

/**
 * @brief Example 2: Profile management
 */
static void example_profile_management(void) {
    print_section("Profile Management");

    // Create profile manager
    printf("Creating profile manager...\n");
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return;
    }

    // Create profiles
    printf("\nCreating profiles...\n");
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

    // Create server configurations
    printf("\nAdding servers to profiles...\n");
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

    // Get profile count
    size_t profile_count = kmcp_profile_get_count(manager);
    printf("\nTotal profiles: %zu\n", profile_count);

    // Get profile names
    char** profile_names = NULL;
    size_t count = 0;
    result = kmcp_profile_get_names(manager, &profile_names, &count);
    if (result != KMCP_SUCCESS || !profile_names) {
        printf("Failed to get profile names\n");
        kmcp_profile_manager_close(manager);
        return;
    }

    // Print profile names
    printf("\nProfile names:\n");
    for (size_t i = 0; i < count; i++) {
        printf("  %s\n", profile_names[i]);
        free(profile_names[i]);
    }
    free(profile_names);

    // Activate development profile
    printf("\nActivating development profile...\n");
    result = kmcp_profile_activate(manager, "development");
    if (result != KMCP_SUCCESS) {
        printf("Failed to activate development profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }

    // Print active profile
    const char* active_profile = kmcp_profile_get_active(manager);
    printf("Active profile: %s\n", active_profile ? active_profile : "None");

    // Save profiles to file
    printf("\nSaving profiles to file...\n");
    result = kmcp_profile_save(manager, "profiles.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to save profiles: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return;
    }

    printf("Profiles saved to profiles.json\n");

    // Close profile manager
    kmcp_profile_manager_close(manager);
}

/**
 * @brief Example 3: Configuration parser
 */
static void example_config_parser(const char* config_file) {
    print_section("Configuration Parser");

    // Create configuration parser with options
    printf("Creating configuration parser with options...\n");
    kmcp_config_parser_options_t options;
    memset(&options, 0, sizeof(options));
    options.enable_env_vars = true;
    options.enable_includes = true;
    options.validation = KMCP_CONFIG_VALIDATION_BASIC;
    options.default_profile = "default";

    kmcp_config_parser_t* parser = kmcp_config_parser_create_with_options(config_file, &options);
    if (!parser) {
        printf("Failed to create configuration parser\n");
        return;
    }

    // Get client configuration
    printf("\nClient configuration:\n");
    const char* client_name = kmcp_config_parser_get_string(parser, "clientConfig.clientName", "unknown");
    const char* client_version = kmcp_config_parser_get_string(parser, "clientConfig.clientVersion", "0.0.0");
    bool use_server_manager = kmcp_config_parser_get_boolean(parser, "clientConfig.useServerManager", false);
    int request_timeout = kmcp_config_parser_get_int(parser, "clientConfig.requestTimeoutMs", 5000);

    printf("  Client Name: %s\n", client_name);
    printf("  Client Version: %s\n", client_version);
    printf("  Use Server Manager: %s\n", use_server_manager ? "true" : "false");
    printf("  Request Timeout: %d ms\n", request_timeout);

    // Get tool access control configuration
    printf("\nTool Access Control:\n");
    bool default_allow = kmcp_config_parser_get_boolean(parser, "toolAccessControl.defaultAllow", true);
    printf("  Default Allow: %s\n", default_allow ? "true" : "false");

    // Print client configuration summary
    printf("\nClient configuration summary:\n");
    printf("  Client Name: %s\n", client_name);
    printf("  Client Version: %s\n", client_version);
    printf("  Use Server Manager: %s\n", use_server_manager ? "true" : "false");
    printf("  Request Timeout: %d ms\n", request_timeout);

    // Get server configurations
    kmcp_server_config_t** servers = NULL;
    size_t server_count = 0;
    kmcp_error_t result = kmcp_config_parser_get_servers(parser, &servers, &server_count);
    if (result == KMCP_SUCCESS && servers != NULL) {
        printf("\nServer configurations successfully parsed\n");
        printf("  Server count: %zu\n", server_count);

        // Free server configurations
        for (size_t i = 0; i < server_count; i++) {
            if (servers[i]) {
                kmcp_server_manager_config_free(servers[i]);
            }
        }
        free(servers);
    } else {
        printf("Failed to get server configurations: %s\n", kmcp_error_message(result));
    }

    // Get tool access control configuration
    printf("\nTool access control configuration:\n");
    printf("  Default Allow: %s\n", default_allow ? "true" : "false");

    // Get disallowed tools
    printf("  Disallowed tools:\n");
    const char* disallowed_tools[] = {"file_write", "execute_command"};
    for (size_t i = 0; i < sizeof(disallowed_tools) / sizeof(disallowed_tools[0]); i++) {
        printf("    %s\n", disallowed_tools[i]);
    }

    // Close parser
    kmcp_config_parser_close(parser);
}

/**
 * @brief Example 4: Server registry integration
 */
static void example_server_registry(void) {
    print_section("Server Registry Integration");

    // Create registry
    printf("Creating server registry...\n");
    kmcp_registry_t* registry = kmcp_registry_create("http://localhost:8080/registry");
    if (!registry) {
        printf("Failed to create server registry\n");
        return;
    }

    // Create registry with custom configuration
    printf("\nCreating registry with custom configuration...\n");
    kmcp_registry_config_t config;
    memset(&config, 0, sizeof(config));
    config.registry_url = "http://localhost:8080/registry";
    config.api_key = "test_api_key";
    config.cache_ttl_seconds = 60;
    config.connect_timeout_ms = 1000;
    config.request_timeout_ms = 5000;
    config.max_retries = 2;

    kmcp_registry_t* custom_registry = kmcp_registry_create_with_config(&config);
    if (!custom_registry) {
        printf("Failed to create registry with custom configuration\n");
        kmcp_registry_close(registry);
        return;
    }

    // Search for servers (simulated)
    printf("\nSearching for servers (simulated)...\n");

    // Simulate server search results
    printf("Found 2 servers (simulated)\n");

    // Print simulated server information
    printf("\nServer 1:\n");
    printf("  Name: local_server\n");
    printf("  URL: http://localhost:8080\n");
    printf("  Description: Local development server\n");
    printf("  Version: 1.0.0\n");

    printf("\nServer 2:\n");
    printf("  Name: remote_server\n");
    printf("  URL: https://api.example.com\n");
    printf("  Description: Remote production server\n");
    printf("  Version: 2.0.0\n");

    // Add server to server manager
    printf("\nAdding server to server manager...\n");
    kmcp_server_manager_t* manager = kmcp_server_manager_create();
    if (!manager) {
        printf("Failed to create server manager\n");
        kmcp_registry_close(custom_registry);
        kmcp_registry_close(registry);
        return;
    }

    // Create server configuration
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = "demo_server";
    server_config.is_http = true;
    server_config.url = "http://localhost:8080";

    // Add server to manager
    kmcp_error_t result = kmcp_server_add(manager, &server_config);
    if (result == KMCP_SUCCESS) {
        printf("Server added successfully\n");
    } else {
        printf("Failed to add server: %s\n", kmcp_error_message(result));
    }

    // Clean up
    kmcp_server_manager_close(manager);
    kmcp_registry_close(custom_registry);
    kmcp_registry_close(registry);
}

/**
 * @brief Main function
 */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);
    mcp_log_info("Starting KMCP comprehensive example");

    // Create temporary configuration file
    char config_path[MAX_PATH];
#ifdef _WIN32
    snprintf(config_path, sizeof(config_path), "%s\\kmcp_example_config.json", getenv("TEMP"));
#else
    snprintf(config_path, sizeof(config_path), "/tmp/kmcp_example_config.json");
#endif

    printf("Creating example configuration file: %s\n", config_path);
    if (create_example_config(config_path) != 0) {
        mcp_log_error("Failed to create example configuration file");
        mcp_log_close();
        return 1;
    }

    // Run examples
    example_basic_client(config_path);
    example_profile_management();
    example_config_parser(config_path);
    example_server_registry();

    // Clean up
    printf("\nCleaning up...\n");
    remove(config_path);
    remove("profiles.json");

    // Shutdown logging
    mcp_log_info("KMCP comprehensive example completed");
    mcp_log_close();

    return 0;
}
