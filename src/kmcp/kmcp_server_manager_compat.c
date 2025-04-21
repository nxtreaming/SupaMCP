/**
 * @file kmcp_server_manager_compat.c
 * @brief Compatibility functions for server manager
 */

#include "kmcp_server_manager.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Remove a server from the manager
 */
kmcp_error_t kmcp_server_remove(kmcp_server_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // TODO: Implement server removal
    // This is a stub implementation that always returns success
    mcp_log_info("Removing server: %s", name);
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

    // TODO: Implement server configuration retrieval by name
    // This is a stub implementation that always returns not found
    mcp_log_info("Getting server configuration: %s", name);
    return KMCP_ERROR_SERVER_NOT_FOUND;
}

/**
 * @brief Get a server configuration by index
 */
kmcp_error_t kmcp_server_get_config_by_index(kmcp_server_manager_t* manager, size_t index, kmcp_server_config_t** config) {
    if (!manager || !config) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // TODO: Implement server configuration retrieval by index
    // This is a stub implementation that always returns not found
    mcp_log_info("Getting server configuration at index: %zu", index);
    return KMCP_ERROR_SERVER_NOT_FOUND;
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

