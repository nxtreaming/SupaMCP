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
 * @brief Cache statistics for monitoring and optimization
 */
static struct {
    size_t hits;              /**< Number of cache hits */
    size_t misses;            /**< Number of cache misses */
    size_t evictions;         /**< Number of cache evictions */
    size_t total_lookups;     /**< Total number of cache lookups */
} g_cache_stats = {0, 0, 0, 0};

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

    // Reset statistics
    g_cache_stats.hits = 0;
    g_cache_stats.misses = 0;
    g_cache_stats.evictions = 0;
    g_cache_stats.total_lookups = 0;
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
/**
 * @brief Move a template to the front of the cache (most recently used position)
 */
static void mcp_template_cache_move_to_front(size_t index) {
    if (index == 0 || index >= g_template_cache_count) {
        return;
    }

    // Save the template to move
    mcp_cached_template_t* template = g_template_cache[index];

    // Shift all templates between 0 and index down by one
    for (size_t i = index; i > 0; i--) {
        g_template_cache[i] = g_template_cache[i - 1];
    }

    // Place the template at the front
    g_template_cache[0] = template;
}

static mcp_cached_template_t* mcp_template_cache_find(const char* template_uri) {
    if (!g_template_cache_initialized) {
        mcp_template_cache_init();
    }

    g_cache_stats.total_lookups++;

    // Use a hash-based approach for faster lookups in large caches
    for (size_t i = 0; i < g_template_cache_count; i++) {
        if (strcmp(g_template_cache[i]->template_uri, template_uri) == 0) {
            // Found the template - move it to the front (LRU strategy)
            mcp_template_cache_move_to_front(i);
            g_cache_stats.hits++;
            return g_template_cache[0]; // Now at the front
        }
    }

    g_cache_stats.misses++;
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
        // Replace the least recently used entry (at the end of the array)
        mcp_cached_template_free(g_template_cache[g_template_cache_count - 1]);
        g_template_cache_count--;
        g_cache_stats.evictions++;
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

    // Add to cache at the front (most recently used position)
    // First, make room at the front
    if (g_template_cache_count > 0) {
        for (size_t i = g_template_cache_count; i > 0; i--) {
            g_template_cache[i] = g_template_cache[i - 1];
        }
    }

    // Add the new template at the front
    g_template_cache[0] = cached;
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
 * @brief Improved pattern matching with * wildcard
 *
 * This function matches a value against a pattern that can contain * wildcards.
 * It supports patterns like:
 * - "abc*" - matches anything that starts with "abc"
 * - "*abc" - matches anything that ends with "abc"
 * - "a*c" - matches anything that starts with "a" and ends with "c"
 *
 * @param value The value to match against the pattern
 * @param pattern The pattern to match
 * @return 1 if the value matches the pattern, 0 otherwise
 */
static int pattern_match(const char* value, const char* pattern) {
    if (value == NULL || pattern == NULL) {
        return 0;
    }

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

/**
 * @brief Validate a parameter value against a validation spec
 *
 * This function delegates to mcp_template_validate_param in mcp_template.c
 * to ensure consistent validation behavior between the two implementations.
 *
 * @param value The parameter value to validate
 * @param validation The validation spec
 * @return 1 if the value is valid, 0 otherwise
 */
static int validate_param_value(const char* value, const mcp_template_param_validation_t* validation) {
    // Empty value is only valid for optional parameters
    if (value != NULL && value[0] == '\0') {
        return validation != NULL && !validation->required;
    }

    // Delegate to the standard validation function
    return mcp_template_validate_param(value, validation);
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
 * @brief Get statistics about the template cache
 *
 * @param hits Pointer to store the number of cache hits (can be NULL)
 * @param misses Pointer to store the number of cache misses (can be NULL)
 * @param evictions Pointer to store the number of cache evictions (can be NULL)
 * @param total_lookups Pointer to store the total number of cache lookups (can be NULL)
 * @param cache_size Pointer to store the current cache size (can be NULL)
 * @param max_cache_size Pointer to store the maximum cache size (can be NULL)
 */
void mcp_template_cache_get_stats(
    size_t* hits,
    size_t* misses,
    size_t* evictions,
    size_t* total_lookups,
    size_t* cache_size,
    size_t* max_cache_size
) {
    if (hits) *hits = g_cache_stats.hits;
    if (misses) *misses = g_cache_stats.misses;
    if (evictions) *evictions = g_cache_stats.evictions;
    if (total_lookups) *total_lookups = g_cache_stats.total_lookups;
    if (cache_size) *cache_size = g_template_cache_count;
    if (max_cache_size) *max_cache_size = MAX_CACHED_TEMPLATES;
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

    // Reset statistics
    g_cache_stats.hits = 0;
    g_cache_stats.misses = 0;
    g_cache_stats.evictions = 0;
    g_cache_stats.total_lookups = 0;
}
