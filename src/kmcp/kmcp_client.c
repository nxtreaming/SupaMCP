#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "kmcp_client.h"
#include "kmcp_config_parser.h"
#include "kmcp_tool_access.h"
#include "kmcp_server_connection.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_client.h"
#include "mcp_transport.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Forward declarations for external functions
void mcp_client_destroy(mcp_client_t* client);
void mcp_transport_destroy(mcp_transport_t* transport);

/**
 * @brief Complete definition of client structure
 */
struct kmcp_client {
    kmcp_client_config_t config;        // Client configuration
    kmcp_server_manager_t* manager;     // Server manager
    kmcp_tool_access_t* tool_access;    // Tool access control
};

/**
 * @brief Create a client
 */
kmcp_client_t* kmcp_client_create(kmcp_client_config_t* config) {
    if (!config) {
        mcp_log_error("Invalid parameter: config is NULL");
        return NULL;
    }

    // Allocate memory
    kmcp_client_t* client = (kmcp_client_t*)malloc(sizeof(kmcp_client_t));
    if (!client) {
        mcp_log_error("Failed to allocate memory for client");
        return NULL;
    }

    // Initialize fields
    memset(client, 0, sizeof(kmcp_client_t));

    // Copy configuration
    client->config.name = config->name ? mcp_strdup(config->name) : mcp_strdup("kmcp-client");
    client->config.version = config->version ? mcp_strdup(config->version) : mcp_strdup("1.0.0");
    client->config.use_manager = config->use_manager;
    client->config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000;

    // Create server manager
    client->manager = kmcp_server_manager_create();
    if (!client->manager) {
        mcp_log_error("Failed to create server manager");
        free(client->config.name);
        free(client->config.version);
        free(client);
        return NULL;
    }

    // Create tool access control
    client->tool_access = kmcp_tool_access_create(true); // Allow all tools by default
    if (!client->tool_access) {
        mcp_log_error("Failed to create tool access control");
        kmcp_server_manager_destroy(client->manager);
        free(client->config.name);
        free(client->config.version);
        free(client);
        return NULL;
    }

    return client;
}

/**
 * @brief Create a client from a configuration file
 */
kmcp_client_t* kmcp_client_create_from_file(const char* config_file) {
    if (!config_file) {
        mcp_log_error("Invalid parameter: config_file is NULL");
        return NULL;
    }

    mcp_log_info("Creating KMCP client from config file: %s", config_file);

    // Create configuration parser
    kmcp_config_parser_t* parser = kmcp_config_parser_create(config_file);
    if (!parser) {
        mcp_log_error("Failed to create config parser");
        return NULL;
    }

    // Parse client configuration
    kmcp_client_config_t config;
    if (kmcp_config_parser_get_client(parser, &config) != 0) {
        mcp_log_error("Failed to parse client configuration");
        kmcp_config_parser_close(parser);
        return NULL;
    }

    mcp_log_info("Creating KMCP client with name: %s, version: %s",
                 config.name ? config.name : "(null)",
                 config.version ? config.version : "(null)");

    // Create client
    kmcp_client_t* client = kmcp_client_create(&config);
    if (!client) {
        mcp_log_error("Failed to create client");
        free(config.name);
        free(config.version);
        kmcp_config_parser_close(parser);
        return NULL;
    }

    // Free temporary configuration strings
    free(config.name);
    free(config.version);

    // Load server configurations
    mcp_log_info("Loading server configurations");
    int server_result = kmcp_server_manager_load(client->manager, config_file);
    if (server_result != 0) {
        mcp_log_error("Failed to load server configurations (error code: %d)", server_result);
        // Continue execution, as some server configurations may be valid
    }

    // Connect to servers
    mcp_log_info("Connecting to servers");
    int connect_result = kmcp_server_manager_connect(client->manager);
    if (connect_result != 0) {
        mcp_log_error("Failed to connect to servers (error code: %d)", connect_result);
        // Continue execution, as some server connections may be successful
    }

    // Get server count
    size_t server_count = kmcp_server_manager_get_count(client->manager);
    mcp_log_info("Connected to %zu server(s)", server_count);

    // Load tool access control
    mcp_log_info("Loading tool access control");
    int access_result = kmcp_config_parser_get_access(parser, client->tool_access);
    if (access_result != 0) {
        mcp_log_error("Failed to load tool access control (error code: %d)", access_result);
        // Continue execution, using default access control
    }

    kmcp_config_parser_close(parser);
    mcp_log_info("KMCP client created successfully");
    return client;
}

/**
 * @brief Call a tool
 */
