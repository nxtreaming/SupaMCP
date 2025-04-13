#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mcp_server.h>
#include <mcp_log.h>
#include <mcp_json.h>
#include <mcp_template.h>
#include <mcp_template_security.h>
#include <mcp_types.h>
#include <mcp_string_utils.h>
#include <mcp_stdio_transport.h>

// Global server instance
static mcp_server_t* g_server = NULL;

// Signal handler flag
static volatile int g_running = 1;

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig) {
    (void)sig; // Unused
    g_running = 0;
    if (g_server) {
        mcp_server_stop(g_server);
    }
}

/**
 * @brief Default resource handler
 */
static mcp_error_code_t default_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    (void)server; // Unused
    (void)user_data; // Unused
    (void)uri; // Unused

    if (content == NULL || content_count == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Invalid parameters");
        }
        return MCP_ERROR_INVALID_PARAMS;
    }

    *content = NULL;
    *content_count = 0;

    // Resource not found
    return MCP_ERROR_RESOURCE_NOT_FOUND;
}

/**
 * @brief Template resource handler
 */
static mcp_error_code_t template_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    (void)server; // Unused
    (void)user_data; // Unused
    (void)uri; // Unused

    if (content == NULL || content_count == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Invalid parameters");
        }
        return MCP_ERROR_INVALID_PARAMS;
    }

    *content = NULL;
    *content_count = 0;

    return MCP_ERROR_NONE;
}

/**
 * @brief Sample validator for user templates
 */
static bool user_template_validator(
    const char* template_uri,
    const mcp_json_t* params,
    void* user_data
) {
    (void)template_uri; // Unused
    (void)user_data; // Unused
    (void)params; // Unused

    return true;
}

/**
 * @brief Sample validator for post templates
 */
static bool post_template_validator(
    const char* template_uri,
    const mcp_json_t* params,
    void* user_data
) {
    (void)template_uri; // Unused
    (void)user_data; // Unused
    (void)params; // Unused

    return true;
}

int main(int argc, char** argv) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Starting secure template server example");

    // Create server configuration
    mcp_server_config_t config = {0};
    config.name = "secure-template-server";
    config.version = "1.0.0";
    config.description = "Secure template-based resource server example";

    // Set server capabilities
    mcp_server_capabilities_t capabilities = {0};
    capabilities.resources_supported = true;
    capabilities.tools_supported = false;

    // Create the server
    g_server = mcp_server_create(&config, &capabilities);
    if (g_server == NULL) {
        mcp_log_error("Failed to create server");
        return 1;
    }

    // Set the default resource handler
    mcp_server_set_resource_handler(g_server, default_resource_handler, NULL);

    // Register template-based resource handlers
    const char* user_template = "example://{name}";
    mcp_server_register_template_handler(g_server, user_template, template_resource_handler, (void*)user_template);

    const char* user_profile_template = "example://{name}/profile";
    mcp_server_register_template_handler(g_server, user_profile_template, template_resource_handler, (void*)user_profile_template);

    const char* user_post_template = "example://{name}/posts/{post_id:int}";
    mcp_server_register_template_handler(g_server, user_post_template, template_resource_handler, (void*)user_post_template);

    const char* user_settings_template = "example://{name}/settings/{setting:pattern:theme*}";
    mcp_server_register_template_handler(g_server, user_settings_template, template_resource_handler, (void*)user_settings_template);

    // Add resource templates to the server
    mcp_resource_template_t user_template_def = {0};
    user_template_def.uri_template = "example://{name}";
    user_template_def.name = "User";
    user_template_def.description = "Access a user by name";
    mcp_server_add_resource_template(g_server, &user_template_def);

    mcp_resource_template_t user_profile_template_def = {0};
    user_profile_template_def.uri_template = "example://{name}/profile";
    user_profile_template_def.name = "User Profile";
    user_profile_template_def.description = "Access a user's profile by name";
    mcp_server_add_resource_template(g_server, &user_profile_template_def);

    mcp_resource_template_t user_post_template_def = {0};
    user_post_template_def.uri_template = "example://{name}/posts/{post_id:int}";
    user_post_template_def.name = "User Post";
    user_post_template_def.description = "Access a user's post by ID";
    mcp_server_add_resource_template(g_server, &user_post_template_def);

    mcp_resource_template_t user_settings_template_def = {0};
    user_settings_template_def.uri_template = "example://{name}/settings/{setting:pattern:theme*}";
    user_settings_template_def.name = "User Settings";
    user_settings_template_def.description = "Access a user's settings";
    mcp_server_add_resource_template(g_server, &user_settings_template_def);

    // Set up template security

    // 1. Add access control for templates
    const char* admin_roles[] = {"admin"};
    const char* user_roles[] = {"user", "admin"};

    // User template - accessible by user and admin roles
    mcp_server_add_template_acl(g_server, user_template, user_roles, 2);

    // User profile template - accessible by user and admin roles
    mcp_server_add_template_acl(g_server, user_profile_template, user_roles, 2);

    // User post template - accessible by user and admin roles
    mcp_server_add_template_acl(g_server, user_post_template, user_roles, 2);

    // User settings template - accessible only by admin role
    mcp_server_add_template_acl(g_server, user_settings_template, admin_roles, 1);

    // 2. Set validators for templates
    mcp_server_set_template_validator(g_server, user_template, user_template_validator, NULL);
    mcp_server_set_template_validator(g_server, user_profile_template, user_template_validator, NULL);
    mcp_server_set_template_validator(g_server, user_post_template, post_template_validator, NULL);

    // Create a transport
    mcp_transport_t* transport = mcp_transport_stdio_create();
    if (transport == NULL) {
        mcp_log_error("Failed to create transport");
        mcp_server_destroy(g_server);
        return 1;
    }

    // Start the server
    if (mcp_server_start(g_server, transport) != 0) {
        mcp_log_error("Failed to start server");
        mcp_server_destroy(g_server);
        return 1;
    }

    mcp_log_info("Server started");

    // Wait for user input to stop the server
    printf("Server running. Press Enter to stop...\n");
    getchar();

    // Clean up
    mcp_server_destroy(g_server);
    g_server = NULL;

    mcp_log_info("Server stopped");
    mcp_log_close();

    return 0;
}
