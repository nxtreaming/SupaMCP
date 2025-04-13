#ifndef MCP_TEMPLATE_SECURITY_H
#define MCP_TEMPLATE_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include "mcp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle representing a template security context
 */
typedef struct mcp_template_security mcp_template_security_t;

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
 * @brief Creates a new template security context
 * 
 * @return A newly allocated template security context, or NULL on error
 */
mcp_template_security_t* mcp_template_security_create(void);

/**
 * @brief Destroys a template security context
 * 
 * @param security The template security context to destroy
 */
void mcp_template_security_destroy(mcp_template_security_t* security);

/**
 * @brief Adds an access control list entry for a template
 * 
 * @param security The template security context
 * @param template_uri The template URI pattern
 * @param allowed_roles Array of roles allowed to access this template
 * @param allowed_roles_count Number of allowed roles
 * @return 0 on success, non-zero on error
 */
int mcp_template_security_add_acl(
    mcp_template_security_t* security,
    const char* template_uri,
    const char** allowed_roles,
    size_t allowed_roles_count
);

/**
 * @brief Sets a custom validator for a template
 * 
 * @param security The template security context
 * @param template_uri The template URI pattern
 * @param validator The validator function
 * @param validator_data User data to pass to the validator
 * @return 0 on success, non-zero on error
 */
int mcp_template_security_set_validator(
    mcp_template_security_t* security,
    const char* template_uri,
    mcp_template_validator_t validator,
    void* validator_data
);

/**
 * @brief Sets a default validator for all templates
 * 
 * @param security The template security context
 * @param validator The validator function
 * @param validator_data User data to pass to the validator
 * @return 0 on success, non-zero on error
 */
int mcp_template_security_set_default_validator(
    mcp_template_security_t* security,
    mcp_template_validator_t validator,
    void* validator_data
);

/**
 * @brief Checks if a user has access to a template
 * 
 * @param security The template security context
 * @param template_uri The template URI pattern
 * @param user_role The user's role
 * @param params The parameters to validate
 * @return true if access is allowed, false otherwise
 */
bool mcp_template_security_check_access(
    mcp_template_security_t* security,
    const char* template_uri,
    const char* user_role,
    const mcp_json_t* params
);

/**
 * @brief Validates parameters for a template
 * 
 * @param security The template security context
 * @param template_uri The template URI pattern
 * @param params The parameters to validate
 * @return true if the parameters are valid, false otherwise
 */
bool mcp_template_security_validate_params(
    mcp_template_security_t* security,
    const char* template_uri,
    const mcp_json_t* params
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_TEMPLATE_SECURITY_H */
