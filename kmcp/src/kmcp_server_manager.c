#include "kmcp_server_manager.h"
#include "kmcp_server_connection.h"
#include "kmcp_config_parser.h"
#include "kmcp_process.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include "mcp_socket_utils.h"
#include <mcp_transport.h>
#include <mcp_transport_factory.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

    // Health check thread
    volatile bool health_check_running;   // Whether health check thread is running
    mcp_thread_t health_check_thread;    // Health check thread handle
    int health_check_interval_ms;        // Interval between health checks
    int health_check_max_attempts;       // Maximum reconnection attempts
    int health_check_retry_interval_ms;  // Interval between reconnection attempts
};

/**
 * @brief Create a server manager
 *
 * @return kmcp_server_manager_t* Returns a new server manager or NULL on failure
 */
kmcp_server_manager_t* kmcp_server_create() {
    // Allocate memory for server manager and initialize to zero
    kmcp_server_manager_t* manager = (kmcp_server_manager_t*)calloc(1, sizeof(kmcp_server_manager_t));
    if (!manager) {
        mcp_log_error("Failed to allocate memory for server manager");
        return NULL;
    }

    // Fields are already initialized to zero by calloc

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
 void kmcp_server_config_free(kmcp_server_config_t* config) {
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
kmcp_error_t kmcp_server_load(kmcp_server_manager_t* manager, const char* config_file) {
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
        result = kmcp_server_add(manager, servers[i]);
        if (result != 0) {
            mcp_log_error("Failed to add server: %s", servers[i]->name ? servers[i]->name : "(unnamed)");
            // Continue adding other servers
        } else {
            success_count++;
        }

        // Free server configuration
        // Note: kmcp_server_add copies the configuration, so it's safe to free here
        kmcp_server_config_free(servers[i]);
    }

    // Free server configuration array
    free(servers);
    kmcp_config_parser_close(parser);

    mcp_log_info("Successfully added %d out of %zu server configurations", success_count, server_count);
    return success_count > 0 ? KMCP_SUCCESS : KMCP_ERROR_SERVER_NOT_FOUND;
}

/**
 * @brief Add a server to the manager
 *
 * @param manager Server manager (must not be NULL)
 * @param config Server configuration (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_add(kmcp_server_manager_t* manager, kmcp_server_config_t* config) {
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

    // Create a new server connection and initialize to zero
    kmcp_server_connection_t* connection = (kmcp_server_connection_t*)calloc(1, sizeof(kmcp_server_connection_t));
    if (!connection) {
        mcp_log_error("Failed to allocate memory for server connection");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

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
 * @brief Internal helper function to connect a single server connection
 *
 * @param connection Server connection to connect (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
static kmcp_error_t _kmcp_connect_single_server(kmcp_server_connection_t* connection) {
    if (!connection) {
        mcp_log_error("Invalid parameter: connection is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // If already connected, skip
    if (connection->is_connected) {
        mcp_log_info("Server '%s' is already connected, skipping",
                   connection->config.name ? connection->config.name : "(unnamed)");
        return KMCP_SUCCESS;
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
            return KMCP_ERROR_CONNECTION_FAILED; // Return error
        }

        mcp_log_info("Created HTTP client for server: %s", connection->config.name);

        // Query supported tools from the server
        kmcp_error_t result = kmcp_http_get_tools(
            connection->http_client,
            &connection->supported_tools,
            &connection->supported_tools_count
        );

        if (result != KMCP_SUCCESS) {
            mcp_log_warn("Failed to query supported tools from HTTP server: %s", connection->config.name);
            mcp_log_warn("Using default tools list");

            // Use default tools if query fails
            connection->supported_tools = (char**)malloc(3 * sizeof(char*));
            if (connection->supported_tools) {
                connection->supported_tools[0] = mcp_strdup("echo");
                connection->supported_tools[1] = mcp_strdup("ping");
                connection->supported_tools[2] = mcp_strdup("http_tool");
                connection->supported_tools_count = 3;
                mcp_log_info("Added default supported tools for HTTP server: echo, ping, http_tool");
            }
        } else {
            mcp_log_info("Successfully queried %zu tools from HTTP server: %s",
                         connection->supported_tools_count, connection->config.name);
        }

        // Query supported resources from the server
        result = kmcp_http_get_resources(
            connection->http_client,
            &connection->supported_resources,
            &connection->supported_resources_count
        );

        if (result != KMCP_SUCCESS) {
            mcp_log_warn("Failed to query supported resources from HTTP server: %s", connection->config.name);
            mcp_log_warn("Using default resources list");

            // Use default resources if query fails
            connection->supported_resources = (char**)malloc(3 * sizeof(char*));
            if (connection->supported_resources) {
                connection->supported_resources[0] = mcp_strdup("http://example");
                connection->supported_resources[1] = mcp_strdup("http://data");
                connection->supported_resources[2] = mcp_strdup("http://image");
                connection->supported_resources_count = 3;
                mcp_log_info("Added default supported resources for HTTP server: http://example, http://data, http://image");
            }
        } else {
            mcp_log_info("Successfully queried %zu resources from HTTP server: %s",
                         connection->supported_resources_count, connection->config.name);
        }

        // Log the results
        if (connection->supported_tools_count > 0) {
            mcp_log_info("Server %s supports %zu tools:", connection->config.name, connection->supported_tools_count);
            for (size_t tool_idx = 0; tool_idx < connection->supported_tools_count && tool_idx < 10; tool_idx++) {
                mcp_log_info("  - %s", connection->supported_tools[tool_idx]);
            }
            if (connection->supported_tools_count > 10) {
                mcp_log_info("  ... and %zu more", connection->supported_tools_count - 10);
            }
        } else {
            mcp_log_warn("Server %s does not support any tools", connection->config.name);
        }

        if (connection->supported_resources_count > 0) {
            mcp_log_info("Server %s supports %zu resources:", connection->config.name, connection->supported_resources_count);
            for (size_t res_idx = 0; res_idx < connection->supported_resources_count && res_idx < 10; res_idx++) {
                mcp_log_info("  - %s", connection->supported_resources[res_idx]);
            }
            if (connection->supported_resources_count > 10) {
                mcp_log_info("  ... and %zu more", connection->supported_resources_count - 10);
            }
        } else {
            mcp_log_warn("Server %s does not support any resources", connection->config.name);
        }

        // Mark as connected
        connection->is_connected = true;
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
                return KMCP_ERROR_PROCESS_FAILED;
            }

            int start_result = kmcp_process_start(connection->process);
            if (start_result != 0) {
                mcp_log_error("Failed to start process for server: %s (error code: %d)", connection->config.name, start_result);
                kmcp_process_close(connection->process);
                connection->process = NULL;
                return KMCP_ERROR_PROCESS_FAILED;
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
                return KMCP_ERROR_PROCESS_FAILED;
            }

            // Create transport layer based on process input/output
            mcp_log_info("Process started for server: %s", connection->config.name);

            // Check if the process is still running
            if (!kmcp_process_is_running(connection->process)) {
                int exit_code = 0;
                kmcp_process_get_exit_code(connection->process, &exit_code);
                mcp_log_error("Process for server %s exited prematurely with code: %d",
                             connection->config.name ? connection->config.name : "(unnamed)", exit_code);
                kmcp_process_close(connection->process);
                connection->process = NULL;
                return KMCP_ERROR_PROCESS_FAILED;
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
                return KMCP_ERROR_CONNECTION_FAILED;
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
                return KMCP_ERROR_CONNECTION_FAILED;
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
        } else {
             mcp_log_warn("Server '%s' has no command configured, cannot connect locally.",
                        connection->config.name ? connection->config.name : "(unnamed)");
             return KMCP_ERROR_CONFIG_INVALID; // Cannot connect without command or URL
        }
    }

    return KMCP_SUCCESS;
}


/**
 * @brief Connect to all servers in the manager
 *
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS if at least one server was connected, or an error code on failure
 */
kmcp_error_t kmcp_server_connect(kmcp_server_manager_t* manager) {
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
            continue; // Skip NULL connection
        }

        // Attempt to connect this server using the helper function
        kmcp_error_t connect_result = _kmcp_connect_single_server(connection);
        if (connect_result == KMCP_SUCCESS) {
            success_count++;
        } else {
            mcp_log_error("Failed to connect to server '%s' (error: %d)",
                        connection->config.name ? connection->config.name : "(unnamed)", connect_result);
            // Continue trying to connect other servers
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
 * @brief Disconnect from all servers in the manager
 *
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_disconnect(kmcp_server_manager_t* manager) {
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
            //MUST reset connection->transport, becasue it has been destoryed in mcp_client_destroy()
            connection->transport = NULL; // Transport is destroyed by mcp_client_destroy
        }

        // Close HTTP client
        if (connection->http_client) {
            mcp_log_debug("Destroying HTTP client for server: %s", connection->config.name);
            kmcp_http_client_close(connection->http_client);
            connection->http_client = NULL;
        }

        // Transport layer is handled by client destroy or http client close, no need to destroy separately

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
 *
 * @param manager Server manager (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @return int Returns the server index on success, or -1 if no server supports the tool
 */
int kmcp_server_select_tool(kmcp_server_manager_t* manager, const char* tool_name) {
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
    // First check connected servers, then try disconnected ones

    // First pass: check connected servers
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Skip disconnected servers in first pass
        if (!connection->is_connected) {
            continue;
        }

        // Check if this server supports the tool
        for (size_t j = 0; j < connection->supported_tools_count; j++) {
            const char* supported_tool = connection->supported_tools[j];
            if (!supported_tool) continue;

            if (strcmp(supported_tool, tool_name) == 0) {
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

    // Second pass: check disconnected servers
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Skip null or connected servers in second pass
        if (!connection || connection->is_connected) {
            continue;
        }

        // Check if this server supports the tool
        for (size_t j = 0; j < connection->supported_tools_count; j++) {
            const char* supported_tool = connection->supported_tools[j];
            if (!supported_tool) continue;

            if (strcmp(supported_tool, tool_name) == 0) {
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
 *
 * @param manager Server manager (must not be NULL)
 * @param resource_uri Resource URI (must not be NULL)
 * @return int Returns the server index on success, or -1 if no server supports the resource
 */
int kmcp_server_select_resource(kmcp_server_manager_t* manager, const char* resource_uri) {
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
    // First check connected servers, then try disconnected ones

    // First pass: check connected servers
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Skip null or disconnected servers in first pass
        if (!connection || !connection->is_connected) {
            continue;
        }

        // Check if this server supports the resource
        for (size_t j = 0; j < connection->supported_resources_count; j++) {
            const char* prefix = connection->supported_resources[j];
            if (!prefix) continue;

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

    // Second pass: check disconnected servers
    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];

        // Skip null or connected servers in second pass
        if (!connection || connection->is_connected) {
            continue;
        }

        // Check if this server supports the resource
        for (size_t j = 0; j < connection->supported_resources_count; j++) {
            const char* prefix = connection->supported_resources[j];
            if (!prefix) continue;

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
kmcp_server_connection_t* kmcp_server_get_connection(kmcp_server_manager_t* manager, int index) {
    if (!manager || index < 0 || (size_t)index >= manager->server_count) {
        mcp_log_error("Invalid parameters");
        return NULL;
    }

    return manager->servers[index];
}

/**
 * @brief Get the number of servers
 */
size_t kmcp_server_get_count(kmcp_server_manager_t* manager) {
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
 * @brief Destroy the server manager and free all resources
 *
 * @param manager Server manager to destroy (can be NULL)
 */
void kmcp_server_destroy(kmcp_server_manager_t* manager) {
    if (!manager) {
        return;
    }

    mcp_log_info("Destroying server manager");

    // Disconnect all servers
    kmcp_server_disconnect(manager);

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

    // Stop health check thread if running
    if (manager->health_check_running) {
        kmcp_server_stop_health_check(manager);
    }

    // Free mutex
    if (manager->mutex) {
        mcp_mutex_destroy(manager->mutex);
    }

    free(manager);
    mcp_log_info("Server manager destroyed");
}

/**
 * @brief Health check thread function
 *
 * @param arg Pointer to the server manager (must not be NULL)
 * @return void* Always returns NULL
 */
static void* health_check_thread_func(void* arg) {
    kmcp_server_manager_t* manager = (kmcp_server_manager_t*)arg;
    if (!manager) {
        mcp_log_error("Health check thread received NULL manager");
        return NULL;
    }

    mcp_log_info("Health check thread started with interval %d ms", manager->health_check_interval_ms);

    while (manager->health_check_running) {
        // Check health of all servers
        mcp_log_debug("Running health check cycle");
        kmcp_error_t result = kmcp_server_check_health(
            manager,
            manager->health_check_max_attempts,
            manager->health_check_retry_interval_ms
        );

        if (result != KMCP_SUCCESS) {
            mcp_log_warn("Health check cycle completed with issues");
        } else {
            mcp_log_debug("Health check cycle completed successfully");
        }

        // Sleep for the specified interval
#ifdef _WIN32
        Sleep(manager->health_check_interval_ms);
#else
        usleep(manager->health_check_interval_ms * 1000);
#endif
    }

    mcp_log_info("Health check thread stopped");
    return NULL;
}

/**
 * @brief Get current timestamp in milliseconds
 */
static int64_t get_current_time_ms() {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER li;
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals to milliseconds
    // Windows epoch starts at January 1, 1601 (UTC)
    // Need to subtract the difference to get Unix epoch (January 1, 1970)
    return (int64_t)((li.QuadPart - 116444736000000000LL) / 10000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

/**
 * @brief Check if a server is healthy
 *
 * @param connection Server connection to check (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS if the server is healthy, or an error code otherwise
 */
static kmcp_error_t check_server_health(kmcp_server_connection_t* connection) {
    if (!connection) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // If not connected, server is not healthy
    if (!connection->is_connected) {
        connection->is_healthy = false;
        mcp_log_debug("Server '%s' is not connected",
                    connection->config.name ? connection->config.name : "(unnamed)");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Check if server is responsive
    if (connection->config.is_http) {
        // For HTTP connections, check if HTTP client is valid
        if (!connection->http_client) {
            connection->is_healthy = false;
            mcp_log_debug("Server '%s' has no HTTP client",
                        connection->config.name ? connection->config.name : "(unnamed)");
            return KMCP_ERROR_CONNECTION_FAILED;
        }

        // Send a simple ping request to check if server is responsive
        int status = 0;
        char* response = NULL;
        kmcp_error_t result = kmcp_http_client_send_with_timeout(
            connection->http_client,
            "GET",
            "ping",
            NULL,
            NULL,
            &response,
            &status,
            2000  // Use a short timeout (2 seconds) for health checks
        );

        // Free response
        if (response) {
            free(response);
        }

        // Check result
        if (result != KMCP_SUCCESS || status != 200) {
            connection->is_healthy = false;
            mcp_log_debug("Server '%s' HTTP health check failed: result=%d, status=%d",
                        connection->config.name ? connection->config.name : "(unnamed)",
                        result, status);
            return KMCP_ERROR_CONNECTION_FAILED;
        }
    } else {
        // For local connections, check if client is valid
        if (!connection->client) {
            connection->is_healthy = false;
            mcp_log_debug("Server '%s' has no MCP client",
                        connection->config.name ? connection->config.name : "(unnamed)");
            return KMCP_ERROR_CONNECTION_FAILED;
        }

        // Send a simple ping request to check if server is responsive
        mcp_content_item_t** response_items = NULL;
        size_t response_count = 0;
        int result = mcp_client_call_tool(
            connection->client,
            "ping",
            NULL,
            &response_items,
            &response_count,
            NULL  // No need for async flag
        );

        // Free response
        if (response_items) {
            // Just free the array, assuming the client API takes care of freeing the items
            // or they are freed elsewhere
            free(response_items);
        }

        // Check result
        if (result != 0) {
            connection->is_healthy = false;
            mcp_log_debug("Server '%s' MCP ping failed: result=%d",
                        connection->config.name ? connection->config.name : "(unnamed)",
                        result);
            return KMCP_ERROR_CONNECTION_FAILED;
        }
    }

    // Server is healthy
    connection->is_healthy = true;
    connection->health_check_failures = 0;
    connection->last_health_check_time = get_current_time_ms();

    mcp_log_debug("Server '%s' health check passed",
                connection->config.name ? connection->config.name : "(unnamed)");
    return KMCP_SUCCESS;
}

/**
 * @brief Check the health of all server connections
 */
kmcp_error_t kmcp_server_check_health(
    kmcp_server_manager_t* manager,
    int max_attempts,
    int retry_interval_ms
) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Checking health of all server connections");

    mcp_mutex_lock(manager->mutex);

    int success_count = 0;
    int total_count = 0;

    for (size_t i = 0; i < manager->server_count; i++) {
        kmcp_server_connection_t* connection = manager->servers[i];
        if (!connection) {
            mcp_log_error("Server connection at index %zu is NULL", i);
            continue;
        }

        total_count++;

        // Check if server is healthy
        kmcp_error_t result = check_server_health(connection);
        if (result == KMCP_SUCCESS) {
            mcp_log_info("Server '%s' is healthy",
                       connection->config.name ? connection->config.name : "(unnamed)");
            success_count++;
            continue;
        }

        // Server is not healthy, increment failure count
        connection->health_check_failures++;
        mcp_log_warn("Server '%s' is not healthy (failure count: %d)",
                   connection->config.name ? connection->config.name : "(unnamed)",
                   connection->health_check_failures);

        // If server is not connected, try to reconnect
        if (!connection->is_connected) {
            mcp_log_info("Attempting to reconnect to server '%s'",
                       connection->config.name ? connection->config.name : "(unnamed)");

            // Unlock mutex before calling reconnect to avoid deadlock
            mcp_mutex_unlock(manager->mutex);

            // Attempt to reconnect
            result = kmcp_server_reconnect(manager, (int)i, max_attempts, retry_interval_ms);

            // Lock mutex again
            mcp_mutex_lock(manager->mutex);

            if (result == KMCP_SUCCESS) {
                mcp_log_info("Successfully reconnected to server '%s'",
                           connection->config.name ? connection->config.name : "(unnamed)");
                success_count++;
            } else {
                mcp_log_error("Failed to reconnect to server '%s'",
                            connection->config.name ? connection->config.name : "(unnamed)");
            }
        }
    }

    mcp_mutex_unlock(manager->mutex);

    if (total_count == 0) {
        mcp_log_info("No servers to check");
        return KMCP_SUCCESS;
    }

    if (success_count == 0) {
        mcp_log_error("All servers are unhealthy");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    if (success_count < total_count) {
        mcp_log_warn("%d out of %d servers are healthy", success_count, total_count);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    mcp_log_info("All %d servers are healthy", total_count);
    return KMCP_SUCCESS;
}

/**
 * @brief Start a background health check thread
 *
 * @param manager Server manager (must not be NULL)
 * @param interval_ms Interval between health checks in milliseconds (must be positive)
 * @param max_attempts Maximum number of reconnection attempts (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_start_health_check(
    kmcp_server_manager_t* manager,
    int interval_ms,
    int max_attempts,
    int retry_interval_ms
) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    if (interval_ms <= 0) {
        mcp_log_error("Invalid interval: %d", interval_ms);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_mutex_lock(manager->mutex);

    // Check if health check thread is already running
    if (manager->health_check_running) {
        mcp_log_warn("Health check thread is already running");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_SUCCESS;
    }

    // Set health check parameters
    manager->health_check_interval_ms = interval_ms;
    manager->health_check_max_attempts = max_attempts;
    manager->health_check_retry_interval_ms = retry_interval_ms;

    // Set running flag before creating thread
    manager->health_check_running = true;

    // Create and start health check thread
    int result = mcp_thread_create(&manager->health_check_thread, health_check_thread_func, manager);
    if (result != 0) {
        mcp_log_error("Failed to create health check thread");
        manager->health_check_running = false;
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_THREAD_CREATION;
    }

    mcp_log_info("Health check thread started with interval %d ms", interval_ms);
    mcp_mutex_unlock(manager->mutex);
    return KMCP_SUCCESS;
}

/**
 * @brief Stop the background health check thread
 *
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_stop_health_check(kmcp_server_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_mutex_lock(manager->mutex);

    // Check if health check thread is running
    if (!manager->health_check_running) {
        mcp_log_warn("Health check thread is not running");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_SUCCESS;
    }

    // Signal thread to stop
    manager->health_check_running = false;

    // Release mutex while waiting for thread to finish
    mcp_mutex_unlock(manager->mutex);

    // Wait for thread to finish
    int result = mcp_thread_join(manager->health_check_thread, NULL);
    if (result != 0) {
        mcp_log_error("Failed to join health check thread");
        return KMCP_ERROR_INTERNAL;
    }

    mcp_log_info("Health check thread stopped");
    return KMCP_SUCCESS;
}

/**
 * @brief Reconnect to a server
 *
 * @param manager Server manager (must not be NULL)
 * @param server_index Index of the server to reconnect to
 * @param max_attempts Maximum number of reconnection attempts (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_reconnect(
    kmcp_server_manager_t* manager,
    int server_index,
    int max_attempts,
    int retry_interval_ms
) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    if (server_index < 0 || server_index >= (int)manager->server_count) {
        mcp_log_error("Invalid server index: %d", server_index);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_mutex_lock(manager->mutex);

    kmcp_server_connection_t* connection = manager->servers[server_index];
    if (!connection) {
        mcp_log_error("Server connection at index %d is NULL", server_index);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_SERVER_NOT_FOUND;
    }

    // If already connected, skip
    if (connection->is_connected) {
        mcp_log_info("Server '%s' is already connected, skipping",
                   connection->config.name ? connection->config.name : "(unnamed)");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_SUCCESS;
    }

    mcp_log_info("Reconnecting to server '%s'",
               connection->config.name ? connection->config.name : "(unnamed)");

    // Attempt to reconnect
    int attempt = 0;
    kmcp_error_t result = KMCP_ERROR_CONNECTION_FAILED;

    while ((max_attempts == 0 || attempt < max_attempts) && result != KMCP_SUCCESS) {
        if (attempt > 0) {
            mcp_log_info("Reconnection attempt %d for server '%s'",
                       attempt, connection->config.name ? connection->config.name : "(unnamed)");
            mcp_sleep_ms(retry_interval_ms);
        }

        // Disconnect first if needed
        if (connection->client) {
            mcp_client_destroy(connection->client);
            connection->client = NULL;
        }

        if (connection->transport) {
            mcp_transport_destroy(connection->transport);
            connection->transport = NULL;
        }

        if (connection->http_client) {
            kmcp_http_client_close(connection->http_client);
            connection->http_client = NULL;
        }

        if (connection->process) {
            kmcp_process_terminate(connection->process);
            kmcp_process_close(connection->process);
            connection->process = NULL;
        }

        // Free previous tools and resources
        if (connection->supported_tools) {
            for (size_t j = 0; j < connection->supported_tools_count; j++) {
                free(connection->supported_tools[j]);
            }
            free(connection->supported_tools);
            connection->supported_tools = NULL;
            connection->supported_tools_count = 0;
        }

        if (connection->supported_resources) {
            for (size_t j = 0; j < connection->supported_resources_count; j++) {
                free(connection->supported_resources[j]);
            }
            free(connection->supported_resources);
            connection->supported_resources = NULL;
            connection->supported_resources_count = 0;
        }

        // Reset connection state
        connection->is_connected = false;

        // Try to connect using the helper function for the specific connection
        result = _kmcp_connect_single_server(connection);
        attempt++;
    }

    if (result == KMCP_SUCCESS) {
        mcp_log_info("Successfully reconnected to server '%s' after %d attempts",
                   connection->config.name ? connection->config.name : "(unnamed)", attempt);
    } else {
        mcp_log_error("Failed to reconnect to server '%s' after %d attempts",
                    connection->config.name ? connection->config.name : "(unnamed)", attempt);
    }

    mcp_mutex_unlock(manager->mutex);
    return result;
}

/**
 * @brief Reconnect to all disconnected servers
 *
 * @param manager Server manager (must not be NULL)
 * @param max_attempts Maximum number of reconnection attempts (0 for unlimited)
 * @param retry_interval_ms Interval between reconnection attempts in milliseconds
 * @return kmcp_error_t Returns KMCP_SUCCESS if all servers were reconnected, or an error code otherwise
 */
kmcp_error_t kmcp_server_reconnect_all(
    kmcp_server_manager_t* manager,
    int max_attempts,
    int retry_interval_ms
) {
    if (!manager) {
        mcp_log_error("Invalid parameter");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Reconnecting to all disconnected servers");

    mcp_mutex_lock(manager->mutex);

    int success_count = 0;
    int total_count = 0;

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

        total_count++;

        // Unlock mutex before calling reconnect to avoid deadlock
        mcp_mutex_unlock(manager->mutex);

        // Attempt to reconnect
        kmcp_error_t result = kmcp_server_reconnect(manager, (int)i, max_attempts, retry_interval_ms);

        // Lock mutex again
        mcp_mutex_lock(manager->mutex);

        if (result == KMCP_SUCCESS) {
            success_count++;
        }
    }

    mcp_mutex_unlock(manager->mutex);

    if (total_count == 0) {
        mcp_log_info("No disconnected servers to reconnect");
        return KMCP_SUCCESS;
    }

    if (success_count == 0) {
        mcp_log_error("Failed to reconnect to any server");
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    if (success_count < total_count) {
        mcp_log_warn("Reconnected to %d out of %d disconnected servers", success_count, total_count);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    mcp_log_info("Successfully reconnected to all %d disconnected servers", total_count);
    return KMCP_SUCCESS;
}

/**
 * @brief Remove a server from the manager
 */
kmcp_error_t kmcp_server_remove(kmcp_server_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Removing server: %s", name);
    mcp_mutex_lock(manager->mutex);

    // Find the server index
    int server_index = -1;
    for (size_t i = 0; i < manager->server_count; i++) {
        if (manager->servers[i] && manager->servers[i]->config.name && strcmp(manager->servers[i]->config.name, name) == 0) {
            server_index = (int)i;
            break;
        }
    }

    if (server_index == -1) {
        mcp_log_error("Server not found: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_SERVER_NOT_FOUND;
    }

    // Get the connection
    kmcp_server_connection_t* connection = manager->servers[server_index];

    // Disconnect if connected
    if (connection->is_connected) {
        mcp_log_info("Disconnecting server '%s' before removal", name);
        if (connection->client) {
            mcp_client_destroy(connection->client);
            connection->client = NULL;
            connection->transport = NULL; // Transport is destroyed by mcp_client_destroy
        }
        if (connection->http_client) {
            kmcp_http_client_close(connection->http_client);
            connection->http_client = NULL;
        }
        if (connection->process) {
            kmcp_process_terminate(connection->process);
            kmcp_process_close(connection->process);
            connection->process = NULL;
        }
        connection->is_connected = false;
    }

    // Free connection resources
    free(connection->config.name);
    free(connection->config.command);
    free(connection->config.url);
    free(connection->config.api_key);
    if (connection->config.args) {
        for (size_t j = 0; j < connection->config.args_count; j++) {
            free(connection->config.args[j]);
        }
        free(connection->config.args);
    }
    if (connection->config.env) {
        for (size_t j = 0; j < connection->config.env_count; j++) {
            free(connection->config.env[j]);
        }
        free(connection->config.env);
    }
    if (connection->supported_tools) {
        for (size_t j = 0; j < connection->supported_tools_count; j++) {
            free(connection->supported_tools[j]);
        }
        free(connection->supported_tools);
    }
    if (connection->supported_resources) {
        for (size_t j = 0; j < connection->supported_resources_count; j++) {
            free(connection->supported_resources[j]);
        }
        free(connection->supported_resources);
    }
    free(connection);

    // Shift remaining servers in the array
    for (size_t i = (size_t)server_index; i < manager->server_count - 1; i++) {
        manager->servers[i] = manager->servers[i + 1];
    }
    manager->servers[manager->server_count - 1] = NULL; // Clear the last slot
    manager->server_count--;

    // Clear the tool and resource maps as indices are now invalid
    // A more sophisticated approach would update indices, but clearing is simpler
    mcp_hashtable_clear(manager->tool_map);
    mcp_hashtable_clear(manager->resource_map);

    mcp_mutex_unlock(manager->mutex);
    mcp_log_info("Server removed: %s", name);
    return KMCP_SUCCESS;
}

/**
 * @brief Get a server configuration by name
 */
kmcp_error_t kmcp_server_get_config(kmcp_server_manager_t* manager, const char* name, kmcp_server_config_t** config) {
    if (!manager || !name || !config) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }
    *config = NULL; // Initialize output

    mcp_log_debug("Getting server configuration: %s", name);
    mcp_mutex_lock(manager->mutex);

    // Find the server by name
    kmcp_server_connection_t* connection = NULL;
    for (size_t i = 0; i < manager->server_count; i++) {
        if (manager->servers[i] && manager->servers[i]->config.name && strcmp(manager->servers[i]->config.name, name) == 0) {
            connection = manager->servers[i];
            break;
        }
    }

    if (!connection) {
        mcp_log_error("Server not found: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_SERVER_NOT_FOUND;
    }

    // Clone the configuration (with explicit cast)
    *config = kmcp_server_config_clone((const kmcp_server_config_t*)&connection->config);
    if (!*config) {
        mcp_log_error("Failed to clone server configuration for %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    mcp_mutex_unlock(manager->mutex);
    return KMCP_SUCCESS;
}

/**
 * @brief Get a server configuration by index
 */
kmcp_error_t kmcp_server_get_config_by_index(kmcp_server_manager_t* manager, size_t index, kmcp_server_config_t** config) {
    if (!manager || !config) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }
    *config = NULL; // Initialize output

    mcp_log_debug("Getting server configuration at index: %zu", index);
    mcp_mutex_lock(manager->mutex);

    // Validate index
    if (index >= manager->server_count) {
        mcp_log_error("Invalid server index: %zu (count: %zu)", index, manager->server_count);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Get the connection
    kmcp_server_connection_t* connection = manager->servers[index];
    if (!connection) {
         mcp_log_error("Server connection at index %zu is NULL", index);
         mcp_mutex_unlock(manager->mutex);
         return KMCP_ERROR_INTERNAL; // Should not happen if index is valid
    }

    // Clone the configuration (with explicit cast)
    *config = kmcp_server_config_clone((const kmcp_server_config_t*)&connection->config);
    if (!*config) {
        mcp_log_error("Failed to clone server configuration at index %zu", index);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    mcp_mutex_unlock(manager->mutex);
    return KMCP_SUCCESS;
}

/**
 * @brief Clone a server configuration
 */
kmcp_server_config_t* kmcp_server_config_clone(const kmcp_server_config_t* config) {
    if (!config) {
        mcp_log_error("Invalid parameter: config is NULL");
        return NULL;
    }

    // Allocate memory for the new configuration
    kmcp_server_config_t* new_config = (kmcp_server_config_t*)malloc(sizeof(kmcp_server_config_t));
    if (!new_config) {
        mcp_log_error("Failed to allocate memory for server configuration");
        return NULL;
    }

    // Initialize the new configuration
    memset(new_config, 0, sizeof(kmcp_server_config_t));

    // Copy simple fields
    new_config->is_http = config->is_http;
    new_config->args_count = config->args_count;
    new_config->env_count = config->env_count;

    // Copy name
    if (config->name) {
        new_config->name = mcp_strdup(config->name);
        if (!new_config->name) {
            mcp_log_error("Failed to duplicate server name");
            kmcp_server_config_free(new_config);
            return NULL;
        }
    }

    // Copy URL
    if (config->url) {
        new_config->url = mcp_strdup(config->url);
        if (!new_config->url) {
            mcp_log_error("Failed to duplicate server URL");
            kmcp_server_config_free(new_config);
            return NULL;
        }
    }

    // Copy API key
    if (config->api_key) {
        new_config->api_key = mcp_strdup(config->api_key);
        if (!new_config->api_key) {
            mcp_log_error("Failed to duplicate server API key");
            kmcp_server_config_free(new_config);
            return NULL;
        }
    }

    // Copy command
    if (config->command) {
        new_config->command = mcp_strdup(config->command);
        if (!new_config->command) {
            mcp_log_error("Failed to duplicate server command");
            kmcp_server_config_free(new_config);
            return NULL;
        }
    }

    // Copy arguments
    if (config->args && config->args_count > 0) {
        new_config->args = (char**)malloc(config->args_count * sizeof(char*));
        if (!new_config->args) {
            mcp_log_error("Failed to allocate memory for server arguments");
            kmcp_server_config_free(new_config);
            return NULL;
        }

        // Initialize arguments array
        memset(new_config->args, 0, config->args_count * sizeof(char*));

        // Copy each argument
        for (size_t i = 0; i < config->args_count; i++) {
            if (config->args[i]) {
                new_config->args[i] = mcp_strdup(config->args[i]);
                if (!new_config->args[i]) {
                    mcp_log_error("Failed to duplicate server argument");
                    kmcp_server_config_free(new_config);
                    return NULL;
                }
            }
        }
    }

    // Copy environment variables
    if (config->env && config->env_count > 0) {
        new_config->env = (char**)malloc(config->env_count * sizeof(char*));
        if (!new_config->env) {
            mcp_log_error("Failed to allocate memory for server environment variables");
            kmcp_server_config_free(new_config);
            return NULL;
        }

        // Initialize environment variables array
        memset(new_config->env, 0, config->env_count * sizeof(char*));

        // Copy each environment variable
        for (size_t i = 0; i < config->env_count; i++) {
            if (config->env[i]) {
                new_config->env[i] = mcp_strdup(config->env[i]);
                if (!new_config->env[i]) {
                    mcp_log_error("Failed to duplicate server environment variable");
                    kmcp_server_config_free(new_config);
                    return NULL;
                }
            }
        }
    }

    return new_config;
}
