#include "kmcp_config_parser.h"
#include "mcp_log.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Helper functions for JSON operations

/**
 * @brief Get a string from a JSON object property
 *
 * @param json The JSON object
 * @param name The property name
 * @param result Pointer to receive the string value
 * @param required Whether the property is required
 * @return int 0 on success, -1 on failure
 */
static int get_string_from_property(const mcp_json_t* json, const char* name, const char** result, bool required) {
    if (!json || !name || !result) return -1;
    *result = NULL;

    mcp_json_t* value = mcp_json_object_get_property(json, name);
    if (!value) {
        if (required) {
            mcp_log_error("Required property '%s' not found", name);
            return -1;
        }
        return 0; // Not found but not required
    }

    if (!mcp_json_is_string(value)) {
        mcp_log_error("Property '%s' is not a string", name);
        return -1;
    }

    return mcp_json_get_string(value, result);
}

/**
 * @brief Get a string from a JSON array item
 *
 * @param json The JSON array
 * @param index The array index
 * @param result Pointer to receive the string value
 * @return int 0 on success, -1 on failure
 */
static int get_string_from_array_item(const mcp_json_t* json, int index, const char** result) {
    if (!json || index < 0 || !result) return -1;
    *result = NULL;

    mcp_json_t* item = mcp_json_array_get_item(json, index);
    if (!item) {
        mcp_log_error("Array item at index %d not found", index);
        return -1;
    }

    if (!mcp_json_is_string(item)) {
        mcp_log_error("Array item at index %d is not a string", index);
        return -1;
    }

    return mcp_json_get_string(item, result);
}

/**
 * @brief Get a boolean from a JSON object property
 *
 * @param json The JSON object
 * @param name The property name
 * @param result Pointer to receive the boolean value
 * @param default_value Default value to use if property is not found
 * @return int 0 on success, -1 on failure
 */
static int get_boolean_from_property(const mcp_json_t* json, const char* name, bool* result, bool default_value) {
    if (!json || !name || !result) return -1;
    *result = default_value;

    mcp_json_t* value = mcp_json_object_get_property(json, name);
    if (!value) {
        return 0; // Use default value
    }

    if (!mcp_json_is_boolean(value)) {
        mcp_log_error("Property '%s' is not a boolean", name);
        return -1;
    }

    return mcp_json_get_boolean(value, result);
}

/**
 * @brief Get a number from a JSON object property
 *
 * @param json The JSON object
 * @param name The property name
 * @param result Pointer to receive the number value
 * @param default_value Default value to use if property is not found
 * @return int 0 on success, -1 on failure
 */
static int get_number_from_property(const mcp_json_t* json, const char* name, double* result, double default_value) {
    if (!json || !name || !result) return -1;
    *result = default_value;

    mcp_json_t* value = mcp_json_object_get_property(json, name);
    if (!value) {
        return 0; // Use default value
    }

    if (!mcp_json_is_number(value)) {
        mcp_log_error("Property '%s' is not a number", name);
        return -1;
    }

    return mcp_json_get_number(value, result);
}

/**
 * @brief Complete definition of configuration parser structure
 */
struct kmcp_config_parser {
    char* file_path;       // Configuration file path
    mcp_json_t* json;      // Parsed JSON
};

/**
 * @brief Create a configuration parser
 */
