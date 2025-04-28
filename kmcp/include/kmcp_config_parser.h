/**
 * @file kmcp_config_parser.h
 * @brief Enhanced configuration file parser for KMCP
 *
 * This module provides functions for parsing, validating, and managing
 * configuration files for KMCP. It supports environment variable substitution,
 * configuration validation, multiple configuration files, and profiles.
 */

#ifndef KMCP_CONFIG_PARSER_H
#define KMCP_CONFIG_PARSER_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"
#include "kmcp_server_manager.h"
#include "kmcp_client.h"
#include "kmcp_tool_access.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration validation level
 */
typedef enum {
    KMCP_CONFIG_VALIDATION_NONE,    /**< No validation */
    KMCP_CONFIG_VALIDATION_BASIC,   /**< Basic validation (check required fields) */
    KMCP_CONFIG_VALIDATION_STRICT   /**< Strict validation (check all fields) */
} kmcp_config_validation_level_t;

/**
 * @brief Configuration parser options
 */
typedef struct {
    bool enable_env_vars;                      /**< Enable environment variable substitution */
    bool enable_includes;                      /**< Enable include directives */
    kmcp_config_validation_level_t validation; /**< Validation level */
    const char* default_profile;               /**< Default profile name */
    const char* config_dir;                    /**< Configuration directory */
} kmcp_config_parser_options_t;

/**
 * @brief Configuration parser structure
 */
typedef struct kmcp_config_parser kmcp_config_parser_t;

/**
 * @brief Create a configuration parser with default options
 *
 * Creates a configuration parser for parsing a JSON configuration file.
 * The parser can be used to extract client, server, and tool access control configurations.
 * Default options are used (no environment variable substitution, no includes, basic validation).
 *
 * @param file_path Configuration file path (must not be NULL)
 * @return kmcp_config_parser_t* Returns configuration parser pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the parser using kmcp_config_parser_close()
 * @see kmcp_config_parser_close()
 */
kmcp_config_parser_t* kmcp_config_parser_create(const char* file_path);

/**
 * @brief Create a configuration parser with options
 *
 * Creates a configuration parser for parsing a JSON configuration file with the specified options.
 * The parser can be used to extract client, server, and tool access control configurations.
 *
 * @param file_path Configuration file path (must not be NULL)
 * @param options Configuration parser options (must not be NULL)
 * @return kmcp_config_parser_t* Returns configuration parser pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the parser using kmcp_config_parser_close()
 * @see kmcp_config_parser_close()
 */
kmcp_config_parser_t* kmcp_config_parser_create_with_options(const char* file_path, const kmcp_config_parser_options_t* options);

/**
 * @brief Parse server configurations
 *
 * Parses server configurations from the configuration file.
 * The configuration file should contain a "servers" array with server configurations.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param servers Pointer to server configuration array, memory allocated by function, caller responsible for freeing
 * @param server_count Pointer to server count (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_PARSE_FAILED if the configuration file cannot be parsed
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the server configurations
 */
kmcp_error_t kmcp_config_parser_get_servers(
    kmcp_config_parser_t* parser,
    kmcp_server_config_t*** servers,
    size_t* server_count
);

/**
 * @brief Parse client configuration
 *
 * Parses client configuration from the configuration file.
 * The configuration file should contain a "client" object with client settings.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param config Client configuration (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_PARSE_FAILED if the configuration file cannot be parsed
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_config_parser_get_client(
    kmcp_config_parser_t* parser,
    kmcp_client_config_t* config
);

/**
 * @brief Parse tool access control configuration
 *
 * Parses tool access control configuration from the configuration file.
 * The configuration file should contain a "toolAccessControl" object
 * with "allowedTools" and "disallowedTools" arrays.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param access Tool access control (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_PARSE_FAILED if the configuration file cannot be parsed
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_config_parser_get_access(
    kmcp_config_parser_t* parser,
    kmcp_tool_access_t* access
);



/**
 * @brief Validate configuration file
 *
 * Validates the configuration file against a schema.
 * The validation level is determined by the parser options.
 *
 * @param parser Configuration parser (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if parser is NULL
 *         - KMCP_ERROR_CONFIG_INVALID if the configuration file is invalid
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_config_parser_validate(kmcp_config_parser_t* parser);

/**
 * @brief Merge configuration files
 *
 * Merges multiple configuration files into a single configuration.
 * Later files override settings from earlier files.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param file_paths Array of configuration file paths (must not be NULL)
 * @param file_count Number of configuration files
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_FILE_NOT_FOUND if any configuration file is not found
 *         - KMCP_ERROR_PARSE_FAILED if any configuration file cannot be parsed
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_config_parser_merge(
    kmcp_config_parser_t* parser,
    const char** file_paths,
    size_t file_count
);

/**
 * @brief Get a string value from the configuration
 *
 * Retrieves a string value from the configuration file.
 * Environment variables are substituted if enabled in the parser options.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return const char* Returns the string value, or default_value if not found
 */
const char* kmcp_config_parser_get_string(
    kmcp_config_parser_t* parser,
    const char* path,
    const char* default_value
);

/**
 * @brief Get a boolean value from the configuration
 *
 * Retrieves a boolean value from the configuration file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return bool Returns the boolean value, or default_value if not found
 */
bool kmcp_config_parser_get_boolean(
    kmcp_config_parser_t* parser,
    const char* path,
    bool default_value
);

/**
 * @brief Get a number value from the configuration
 *
 * Retrieves a number value from the configuration file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return double Returns the number value, or default_value if not found
 */
double kmcp_config_parser_get_number(
    kmcp_config_parser_t* parser,
    const char* path,
    double default_value
);

/**
 * @brief Get an integer value from the configuration
 *
 * Retrieves an integer value from the configuration file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return int Returns the integer value, or default_value if not found
 */
int kmcp_config_parser_get_int(
    kmcp_config_parser_t* parser,
    const char* path,
    int default_value
);

/**
 * @brief Save configuration to a file
 *
 * Saves the current configuration to a file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param file_path File path to save to (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_IO if the file cannot be written
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_config_parser_save(
    kmcp_config_parser_t* parser,
    const char* file_path
);

/**
 * @brief Close the configuration parser
 *
 * Closes the configuration parser and frees all associated resources.
 *
 * @param parser Configuration parser (can be NULL, in which case this function does nothing)
 *
 * @note After calling this function, the parser pointer is no longer valid and should not be used.
 */
void kmcp_config_parser_close(kmcp_config_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_CONFIG_PARSER_H */
