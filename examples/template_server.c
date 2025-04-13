#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp_server.h>
#include <mcp_transport.h>
#include <mcp_tcp_transport.h>
#include <mcp_log.h>
#include <mcp_json.h>
#include <mcp_template.h>
#include <mcp_template_optimized.h>
#include <mcp_string_utils.h>

// Handler for template-based resources
mcp_error_code_t template_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    (void)server; // Unused
    (void)user_data; // Unused

    printf("Template resource handler called for URI: %s\n", uri);

    // Extract parameters from the URI using the optimized function
    const char* template_uri = (const char*)user_data;
    mcp_json_t* params = mcp_template_extract_params_optimized(uri, template_uri);
    if (params == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Failed to extract parameters from URI");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Convert parameters to a string
    char* params_str = mcp_json_stringify(params);
    mcp_json_destroy(params);

    if (params_str == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Failed to stringify parameters");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Create a response message
    char response[1024];
    snprintf(response, sizeof(response), "Template resource: %s\nParameters: %s", uri, params_str);
    free(params_str);

    // Create a content item
    *content_count = 1;
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Failed to allocate memory for content");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    (*content)[0] = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if ((*content)[0] == NULL) {
        free(*content);
        *content = NULL;
        if (error_message) {
            *error_message = mcp_strdup("Failed to allocate memory for content item");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Initialize the content item
    (*content)[0]->type = MCP_CONTENT_TYPE_TEXT;
    (*content)[0]->mime_type = mcp_strdup("text/plain");
    (*content)[0]->data = mcp_strdup(response);
    (*content)[0]->data_size = strlen(response) + 1; // Include null terminator

    return MCP_ERROR_NONE;
}

// Default resource handler
mcp_error_code_t default_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    (void)server; // Unused
    (void)user_data; // Unused

    printf("Default resource handler called for URI: %s\n", uri);

    // Check if the resource exists
    if (strncmp(uri, "example://", 10) != 0) {
        if (error_message) {
            *error_message = mcp_strdup("Resource not found");
        }
        return MCP_ERROR_RESOURCE_NOT_FOUND;
    }

    // Create a response message
    char response[256];
    snprintf(response, sizeof(response), "Resource: %s", uri);

    // Create a content item
    *content_count = 1;
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (*content == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Failed to allocate memory for content");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    (*content)[0] = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if ((*content)[0] == NULL) {
        free(*content);
        *content = NULL;
        if (error_message) {
            *error_message = mcp_strdup("Failed to allocate memory for content item");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Initialize the content item
    (*content)[0]->type = MCP_CONTENT_TYPE_TEXT;
    (*content)[0]->mime_type = mcp_strdup("text/plain");
    (*content)[0]->data = mcp_strdup(response);
    (*content)[0]->data_size = strlen(response) + 1; // Include null terminator

    return MCP_ERROR_NONE;
}

int main(int argc, char** argv) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Starting template server example");

    // Create server configuration
    mcp_server_config_t config = {0};
    config.name = "template-server";
    config.version = "1.0.0";
    config.description = "Template-based resource server example";

    // Set server capabilities
    mcp_server_capabilities_t capabilities = {0};
    capabilities.resources_supported = true;
    capabilities.tools_supported = false;

    // Create the server
    mcp_server_t* server = mcp_server_create(&config, &capabilities);
    if (server == NULL) {
        mcp_log_error("Failed to create server");
        return 1;
    }

    // Set the default resource handler
    mcp_server_set_resource_handler(server, default_resource_handler, NULL);

    // Register template-based resource handlers
    const char* user_template = "example://{name}";
    mcp_server_register_template_handler(server, user_template, template_resource_handler, (void*)user_template);

    const char* user_profile_template = "example://{name}/profile";
    mcp_server_register_template_handler(server, user_profile_template, template_resource_handler, (void*)user_profile_template);

    const char* user_post_template = "example://{name}/posts/{post_id:int}";
    mcp_server_register_template_handler(server, user_post_template, template_resource_handler, (void*)user_post_template);

    const char* user_settings_template = "example://{name}/settings/{setting:pattern:theme*}";
    mcp_server_register_template_handler(server, user_settings_template, template_resource_handler, (void*)user_settings_template);

    // Add resource templates to the server
    mcp_resource_template_t user_template_def = {0};
    user_template_def.uri_template = "example://{name}";
    user_template_def.name = "User";
    user_template_def.description = "Access a user by name";
    mcp_server_add_resource_template(server, &user_template_def);

    mcp_resource_template_t user_profile_template_def = {0};
    user_profile_template_def.uri_template = "example://{name}/profile";
    user_profile_template_def.name = "User Profile";
    user_profile_template_def.description = "Access a user's profile by name";
    mcp_server_add_resource_template(server, &user_profile_template_def);

    mcp_resource_template_t user_post_template_def = {0};
    user_post_template_def.uri_template = "example://{name}/posts/{post_id:int}";
    user_post_template_def.name = "User Post";
    user_post_template_def.description = "Access a user's post by ID";
    mcp_server_add_resource_template(server, &user_post_template_def);

    mcp_resource_template_t user_settings_template_def = {0};
    user_settings_template_def.uri_template = "example://{name}/settings/{setting:pattern:theme*}";
    user_settings_template_def.name = "User Settings";
    user_settings_template_def.description = "Access a user's settings";
    mcp_server_add_resource_template(server, &user_settings_template_def);

    // Create a TCP transport
    mcp_transport_t* transport = mcp_transport_tcp_create("127.0.0.1", 8080, 0);
    if (transport == NULL) {
        mcp_log_error("Failed to create TCP transport");
        mcp_server_destroy(server);
        return 1;
    }

    // Start the server
    mcp_log_info("Starting server on 127.0.0.1:8080");
    if (mcp_server_start(server, transport) != 0) {
        mcp_log_error("Failed to start server");
        mcp_transport_destroy(transport);
        mcp_server_destroy(server);
        return 1;
    }

    // Wait for user input to stop the server
    printf("Server running. Press Enter to stop...\n");
    getchar();

    // Stop and clean up
    mcp_server_stop(server);
    mcp_server_destroy(server);
    mcp_transport_destroy(transport);
    mcp_log_close();

    return 0;
}
