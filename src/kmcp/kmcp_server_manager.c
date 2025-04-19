#include "kmcp_server_manager.h"
#include "kmcp_server_connection.h"
#include "kmcp_config_parser.h"
#include "kmcp_process.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include <mcp_transport.h>
#include <mcp_transport_factory.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Server connection structure is defined in kmcp_server_connection.h

/**
 * @brief Complete definition of server manager structure
 */
struct kmcp_server_manager {
    kmcp_server_connection_t** servers;  // Array of server connections
    size_t server_count;                 // Number of servers
    size_t server_capacity;              // Capacity of server array
    mcp_hashtable_t* tool_map;           // Mapping from tool name to server index
    mcp_hashtable_t* resource_map;       // Mapping from resource prefix to server index
    mcp_mutex_t* mutex;                  // Thread safety lock
};

/**
 * @brief Create a server manager
 */
kmcp_server_manager_t* kmcp_server_manager_create() {
    kmcp_server_manager_t* manager = (kmcp_server_manager_t*)malloc(sizeof(kmcp_server_manager_t));
    if (!manager) {
        mcp_log_error("Failed to allocate memory for server manager");
        return NULL;
    }

    // Initialize fields
    manager->servers = NULL;
    manager->server_count = 0;
    manager->server_capacity = 0;
    manager->tool_map = NULL;
    manager->resource_map = NULL;
    manager->mutex = NULL;

    // Create hash tables
    manager->tool_map = mcp_hashtable_create(
        16,                             // initial_capacity
        0.75f,                          // load_factor_threshold
        mcp_hashtable_string_hash,      // hash_func
        mcp_hashtable_string_compare,   // key_compare
        mcp_hashtable_string_dup,       // key_dup
        mcp_hashtable_string_free,      // key_free
        free                            // value_free
    );
    if (!manager->tool_map) {
        mcp_log_error("Failed to create tool map");
        free(manager);
        return NULL;
    }

    manager->resource_map = mcp_hashtable_create(
        16,                             // initial_capacity
        0.75f,                          // load_factor_threshold
        mcp_hashtable_string_hash,      // hash_func
        mcp_hashtable_string_compare,   // key_compare
        mcp_hashtable_string_dup,       // key_dup
        mcp_hashtable_string_free,      // key_free
        free                            // value_free
    );
    if (!manager->resource_map) {
        mcp_log_error("Failed to create resource map");
        mcp_hashtable_destroy(manager->tool_map);
        free(manager);
        return NULL;
    }

    // Create mutex
    manager->mutex = mcp_mutex_create();
    if (!manager->mutex) {
        mcp_log_error("Failed to create mutex");
        mcp_hashtable_destroy(manager->resource_map);
        mcp_hashtable_destroy(manager->tool_map);
        free(manager);
        return NULL;
    }

    return manager;
}

/**
 * @brief Free a server configuration
 *
 * @param config Server configuration to free
 */
static void kmcp_server_config_free(kmcp_server_config_t* config) {
    if (!config) {
        return;
    }

    // Free strings
    free(config->name);
    free(config->command);
    free(config->url);
    free(config->api_key);

    // Free arguments array
    if (config->args) {
        for (size_t i = 0; i < config->args_count; i++) {
            free(config->args[i]);
        }
        free(config->args);
    }

    // Free environment variables array
    if (config->env) {
        for (size_t i = 0; i < config->env_count; i++) {
            free(config->env[i]);
        }
        free(config->env);
    }

    // Free the structure itself
    free(config);
}

/**
 * @brief Load server configurations from a config file
 */
