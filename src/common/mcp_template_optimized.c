#include "mcp_template.h"
#include "mcp_string_utils.h"
#include "mcp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Forward declaration of internal functions from mcp_template.c
extern void mcp_template_free_validation(mcp_template_param_validation_t* validation);
extern int mcp_template_parse_param_spec(const char* param_spec, char* param_name, size_t param_name_size, mcp_template_param_validation_t* validation);

/**
 * @brief Structure to hold a cached template
 */
typedef struct {
    char* template_uri;                 /**< The template URI pattern */
    char** static_parts;                /**< Array of static parts between parameters */
    size_t* static_part_lengths;        /**< Array of static part lengths (for optimization) */
    char** param_names;                 /**< Array of parameter names */
    mcp_template_param_validation_t* validations; /**< Array of parameter validations */
    size_t param_count;                 /**< Number of parameters */
    size_t static_count;                /**< Number of static parts (always param_count + 1) */
} mcp_cached_template_t;

/**
 * @brief Global cache of parsed templates
 */
#define MAX_CACHED_TEMPLATES 128
static mcp_cached_template_t* g_template_cache[MAX_CACHED_TEMPLATES] = {NULL};
static size_t g_template_cache_count = 0;
static int g_template_cache_initialized = 0;

/**
 * @brief Initialize the template cache
 */
static void mcp_template_cache_init(void) {
    if (g_template_cache_initialized) {
        return;
    }

    memset(g_template_cache, 0, sizeof(g_template_cache));
    g_template_cache_count = 0;
    g_template_cache_initialized = 1;
}

/**
 * @brief Free a cached template
 */
static void mcp_cached_template_free(mcp_cached_template_t* cached) {
    if (cached == NULL) {
        return;
    }

    free(cached->template_uri);

    for (size_t i = 0; i < cached->static_count; i++) {
        free(cached->static_parts[i]);
    }
    free(cached->static_parts);
    free(cached->static_part_lengths);

    for (size_t i = 0; i < cached->param_count; i++) {
        free(cached->param_names[i]);
        mcp_template_free_validation(&cached->validations[i]);
    }
    free(cached->param_names);
    free(cached->validations);

    free(cached);
}

/**
 * @brief Find a cached template
 */
static mcp_cached_template_t* mcp_template_cache_find(const char* template_uri) {
    if (!g_template_cache_initialized) {
        mcp_template_cache_init();
    }

    for (size_t i = 0; i < g_template_cache_count; i++) {
        if (strcmp(g_template_cache[i]->template_uri, template_uri) == 0) {
            return g_template_cache[i];
        }
    }

    return NULL;
}

/**
 * @brief Add a template to the cache
 */