kmcp_config_parser_t* kmcp_config_parser_create(const char* file_path) {
    if (!file_path) {
        mcp_log_error("Invalid parameter: file_path is NULL");
        return NULL;
    }

    // Allocate memory
    kmcp_config_parser_t* parser = (kmcp_config_parser_t*)malloc(sizeof(kmcp_config_parser_t));
    if (!parser) {
        mcp_log_error("Failed to allocate memory for config parser");
        return NULL;
    }

    // Initialize fields
    parser->file_path = mcp_strdup(file_path);
    parser->json = NULL;

    if (!parser->file_path) {
        mcp_log_error("Failed to duplicate file path");
        free(parser);
        return NULL;
    }

    // Read file content in text mode for JSON parsing
    FILE* file = fopen(file_path, "r");
    if (!file) {
        mcp_log_error("Failed to open config file: %s", file_path);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        mcp_log_error("Config file is empty: %s", file_path);
        fclose(file);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    // Allocate memory and read file content
    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        mcp_log_error("Failed to allocate memory for file content");
        fclose(file);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);

    if (read_size == 0) {
        mcp_log_error("Failed to read config file: %s", file_path);
        free(buffer);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    // Log the actual read size vs file size (for debugging)
    mcp_log_debug("Read %zu bytes from config file (file size: %ld bytes)", read_size, file_size);

    buffer[read_size] = '\0';

    // Parse JSON
    parser->json = mcp_json_parse(buffer);
    free(buffer);

    if (!parser->json) {
        mcp_log_error("Failed to parse JSON from config file: %s", file_path);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    return parser;
}

/**
 * @brief Parse server configurations
 */
int kmcp_config_parser_get_servers(
    kmcp_config_parser_t* parser,
    kmcp_server_config_t*** servers,
    size_t* server_count
) {
    if (!parser || !servers || !server_count) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // Initialize output parameters
    *servers = NULL;
    *server_count = 0;

    // Get mcpServers object
    mcp_json_t* mcp_servers = mcp_json_object_get_property(parser->json, "mcpServers");
    if (!mcp_servers) {
        mcp_log_error("mcpServers not found in config file");
        return -1;
    }

    // Get all keys of mcpServers object
    char** keys = NULL;
    size_t key_count = 0;
    if (mcp_json_object_get_property_names(mcp_servers, &keys, &key_count) != 0 || key_count == 0) {
        mcp_log_error("Failed to get server keys or no servers defined");
        return -1;
    }

    // Allocate server configuration array
    *servers = (kmcp_server_config_t**)malloc(key_count * sizeof(kmcp_server_config_t*));
    if (!*servers) {
        mcp_log_error("Failed to allocate memory for server configs");
        // Free keys array
        for (size_t i = 0; i < key_count; i++) {
            free(keys[i]);
        }
        free(keys);
        return -1;
    }

    // Parse each server configuration
    size_t valid_count = 0;
    for (size_t i = 0; i < key_count; i++) {
        const char* server_name = keys[i];
        mcp_json_t* server_json = mcp_json_object_get_property(mcp_servers, server_name);
        if (!server_json) {
            mcp_log_warn("Server %s is not an object, skipping", server_name);
            continue;
        }

        // Allocate server configuration
        kmcp_server_config_t* config = (kmcp_server_config_t*)malloc(sizeof(kmcp_server_config_t));
        if (!config) {
            mcp_log_error("Failed to allocate memory for server config");
            continue;
        }

        // Initialize configuration
        memset(config, 0, sizeof(kmcp_server_config_t));
        config->name = mcp_strdup(server_name);

        // Check if it's an HTTP server
        const char* url = NULL;
        if (get_string_from_property(server_json, "url", &url, false) == 0 && url) {
            config->url = mcp_strdup(url);
            config->is_http = true;
            mcp_log_info("Server '%s' configured as HTTP server with URL: %s", server_name, url);
        } else {
            // Local process server
            const char* command = NULL;
            if (get_string_from_property(server_json, "command", &command, true) == 0 && command) {
                config->command = mcp_strdup(command);
                mcp_log_info("Server '%s' configured as local process with command: %s", server_name, command);

                // Parse arguments array
                mcp_json_t* args_json = mcp_json_object_get_property(server_json, "args");
                if (args_json && mcp_json_is_array(args_json)) {
                    size_t args_count = mcp_json_array_get_size(args_json);
                    if (args_count > 0) {
                        config->args = (char**)malloc(args_count * sizeof(char*));
                        if (config->args) {
                            config->args_count = 0;
                            for (size_t j = 0; j < args_count; j++) {
                                const char* arg = NULL;
                                if (get_string_from_array_item(args_json, (int)j, &arg) == 0 && arg) {
                                    mcp_log_debug("Server '%s' arg[%zu]: %s", server_name, j, arg);
                                    config->args[config->args_count] = mcp_strdup(arg);
                                    config->args_count++;
                                }
                            }
                        }
                    }
                }

                // Parse environment variables object
                mcp_json_t* env_json = mcp_json_object_get_property(server_json, "env");
                if (env_json && mcp_json_is_object(env_json)) {
                    char** env_keys = NULL;
                    size_t env_key_count = 0;
                    if (mcp_json_object_get_property_names(env_json, &env_keys, &env_key_count) == 0 && env_key_count > 0) {
                        config->env = (char**)malloc(env_key_count * sizeof(char*));
                        if (config->env) {
                            config->env_count = 0;
                            for (size_t j = 0; j < env_key_count; j++) {
                                const char* key = env_keys[j];
                                const char* value = NULL;
                                if (key && get_string_from_property(env_json, key, &value, false) == 0 && value) {
                                    // Format: KEY=VALUE
                                    size_t env_len = strlen(key) + strlen(value) + 2; // +2 for '=' and '\0'
                                    char* env_var = (char*)malloc(env_len);
                                    if (env_var) {
                                        snprintf(env_var, env_len, "%s=%s", key, value);
                                        config->env[config->env_count] = env_var;
                                        config->env_count++;
                                    }
                                }
                            }
                        }

                        // Free environment variable keys array
                        for (size_t j = 0; j < env_key_count; j++) {
                            free(env_keys[j]);
                        }
                        free(env_keys);
                    }
                }
            }
        }

        // Add to server configuration array
        (*servers)[valid_count] = config;
        valid_count++;
    }

    // Free keys array
    for (size_t i = 0; i < key_count; i++) {
        free(keys[i]);
    }
    free(keys);

    // Update server count
    *server_count = valid_count;

    return 0;
}

/**
 * @brief Parse client configuration
 */
int kmcp_config_parser_get_client(
    kmcp_config_parser_t* parser,
    kmcp_client_config_t* config
) {
    if (!parser || !config) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // Initialize configuration
    memset(config, 0, sizeof(kmcp_client_config_t));

    // Set default values
    config->timeout_ms = 30000; // Default 30 seconds
    config->use_manager = true;

    // Get clientConfig object
    mcp_json_t* client_config = mcp_json_object_get_property(parser->json, "clientConfig");
    if (!client_config) {
        mcp_log_warn("clientConfig not found in config file, using defaults");
        return 0;
    }

    // Parse client name
    const char* name = NULL;
    if (get_string_from_property(client_config, "clientName", &name, false) == 0 && name) {
        config->name = mcp_strdup(name);
        mcp_log_info("Client name: %s", name);
    } else {
        config->name = mcp_strdup("kmcp-client");
        mcp_log_info("Using default client name: %s", config->name);
    }

    // Parse client version
    const char* version = NULL;
    if (get_string_from_property(client_config, "clientVersion", &version, false) == 0 && version) {
        config->version = mcp_strdup(version);
        mcp_log_info("Client version: %s", version);
    } else {
        config->version = mcp_strdup("1.0.0");
        mcp_log_info("Using default client version: %s", config->version);
    }

    // Parse whether to use server manager
    bool use_manager = config->use_manager;
    if (get_boolean_from_property(client_config, "useServerManager", &use_manager, true) == 0) {
        config->use_manager = use_manager;
        mcp_log_info("Server manager %s", use_manager ? "enabled" : "disabled");
    }

    // Parse request timeout
    double timeout_ms = (double)config->timeout_ms;
    if (get_number_from_property(client_config, "requestTimeoutMs", &timeout_ms, (double)config->timeout_ms) == 0) {
        // Validate timeout value (must be positive)
        if (timeout_ms <= 0) {
            mcp_log_warn("Invalid request timeout value: %.0f ms, using default: %u ms", timeout_ms, config->timeout_ms);
        } else {
            config->timeout_ms = (uint32_t)timeout_ms;
            mcp_log_info("Request timeout: %u ms", config->timeout_ms);
        }
    }

    return 0;
}

/**
 * @brief Parse tool access control configuration
 */
int kmcp_config_parser_get_access(
    kmcp_config_parser_t* parser,
    kmcp_tool_access_t* access
) {
    if (!parser || !access) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // Get toolAccessControl object
    mcp_json_t* access_config = mcp_json_object_get_property(parser->json, "toolAccessControl");
    if (!access_config) {
        mcp_log_warn("toolAccessControl not found in config file");
        return 0;
    }

    // Parse default allow policy
    bool default_allow = true; // Default is true
    if (get_boolean_from_property(access_config, "defaultAllow", &default_allow, true) == 0) {
        mcp_log_info("Tool access control default policy: %s", default_allow ? "allow" : "deny");
    }

    // Create tool access control
    // Note: Here we assume access is a pointer to an already created kmcp_tool_access_t object
    // If this is not the case, this code needs to be modified

    // Parse allowed tools list
    mcp_json_t* allowed_tools = mcp_json_object_get_property(access_config, "allowedTools");
    if (allowed_tools && mcp_json_is_array(allowed_tools)) {
        size_t count = mcp_json_array_get_size(allowed_tools);
        for (size_t i = 0; i < count; i++) {
            const char* tool = NULL;
            if (get_string_from_array_item(allowed_tools, (int)i, &tool) == 0 && tool) {
                kmcp_tool_access_add(access, tool, true);
            }
        }
    }

    // Parse disallowed tools list
    mcp_json_t* disallowed_tools = mcp_json_object_get_property(access_config, "disallowedTools");
    if (disallowed_tools && mcp_json_is_array(disallowed_tools)) {
        size_t count = mcp_json_array_get_size(disallowed_tools);
        for (size_t i = 0; i < count; i++) {
            const char* tool = NULL;
            if (get_string_from_array_item(disallowed_tools, (int)i, &tool) == 0 && tool) {
                kmcp_tool_access_add(access, tool, false);
            }
        }
    }

    return 0;
}

/**
 * @brief Close the configuration parser
 */
void kmcp_config_parser_close(kmcp_config_parser_t* parser) {
    if (!parser) {
        return;
    }

    // Free resources
    free(parser->file_path);
    if (parser->json) {
        mcp_json_destroy(parser->json);
    }

    free(parser);
}