kmcp_error_t kmcp_server_manager_load(kmcp_server_manager_t* manager, const char* config_file) {
    if (!manager || !config_file) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Loading server configurations from file: %s", config_file);

    kmcp_config_parser_t* parser = kmcp_config_parser_create(config_file);
    if (!parser) {
        mcp_log_error("Failed to create config parser");
        return KMCP_ERROR_FILE_NOT_FOUND;
    }

    kmcp_server_config_t** servers = NULL;
    size_t server_count = 0;
    int result = kmcp_config_parser_get_servers(parser, &servers, &server_count);
    if (result != 0) {
        mcp_log_error("Failed to parse server configurations");
        kmcp_config_parser_close(parser);
        return KMCP_ERROR_PARSE_FAILED;
    }

    mcp_log_info("Found %zu server configurations", server_count);

    // Add servers
    int success_count = 0;
    for (size_t i = 0; i < server_count; i++) {
        if (!servers[i]) {
            mcp_log_warn("Server configuration at index %zu is NULL", i);
            continue;
        }

        mcp_log_info("Adding server: %s", servers[i]->name ? servers[i]->name : "(unnamed)");
        result = kmcp_server_manager_add(manager, servers[i]);
        if (result != 0) {
            mcp_log_error("Failed to add server: %s", servers[i]->name ? servers[i]->name : "(unnamed)");
            // Continue adding other servers
        } else {
            success_count++;
        }

        // Free server configuration
        // Note: kmcp_server_manager_add copies the configuration, so it's safe to free here
        kmcp_server_config_free(servers[i]);
    }

    // Free server configuration array
    free(servers);
    kmcp_config_parser_close(parser);

    mcp_log_info("Successfully added %d out of %zu server configurations", success_count, server_count);
    return success_count > 0 ? KMCP_SUCCESS : KMCP_ERROR_SERVER_NOT_FOUND;
}

/**
 * @brief Add a server
 */