static mcp_cached_template_t* mcp_template_cache_add(const char* template_uri) {
    if (!g_template_cache_initialized) {
        mcp_template_cache_init();
    }

    // Check if we already have this template
    mcp_cached_template_t* existing = mcp_template_cache_find(template_uri);
    if (existing != NULL) {
        return existing;
    }

    // Check if the cache is full
    if (g_template_cache_count >= MAX_CACHED_TEMPLATES) {
        // Replace the oldest entry (simple LRU strategy)
        mcp_cached_template_free(g_template_cache[0]);

        // Shift all entries down
        for (size_t i = 0; i < MAX_CACHED_TEMPLATES - 1; i++) {
            g_template_cache[i] = g_template_cache[i + 1];
        }
        g_template_cache_count--;
    }

    // Parse the template
    mcp_cached_template_t* cached = (mcp_cached_template_t*)calloc(1, sizeof(mcp_cached_template_t));
    if (cached == NULL) {
        return NULL;
    }

    cached->template_uri = mcp_strdup(template_uri);
    if (cached->template_uri == NULL) {
        free(cached);
        return NULL;
    }

    // Count parameters
    const char* p = template_uri;
    size_t param_count = 0;
    while ((p = strchr(p, '{')) != NULL) {
        param_count++;
        p++;
    }

    cached->param_count = param_count;
    cached->static_count = param_count + 1;

    // Allocate arrays
    cached->static_parts = (char**)calloc(cached->static_count, sizeof(char*));
    cached->static_part_lengths = (size_t*)calloc(cached->static_count, sizeof(size_t));
    cached->param_names = (char**)calloc(cached->param_count, sizeof(char*));
    cached->validations = (mcp_template_param_validation_t*)calloc(cached->param_count, sizeof(mcp_template_param_validation_t));

    if (cached->static_parts == NULL || cached->static_part_lengths == NULL ||
        cached->param_names == NULL || cached->validations == NULL) {
        mcp_cached_template_free(cached);
        return NULL;
    }

    // Parse the template
    p = template_uri;
    size_t static_index = 0;
    size_t param_index = 0;
    const char* static_start = p;

    while (*p) {
        if (*p == '{') {
            // Found a parameter start

            // Add the static part before this parameter
            size_t static_len = p - static_start;
            cached->static_parts[static_index] = (char*)malloc(static_len + 1);
            if (cached->static_parts[static_index] == NULL) {
                mcp_cached_template_free(cached);
                return NULL;
            }

            memcpy(cached->static_parts[static_index], static_start, static_len);
            cached->static_parts[static_index][static_len] = '\0';
            cached->static_part_lengths[static_index] = static_len;
            static_index++;

            // Find the end of the parameter
            const char* param_end = strchr(p, '}');
            if (param_end == NULL) {
                // Malformed template
                mcp_cached_template_free(cached);
                return NULL;
            }

            // Extract parameter specification
            size_t param_spec_len = param_end - p - 1;
            char param_spec[256];
            if (param_spec_len >= sizeof(param_spec)) {
                // Parameter spec too long
                mcp_cached_template_free(cached);
                return NULL;
            }

            memcpy(param_spec, p + 1, param_spec_len);
            param_spec[param_spec_len] = '\0';

            // Parse parameter specification
            char param_name[128];
            if (mcp_template_parse_param_spec(param_spec, param_name, sizeof(param_name), &cached->validations[param_index]) != 0) {
                // Failed to parse parameter specification
                mcp_cached_template_free(cached);
                return NULL;
            }

            // Store parameter name
            cached->param_names[param_index] = mcp_strdup(param_name);
            if (cached->param_names[param_index] == NULL) {
                mcp_cached_template_free(cached);
                return NULL;
            }

            param_index++;

            // Move to the next static part
            p = param_end + 1;
            static_start = p;
        } else {
            p++;
        }
    }

    // Add the final static part
    cached->static_parts[static_index] = mcp_strdup(static_start);
    if (cached->static_parts[static_index] == NULL) {
        mcp_cached_template_free(cached);
        return NULL;
    }
    cached->static_part_lengths[static_index] = strlen(static_start);

    // Add to cache
    g_template_cache[g_template_cache_count] = cached;
    g_template_cache_count++;

    return cached;
}

/**
 * @brief Find the next static part in the URI
 * 
 * This function finds the next occurrence of a static part in the URI.
 * It handles empty static parts correctly.
 * 
 * @param uri The URI to search in
 * @param static_part The static part to find
 * @param static_part_len The length of the static part
 * @return Pointer to the next static part in the URI, or NULL if not found
 */
static const char* find_next_static_part(const char* uri, const char* static_part, size_t static_part_len) {
    // If the static part is empty, it matches the end of the URI
    if (static_part_len == 0 || *static_part == '\0') {
        return uri + strlen(uri);
    }
    
    // If the static part is not empty, use strstr to find it
    return strstr(uri, static_part);
}

/**
 * @brief Simple pattern matching with * wildcard
 * 
 * @param value The value to match against the pattern
 * @param pattern The pattern to match
 * @return 1 if the value matches the pattern, 0 otherwise
 */
