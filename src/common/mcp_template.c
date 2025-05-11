#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>
#include <float.h>
#include "mcp_template.h"
#include "mcp_string_utils.h"
#include "mcp_log.h"

/**
 * @brief Initializes a parameter validation structure.
 *
 * @param validation The validation structure to initialize
 * @param type The parameter type
 * @param required Whether the parameter is required
 * @param default_value Default value for optional parameters (can be NULL)
 * @return 0 on success, -1 on error
 */
int mcp_template_init_validation(
    mcp_template_param_validation_t* validation,
    mcp_template_param_type_t type,
    bool required,
    const char* default_value)
{
    if (validation == NULL) {
        return -1;
    }

    // Initialize the structure
    validation->type = type;
    validation->required = required;
    validation->default_value = default_value ? mcp_strdup(default_value) : NULL;
    validation->pattern = NULL;

    // Initialize ranges with default values
    switch (type) {
        case MCP_TEMPLATE_PARAM_TYPE_INT:
            validation->range.int_range.min = INT_MIN;
            validation->range.int_range.max = INT_MAX;
            break;
        case MCP_TEMPLATE_PARAM_TYPE_FLOAT:
            validation->range.float_range.min = -FLT_MAX;
            validation->range.float_range.max = FLT_MAX;
            break;
        case MCP_TEMPLATE_PARAM_TYPE_STRING:
            validation->range.string_range.min_len = 0;
            validation->range.string_range.max_len = SIZE_MAX;
            break;
        default:
            // No range for other types
            break;
    }

    return 0;
}

/**
 * @brief Frees resources associated with a parameter validation structure.
 *
 * @param validation The validation structure to clean up
 */
void mcp_template_free_validation(mcp_template_param_validation_t* validation)
{
    if (validation == NULL) {
        return;
    }

    free(validation->default_value);
    free(validation->pattern);
    // Note: We don't free the validation structure itself
    // as it might be allocated on the stack
    validation->default_value = NULL;
    validation->pattern = NULL;
}

/**
 * @brief Parses a template parameter specification.
 *
 * This function parses a parameter specification like "name:type=default" into its components.
 *
 * @param param_spec The parameter specification string
 * @param name Output buffer for the parameter name
 * @param name_size Size of the name buffer
 * @param validation Output validation structure
 * @return 0 on success, -1 on error
 */