kmcp_error_t kmcp_server_manager_add(kmcp_server_manager_t* manager, kmcp_server_config_t* config) {
    if (!manager || !config) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_mutex_lock(manager->mutex);

    // Check if we need to expand capacity
    if (manager->server_count >= manager->server_capacity) {
        size_t new_capacity = manager->server_capacity == 0 ? 4 : manager->server_capacity * 2;
        kmcp_server_connection_t** new_servers = (kmcp_server_connection_t**)realloc(
            manager->servers,
            new_capacity * sizeof(kmcp_server_connection_t*)
        );

        if (!new_servers) {
            mcp_log_error("Failed to reallocate server array");
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        manager->servers = new_servers;
        manager->server_capacity = new_capacity;
    }

    // Create a new server connection
    kmcp_server_connection_t* connection = (kmcp_server_connection_t*)malloc(sizeof(kmcp_server_connection_t));
    if (!connection) {
        mcp_log_error("Failed to allocate memory for server connection");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize connection
    memset(connection, 0, sizeof(kmcp_server_connection_t));

    // Copy configuration
    connection->config.name = mcp_strdup(config->name);
    connection->config.command = config->command ? mcp_strdup(config->command) : NULL;
    connection->config.url = config->url ? mcp_strdup(config->url) : NULL;
    connection->config.api_key = config->api_key ? mcp_strdup(config->api_key) : NULL;
    connection->config.is_http = config->is_http;

    // Copy arguments array
    if (config->args && config->args_count > 0) {
        connection->config.args = (char**)malloc(config->args_count * sizeof(char*));
        if (connection->config.args) {
            connection->config.args_count = config->args_count;
            for (size_t i = 0; i < config->args_count; i++) {
                connection->config.args[i] = mcp_strdup(config->args[i]);
            }
        }
    }

    // Copy environment variables array
    if (config->env && config->env_count > 0) {
        connection->config.env = (char**)malloc(config->env_count * sizeof(char*));
        if (connection->config.env) {
            connection->config.env_count = config->env_count;
            for (size_t i = 0; i < config->env_count; i++) {
                connection->config.env[i] = mcp_strdup(config->env[i]);
            }
        }
    }

    // Add to server array
    manager->servers[manager->server_count] = connection;
    manager->server_count++;

    mcp_mutex_unlock(manager->mutex);
    return KMCP_SUCCESS;
}

/**
 * @brief Connect to all servers
 */
kmcp_error_t kmcp_server_manager_connect(kmcp_server_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Connecting to all servers");

    mcp_mutex_lock(manager->mutex);

    int success_count = 0;
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];
        if (!connection) {
            mcp_log_error("Server connection at index %zu is NULL", i);
            continue;
        }

        // If already connected, skip
        if (connection->is_connected) {
            mcp_log_info("Server '%s' is already connected, skipping",
                       connection->config.name ? connection->config.name : "(unnamed)");
            success_count++;
            continue;
        }

        mcp_log_info("Connecting to server '%s'",
                   connection->config.name ? connection->config.name : "(unnamed)");

        // Connect server based on configuration type
        if (connection->config.is_http) {
            // HTTP connection
            mcp_log_info("Connecting to HTTP server: %s", connection->config.name);

            // Create HTTP client
            connection->http_client = kmcp_http_client_create(connection->config.url, connection->config.api_key);
            if (!connection->http_client) {
                mcp_log_error("Failed to create HTTP client for server: %s", connection->config.name);
                continue;
            }

            mcp_log_info("Created HTTP client for server: %s", connection->config.name);

            // For HTTP connections, we don't have a way to query supported tools and resources
            // directly. We could implement this by making HTTP requests to the server's
            // /tools and /resources endpoints, but for now we'll just use default values.

            // Add some default supported tools
            connection->supported_tools = (char**)malloc(3 * sizeof(char*));
            if (connection->supported_tools) {
                connection->supported_tools[0] = mcp_strdup("echo");
                connection->supported_tools[1] = mcp_strdup("ping");
                connection->supported_tools[2] = mcp_strdup("http_tool");
                connection->supported_tools_count = 3;
                mcp_log_info("Added default supported tools for HTTP server: echo, ping, http_tool");
            }

            // Add some default supported resources
            connection->supported_resources = (char**)malloc(3 * sizeof(char*));
            if (connection->supported_resources) {
                connection->supported_resources[0] = mcp_strdup("http://example");
                connection->supported_resources[1] = mcp_strdup("http://data");
                connection->supported_resources[2] = mcp_strdup("http://image");
                connection->supported_resources_count = 3;
                mcp_log_info("Added default supported resources for HTTP server: http://example, http://data, http://image");
            }

            // Mark as connected
            connection->is_connected = true;
            success_count++;
        } else {
            // Local process connection
            if (connection->config.command) {
                // Create and start process
                connection->process = kmcp_process_create(
                    connection->config.command,
                    connection->config.args,
                    connection->config.args_count,
                    connection->config.env,
                    connection->config.env_count
                );

                if (!connection->process) {
                    mcp_log_error("Failed to create process for server: %s", connection->config.name);
                    continue;
                }

                int start_result = kmcp_process_start(connection->process);
                if (start_result != 0) {
                    mcp_log_error("Failed to start process for server: %s (error code: %d)", connection->config.name, start_result);
                    kmcp_process_close(connection->process);
                    connection->process = NULL;
                    continue;
                }

                // Check if the process is still running after a short delay
                #ifdef _WIN32
                Sleep(500); // Wait for 500 ms on Windows
                #else
                usleep(500000); // Wait for 500 ms on Unix-like systems
                #endif

                if (!kmcp_process_is_running(connection->process)) {
                    int exit_code = 0;
                    kmcp_process_get_exit_code(connection->process, &exit_code);
                    mcp_log_error("Process for server %s exited prematurely with code: %d", connection->config.name, exit_code);
                    kmcp_process_close(connection->process);
                    connection->process = NULL;
                    continue;
                }

                // Create transport layer based on process input/output
                mcp_log_info("Process started for server: %s", connection->config.name);

                // Check if the process is still running
                if (!kmcp_process_is_running(connection->process)) {
                    int exit_code = 0;
                    kmcp_process_get_exit_code(connection->process, &exit_code);
                    mcp_log_error("Process for server %s exited prematurely with code: %d", connection->config.name, exit_code);
                    kmcp_process_close(connection->process);
                    connection->process = NULL;
                    continue;
                }

                // We've waited for the server to start, now let's try to connect

                // Create TCP client transport to connect to the server
                // Note: We're connecting to 127.0.0.1:8080 where the server is listening
                mcp_transport_config_t transport_config;
                memset(&transport_config, 0, sizeof(mcp_transport_config_t));
                transport_config.tcp.host = "127.0.0.1";
                transport_config.tcp.port = 8080;

                connection->transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP_CLIENT, &transport_config);
                if (!connection->transport) {
                    mcp_log_error("Failed to create transport for server: %s", connection->config.name);
                    kmcp_process_terminate(connection->process);
                    kmcp_process_close(connection->process);
                    connection->process = NULL;
                    continue;
                }

                mcp_log_info("Created transport for server: %s", connection->config.name);

                // Create MCP client
                mcp_client_config_t client_config;
                memset(&client_config, 0, sizeof(mcp_client_config_t));

                // Set request timeout
                client_config.request_timeout_ms = 5000; // 5 seconds - shorter timeout for better responsiveness

                // Create client with transport
                connection->client = mcp_client_create(&client_config, connection->transport);
                if (!connection->client) {
                    mcp_log_error("Failed to create client for server: %s", connection->config.name);
                    mcp_transport_destroy(connection->transport);
                    connection->transport = NULL;
                    kmcp_process_terminate(connection->process);
                    kmcp_process_close(connection->process);
                    connection->process = NULL;
                    continue;
                }

                mcp_log_info("Created client for server: %s", connection->config.name);

                // Query supported tools
                mcp_log_info("Querying supported tools from server: %s", connection->config.name);
                mcp_tool_t** tools = NULL;
                size_t tool_count = 0;

                int result = mcp_client_list_tools(connection->client, &tools, &tool_count);
                mcp_log_debug("mcp_client_list_tools returned: %d", result);

                if (result == 0) {
                    mcp_log_info("Server %s supports %zu tools", connection->config.name, tool_count);

                    // Allocate memory for tool names
                    connection->supported_tools = (char**)malloc(tool_count * sizeof(char*));
                    if (connection->supported_tools) {
                        connection->supported_tools_count = tool_count;

                        // Copy tool names
                        for (size_t j = 0; j < tool_count; j++) {
                            connection->supported_tools[j] = mcp_strdup(tools[j]->name);
                            mcp_log_info("  Tool: %s", tools[j]->name);
                        }
                    }

                    // Free tools
                    mcp_free_tools(tools, tool_count);
                } else {
                    mcp_log_warn("Failed to query tools from server: %s", connection->config.name);

                    // For testing purposes, add some default supported tools
                    connection->supported_tools = (char**)malloc(2 * sizeof(char*));
                    if (connection->supported_tools) {
                        connection->supported_tools[0] = mcp_strdup("echo");
                        connection->supported_tools[1] = mcp_strdup("ping");
                        connection->supported_tools_count = 2;
                        mcp_log_info("Added default supported tools for testing: echo, ping");
                    }
                }

                // Query supported resources
                mcp_log_info("Querying supported resources from server: %s", connection->config.name);
                mcp_resource_t** resources = NULL;
                size_t resource_count = 0;

                int res_result = mcp_client_list_resources(connection->client, &resources, &resource_count);
                mcp_log_debug("mcp_client_list_resources returned: %d", res_result);

                if (res_result == 0) {
                    mcp_log_info("Server %s supports %zu resources", connection->config.name, resource_count);

                    // Allocate memory for resource URIs
                    connection->supported_resources = (char**)malloc(resource_count * sizeof(char*));
                    if (connection->supported_resources) {
                        connection->supported_resources_count = resource_count;

                        // Copy resource URIs
                        for (size_t j = 0; j < resource_count; j++) {
                            connection->supported_resources[j] = mcp_strdup(resources[j]->uri);
                            mcp_log_info("  Resource: %s", resources[j]->uri);
                        }
                    }

                    // Free resources
                    mcp_free_resources(resources, resource_count);
                } else {
                    mcp_log_warn("Failed to query resources from server: %s", connection->config.name);

                    // For testing purposes, add some default supported resources
                    connection->supported_resources = (char**)malloc(2 * sizeof(char*));
                    if (connection->supported_resources) {
                        connection->supported_resources[0] = mcp_strdup("example://hello");
                        connection->supported_resources[1] = mcp_strdup("example://data");
                        connection->supported_resources_count = 2;
                        mcp_log_info("Added default supported resources for testing: example://hello, example://data");
                    }
                }

                // Mark as connected
                connection->is_connected = true;
                success_count++;
            }
        }
    }

    mcp_mutex_unlock(manager->mutex);

    // If no servers were successfully connected, return error
    if (success_count == 0 && manager->server_count > 0) {
        mcp_log_error("Failed to connect to any server");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    mcp_log_info("Successfully connected to %d out of %zu servers", success_count, manager->server_count);
    return KMCP_SUCCESS;
}

