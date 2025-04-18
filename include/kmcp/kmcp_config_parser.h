/**
 * @file kmcp_config_parser.h
 * @brief Configuration file parser for parsing JSON format configuration files
 */

#ifndef KMCP_CONFIG_PARSER_H
#define KMCP_CONFIG_PARSER_H

#include <stddef.h>
#include "kmcp_error.h"
#include "kmcp_server_manager.h"
#include "kmcp_client.h"
#include "kmcp_tool_access.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration parser structure
 */
typedef struct kmcp_config_parser kmcp_config_parser_t;

/**
 * @brief Create a configuration parser
 *
 * Creates a configuration parser for parsing a JSON configuration file.
 * The parser can be used to extract client, server, and tool access control configurations.
 *
 * @param file_path Configuration file path (must not be NULL)
 * @return kmcp_config_parser_t* Returns configuration parser pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the parser using kmcp_config_parser_close()
 * @see kmcp_config_parser_close()
 */
kmcp_config_parser_t* kmcp_config_parser_create(const char* file_path);

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
