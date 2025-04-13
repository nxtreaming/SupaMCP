#include "internal/mcp_template_security.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Structure to hold template access control information
 */
typedef struct {
    char* template_uri;                 /**< The template URI pattern */
    char** allowed_roles;               /**< Array of roles allowed to access this template */
    size_t allowed_roles_count;         /**< Number of allowed roles */
    mcp_template_validator_t validator; /**< Custom validator function for this template */
    void* validator_data;               /**< User data to pass to the validator */
} template_acl_entry_t;

/**
 * @brief Creates a new template ACL entry
 */
static template_acl_entry_t* template_acl_entry_create(const char* template_uri) {
    if (template_uri == NULL) {
        return NULL;
    }

    template_acl_entry_t* entry = (template_acl_entry_t*)malloc(sizeof(template_acl_entry_t));
    if (entry == NULL) {
        return NULL;
    }

    entry->template_uri = mcp_strdup(template_uri);
    if (entry->template_uri == NULL) {
        free(entry);
        return NULL;
    }

    entry->allowed_roles = NULL;
    entry->allowed_roles_count = 0;
    entry->validator = NULL;
    entry->validator_data = NULL;

    return entry;
}

/**
 * @brief Frees a template ACL entry
 */
static void template_acl_entry_free(template_acl_entry_t* entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->template_uri);
    
    for (size_t i = 0; i < entry->allowed_roles_count; i++) {
        free(entry->allowed_roles[i]);
    }
    free(entry->allowed_roles);
    
    free(entry);
}

/**
 * @brief Structure to hold the template security context
 */
struct mcp_template_security {
    template_acl_entry_t** entries;     /**< Array of ACL entries */
    size_t entries_count;               /**< Number of ACL entries */
    size_t entries_capacity;            /**< Capacity of the entries array */
    mcp_template_validator_t default_validator; /**< Default validator function */
    void* default_validator_data;       /**< User data for the default validator */
};

mcp_template_security_t* mcp_template_security_create(void) {
    mcp_template_security_t* security = (mcp_template_security_t*)malloc(sizeof(mcp_template_security_t));
    if (security == NULL) {
        return NULL;
    }

    security->entries = NULL;
    security->entries_count = 0;
    security->entries_capacity = 0;
    security->default_validator = NULL;
    security->default_validator_data = NULL;

    return security;
}

void mcp_template_security_destroy(mcp_template_security_t* security) {
    if (security == NULL) {
        return;
    }

    for (size_t i = 0; i < security->entries_count; i++) {
        template_acl_entry_free(security->entries[i]);
    }
    free(security->entries);
    free(security);
}

int mcp_template_security_add_acl(
    mcp_template_security_t* security,
    const char* template_uri,
    const char** allowed_roles,
    size_t allowed_roles_count
) {
    if (security == NULL || template_uri == NULL || (allowed_roles == NULL && allowed_roles_count > 0)) {
        return -1;
    }

    // Check if we need to resize the entries array
    if (security->entries_count >= security->entries_capacity) {
        size_t new_capacity = security->entries_capacity == 0 ? 8 : security->entries_capacity * 2;
        template_acl_entry_t** new_entries = (template_acl_entry_t**)realloc(
            security->entries, new_capacity * sizeof(template_acl_entry_t*));
        
        if (new_entries == NULL) {
            return -1;
        }

        security->entries = new_entries;
        security->entries_capacity = new_capacity;
    }

    // Create a new ACL entry
    template_acl_entry_t* entry = template_acl_entry_create(template_uri);
    if (entry == NULL) {
        return -1;
    }

    // Add allowed roles
    if (allowed_roles_count > 0) {
        entry->allowed_roles = (char**)malloc(allowed_roles_count * sizeof(char*));
        if (entry->allowed_roles == NULL) {
            template_acl_entry_free(entry);
            return -1;
        }

        for (size_t i = 0; i < allowed_roles_count; i++) {
            entry->allowed_roles[i] = mcp_strdup(allowed_roles[i]);
            if (entry->allowed_roles[i] == NULL) {
                template_acl_entry_free(entry);
                return -1;
            }
            entry->allowed_roles_count++;
        }
    }

    // Add the entry to the security context
    security->entries[security->entries_count++] = entry;

    return 0;
}