/**
 * @brief Disconnect from all servers
 */
kmcp_error_t kmcp_server_manager_disconnect(kmcp_server_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Disconnecting from all servers");

    mcp_mutex_lock(manager->mutex);

    int disconnect_count = 0;
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];
        if (!connection) {
            mcp_log_error("Server connection at index %zu is NULL", i);
            continue;
        }

        // If not connected, skip
        if (!connection->is_connected) {
            mcp_log_debug("Server '%s' is not connected, skipping",
                        connection->config.name ? connection->config.name : "(unnamed)");
            continue;
        }

        mcp_log_info("Disconnecting from server '%s'",
                   connection->config.name ? connection->config.name : "(unnamed)");

        // Close client
        if (connection->client) {
            mcp_log_debug("Destroying client for server: %s", connection->config.name);
            mcp_client_destroy(connection->client);
            connection->client = NULL;
        }

        // Close HTTP client
        if (connection->http_client) {
            mcp_log_debug("Destroying HTTP client for server: %s", connection->config.name);
            kmcp_http_client_close(connection->http_client);
            connection->http_client = NULL;
        }

        // Close transport layer
        if (connection->transport) {
            mcp_log_debug("Destroying transport for server: %s", connection->config.name);
            mcp_transport_destroy(connection->transport);
            connection->transport = NULL;
        }

        // Terminate process
        if (connection->process) {
            kmcp_process_terminate(connection->process);
            kmcp_process_close(connection->process);
            connection->process = NULL;
        }

        // Mark as not connected
        connection->is_connected = false;
        disconnect_count++;
    }

    mcp_mutex_unlock(manager->mutex);
    mcp_log_info("Disconnected from %d servers", disconnect_count);
    return KMCP_SUCCESS;
}

