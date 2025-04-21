#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "kmcp_client.h"
#include "kmcp_config_parser.h"
#include "kmcp_tool_access.h"
#include "kmcp_server_connection.h"
#include "kmcp_error.h"
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
 *
 * @param config Client configuration (must not be NULL)
 * @return kmcp_client_t* Returns a new client on success, or NULL on failure
 */
kmcp_client_t* kmcp_client_create(kmcp_client_config_t* config) {
    if (!config) {
        mcp_log_error("Invalid parameter: config is NULL");
        return NULL;
    }

    // Allocate memory and initialize to zero
    kmcp_client_t* client = (kmcp_client_t*)calloc(1, sizeof(kmcp_client_t));
    if (!client) {
        mcp_log_error("Failed to allocate memory for client (size: %zu bytes)", sizeof(kmcp_client_t));
        return NULL;
    }

    // Copy configuration
    client->config.name = config->name ? mcp_strdup(config->name) : mcp_strdup("kmcp-client");
    client->config.version = config->version ? mcp_strdup(config->version) : mcp_strdup("1.0.0");
    client->config.use_manager = config->use_manager;
    client->config.timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 30000;

    // Create server manager
    client->manager = kmcp_server_create();
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
        kmcp_server_destroy(client->manager);
        free(client->config.name);
        free(client->config.version);
        free(client);
        return NULL;
    }

    return client;
}