int mcp_template_security_set_validator(
    mcp_template_security_t* security,
    const char* template_uri,
    mcp_template_validator_t validator,
    void* validator_data
) {
    if (security == NULL || template_uri == NULL || validator == NULL) {
        return -1;
    }

    // Find the ACL entry for this template
    template_acl_entry_t* entry = NULL;
    for (size_t i = 0; i < security->entries_count; i++) {
        if (strcmp(security->entries[i]->template_uri, template_uri) == 0) {
            entry = security->entries[i];
            break;
        }
    }

    // If no entry exists, create one
    if (entry == NULL) {
        // Check if we need to resize the entries array
        if (security->entries_count >= security->entries_capacity) {
            size_t new_capacity = security->entries_capacity == 0 ? 8 : security->entries_capacity * 2;
            template_acl_entry_t** new_entries = (template_acl_entry_t**)realloc(
                security->entries, new_capacity * sizeof(template_acl_entry_t*));
            
            if (new_entries == NULL) {
                return -1;
            }

            security->entries = new_entries;
            security->entries_capacity = new_capacity;
        }

        entry = template_acl_entry_create(template_uri);
        if (entry == NULL) {
            return -1;
        }

        security->entries[security->entries_count++] = entry;
    }

    // Set the validator
    entry->validator = validator;
    entry->validator_data = validator_data;

    return 0;
}

int mcp_template_security_set_default_validator(
    mcp_template_security_t* security,
    mcp_template_validator_t validator,
    void* validator_data
) {
    if (security == NULL || validator == NULL) {
        return -1;
    }

    security->default_validator = validator;
    security->default_validator_data = validator_data;

    return 0;
}

bool mcp_template_security_check_access(
    mcp_template_security_t* security,
    const char* template_uri,
    const char* user_role,
    const mcp_json_t* params
) {
    if (security == NULL || template_uri == NULL) {
        return false;
    }

    // Find the ACL entry for this template
    template_acl_entry_t* entry = NULL;
    for (size_t i = 0; i < security->entries_count; i++) {
        if (strcmp(security->entries[i]->template_uri, template_uri) == 0) {
            entry = security->entries[i];
            break;
        }
    }

    // If no entry exists, allow access if no user role is required
    if (entry == NULL) {
        return user_role == NULL;
    }

    // Check if the user has the required role
    if (user_role != NULL && entry->allowed_roles_count > 0) {
        bool role_match = false;
        for (size_t i = 0; i < entry->allowed_roles_count; i++) {
            if (strcmp(entry->allowed_roles[i], user_role) == 0 || 
                strcmp(entry->allowed_roles[i], "*") == 0) {
                role_match = true;
                break;
            }
        }

        if (!role_match) {
            mcp_log_info("Access denied for role '%s' to template '%s'", 
                         user_role, template_uri);
            return false;
        }
    }

    // Check custom validator if present
    if (entry->validator != NULL) {
        if (!entry->validator(template_uri, params, entry->validator_data)) {
            mcp_log_info("Access denied by custom validator for template '%s'", 
                         template_uri);
            return false;
        }
    } else if (security->default_validator != NULL) {
        // Use default validator if no custom validator is set
        if (!security->default_validator(template_uri, params, security->default_validator_data)) {
            mcp_log_info("Access denied by default validator for template '%s'", 
                         template_uri);
            return false;
        }
    }

    return true;
}

bool mcp_template_security_validate_params(
    mcp_template_security_t* security,
    const char* template_uri,
    const mcp_json_t* params
) {
    if (security == NULL || template_uri == NULL || params == NULL) {
        return false;
    }

    // Find the ACL entry for this template
    template_acl_entry_t* entry = NULL;
    for (size_t i = 0; i < security->entries_count; i++) {
        if (strcmp(security->entries[i]->template_uri, template_uri) == 0) {
            entry = security->entries[i];
            break;
        }
    }

    // If no entry exists, use the default validator
    if (entry == NULL) {
        if (security->default_validator != NULL) {
            return security->default_validator(template_uri, params, security->default_validator_data);
        }
        return true; // No validator, allow by default
    }

    // Check custom validator if present
    if (entry->validator != NULL) {
        return entry->validator(template_uri, params, entry->validator_data);
    } else if (security->default_validator != NULL) {
        // Use default validator if no custom validator is set
        return security->default_validator(template_uri, params, security->default_validator_data);
    }

    return true; // No validator, allow by default
}
