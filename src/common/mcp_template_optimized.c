#include "mcp_template.h"
#include "mcp_string_utils.h"
#include "mcp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Forward declaration of internal functions from mcp_template.c
extern void mcp_template_free_validation(mcp_template_param_validation_t* validation);

/**
 * @brief Validates a parameter value against validation rules.
 *
 * This is a copy of the internal function from mcp_template.c to avoid linker errors.
 *
 * @param value The parameter value to validate
 * @param validation The validation rules
 * @return 1 if the value is valid, 0 otherwise
 */
static int mcp_template_validate_param_value(const char* value, const mcp_template_param_validation_t* validation) {
    if (value == NULL) {
        return 0;
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

            // Simple pattern matching (supports only * wildcard)
            // For example: "abc*" matches "abcdef", "*abc" matches "xyzabc", "a*c" matches "abc"
            const char* pattern = validation->pattern;
            const char* p = pattern;
            const char* v = value;

            mcp_log_debug("Validating '%s' against pattern '%s'", value, pattern);

            while (*p != '\0' && *v != '\0') {
                if (*p == '*') {
                    // Wildcard - skip ahead in pattern
                    p++;
                    if (*p == '\0') {
                        // Pattern ends with *, match anything
                        mcp_log_debug("Pattern ends with *, match anything");
                        return 1;
                    }

                    // Find next occurrence of the character after * in value
                    while (*v != '\0' && *v != *p) {
                        v++;
                    }

                    if (*v == '\0') {
                        // Reached end of value without finding match
                        mcp_log_debug("Reached end of value without finding match for character after *");
                        return 0;
                    }
                } else if (*p == *v) {
                    // Characters match
                    p++;
                    v++;
                } else {
                    // Characters don't match
                    mcp_log_debug("Characters don't match: '%c' != '%c'", *p, *v);
                    return 0;
                }
            }

            // If we've consumed the entire pattern and value, it's a match
            // Or if pattern ends with * and we've consumed the value
            bool match = (*p == '\0' && *v == '\0') || (*p == '*' && *(p+1) == '\0');
            mcp_log_debug("Pattern match result: %d", match);
            return match;
        }

        default:
            // Unknown type, assume valid
            mcp_log_debug("Unknown parameter type %d, assuming valid", validation->type);
            return 1;
    }

    // This code is unreachable because all switch cases return
    // return 1;
}

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
        const char* next_static_in_uri = strstr(u, next_static);

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
        if (!mcp_template_validate_param_value(param_value, &cached->validations[i])) {
            return 0;
        }

        // Move past this parameter and static part
        u = next_static_in_uri + strlen(next_static);
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
        const char* next_static_in_uri = strstr(u, next_static);

        // If the next static part is not found, check if the parameter is optional
        if (next_static_in_uri == NULL) {
            if (!cached->validations[i].required) {
                // Optional parameter, use default value if available
                if (cached->validations[i].default_value != NULL) {
                    // Add the default value to the params
                    mcp_json_t* value = NULL;

                    switch (cached->validations[i].type) {
                        case MCP_TEMPLATE_PARAM_TYPE_INT:
                            value = mcp_json_number_create(atoi(cached->validations[i].default_value));
                            break;
                        case MCP_TEMPLATE_PARAM_TYPE_FLOAT:
                            value = mcp_json_number_create(atof(cached->validations[i].default_value));
                            break;
                        case MCP_TEMPLATE_PARAM_TYPE_BOOL:
                            {
                                bool bool_value = strcmp(cached->validations[i].default_value, "true") == 0 ||
                                                strcmp(cached->validations[i].default_value, "1") == 0;
                                value = mcp_json_boolean_create(bool_value);
                            }
                            break;
                        default:
                            value = mcp_json_string_create(cached->validations[i].default_value);
                            break;
                    }

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

        // Copy the parameter value to our buffer
        memcpy(param_value, u, param_len);
        param_value[param_len] = '\0';

        // Add the parameter value to the params based on its type
        mcp_json_t* value = NULL;

        switch (cached->validations[i].type) {
            case MCP_TEMPLATE_PARAM_TYPE_INT:
                value = mcp_json_number_create(atoi(param_value));
                break;
            case MCP_TEMPLATE_PARAM_TYPE_FLOAT:
                value = mcp_json_number_create(atof(param_value));
                break;
            case MCP_TEMPLATE_PARAM_TYPE_BOOL:
                {
                    bool bool_value = strcmp(param_value, "true") == 0 ||
                                    strcmp(param_value, "1") == 0;
                    value = mcp_json_boolean_create(bool_value);
                }
                break;
            default:
                value = mcp_json_string_create(param_value);
                break;
        }

        if (value != NULL) {
            mcp_json_object_set_property(params, cached->param_names[i], value);
        }

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