int mcp_template_parse_param_spec(const char* param_spec, char* name, size_t name_size, mcp_template_param_validation_t* validation)
{
    if (param_spec == NULL || name == NULL || validation == NULL || name_size == 0) {
        mcp_log_error("Invalid parameters to mcp_template_parse_param_spec");
        return -1;
    }

    mcp_log_debug("Parsing parameter spec: '%s'", param_spec);

    // Initialize validation structure with defaults
    memset(validation, 0, sizeof(mcp_template_param_validation_t));
    validation->type = MCP_TEMPLATE_PARAM_TYPE_STRING;
    validation->required = true;
    // default_value and pattern are already NULL from memset

    // Initialize ranges with default values
    validation->range.int_range.min = INT_MIN;
    validation->range.int_range.max = INT_MAX;
    validation->range.float_range.min = -FLT_MAX;
    validation->range.float_range.max = FLT_MAX;
    validation->range.string_range.min_len = 0;
    validation->range.string_range.max_len = SIZE_MAX;

    // Make a copy of the parameter spec for parsing
    char* spec_copy = mcp_strdup(param_spec);
    if (spec_copy == NULL) {
        return -1;
    }

    // Parse the parameter name (everything before ':' or '=' or '?')
    char* name_end = strpbrk(spec_copy, ":=?");
    if (name_end == NULL) {
        // Simple parameter with no type or default
        strncpy(name, spec_copy, name_size - 1);
        name[name_size - 1] = '\0';
        free(spec_copy);
        return 0;
    }

    // Extract the name
    size_t name_len = name_end - spec_copy;
    if (name_len >= name_size) {
        name_len = name_size - 1;
    }
    memcpy(name, spec_copy, name_len);
    name[name_len] = '\0';

    // Check for optional parameter
    if (*name_end == '?') {
        validation->required = false;
        name_end++;
        // Check if there's more after the '?'
        if (*name_end == '\0') {
            free(spec_copy);
            return 0;
        }
        // If there's a ':' or '=' after '?', continue parsing
        if (*name_end != ':' && *name_end != '=') {
            free(spec_copy);
            return -1; // Invalid format
        }
    }

    // Check for type specification
    if (*name_end == ':') {
        name_end++; // Skip ':'
        char* type_end = strpbrk(name_end, "=?");
        if (type_end == NULL) {
            type_end = name_end + strlen(name_end);
        }

        // Extract the type
        char type_str[32] = {0};
        size_t type_len = type_end - name_end;
        if (type_len >= sizeof(type_str)) {
            type_len = sizeof(type_str) - 1;
        }
        memcpy(type_str, name_end, type_len);
        type_str[type_len] = '\0';

        // Set the type
        if (strcmp(type_str, "int") == 0) {
            validation->type = MCP_TEMPLATE_PARAM_TYPE_INT;
            // Reset range values for int type
            validation->range.int_range.min = INT_MIN;
            validation->range.int_range.max = INT_MAX;
            mcp_log_debug("Set int range to [%d, %d]", validation->range.int_range.min, validation->range.int_range.max);
        } else if (strcmp(type_str, "float") == 0) {
            validation->type = MCP_TEMPLATE_PARAM_TYPE_FLOAT;
            // Reset range values for float type
            validation->range.float_range.min = -FLT_MAX;
            validation->range.float_range.max = FLT_MAX;
            mcp_log_debug("Set float range to [%f, %f]", validation->range.float_range.min, validation->range.float_range.max);
        } else if (strcmp(type_str, "bool") == 0) {
            validation->type = MCP_TEMPLATE_PARAM_TYPE_BOOL;
        } else if (strncmp(type_str, "pattern:", 8) == 0) {
            validation->type = MCP_TEMPLATE_PARAM_TYPE_CUSTOM;
            validation->pattern = mcp_strdup(type_str + 8);
        } else {
            // Default to string
            validation->type = MCP_TEMPLATE_PARAM_TYPE_STRING;
            // Keep the default range values initialized earlier
        }

        name_end = type_end;
    }

    // Check for default value
    if (*name_end == '=') {
        name_end++; // Skip '='
        // The rest of the string is the default value
        validation->default_value = mcp_strdup(name_end);
        validation->required = false; // If there's a default, it's not required
    }

    free(spec_copy);
    return 0;
}

/**
 * @brief Validates a parameter value against its validation rules.
 *
 * This function checks if a parameter value meets the validation requirements.
 *
 * @param value The parameter value to validate
 * @param validation The validation rules to check against
 * @return 1 if the value is valid, 0 otherwise
 */