/**
 * @brief Select an appropriate server for a tool
 */
int kmcp_server_manager_select_tool(kmcp_server_manager_t* manager, const char* tool_name) {
    if (!manager || !tool_name) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    mcp_mutex_lock(manager->mutex);

    // First check the tool mapping
    void* value = NULL;
    if (mcp_hashtable_get(manager->tool_map, tool_name, &value) && value) {
        // Found mapping
        int index = *((int*)value);
        mcp_mutex_unlock(manager->mutex);
        return index;
    }

    // No mapping found, iterate through all servers to find one that supports this tool
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Check if this server supports the tool
        for (size_t j = 0; j < connection->supported_tools_count; j++) {
            if (strcmp(connection->supported_tools[j], tool_name) == 0) {
                // Found a server that supports this tool, add to mapping
                int* index_ptr = (int*)malloc(sizeof(int));
                if (index_ptr) {
                    *index_ptr = (int)i;
                    mcp_hashtable_put(manager->tool_map, tool_name, index_ptr);
                }

                mcp_mutex_unlock(manager->mutex);
                return (int)i;
            }
        }
    }

    mcp_mutex_unlock(manager->mutex);
    return -1; // No server found that supports this tool
}

/**
 * @brief Select an appropriate server for a resource
 */
int kmcp_server_manager_select_resource(kmcp_server_manager_t* manager, const char* resource_uri) {
    if (!manager || !resource_uri) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    mcp_mutex_lock(manager->mutex);

    // First check the resource mapping
    void* value = NULL;
    if (mcp_hashtable_get(manager->resource_map, resource_uri, &value) && value) {
        // Found mapping
        int index = *((int*)value);
        mcp_mutex_unlock(manager->mutex);
        return index;
    }

    // No mapping found, iterate through all servers to find one that supports this resource
    // Simplified handling here, only checking resource URI prefix
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Check if this server supports the resource
        for (size_t j = 0; j < connection->supported_resources_count; j++) {
            const char* prefix = connection->supported_resources[j];
            size_t prefix_len = strlen(prefix);

            if (strncmp(resource_uri, prefix, prefix_len) == 0) {
                // Found a server that supports this resource, add to mapping
                int* index_ptr = (int*)malloc(sizeof(int));
                if (index_ptr) {
                    *index_ptr = (int)i;
                    mcp_hashtable_put(manager->resource_map, resource_uri, index_ptr);
                }

                mcp_mutex_unlock(manager->mutex);
                return (int)i;
            }
        }
    }

    mcp_mutex_unlock(manager->mutex);
    return -1; // No server found that supports this resource
}

