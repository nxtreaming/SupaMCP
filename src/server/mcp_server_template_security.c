#include <mcp_server.h>
#include <mcp_template_security.h>
#include "internal/server_internal.h"
#include "mcp_log.h"

/**
 * @brief Adds an access control list entry for a template
 */
int mcp_server_add_template_acl(
    mcp_server_t* server,
    const char* template_uri,
    const char** allowed_roles,
    size_t allowed_roles_count
) {
    if (server == NULL || template_uri == NULL || (allowed_roles == NULL && allowed_roles_count > 0)) {
        return -1;
    }

    // Create the template security context if it doesn't exist
    if (server->template_security == NULL) {
        server->template_security = mcp_template_security_create();
        if (server->template_security == NULL) {
            return -1;
        }
    }

    // Add the ACL entry
    return mcp_template_security_add_acl(
        server->template_security,
        template_uri,
        allowed_roles,
        allowed_roles_count
    );
}

/**
 * @brief Sets a custom validator for a template
 */
int mcp_server_set_template_validator(
    mcp_server_t* server,
    const char* template_uri,
    mcp_template_validator_t validator,
    void* validator_data
) {
    if (server == NULL || template_uri == NULL || validator == NULL) {
        return -1;
    }

    // Create the template security context if it doesn't exist
    if (server->template_security == NULL) {
        server->template_security = mcp_template_security_create();
        if (server->template_security == NULL) {
            return -1;
        }
    }

    // Set the validator
    return mcp_template_security_set_validator(
        server->template_security,
        template_uri,
        validator,
        validator_data
    );
}

/**
 * @brief Sets a default validator for all templates
 */
int mcp_server_set_default_template_validator(
    mcp_server_t* server,
    mcp_template_validator_t validator,
    void* validator_data
) {
    if (server == NULL || validator == NULL) {
        return -1;
    }

    // Create the template security context if it doesn't exist
    if (server->template_security == NULL) {
        server->template_security = mcp_template_security_create();
        if (server->template_security == NULL) {
            return -1;
        }
    }

    // Set the default validator
    return mcp_template_security_set_default_validator(
        server->template_security,
        validator,
        validator_data
    );
}