int mcp_template_validate_param(const char* value, const mcp_template_param_validation_t* validation)
{
    if (value == NULL) {
        // NULL value is only valid for non-required parameters
        mcp_log_debug("Validating NULL value, required=%d", validation ? validation->required : 1);
        return validation && !validation->required ? 1 : 0;
    }

    if (validation == NULL) {
        // No validation rules, assume valid
        mcp_log_debug("No validation rules for value '%s'", value);
        return 1;
    }

    mcp_log_debug("Validating value '%s' against type %d", value, validation->type);

    // Validate based on type
    switch (validation->type) {
        case MCP_TEMPLATE_PARAM_TYPE_INT: {
            // Check if the value is a valid integer
            char* endptr;
            long int_val = strtol(value, &endptr, 10);
            if (*endptr != '\0') {
                // Not a valid integer
                mcp_log_debug("Integer validation failed: '%s' is not a valid integer", value);
                return 0;
            }

            // Check range
            if (int_val < validation->range.int_range.min || int_val > validation->range.int_range.max) {
                mcp_log_debug("Integer range validation failed: %ld not in range [%d, %d]",
                             int_val, validation->range.int_range.min, validation->range.int_range.max);
                return 0;
            }
            return 1;
        }

        case MCP_TEMPLATE_PARAM_TYPE_FLOAT: {
            // Check if the value is a valid float
            char* endptr;
            float float_val = strtof(value, &endptr);
            if (*endptr != '\0') {
                // Not a valid float
                mcp_log_debug("Float validation failed: '%s' is not a valid float", value);
                return 0;
            }

            // Check range
            if (float_val < validation->range.float_range.min || float_val > validation->range.float_range.max) {
                mcp_log_debug("Float range validation failed: %f not in range [%f, %f]",
                             float_val, validation->range.float_range.min, validation->range.float_range.max);
                return 0;
            }
            return 1;
        }

        case MCP_TEMPLATE_PARAM_TYPE_BOOL: {
            // Check if the value is a valid boolean
            if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0 &&
                strcmp(value, "1") != 0 && strcmp(value, "0") != 0) {
                mcp_log_debug("Boolean validation failed: '%s' is not a valid boolean", value);
                return 0;
            }
            return 1;
        }

        case MCP_TEMPLATE_PARAM_TYPE_STRING: {
            // Check string length
            size_t len = strlen(value);
            if (len < validation->range.string_range.min_len || len > validation->range.string_range.max_len) {
                mcp_log_debug("String length validation failed: %zu not in range [%zu, %zu]",
                             len, validation->range.string_range.min_len, validation->range.string_range.max_len);
                return 0;
            }
            // String parameters are always valid if they pass length check
            return 1;
        }

        case MCP_TEMPLATE_PARAM_TYPE_CUSTOM: {
            // Check against simple pattern
            if (validation->pattern == NULL) {
                // No pattern specified, assume valid
                mcp_log_debug("No pattern specified for custom validation, assuming valid");
                return 1;
            }

            // Improved pattern matching with * wildcard
            // For example: "abc*" matches "abcdef", "*abc" matches "xyzabc", "a*c" matches "abc"
            const char* pattern = validation->pattern;

            mcp_log_debug("Validating '%s' against pattern '%s'", value, pattern);

            // If the pattern ends with *, match anything that starts with the pattern prefix
            size_t pattern_len = strlen(pattern);
            if (pattern_len > 0 && pattern[pattern_len - 1] == '*') {
                // Match prefix
                size_t prefix_len = pattern_len - 1;
                mcp_log_debug("Pattern ends with *, match anything that starts with prefix");
                return strncmp(value, pattern, prefix_len) == 0;
            }

            // If the pattern starts with *, match anything that ends with the pattern suffix
            if (pattern_len > 0 && pattern[0] == '*') {
                // Match suffix
                size_t value_len = strlen(value);
                size_t suffix_len = pattern_len - 1;
                if (value_len < suffix_len) {
                    return 0;
                }
                return strcmp(value + (value_len - suffix_len), pattern + 1) == 0;
            }

            // If the pattern contains * in the middle, split and match both sides
            const char* star = strchr(pattern, '*');
            if (star != NULL) {
                // Split the pattern at the *
                size_t prefix_len = star - pattern;
                const char* suffix = star + 1;
                size_t suffix_len = strlen(suffix);
                size_t value_len = strlen(value);

                // Match prefix
                if (strncmp(value, pattern, prefix_len) != 0) {
                    return 0;
                }

                // Match suffix
                if (value_len < prefix_len + suffix_len) {
                    return 0;
                }
                return strcmp(value + (value_len - suffix_len), suffix) == 0;
            }

            // No wildcards, exact match
            return strcmp(value, pattern) == 0;
        }

        default:
            // Unknown type, assume valid
            mcp_log_debug("Unknown parameter type %d, assuming valid", validation->type);
            return 1;
    }

    // This code is unreachable because all switch cases return
    // But we keep it as a fallback in case future modifications change the control flow
}

/**
 * @brief Expands a URI template by replacing placeholders with values.
 *
 * This function supports the following placeholder formats:
 * - Simple: {name}
 * - Optional: {name?}
 * - Default value: {name=default}
 * - Typed: {name:type}
 * - Combined: {name:type=default}
 *
 * @param template The URI template string (e.g., "example://{name}/resource")
 * @param params A JSON object containing parameter values (e.g., {"name": "test"})
 * @return A newly allocated string with the expanded URI, or NULL on error
 */
/**
 * @brief Helper function to process a template parameter during expansion.
 *
 * @param param_spec The parameter specification string
 * @param param_name Buffer to store the parameter name
 * @param param_name_size Size of the param_name buffer
 * @param params JSON object containing parameter values
 * @param param_value_out Pointer to store the parameter value
 * @param validation Validation structure to fill
 * @return 0 on success, -1 on error
 */
