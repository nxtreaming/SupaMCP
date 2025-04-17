/**
 * @file kmcp_tool_access.h
 * @brief Tool access control to restrict access to specific tools
 */

#ifndef KMCP_TOOL_ACCESS_H
#define KMCP_TOOL_ACCESS_H

#include <stddef.h>
#include <stdbool.h>

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
 * @param access Tool access control
 * @param tool_name Tool name
 * @param allow Whether to allow, true means allow, false means deny
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_tool_access_add(kmcp_tool_access_t* access, const char* tool_name, bool allow);

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
 * @param access Tool access control
 * @param config_file Configuration file path
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_tool_access_load(kmcp_tool_access_t* access, const char* config_file);

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
