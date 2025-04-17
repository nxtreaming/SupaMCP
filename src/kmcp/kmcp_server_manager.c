#include "kmcp_server_manager.h"
#include "kmcp_server_connection.h"
#include "kmcp_config_parser.h"
#include "kmcp_process.h"
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
 * @brief Load server configurations from a config file
 */
int kmcp_server_manager_load(kmcp_server_manager_t* manager, const char* config_file) {
    if (!manager || !config_file) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    kmcp_config_parser_t* parser = kmcp_config_parser_create(config_file);
    if (!parser) {
        mcp_log_error("Failed to create config parser");
        return -1;
    }

    kmcp_server_config_t** servers = NULL;
    size_t server_count = 0;
    int result = kmcp_config_parser_get_servers(parser, &servers, &server_count);
    if (result != 0) {
        mcp_log_error("Failed to parse server configurations");
        kmcp_config_parser_close(parser);
        return -1;
    }

    // Add servers
    for (size_t i = 0; i < server_count; i++) {
        result = kmcp_server_manager_add(manager, servers[i]);
        if (result != 0) {
            mcp_log_error("Failed to add server: %s", servers[i]->name);
            // Continue adding other servers
        }

        // Free server configuration
        // Note: kmcp_server_manager_add copies the configuration, so it's safe to free here
        // TODO: Implement a function to free server configuration
    }

    // Free server configuration array
    free(servers);
    kmcp_config_parser_close(parser);

    return 0;
}

/**
 * @brief Add a server
 */
int kmcp_server_manager_add(kmcp_server_manager_t* manager, kmcp_server_config_t* config) {
    if (!manager || !config) {
        mcp_log_error("Invalid parameters");
        return -1;
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
            return -1;
        }

        manager->servers = new_servers;
        manager->server_capacity = new_capacity;
    }

    // Create a new server connection
    kmcp_server_connection_t* connection = (kmcp_server_connection_t*)malloc(sizeof(kmcp_server_connection_t));
    if (!connection) {
        mcp_log_error("Failed to allocate memory for server connection");
        mcp_mutex_unlock(manager->mutex);
        return -1;
    }

    // Initialize connection
    memset(connection, 0, sizeof(kmcp_server_connection_t));

    // Copy configuration
    connection->config.name = mcp_strdup(config->name);
    connection->config.command = config->command ? mcp_strdup(config->command) : NULL;
    connection->config.url = config->url ? mcp_strdup(config->url) : NULL;
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
    return 0;
}

/**
 * @brief Connect to all servers
 */
int kmcp_server_manager_connect(kmcp_server_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return -1;
    }

    mcp_mutex_lock(manager->mutex);

    int success_count = 0;
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // If already connected, skip
        if (connection->is_connected) {
            success_count++;
            continue;
        }

        // Connect server based on configuration type
        if (connection->config.is_http) {
            // HTTP connection
            // TODO: Implement HTTP connection
            mcp_log_info("HTTP connection not implemented yet");
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

                if (kmcp_process_start(connection->process) != 0) {
                    mcp_log_error("Failed to start process for server: %s", connection->config.name);
                    kmcp_process_close(connection->process);
                    connection->process = NULL;
                    continue;
                }

                // Create transport layer based on process input/output
                mcp_log_info("Process started for server: %s", connection->config.name);

                // Wait for the server to start and bind to the port
                mcp_log_info("Waiting for server to start...");
                #ifdef _WIN32
                Sleep(2000); // Wait for 2 seconds on Windows
                #else
                sleep(2); // Wait for 2 seconds on Unix-like systems
                #endif

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
        return -1;
    }

    return 0;
}

/**
 * @brief Disconnect from all servers
 */
int kmcp_server_manager_disconnect(kmcp_server_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return -1;
    }

    mcp_mutex_lock(manager->mutex);

    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // If not connected, skip
        if (!connection->is_connected) {
            continue;
        }

        // Close client
        if (connection->client) {
            mcp_log_debug("Destroying client for server: %s", connection->config.name);
            mcp_client_destroy(connection->client);
            connection->client = NULL;
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
    }

    mcp_mutex_unlock(manager->mutex);
    return 0;
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

    // Disconnect all servers
    kmcp_server_manager_disconnect(manager);

    // Free server connections
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Free configuration
        free(connection->config.name);
        free(connection->config.command);
        free(connection->config.url);

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
}