static int process_template_param(
    const char* param_spec,
    char* param_name,
    size_t param_name_size,
    const mcp_json_t* params,
    const char** param_value_out,
    mcp_template_param_validation_t* validation
) {
    // Parse parameter specification
    if (mcp_template_parse_param_spec(param_spec, param_name, param_name_size, validation) != 0) {
        mcp_log_warn("Failed to parse template parameter specification: %s", param_spec);
        return -1;
    }

    // Look up parameter value
    const char* param_value = NULL;
    mcp_json_t* value_node = mcp_json_object_get_property(params, param_name);
    bool param_found = false;

    if (value_node != NULL && mcp_json_get_type(value_node) == MCP_JSON_STRING &&
        mcp_json_get_string(value_node, &param_value) == 0 && param_value != NULL) {
        param_found = true;
    }

    // Handle missing parameters
    if (!param_found) {
        if (validation->required) {
            // Required parameter is missing
            mcp_log_warn("Required template parameter '%s' not found", param_name);
            return -1;
        } else if (validation->default_value != NULL) {
            // Use default value for optional parameter
            param_value = validation->default_value;
        } else {
            // Optional parameter with no default, use empty string
            param_value = "";
        }
    }

    // Validate parameter value
    if (param_value != NULL && !mcp_template_validate_param(param_value, validation)) {
        // Parameter value is invalid
        mcp_log_warn("Template parameter '%s' value '%s' is invalid", param_name, param_value);
        return -1;
    }

    *param_value_out = param_value;
    return 0;
}

char* mcp_template_expand(const char* template, const mcp_json_t* params) {
    if (template == NULL) {
        mcp_log_error("Template is NULL in mcp_template_expand");
        return NULL;
    }

    if (params == NULL) {
        mcp_log_error("Params is NULL in mcp_template_expand");
        return NULL;
    }

    mcp_json_type_t type = mcp_json_get_type(params);
    if (type != MCP_JSON_OBJECT) {
        mcp_log_error("Params is not a JSON object in mcp_template_expand, type=%d", type);
        return NULL;
    }

    mcp_log_debug("Expanding template: '%s'", template);

    // First pass: calculate the expanded size and validate parameters
    size_t expanded_size = 0;
    const char* p = template;
    while (*p) {
        if (*p == '{') {
            // Found a placeholder start
            const char* end = strchr(p, '}');
            if (end == NULL) {
                // Malformed template - missing closing brace
                return NULL;
            }

            // Extract parameter specification
            size_t param_spec_len = end - p - 1;
            char param_spec[256]; // Reasonable limit for parameter spec
            if (param_spec_len >= sizeof(param_spec)) {
                // Parameter spec too long
                return NULL;
            }
            memcpy(param_spec, p + 1, param_spec_len);
            param_spec[param_spec_len] = '\0';

            // Process parameter
            char param_name[128];
            mcp_template_param_validation_t validation;
            const char* param_value = NULL;

            int result = process_template_param(
                param_spec, param_name, sizeof(param_name),
                params, &param_value, &validation
            );

            if (result != 0) {
                mcp_template_free_validation(&validation);
                return NULL;
            }

            // Add parameter value length to expanded size
            expanded_size += strlen(param_value);

            // Clean up validation structure
            mcp_template_free_validation(&validation);

            // Move past the placeholder
            p = end + 1;
        } else {
            // Regular character
            expanded_size++;
            p++;
        }
    }

    // Allocate buffer for expanded URI
    char* expanded = (char*)malloc(expanded_size + 1);
    if (expanded == NULL) {
        return NULL;
    }

    // Second pass: fill in the expanded URI
    p = template;
    char* dest = expanded;
    while (*p) {
        if (*p == '{') {
            // Found a placeholder start
            const char* end = strchr(p, '}');

            // Extract parameter specification
            size_t param_spec_len = end - p - 1;
            char param_spec[256];
            memcpy(param_spec, p + 1, param_spec_len);
            param_spec[param_spec_len] = '\0';

            // Process parameter
            char param_name[128];
            mcp_template_param_validation_t validation;
            const char* param_value = NULL;

            int result = process_template_param(
                param_spec, param_name, sizeof(param_name),
                params, &param_value, &validation
            );

            if (result != 0) {
                mcp_template_free_validation(&validation);
                free(expanded);
                return NULL;
            }

            // Copy parameter value to output
            size_t value_len = strlen(param_value);
            memcpy(dest, param_value, value_len);
            dest += value_len;

            // Clean up validation structure
            mcp_template_free_validation(&validation);

            // Move past the placeholder
            p = end + 1;
        } else {
            // Regular character
            *dest++ = *p++;
        }
    }
    *dest = '\0';

    return expanded;
}

