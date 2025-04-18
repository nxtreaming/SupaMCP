/**
 * @file kmcp_tool_access.h
 * @brief Tool access control to restrict access to specific tools
 */

#ifndef KMCP_TOOL_ACCESS_H
#define KMCP_TOOL_ACCESS_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tool access control structure
 */
typedef struct kmcp_tool_access kmcp_tool_access_t;

/**
 * @brief Create tool access control
 *
 * @param default_allow Default allow policy, true means allow by default, false means deny by default
 * @return kmcp_tool_access_t* Returns tool access control pointer on success, NULL on failure
 */
kmcp_tool_access_t* kmcp_tool_access_create(bool default_allow);

/**
 * @brief Add a tool to the access control list
 *
 * Adds a tool to the access control list with the specified permission.
 * If the tool already exists in the list, its permission will be updated.
 *
 * @param access Tool access control (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @param allow Whether to allow, true means allow, false means deny
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_MEMORY_ALLOCATION if memory allocation fails
 */
kmcp_error_t kmcp_tool_access_add(kmcp_tool_access_t* access, const char* tool_name, bool allow);

/**
 * @brief Check if a tool is allowed to access
 *
 * @param access Tool access control
 * @param tool_name Tool name
 * @return bool Returns true if allowed, false if denied
 */
bool kmcp_tool_access_check(kmcp_tool_access_t* access, const char* tool_name);

/**
 * @brief Load access control list from a configuration file
 *
 * Loads tool access control settings from a JSON configuration file.
 * The configuration file should contain a "toolAccessControl" object
 * with "allowedTools" and "disallowedTools" arrays.
 *
 * @param access Tool access control (must not be NULL)
 * @param config_file Configuration file path (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_FILE_NOT_FOUND if the configuration file is not found
 *         - KMCP_ERROR_PARSE_FAILED if the configuration file cannot be parsed
 *         - Other error codes for specific failures
 */
kmcp_error_t kmcp_tool_access_load(kmcp_tool_access_t* access, const char* config_file);

/**
 * @brief Destroy tool access control
 *
 * @param access Tool access control
 */
void kmcp_tool_access_destroy(kmcp_tool_access_t* access);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_TOOL_ACCESS_H */
