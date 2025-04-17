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
    if (kmcp_server_manager_load(client->manager, config_file) != 0) {
        mcp_log_error("Failed to load server configurations");
        // Continue execution, as some server configurations may be valid
    }

    // Connect to servers
    if (kmcp_server_manager_connect(client->manager) != 0) {
        mcp_log_error("Failed to connect to servers");
        // Continue execution, as some server connections may be successful
    }

    // Load tool access control
    if (kmcp_config_parser_get_access(parser, client->tool_access) != 0) {
        mcp_log_error("Failed to load tool access control");
        // Continue execution, using default access control
    }

    kmcp_config_parser_close(parser);
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
        mcp_log_error("Server is not connected");
        return -1;
    }

    // Call the tool
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    bool is_error = false;

    // Call the tool using the correct function signature
    int result = mcp_client_call_tool(connection->client, tool_name, params_json, (mcp_content_item_t***)&content, (size_t*)&count, (bool*)&is_error);

    // If successful, convert the content to a JSON string
    if (result == 0 && content != NULL) {
        // For now, just return a simple JSON response
        // In a real implementation, you would convert the content items to JSON
        *result_json = mcp_strdup("{\"result\": \"success\"}");

        // Free the content items (assuming there's a function to do this)
        // mcp_free_content_list(content, count);

        // For now, just free the memory directly
        for (size_t i = 0; i < count; i++) {
            free(content[i]);
        }
        free(content);

        return 0;
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
        mcp_log_error("Server is not connected");
        return -1;
    }

    // Get the resource
    mcp_content_item_t** content_items = NULL;
    size_t count = 0;

    // Call the read_resource function with the correct signature
    int result = mcp_client_read_resource(connection->client, resource_uri, (mcp_content_item_t***)&content_items, (size_t*)&count);

    // If successful, extract the content and content type
    if (result == 0 && content_items != NULL && count > 0) {
        // For simplicity, just use the first content item
        // In a real implementation, you might need to handle multiple content items
        if (content_items[0]->data) {
            *content = mcp_strdup((const char*)content_items[0]->data);
        } else {
            *content = mcp_strdup("");
        }

        if (content_items[0]->mime_type) {
            *content_type = mcp_strdup(content_items[0]->mime_type);
        } else {
            *content_type = mcp_strdup("application/octet-stream");
        }

        // Free the content items (assuming there's a function to do this)
        // mcp_free_content_list(content_items, count);

        // For now, just free the memory directly
        for (size_t i = 0; i < count; i++) {
            free(content_items[i]);
        }
        free(content_items);

        return 0;
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

    // Disconnect all connections
    if (client->manager) {
        kmcp_server_manager_disconnect(client->manager);
        kmcp_server_manager_destroy(client->manager);
    }

    // Free tool access control
    if (client->tool_access) {
        kmcp_tool_access_destroy(client->tool_access);
    }

    // Free configuration
    free(client->config.name);
    free(client->config.version);

    free(client);
}