/**
 * @brief Checks if a URI matches a template pattern.
 *
 * This function determines if a URI could have been generated from a template.
 * It doesn't extract parameter values, just checks if the pattern matches.
 * It supports advanced template features like optional parameters and typed parameters.
 *
 * @param uri The URI to check
 * @param template The template pattern to match against
 * @return 1 if the URI matches the template pattern, 0 otherwise
 */
int mcp_template_matches(const char* uri, const char* template) {
    if (uri == NULL || template == NULL) {
        return 0;
    }

    const char* u = uri;
    const char* t = template;

    while (*t) {
        if (*t == '{') {
            // Found a placeholder start
            const char* end = strchr(t, '}');
            if (end == NULL) {
                // Malformed template - missing closing brace
                return 0;
            }

            // Extract parameter specification
            size_t param_spec_len = end - t - 1;
            char param_spec[256];
            if (param_spec_len >= sizeof(param_spec)) {
                // Parameter spec too long
                return 0;
            }
            memcpy(param_spec, t + 1, param_spec_len);
            param_spec[param_spec_len] = '\0';

            // Parse parameter specification
            char param_name[128];
            mcp_template_param_validation_t validation;
            if (mcp_template_parse_param_spec(param_spec, param_name, sizeof(param_name), &validation) != 0) {
                // Failed to parse parameter specification
                mcp_template_free_validation(&validation);
                return 0;
            }

            // Skip to the next static part of the template
            t = end + 1;

            // Find the next static character in the template
            char next_static = *t;
            if (next_static == '\0') {
                // This placeholder is at the end of the template
                // The rest of the URI can be anything
                mcp_template_free_validation(&validation);
                return 1;
            }

            // Find the next occurrence of the static character in the URI
            const char* next_static_in_uri = strchr(u, next_static);
            if (next_static_in_uri == NULL) {
                // Static character not found in URI
                if (!validation.required) {
                    // If the parameter is optional, we can skip it
                    // and continue matching from the next static character
                    mcp_log_debug("Optional parameter '%s' not found in URI, skipping", param_name);
                    mcp_template_free_validation(&validation);
                    t = t - 1; // Back up to the static character
                    continue;
                }
                mcp_log_debug("Required parameter '%s' not found in URI", param_name);
                mcp_template_free_validation(&validation);
                return 0;
            }

            // Extract the parameter value from the URI
            size_t value_len = next_static_in_uri - u;
            char value[256]; // Use stack buffer instead of malloc
            if (value_len >= sizeof(value)) {
                // Value too long
                mcp_template_free_validation(&validation);
                return 0;
            }
            memcpy(value, u, value_len);
            value[value_len] = '\0';

            // Validate the parameter value
            int valid = mcp_template_validate_param(value, &validation);
            mcp_template_free_validation(&validation);

            if (!valid) {
                // Parameter value is invalid
                return 0;
            }

            // Skip to the static character in the URI
            u = next_static_in_uri;
        } else if (*u == *t) {
            // Characters match
            u++;
            t++;
        } else {
            // Characters don't match
            return 0;
        }
    }

    // If we've consumed the entire template and URI, it's a match
    return (*u == '\0');
}

/**
 * @brief Extracts parameter values from a URI based on a template pattern.
 *
 * This function extracts the values of parameters from a URI that matches a template.
 * It returns a JSON object containing the parameter values.
 * It supports advanced template features like optional parameters and typed parameters.
 *
 * @param uri The URI to extract parameters from
 * @param template The template pattern to match against
 * @return A newly created JSON object containing parameter values, or NULL on error
 */