static int pattern_match(const char* value, const char* pattern) {
    if (value == NULL || pattern == NULL) {
        return 0;
    }

    // If the pattern ends with *, match anything that starts with the pattern prefix
    size_t pattern_len = strlen(pattern);
    if (pattern_len > 0 && pattern[pattern_len - 1] == '*') {
        // Match prefix
        size_t prefix_len = pattern_len - 1;
        mcp_log_debug("Pattern ends with *, match anything");
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

/**
 * @brief Validate a parameter value against a validation spec
 * 
 * @param value The parameter value to validate
 * @param validation The validation spec
 * @return 1 if the value is valid, 0 otherwise
 */
static int validate_param_value(const char* value, const mcp_template_param_validation_t* validation) {
    if (value == NULL || validation == NULL) {
        return 0;
    }

    // Empty value is only valid for optional parameters
    if (value[0] == '\0') {
        return !validation->required;
    }

    // Validate based on type
    switch (validation->type) {
        case MCP_TEMPLATE_PARAM_TYPE_STRING:
            // String type accepts any value
            return 1;

        case MCP_TEMPLATE_PARAM_TYPE_INT: {
            // Integer type must be a valid integer
            char* endptr;
            long val = strtol(value, &endptr, 10);
            if (*endptr != '\0') {
                // Not a valid integer
                mcp_log_debug("Value '%s' is not a valid integer", value);
                return 0;
            }

            // Check range if specified
            if (val < validation->range.int_range.min) {
                mcp_log_debug("Value %ld is less than minimum %d", val, validation->range.int_range.min);
                return 0;
            }
            if (val > validation->range.int_range.max) {
                mcp_log_debug("Value %ld is greater than maximum %d", val, validation->range.int_range.max);
                return 0;
            }

            return 1;
        }

        case MCP_TEMPLATE_PARAM_TYPE_FLOAT: {
            // Float type must be a valid floating-point number
            char* endptr;
            double val = strtod(value, &endptr);
            if (*endptr != '\0') {
                // Not a valid float
                mcp_log_debug("Value '%s' is not a valid float", value);
                return 0;
            }

            // Check range if specified
            if (val < validation->range.float_range.min) {
                mcp_log_debug("Value %f is less than minimum %f", val, (double)validation->range.float_range.min);
                return 0;
            }
            if (val > validation->range.float_range.max) {
                mcp_log_debug("Value %f is greater than maximum %f", val, (double)validation->range.float_range.max);
                return 0;
            }

            return 1;
        }

        case MCP_TEMPLATE_PARAM_TYPE_BOOL:
            // Boolean type must be "true", "false", "1", or "0"
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                return 1;
            }
            mcp_log_debug("Value '%s' is not a valid boolean", value);
            return 0;

        case MCP_TEMPLATE_PARAM_TYPE_CUSTOM:
            // Custom type must match the specified pattern
            if (validation->pattern == NULL) {
                // No pattern specified, accept any value
                return 1;
            }

            // Simple pattern matching with * wildcard
            return pattern_match(value, validation->pattern);

        default:
            // Unknown type
            mcp_log_debug("Unknown parameter type: %d", validation->type);
            return 0;
    }
}

/**
 * @brief Optimized template matching function
 *
 * This function uses a cached template to match a URI against a template pattern.
 * It's much faster than the original implementation because it doesn't need to
 * parse the template every time.
 *
 * @param uri The URI to match
 * @param template The template pattern to match against
 * @return 1 if the URI matches the template pattern, 0 otherwise
 */
int mcp_template_matches_optimized(const char* uri, const char* template_uri) {
    if (uri == NULL || template_uri == NULL) {
        return 0;
    }

    // Get or create cached template
    mcp_cached_template_t* cached = mcp_template_cache_find(template_uri);
    if (cached == NULL) {
        cached = mcp_template_cache_add(template_uri);
        if (cached == NULL) {
            // Fall back to the original implementation
            return mcp_template_matches(uri, template_uri);
        }
    }

    // Match the URI against the cached template
    const char* u = uri;

    // Check if the URI starts with the first static part
    if (strncmp(u, cached->static_parts[0], cached->static_part_lengths[0]) != 0) {
        return 0;
    }

    // Skip the first static part
    u += cached->static_part_lengths[0];

    // Match each parameter
    for (size_t i = 0; i < cached->param_count; i++) {
        // Find the next static part in the URI
        const char* next_static = cached->static_parts[i + 1];
        const size_t next_static_len = cached->static_part_lengths[i + 1];
        const char* next_static_in_uri = find_next_static_part(u, next_static, next_static_len);

        // If the next static part is not found, check if the parameter is optional
        if (next_static_in_uri == NULL) {
            if (!cached->validations[i].required) {
                // Optional parameter, skip it
                continue;
            } else {
                // Required parameter not found
                return 0;
            }
        }

        // Extract the parameter value
        size_t param_len = next_static_in_uri - u;
        char param_value[256];
        if (param_len >= sizeof(param_value)) {
            // Parameter value too long
            return 0;
        }

        memcpy(param_value, u, param_len);
        param_value[param_len] = '\0';

        // Validate the parameter value
        if (!validate_param_value(param_value, &cached->validations[i])) {
            return 0;
        }

        // Move past this parameter and static part
        u = next_static_in_uri + next_static_len;
    }

    // If we've consumed the entire URI, it's a match
    return (*u == '\0');
}

/**
 * @brief Optimized parameter extraction function
 *
 * This function uses a cached template to extract parameters from a URI.
 * It's much faster than the original implementation because it doesn't need to
 * parse the template every time and minimizes memory allocations and string operations.
 *
 * @param uri The URI to extract parameters from
 * @param template The template pattern to match against
 * @return A newly created JSON object containing parameter values, or NULL on error
 */
mcp_json_t* mcp_template_extract_params_optimized(const char* uri, const char* template_uri) {
    if (uri == NULL || template_uri == NULL) {
        return NULL;
    }

    // Get or create cached template
    mcp_cached_template_t* cached = mcp_template_cache_find(template_uri);
    if (cached == NULL) {
        cached = mcp_template_cache_add(template_uri);
        if (cached == NULL) {
            // Fall back to the original implementation
            return mcp_template_extract_params(uri, template_uri);
        }
    }

    // We'll do the matching as part of the extraction process
    // This avoids the overhead of calling the matching function separately

    // Create a JSON object to hold parameter values
    mcp_json_t* params = mcp_json_object_create();
    if (params == NULL) {
        return NULL;
    }

    // Match the URI against the cached template
    const char* u = uri;

    // Check if the URI starts with the first static part
    if (strncmp(u, cached->static_parts[0], cached->static_part_lengths[0]) != 0) {
        mcp_json_destroy(params);
        return NULL;
    }

    // Skip the first static part
    u += cached->static_part_lengths[0];

    // Pre-allocate a buffer for parameter values to avoid stack allocations in the loop
    char param_value[256];

    // Extract each parameter
    for (size_t i = 0; i < cached->param_count; i++) {
        // Find the next static part in the URI
        const char* next_static = cached->static_parts[i + 1];
        const size_t next_static_len = cached->static_part_lengths[i + 1];
        const char* next_static_in_uri = find_next_static_part(u, next_static, next_static_len);

        // If the next static part is not found, check if the parameter is optional
        if (next_static_in_uri == NULL) {
            if (!cached->validations[i].required) {
                // Optional parameter, use default value if available
                if (cached->validations[i].default_value != NULL) {
                    // Add the default value to the params
                    mcp_json_t* value = NULL;
                    
                    // Create the appropriate JSON value based on the parameter type
                    switch (cached->validations[i].type) {
                        case MCP_TEMPLATE_PARAM_TYPE_INT:
                            value = mcp_json_number_create(atoi(cached->validations[i].default_value));
                            break;
                        case MCP_TEMPLATE_PARAM_TYPE_FLOAT:
                            value = mcp_json_number_create(atof(cached->validations[i].default_value));
                            break;
                        case MCP_TEMPLATE_PARAM_TYPE_BOOL:
                            value = mcp_json_boolean_create(
                                strcmp(cached->validations[i].default_value, "true") == 0 ||
                                strcmp(cached->validations[i].default_value, "1") == 0
                            );
                            break;
                        default:
                            value = mcp_json_string_create(cached->validations[i].default_value);
                            break;
                    }
                    
                    if (value != NULL) {
                        mcp_json_object_set_property(params, cached->param_names[i], value);
                    }
                } else {
                    // No default value, add an empty string
                    mcp_json_t* value = mcp_json_string_create("");
                    if (value != NULL) {
                        mcp_json_object_set_property(params, cached->param_names[i], value);
                    }
                }
                
                // Skip this parameter
                continue;
            } else {
                // Required parameter not found - this shouldn't happen if the URI matched the template
                mcp_json_destroy(params);
                return NULL;
            }
        }

        // Extract the parameter value
        size_t param_len = next_static_in_uri - u;
        if (param_len >= sizeof(param_value)) {
            // Parameter value too long
            mcp_json_destroy(params);
            return NULL;
        }
        
        memcpy(param_value, u, param_len);
        param_value[param_len] = '\0';
        
        // Create the appropriate JSON value based on the parameter type
        mcp_json_t* value = NULL;
        
        switch (cached->validations[i].type) {
            case MCP_TEMPLATE_PARAM_TYPE_INT:
                value = mcp_json_number_create(atoi(param_value));
                break;
            case MCP_TEMPLATE_PARAM_TYPE_FLOAT:
                value = mcp_json_number_create(atof(param_value));
                break;
            case MCP_TEMPLATE_PARAM_TYPE_BOOL:
                value = mcp_json_boolean_create(
                    strcmp(param_value, "true") == 0 ||
                    strcmp(param_value, "1") == 0
                );
                break;
            default:
                value = mcp_json_string_create(param_value);
                break;
        }
        
        if (value == NULL) {
            mcp_json_destroy(params);
            return NULL;
        }
        
        // Add the parameter to the JSON object
        mcp_json_object_set_property(params, cached->param_names[i], value);
        
        // Move past this parameter and static part
        u = next_static_in_uri + next_static_len;
    }

    // If we've consumed the entire URI, it's a match
    if (*u != '\0') {
        mcp_json_destroy(params);
        return NULL;
    }

    return params;
}

/**
 * @brief Clean up the template cache
 *
 * This function should be called when the application is shutting down
 * to free all cached templates.
 */
void mcp_template_cache_cleanup(void) {
    if (!g_template_cache_initialized) {
        return;
    }

    for (size_t i = 0; i < g_template_cache_count; i++) {
        mcp_cached_template_free(g_template_cache[i]);
        g_template_cache[i] = NULL;
    }

    g_template_cache_count = 0;
    g_template_cache_initialized = 0;
}