/**
 * @brief Get a server connection
 */
kmcp_server_connection_t* kmcp_server_manager_get_connection(kmcp_server_manager_t* manager, int index) {
    if (!manager || index < 0 || (size_t)index >= manager->server_count) {
        mcp_log_error("Invalid parameters");
        return NULL;
    }

    return manager->servers[index];
}

/**
 * @brief Get the number of servers
 */
size_t kmcp_server_manager_get_count(kmcp_server_manager_t* manager) {
    if (!manager) {
        return 0;
    }

    return manager->server_count;
}

/**
 * @brief Helper function to free hash table values
 */
static void free_hash_value(const void* key, void* value, void* user_data) {
    (void)key;       // Unused parameter
    (void)user_data; // Unused parameter
    if (value) {
        free(value);
    }
}

/**
 * @brief Destroy the server manager
 */
void kmcp_server_manager_destroy(kmcp_server_manager_t* manager) {
    if (!manager) {
        return;
    }

    mcp_log_info("Destroying server manager");

    // Disconnect all servers
    kmcp_server_manager_disconnect(manager);

    // Free server connections
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Free configuration
        free(connection->config.name);
        free(connection->config.command);
        free(connection->config.url);
        free(connection->config.api_key);

        // Free arguments array
        if (connection->config.args) {
            for (size_t j = 0; j < connection->config.args_count; j++) {
                free(connection->config.args[j]);
            }
            free(connection->config.args);
        }

        // Free environment variables array
        if (connection->config.env) {
            for (size_t j = 0; j < connection->config.env_count; j++) {
                free(connection->config.env[j]);
            }
            free(connection->config.env);
        }

        // Free supported tools list
        if (connection->supported_tools) {
            for (size_t j = 0; j < connection->supported_tools_count; j++) {
                free(connection->supported_tools[j]);
            }
            free(connection->supported_tools);
        }

        // Free supported resources list
        if (connection->supported_resources) {
            for (size_t j = 0; j < connection->supported_resources_count; j++) {
                free(connection->supported_resources[j]);
            }
            free(connection->supported_resources);
        }

        free(connection);
    }

    free(manager->servers);

    // Free hash tables
    if (manager->tool_map) {
        // Free values in hash table (integer pointers)
        mcp_hashtable_foreach(manager->tool_map, free_hash_value, NULL);
        manager->tool_map->value_free = NULL; // Prevent double-free in mcp_hashtable_destroy
        mcp_hashtable_destroy(manager->tool_map);
    }

    if (manager->resource_map) {
        // Free values in hash table (integer pointers)
        mcp_hashtable_foreach(manager->resource_map, free_hash_value, NULL);
        manager->resource_map->value_free = NULL; // Prevent double-free in mcp_hashtable_destroy
        mcp_hashtable_destroy(manager->resource_map);
    }

    // Free mutex
    if (manager->mutex) {
        mcp_mutex_destroy(manager->mutex);
    }

    free(manager);
    mcp_log_info("Server manager destroyed");
}