mcp_json_t* mcp_template_extract_params(const char* uri, const char* template) {
    if (uri == NULL || template == NULL) {
        return NULL;
    }

    // Create a JSON object to hold parameter values
    mcp_json_t* params = mcp_json_object_create();
    if (params == NULL) {
        return NULL;
    }

    const char* u = uri;
    const char* t = template;

    while (*t) {
        if (*t == '{') {
            // Found a placeholder start
            const char* end = strchr(t, '}');
            if (end == NULL) {
                // Malformed template - missing closing brace
                mcp_json_destroy(params);
                return NULL;
            }

            // Extract parameter specification
            size_t param_spec_len = end - t - 1;
            char param_spec[256];
            if (param_spec_len >= sizeof(param_spec)) {
                // Parameter spec too long
                mcp_json_destroy(params);
                return NULL;
            }
            memcpy(param_spec, t + 1, param_spec_len);
            param_spec[param_spec_len] = '\0';

            // Parse parameter specification
            char param_name[128];
            mcp_template_param_validation_t validation;
            if (mcp_template_parse_param_spec(param_spec, param_name, sizeof(param_name), &validation) != 0) {
                // Failed to parse parameter specification
                mcp_json_destroy(params);
                return NULL;
            }

            // Find the end of the parameter value in the URI
            const char* value_start = u;
            const char* value_end = NULL;

            // The value ends at the next static part of the template
            t = end + 1;
            if (*t == '\0') {
                // This placeholder is at the end of the template
                // The value is the rest of the URI
                value_end = value_start + strlen(value_start);
            } else {
                // Find the next static character in the URI
                char next_static = *t;
                const char* next_static_in_uri = strchr(u, next_static);
                if (next_static_in_uri == NULL) {
                    // Static character not found in URI
                    if (!validation.required) {
                        // If the parameter is optional, we can skip it
                        // and continue matching from the next static character
                        if (validation.default_value != NULL) {
                            // Add default value to JSON object
                            mcp_json_t* value_node = mcp_json_string_create(validation.default_value);
                            if (value_node == NULL || mcp_json_object_set_property(params, param_name, value_node) != 0) {
                                mcp_template_free_validation(&validation);
                                mcp_json_destroy(params);
                                return NULL;
                            }
                        }
                        mcp_template_free_validation(&validation);
                        continue;
                    }
                    mcp_template_free_validation(&validation);
                    mcp_json_destroy(params);
                    return NULL;
                }
                value_end = next_static_in_uri;
                u = next_static_in_uri;
            }

            // Extract parameter value
            size_t value_len = value_end - value_start;
            char value[256]; // Use stack buffer instead of malloc
            if (value_len >= sizeof(value)) {
                // Value too long
                mcp_template_free_validation(&validation);
                mcp_json_destroy(params);
                return NULL;
            }
            memcpy(value, value_start, value_len);
            value[value_len] = '\0';

            // Validate parameter value
            if (!mcp_template_validate_param(value, &validation)) {
                // Parameter value is invalid
                mcp_template_free_validation(&validation);
                mcp_json_destroy(params);
                return NULL;
            }

            // Convert value to appropriate type based on validation
            mcp_json_t* value_node = NULL;
            switch (validation.type) {
                case MCP_TEMPLATE_PARAM_TYPE_INT: {
                    long int_val = strtol(value, NULL, 10);
                    value_node = mcp_json_number_create((double)int_val);
                    break;
                }
                case MCP_TEMPLATE_PARAM_TYPE_FLOAT: {
                    float float_val = strtof(value, NULL);
                    value_node = mcp_json_number_create((double)float_val);
                    break;
                }
                case MCP_TEMPLATE_PARAM_TYPE_BOOL: {
                    bool bool_val = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
                    value_node = mcp_json_boolean_create(bool_val);
                    break;
                }
                case MCP_TEMPLATE_PARAM_TYPE_STRING:
                case MCP_TEMPLATE_PARAM_TYPE_CUSTOM:
                default:
                    value_node = mcp_json_string_create(value);
                    break;
            }

            // Add parameter to JSON object
            if (value_node == NULL || mcp_json_object_set_property(params, param_name, value_node) != 0) {
                mcp_template_free_validation(&validation);
                mcp_json_destroy(params);
                return NULL;
            }

            mcp_template_free_validation(&validation);
        } else if (*u == *t) {
            // Characters match
            u++;
            t++;
        } else {
            // Characters don't match
            mcp_json_destroy(params);
            return NULL;
        }
    }

    return params;
}