/**
 * @brief Create a client from a configuration file
 *
 * @param config_file Path to the configuration file (must not be NULL)
 * @return kmcp_client_t* Returns a new client on success, or NULL on failure
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
    int server_result = kmcp_server_load(client->manager, config_file);
    if (server_result != 0) {
        mcp_log_error("Failed to load server configurations (error code: %d)", server_result);
        // Continue execution, as some server configurations may be valid
    }

    // Connect to servers
    mcp_log_info("Connecting to servers");
    int connect_result = kmcp_server_connect(client->manager);
    if (connect_result != 0) {
        mcp_log_error("Failed to connect to servers (error code: %d)", connect_result);
        // Continue execution, as some server connections may be successful
    }

    // Get server count
    size_t server_count = kmcp_server_get_count(client->manager);
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
 *
 * Calls a tool on an appropriate server based on tool name.
 * The server selection is handled automatically by the client.
 *
 * @param client Client (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @param params_json Parameter JSON string (must not be NULL)
 * @param result_json Pointer to result JSON string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_client_call_tool(
    kmcp_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
) {
    if (!client || !tool_name || !params_json || !result_json) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize result pointer to NULL for safety
    *result_json = NULL;

    // Check tool access permission
    if (!kmcp_tool_access_check(client->tool_access, tool_name)) {
        mcp_log_error("Tool access denied: %s", tool_name);
        return KMCP_ERROR_PERMISSION_DENIED;
    }

    // Select appropriate server
    int server_index = kmcp_server_select_tool(client->manager, tool_name);
    if (server_index < 0) {
        mcp_log_error("No server found for tool: %s", tool_name);
        return KMCP_ERROR_TOOL_NOT_FOUND;
    }

    // Get server connection
    kmcp_server_connection_t* connection = kmcp_server_get_connection(client->manager, server_index);
    if (!connection) {
        mcp_log_error("Failed to get server connection");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Check connection status
    if (!connection->is_connected) {
        mcp_log_error("Server is not connected");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Call the tool
    mcp_content_item_t** content = NULL;
    size_t count = 0;
    bool is_error = false;

    mcp_log_info("Calling tool '%s' on server '%s'", tool_name, connection->config.name);

    int result = 0;

    // Check if this is an HTTP connection
    if (connection->config.is_http) {
        if (!connection->http_client) {
            mcp_log_error("HTTP client is not available");
            return KMCP_ERROR_CONNECTION_FAILED;
        }

        // Call the tool using HTTP client
        char* http_result = NULL;
        kmcp_error_t http_result_code = kmcp_http_client_call_tool(
            connection->http_client,
            tool_name,
            params_json,
            &http_result
        );

        if (http_result_code != KMCP_SUCCESS) {
            mcp_log_error("Failed to call tool '%s' via HTTP: error code %d", tool_name, http_result_code);
            return http_result_code;
        }

        // Create a content item with the HTTP result
        content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
        if (!content) {
            mcp_log_error("Failed to allocate memory for content item array");
            free(http_result);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        content[0] = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
        if (!content[0]) {
            mcp_log_error("Failed to allocate memory for content item");
            free(content);
            free(http_result);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        content[0]->data = (unsigned char*)http_result;
        content[0]->mime_type = mcp_strdup("application/json");
        count = 1;
        is_error = false; // Assume success unless the result indicates otherwise

        // Check if the result contains an error indication
        if (strstr(http_result, "\"error\":") != NULL) {
            is_error = true;
        }
    } else {
        if (!connection->client) {
            mcp_log_error("MCP client is not available");
            return KMCP_ERROR_CONNECTION_FAILED;
        }

        // Call the tool using mcp_client
        result = mcp_client_call_tool(connection->client, tool_name, params_json, &content, &count, &is_error);
    }

    // Check for errors
    if (result != 0) {
        mcp_log_error("Failed to call tool '%s': error code %d", tool_name, result);
        return KMCP_ERROR_INTERNAL;
    }

    // Handle tool execution errors
    if (is_error) {
        mcp_log_warn("Tool '%s' returned an error", tool_name);
        // We still process the content, as it may contain error details
    }

    // Process the content items
    if (content != NULL && count > 0) {
        // Check if we have a single JSON content item
        if (count == 1 && content[0] && content[0]->data && content[0]->mime_type) {
            // If it's JSON, just return it directly
            if (strstr(content[0]->mime_type, "json") ||
                strstr(content[0]->mime_type, "application/json")) {
                *result_json = mcp_strdup((const char*)content[0]->data);
            } else {
                // For non-JSON content, wrap it in a JSON object
                // Allocate enough space for the JSON wrapper and the content
                size_t content_len = strlen((const char*)content[0]->data);
                size_t mime_type_len = strlen(content[0]->mime_type);
                size_t json_len = 50 + content_len + mime_type_len; // Extra space for JSON formatting

                *result_json = (char*)calloc(1, json_len);
                if (!*result_json) {
                    mcp_log_error("Failed to allocate memory for result JSON (size: %zu bytes)", json_len);
                    free(content[0]->data);
                    free(content[0]->mime_type);
                    free(content[0]);
                    free(content);
                    return KMCP_ERROR_MEMORY_ALLOCATION;
                }
                snprintf(*result_json, json_len,
                         "{\"content\": \"%s\", \"mime_type\": \"%s\"}",
                         (const char*)content[0]->data, content[0]->mime_type);
            }
        } else {
            // Multiple content items - create a JSON array
            // First, calculate the required buffer size
            size_t buffer_size = 256; // Start with some space for JSON structure
            for (size_t i = 0; i < count; i++) {
                if (content[i] && content[i]->data) {
                    buffer_size += strlen((const char*)content[i]->data) + 128; // Extra space for JSON formatting
                }
            }

            // Allocate the buffer and initialize to zero
            *result_json = (char*)calloc(1, buffer_size);
            if (!*result_json) {
                mcp_log_error("Failed to allocate memory for result JSON (size: %zu bytes)", buffer_size);
                // Clean up content items
                for (size_t i = 0; i < count; i++) {
                    if (content[i]) {
                        free(content[i]->data);
                        free(content[i]->mime_type);
                        free(content[i]);
                    }
                }
                free(content);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }
            // Initialize with array opening
            strcpy(*result_json, "{\"items\": [");
            size_t pos = strlen(*result_json);

            // Add each content item
            for (size_t i = 0; i < count; i++) {
                if (content[i] && content[i]->data && content[i]->mime_type) {
                    // Add comma if not the first item
                    if (i > 0) {
                        strcat(*result_json + pos, ",");
                        pos += 1;
                    }

                    // Add the item as a JSON object
                    int written = snprintf(*result_json + pos, buffer_size - pos,
                                         "{\"content\": \"%s\", \"mime_type\": \"%s\"}",
                                         (const char*)content[i]->data, content[i]->mime_type);
                    if (written > 0) {
                        pos += written;
                    }
                }
            }

            // Close the array and object
            strcat(*result_json + pos, "]}");
        }

        // If we failed to allocate memory for the result
        if (!*result_json) {
            mcp_log_error("Failed to allocate memory for result JSON");
            // Clean up content items
            for (size_t i = 0; i < count; i++) {
                if (content[i]) {
                    free(content[i]->data);
                    free(content[i]->mime_type);
                    free(content[i]);
                }
            }
            free(content);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        // Clean up content items
        for (size_t i = 0; i < count; i++) {
            if (content[i]) {
                free(content[i]->data);
                free(content[i]->mime_type);
                free(content[i]);
            }
        }
        free(content);

        return is_error ? KMCP_ERROR_TOOL_EXECUTION : KMCP_SUCCESS;
    } else {
        // No content returned
        *result_json = mcp_strdup("{\"result\": \"success\", \"count\": 0}");
        if (!*result_json) {
            mcp_log_error("Failed to allocate memory for result JSON");
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }
        return is_error ? KMCP_ERROR_TOOL_EXECUTION : KMCP_SUCCESS;
    }
}

/**
 * @brief Get a resource
 *
 * Retrieves a resource from an appropriate server based on resource URI.
 * The server selection is handled automatically by the client.
 *
 * @param client Client (must not be NULL)
 * @param resource_uri Resource URI (must not be NULL)
 * @param content Pointer to content string, memory allocated by function, caller responsible for freeing
 * @param content_type Pointer to content type string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_client_get_resource(
    kmcp_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
) {
    if (!client || !resource_uri || !content || !content_type) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output pointers to NULL for safety
    *content = NULL;
    *content_type = NULL;

    // Select appropriate server
    int server_index = kmcp_server_select_resource(client->manager, resource_uri);
    if (server_index < 0) {
        mcp_log_error("No server found for resource: %s", resource_uri);
        return KMCP_ERROR_RESOURCE_NOT_FOUND;
    }

    // Get server connection
    kmcp_server_connection_t* connection = kmcp_server_get_connection(client->manager, server_index);
    if (!connection) {
        mcp_log_error("Failed to get server connection");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Check connection status
    if (!connection->is_connected) {
        mcp_log_error("Server is not connected");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Get the resource
    mcp_content_item_t** content_items = NULL;
    size_t count = 0;

    mcp_log_info("Getting resource '%s' from server '%s'", resource_uri, connection->config.name);

    int result = 0;

    // Check if this is an HTTP connection
    if (connection->config.is_http) {
        if (!connection->http_client) {
            mcp_log_error("HTTP client is not available");
            return KMCP_ERROR_CONNECTION_FAILED;
        }

        // Get the resource using HTTP client
        char* http_content = NULL;
        char* http_content_type = NULL;
        kmcp_error_t http_result_code = kmcp_http_client_get_resource(
            connection->http_client,
            resource_uri,
            &http_content,
            &http_content_type
        );

        if (http_result_code != KMCP_SUCCESS) {
            mcp_log_error("Failed to get resource '%s' via HTTP: error code %d", resource_uri, http_result_code);
            return http_result_code;
        }

        // Create a content item with the HTTP result
        content_items = (mcp_content_item_t**)calloc(1, sizeof(mcp_content_item_t*));
        if (!content_items) {
            mcp_log_error("Failed to allocate memory for content item array (size: %zu bytes)", sizeof(mcp_content_item_t*));
            free(http_content);
            free(http_content_type);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        content_items[0] = (mcp_content_item_t*)calloc(1, sizeof(mcp_content_item_t));
        if (!content_items[0]) {
            mcp_log_error("Failed to allocate memory for content item (size: %zu bytes)", sizeof(mcp_content_item_t));
            free(content_items);
            free(http_content);
            free(http_content_type);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        content_items[0]->data = (unsigned char*)http_content;
        content_items[0]->mime_type = http_content_type;
        count = 1;
    } else {
        if (!connection->client) {
            mcp_log_error("MCP client is not available");
            return KMCP_ERROR_CONNECTION_FAILED;
        }

        // Call the read_resource function
        result = mcp_client_read_resource(connection->client, resource_uri, &content_items, &count);
    }

    // Check for errors
    if (result != 0) {
        mcp_log_error("Failed to get resource '%s': error code %d", resource_uri, result);
        return KMCP_ERROR_INTERNAL;
    }

    // If successful, extract the content and content type
    if (content_items != NULL && count > 0) {
        // For simplicity, just use the first content item
        // In a real implementation, you might need to handle multiple content items
        if (content_items[0] && content_items[0]->data) {
            *content = mcp_strdup((const char*)content_items[0]->data);
            if (!*content) {
                mcp_log_error("Failed to allocate memory for content");
                // Clean up content items
                for (size_t i = 0; i < count; i++) {
                    if (content_items[i]) {
                        free(content_items[i]->data);
                        free(content_items[i]->mime_type);
                        free(content_items[i]);
                    }
                }
                free(content_items);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }
            mcp_log_debug("Resource data size: %zu bytes", strlen(*content));
        } else {
            *content = mcp_strdup("");
            if (!*content) {
                mcp_log_error("Failed to allocate memory for empty content");
                // Clean up content items
                for (size_t i = 0; i < count; i++) {
                    if (content_items[i]) {
                        free(content_items[i]->data);
                        free(content_items[i]->mime_type);
                        free(content_items[i]);
                    }
                }
                free(content_items);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }
            mcp_log_debug("Resource data is empty");
        }

        if (content_items[0] && content_items[0]->mime_type) {
            *content_type = mcp_strdup(content_items[0]->mime_type);
            if (!*content_type) {
                mcp_log_error("Failed to allocate memory for content type");
                free(*content);
                *content = NULL;
                // Clean up content items
                for (size_t i = 0; i < count; i++) {
                    if (content_items[i]) {
                        free(content_items[i]->data);
                        free(content_items[i]->mime_type);
                        free(content_items[i]);
                    }
                }
                free(content_items);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }
            mcp_log_debug("Resource content type: %s", *content_type);
        } else {
            *content_type = mcp_strdup("application/octet-stream");
            if (!*content_type) {
                mcp_log_error("Failed to allocate memory for default content type");
                free(*content);
                *content = NULL;
                // Clean up content items
                for (size_t i = 0; i < count; i++) {
                    if (content_items[i]) {
                        free(content_items[i]->data);
                        free(content_items[i]->mime_type);
                        free(content_items[i]);
                    }
                }
                free(content_items);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }
            mcp_log_debug("Using default content type: %s", *content_type);
        }

        // Clean up content items
        for (size_t i = 0; i < count; i++) {
            if (content_items[i]) {
                free(content_items[i]->data);
                free(content_items[i]->mime_type);
                free(content_items[i]);
            }
        }
        free(content_items);

        return KMCP_SUCCESS;
    } else {
        // No content returned
        mcp_log_warn("No content returned for resource '%s'", resource_uri);

        // Allocate empty content
        *content = mcp_strdup("");
        if (!*content) {
            mcp_log_error("Failed to allocate memory for empty content");
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        // Allocate default content type
        *content_type = mcp_strdup("application/octet-stream");
        if (!*content_type) {
            mcp_log_error("Failed to allocate memory for default content type");
            free(*content);
            *content = NULL;
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        return KMCP_SUCCESS;
    }
}

/**
 * @brief Get the server manager
 *
 * @param client Client (must not be NULL)
 * @return kmcp_server_manager_t* Returns the server manager, or NULL if client is NULL
 */
kmcp_server_manager_t* kmcp_client_get_manager(kmcp_client_t* client) {
    if (!client) {
        return NULL;
    }

    return client->manager;
}

/**
 * @brief Close the client and free all resources
 *
 * @param client Client to close (can be NULL)
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
        kmcp_server_disconnect(client->manager);
        kmcp_server_destroy(client->manager);
        client->manager = NULL;
    }

    // Free tool access control
    if (client->tool_access) {
        mcp_log_debug("Destroying tool access control");
        kmcp_tool_access_destroy(client->tool_access);
        client->tool_access = NULL;
    }

    // Free configuration with null checks
    mcp_log_debug("Freeing client configuration");
    if (client->config.name) {
        free(client->config.name);
        client->config.name = NULL;
    }

    if (client->config.version) {
        free(client->config.version);
        client->config.version = NULL;
    }

    // Free client structure
    free(client);
    mcp_log_info("KMCP client closed successfully");
}