int kmcp_client_call_tool(
    kmcp_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
) {
    if (!client || !tool_name || !params_json || !result_json) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // Check tool access permission
    if (!kmcp_tool_access_check(client->tool_access, tool_name)) {
        mcp_log_error("Tool access denied: %s", tool_name);
        return -1;
    }

    // Select appropriate server
    int server_index = kmcp_server_manager_select_tool(client->manager, tool_name);
    if (server_index < 0) {
        mcp_log_error("No server found for tool: %s", tool_name);
        return -1;
    }

    // Get server connection
    kmcp_server_connection_t* connection = kmcp_server_manager_get_connection(client->manager, server_index);
    if (!connection) {
        mcp_log_error("Failed to get server connection");
        return -1;
    }

    // Check connection status
    if (!connection->is_connected || !connection->client) {
        mcp_log_error("Server is not connected or client is not available");
        return -1;
    }

    // Call the tool
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    bool is_error = false;

    mcp_log_info("Calling tool '%s' on server '%s'", tool_name, connection->config.name);

    // Call the tool using the correct function signature
    int result = mcp_client_call_tool(connection->client, tool_name, params_json, (mcp_content_item_t***)&content, (size_t*)&count, (bool*)&is_error);

    // Check for errors
    if (result != 0) {
        mcp_log_error("Failed to call tool '%s': error code %d", tool_name, result);
        return result;
    }

    if (is_error) {
        mcp_log_warn("Tool '%s' returned an error", tool_name);
    }

    // If successful, convert the content to a JSON string
    if (content != NULL && count > 0) {
        // Build a JSON response from the content items
        // For now, we'll just use the first content item's data as the result
        if (count == 1 && content[0]->data) {
            *result_json = mcp_strdup((const char*)content[0]->data);
        } else {
            // Create a simple JSON response with success status
            *result_json = mcp_strdup("{\"result\": \"success\", \"count\": ");

            // Add the count as a string
            char count_str[32];
            snprintf(count_str, sizeof(count_str), "%zu}", count);

            // Reallocate the result string to include the count
            size_t len = strlen(*result_json);
            *result_json = (char*)realloc(*result_json, len + strlen(count_str) + 1);
            strcat(*result_json, count_str);
        }

        // Free the content items
        for (size_t i = 0; i < count; i++) {
            if (content[i]) {
                free(content[i]->data);
                free(content[i]->mime_type);
                free(content[i]);
            }
        }
        free(content);

        result = 0;
    } else {
        // No content returned
        *result_json = mcp_strdup("{\"result\": \"success\", \"count\": 0}");
        result = 0;
    }

    return result;
}

/**
 * @brief Get a resource
 */
int kmcp_client_get_resource(
    kmcp_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
) {
    if (!client || !resource_uri || !content || !content_type) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // Select appropriate server
    int server_index = kmcp_server_manager_select_resource(client->manager, resource_uri);
    if (server_index < 0) {
        mcp_log_error("No server found for resource: %s", resource_uri);
        return -1;
    }

    // Get server connection
    kmcp_server_connection_t* connection = kmcp_server_manager_get_connection(client->manager, server_index);
    if (!connection) {
        mcp_log_error("Failed to get server connection");
        return -1;
    }

    // Check connection status
    if (!connection->is_connected || !connection->client) {
        mcp_log_error("Server is not connected or client is not available");
        return -1;
    }

    // Get the resource
    mcp_content_item_t** content_items = NULL;
    size_t count = 0;

    mcp_log_info("Getting resource '%s' from server '%s'", resource_uri, connection->config.name);

    // Call the read_resource function with the correct signature
    int result = mcp_client_read_resource(connection->client, resource_uri, (mcp_content_item_t***)&content_items, (size_t*)&count);

    // Check for errors
    if (result != 0) {
        mcp_log_error("Failed to get resource '%s': error code %d", resource_uri, result);
        return result;
    }

    // If successful, extract the content and content type
    if (content_items != NULL && count > 0) {
        // For simplicity, just use the first content item
        // In a real implementation, you might need to handle multiple content items
        if (content_items[0]->data) {
            *content = mcp_strdup((const char*)content_items[0]->data);
            mcp_log_debug("Resource data size: %zu bytes", strlen(*content));
        } else {
            *content = mcp_strdup("");
            mcp_log_debug("Resource data is empty");
        }

        if (content_items[0]->mime_type) {
            *content_type = mcp_strdup(content_items[0]->mime_type);
            mcp_log_debug("Resource content type: %s", *content_type);
        } else {
            *content_type = mcp_strdup("application/octet-stream");
            mcp_log_debug("Using default content type: %s", *content_type);
        }

        // Free the content items
        for (size_t i = 0; i < count; i++) {
            if (content_items[i]) {
                free(content_items[i]->data);
                free(content_items[i]->mime_type);
                free(content_items[i]);
            }
        }
        free(content_items);

        result = 0;
    } else {
        // No content returned
        mcp_log_warn("No content returned for resource '%s'", resource_uri);
        *content = mcp_strdup("");
        *content_type = mcp_strdup("application/octet-stream");
        result = 0;
    }

    return result;
}

/**
 * @brief Get the server manager
 */
kmcp_server_manager_t* kmcp_client_get_manager(kmcp_client_t* client) {
    if (!client) {
        return NULL;
    }

    return client->manager;
}

/**
 * @brief Close the client
 */
void kmcp_client_close(kmcp_client_t* client) {
    // This function is a wrapper around the internal client close functionality
    if (!client) {
        return;
    }

    mcp_log_info("Closing KMCP client");

    // Disconnect all connections
    if (client->manager) {
        mcp_log_debug("Disconnecting server connections");
        kmcp_server_manager_disconnect(client->manager);
        kmcp_server_manager_destroy(client->manager);
        client->manager = NULL;
    }

    // Free tool access control
    if (client->tool_access) {
        mcp_log_debug("Destroying tool access control");
        kmcp_tool_access_destroy(client->tool_access);
        client->tool_access = NULL;
    }

    // Free configuration
    mcp_log_debug("Freeing client configuration");
    free(client->config.name);
    free(client->config.version);

    // Free client structure
    free(client);
    mcp_log_info("KMCP client closed successfully");
}
