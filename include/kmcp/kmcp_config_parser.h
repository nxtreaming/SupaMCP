/**
 * @file kmcp_config_parser.h
 * @brief Configuration file parser for parsing JSON format configuration files
 */

#ifndef KMCP_CONFIG_PARSER_H
#define KMCP_CONFIG_PARSER_H

#include <stddef.h>
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
 * @param file_path Configuration file path
 * @return kmcp_config_parser_t* Returns configuration parser pointer on success, NULL on failure
 */
kmcp_config_parser_t* kmcp_config_parser_create(const char* file_path);

/**
 * @brief Parse server configurations
 *
 * @param parser Configuration parser
 * @param servers Pointer to server configuration array, memory allocated by function, caller responsible for freeing
 * @param server_count Pointer to server count
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_config_parser_get_servers(
    kmcp_config_parser_t* parser,
    kmcp_server_config_t*** servers,
    size_t* server_count
);

/**
 * @brief Parse client configuration
 *
 * @param parser Configuration parser
 * @param config Client configuration
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_config_parser_get_client(
    kmcp_config_parser_t* parser,
    kmcp_client_config_t* config
);

/**
 * @brief Parse tool access control configuration
 *
 * @param parser Configuration parser
 * @param access Tool access control
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_config_parser_get_access(
    kmcp_config_parser_t* parser,
    kmcp_tool_access_t* access
);

/**
 * @brief Close the configuration parser
 *
 * @param parser Configuration parser
 */
void kmcp_config_parser_close(kmcp_config_parser_t* parser);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_CONFIG_PARSER_H */
