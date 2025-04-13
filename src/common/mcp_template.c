#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "mcp_template.h"
#include "mcp_string_utils.h"
#include "mcp_log.h"

/**
 * @brief Expands a URI template by replacing placeholders with values.
 * 
 * This function supports simple {name} placeholders in URI templates.
 * It replaces each placeholder with the corresponding value from the params object.
 * 
 * @param template The URI template string (e.g., "example://{name}/resource")
 * @param params A JSON object containing parameter values (e.g., {"name": "test"})
 * @return A newly allocated string with the expanded URI, or NULL on error
 */
char* mcp_template_expand(const char* template, const mcp_json_t* params) {
    if (template == NULL || params == NULL || mcp_json_get_type(params) != MCP_JSON_OBJECT) {
        return NULL;
    }

    // First pass: calculate the expanded size
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

            // Extract parameter name
            size_t param_name_len = end - p - 1;
            char param_name[256]; // Reasonable limit for parameter name
            if (param_name_len >= sizeof(param_name)) {
                // Parameter name too long
                return NULL;
            }
            memcpy(param_name, p + 1, param_name_len);
            param_name[param_name_len] = '\0';

            // Look up parameter value
            const char* param_value = NULL;
            mcp_json_t* value_node = mcp_json_object_get_property(params, param_name);
            if (value_node == NULL || mcp_json_get_type(value_node) != MCP_JSON_STRING || 
                mcp_json_get_string(value_node, &param_value) != 0 || param_value == NULL) {
                // Parameter not found or not a string
                mcp_log_warn("Template parameter '%s' not found or not a string", param_name);
                return NULL;
            }

            // Add parameter value length to expanded size
            expanded_size += strlen(param_value);
            
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
            
            // Extract parameter name (we already validated it in the first pass)
            size_t param_name_len = end - p - 1;
            char param_name[256];
            memcpy(param_name, p + 1, param_name_len);
            param_name[param_name_len] = '\0';

            // Look up parameter value
            const char* param_value = NULL;
            mcp_json_t* value_node = mcp_json_object_get_property(params, param_name);
            mcp_json_get_string(value_node, &param_value);

            // Copy parameter value to output
            size_t value_len = strlen(param_value);
            memcpy(dest, param_value, value_len);
            dest += value_len;
            
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

            // Skip to the next static part of the template
            t = end + 1;
            
            // Find the next static character in the template
            char next_static = *t;
            if (next_static == '\0') {
                // This placeholder is at the end of the template
                // The rest of the URI can be anything
                return 1;
            }

            // Find the next occurrence of the static character in the URI
            const char* next_static_in_uri = strchr(u, next_static);
            if (next_static_in_uri == NULL) {
                // Static character not found in URI
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

            // Extract parameter name
            size_t param_name_len = end - t - 1;
            char param_name[256]; // Reasonable limit for parameter name
            if (param_name_len >= sizeof(param_name)) {
                // Parameter name too long
                mcp_json_destroy(params);
                return NULL;
            }
            memcpy(param_name, t + 1, param_name_len);
            param_name[param_name_len] = '\0';

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
                    mcp_json_destroy(params);
                    return NULL;
                }
                value_end = next_static_in_uri;
                u = next_static_in_uri;
            }

            // Extract parameter value
            size_t value_len = value_end - value_start;
            char* value = (char*)malloc(value_len + 1);
            if (value == NULL) {
                mcp_json_destroy(params);
                return NULL;
            }
            memcpy(value, value_start, value_len);
            value[value_len] = '\0';

            // Add parameter to JSON object
            mcp_json_t* value_node = mcp_json_string_create(value);
            if (value_node == NULL || mcp_json_object_set_property(params, param_name, value_node) != 0) {
                free(value);
                mcp_json_destroy(params);
                return NULL;
            }

            free(value);
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
