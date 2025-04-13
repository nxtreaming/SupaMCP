#ifndef MCP_TEMPLATE_SECURITY_PUBLIC_H
#define MCP_TEMPLATE_SECURITY_PUBLIC_H

#include <stdbool.h>
#include <stddef.h>
#include "mcp_json.h"
#include "mcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function type for template parameter validation
 * 
 * @param template_uri The template URI pattern
 * @param params The parameters to validate
 * @param user_data User data passed to the validator
 * @return true if the parameters are valid, false otherwise
 */
typedef bool (*mcp_template_validator_t)(
    const char* template_uri,
    const mcp_json_t* params,
    void* user_data
);

/**
 * @brief Adds an access control list entry for a template
 * 
 * This function adds an access control list entry for a template URI pattern.
 * Only users with the specified roles will be allowed to access the template.
 * 
 * @param server The server instance
 * @param template_uri The template URI pattern
 * @param allowed_roles Array of roles allowed to access this template
 * @param allowed_roles_count Number of allowed roles
 * @return 0 on success, non-zero on error
 */
int mcp_server_add_template_acl(
    mcp_server_t* server,
    const char* template_uri,
    const char** allowed_roles,
    size_t allowed_roles_count
);

/**
 * @brief Sets a custom validator for a template
 * 
 * This function sets a custom validator for a template URI pattern.
 * The validator will be called to validate parameters for the template.
 * 
 * @param server The server instance
 * @param template_uri The template URI pattern
 * @param validator The validator function
 * @param validator_data User data to pass to the validator
 * @return 0 on success, non-zero on error
 */
int mcp_server_set_template_validator(
    mcp_server_t* server,
    const char* template_uri,
    mcp_template_validator_t validator,
    void* validator_data
);

/**
 * @brief Sets a default validator for all templates
 * 
 * This function sets a default validator for all templates.
 * The validator will be called to validate parameters for templates
 * that don't have a custom validator.
 * 
 * @param server The server instance
 * @param validator The validator function
 * @param validator_data User data to pass to the validator
 * @return 0 on success, non-zero on error
 */
int mcp_server_set_default_template_validator(
    mcp_server_t* server,
    mcp_template_validator_t validator,
    void* validator_data
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_TEMPLATE_SECURITY_PUBLIC_H */
