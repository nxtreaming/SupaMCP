#ifndef MCP_TEMPLATE_H
#define MCP_TEMPLATE_H

#include "mcp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parameter type enumeration for template parameters
 */
typedef enum {
    MCP_TEMPLATE_PARAM_TYPE_STRING,  /**< String parameter (default) */
    MCP_TEMPLATE_PARAM_TYPE_INT,     /**< Integer parameter */
    MCP_TEMPLATE_PARAM_TYPE_FLOAT,   /**< Floating-point parameter */
    MCP_TEMPLATE_PARAM_TYPE_BOOL,    /**< Boolean parameter */
    MCP_TEMPLATE_PARAM_TYPE_CUSTOM   /**< Custom parameter with regex pattern */
} mcp_template_param_type_t;

/**
 * @brief Parameter validation structure
 */
typedef struct {
    mcp_template_param_type_t type;  /**< Parameter type */
    bool required;                    /**< Whether the parameter is required */
    char* default_value;              /**< Default value for optional parameters */
    char* pattern;                    /**< Regex pattern for validation (for CUSTOM type) */
    union {
        struct { int min; int max; } int_range;       /**< Range for INT type */
        struct { float min; float max; } float_range; /**< Range for FLOAT type */
        struct { size_t min_len; size_t max_len; } string_range; /**< Length range for STRING type */
    } range;                          /**< Range validation */
} mcp_template_param_validation_t;

/**
 * @brief Expands a URI template by replacing placeholders with values.
 *
 * This function supports the following placeholder formats:
 * - Simple: {name} - A required parameter with no type validation
 * - Optional: {name?} - An optional parameter that can be omitted
 * - Default value: {name=default} - A parameter with a default value if not provided
 * - Typed: {name:type} - A parameter with type validation (int, float, bool, string)
 * - Pattern matching: {name:pattern:pattern*} - A parameter that must match a pattern with * wildcards
 * - Combined: {name:type=default} - A parameter with both type validation and a default value
 * - Combined: {name:type?} - An optional parameter with type validation
 *
 * Type validation supports the following types:
 * - int: Integer values (validated with strtol)
 * - float: Floating-point values (validated with strtof)
 * - bool: Boolean values ("true", "false", "1", "0")
 * - string: String values (default)
 * - pattern:xyz* - Pattern matching with * wildcard
 *
 * @param template The URI template string (e.g., "example://{name}/resource")
 * @param params A JSON object containing parameter values (e.g., {"name": "test"})
 * @return A newly allocated string with the expanded URI, or NULL on error
 */
char* mcp_template_expand(const char* template, const mcp_json_t* params);

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
int mcp_template_matches(const char* uri, const char* template);

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
mcp_json_t* mcp_template_extract_params(const char* uri, const char* template);

/**
 * @brief Validates a parameter value against its validation rules.
 *
 * This function checks if a parameter value meets the validation requirements.
 *
 * @param value The parameter value to validate
 * @param validation The validation rules to check against
 * @return 1 if the value is valid, 0 otherwise
 */
int mcp_template_validate_param(const char* value, const mcp_template_param_validation_t* validation);

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
int mcp_template_parse_param_spec(const char* param_spec, char* name, size_t name_size, mcp_template_param_validation_t* validation);

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
    const char* default_value);

/**
 * @brief Frees resources associated with a parameter validation structure.
 *
 * @param validation The validation structure to clean up
 */
void mcp_template_free_validation(mcp_template_param_validation_t* validation);

/**
 * @brief Optimized version of mcp_template_matches that uses a template cache.
 *
 * This function is faster than mcp_template_matches for templates that are used repeatedly.
 * It caches parsed templates to avoid the overhead of parsing the template every time.
 *
 * @param uri The URI to check
 * @param template The template pattern to match against
 * @return 1 if the URI matches the template pattern, 0 otherwise
 */
int mcp_template_matches_optimized(const char* uri, const char* template);

/**
 * @brief Optimized version of mcp_template_extract_params that uses a template cache.
 *
 * This function is faster than mcp_template_extract_params for templates that are used repeatedly.
 * It caches parsed templates to avoid the overhead of parsing the template every time.
 *
 * @param uri The URI to extract parameters from
 * @param template The template pattern to match against
 * @return A newly created JSON object containing parameter values, or NULL on error
 */
mcp_json_t* mcp_template_extract_params_optimized(const char* uri, const char* template);

/**
 * @brief Cleans up the template cache.
 *
 * This function should be called when the application is shutting down
 * to free all cached templates.
 */
void mcp_template_cache_cleanup(void);

/**
 * @brief Get statistics about the template cache.
 *
 * This function returns statistics about the template cache, which can be
 * useful for monitoring and optimization.
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
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_TEMPLATE_H */
