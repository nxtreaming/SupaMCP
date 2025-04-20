/**
 * @file kmcp_cli.h
 * @brief Command-line interface for KMCP
 * 
 * This file defines the command-line interface for KMCP, providing
 * functions for parsing and executing commands.
 */

#ifndef KMCP_CLI_H
#define KMCP_CLI_H

#include "kmcp_error.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Command context structure
 */
typedef struct kmcp_cli_context kmcp_cli_context_t;

/**
 * @brief Command handler function type
 */
typedef kmcp_error_t (*kmcp_cli_command_handler_t)(kmcp_cli_context_t* context, int argc, char** argv);

/**
 * @brief Command definition structure
 */
typedef struct {
    const char* name;                  /**< Command name */
    const char* description;           /**< Command description */
    const char* usage;                 /**< Command usage */
    kmcp_cli_command_handler_t handler; /**< Command handler function */
} kmcp_cli_command_t;

/**
 * @brief Create a CLI context
 * 
 * @param config_file Path to the configuration file (can be NULL for default)
 * @return kmcp_cli_context_t* Returns CLI context pointer on success, NULL on failure
 */
kmcp_cli_context_t* kmcp_cli_create(const char* config_file);

/**
 * @brief Close a CLI context and free resources
 * 
 * @param context CLI context (must not be NULL)
 */
void kmcp_cli_close(kmcp_cli_context_t* context);

/**
 * @brief Execute a command
 * 
 * @param context CLI context (must not be NULL)
 * @param argc Argument count
 * @param argv Argument array
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_cli_execute(kmcp_cli_context_t* context, int argc, char** argv);

/**
 * @brief Print command help
 * 
 * @param context CLI context (must not be NULL)
 * @param command Command name (can be NULL for general help)
 */
void kmcp_cli_print_help(kmcp_cli_context_t* context, const char* command);

/**
 * @brief Get the current profile name
 * 
 * @param context CLI context (must not be NULL)
 * @return const char* Returns the current profile name, or NULL if no profile is active
 */
const char* kmcp_cli_get_current_profile(kmcp_cli_context_t* context);

/**
 * @brief Set the current profile
 * 
 * @param context CLI context (must not be NULL)
 * @param profile_name Profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_cli_set_current_profile(kmcp_cli_context_t* context, const char* profile_name);

/**
 * @brief Parse a resource identifier with scope
 * 
 * Parses a resource identifier with optional scope prefix:
 * - @CLIENT/SERVER - Client and server scope
 * - #PROFILE/SERVER - Profile and server scope
 * - SERVER - Default scope (current profile)
 * 
 * @param context CLI context (must not be NULL)
 * @param identifier Resource identifier (must not be NULL)
 * @param[out] client_name Pointer to store client name (can be NULL)
 * @param[out] profile_name Pointer to store profile name (can be NULL)
 * @param[out] server_name Pointer to store server name (can be NULL)
 * @param[out] resource_name Pointer to store resource name (can be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_cli_parse_identifier(
    kmcp_cli_context_t* context,
    const char* identifier,
    const char** client_name,
    const char** profile_name,
    const char** server_name,
    const char** resource_name
);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_CLI_H */
