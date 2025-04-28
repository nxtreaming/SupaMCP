/**
 * @file kmcp_config_parser.c
 * @brief Implementation of enhanced configuration file parser for KMCP
 */

#include "kmcp_config_parser.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include "mcp_string_utils.h"
// TODO: Add JSON schema validation in the future
// #include "mcp_json_schema_validate.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Additional error codes
#define KMCP_ERROR_CONFIG_INVALID KMCP_ERROR_INVALID_PARAMETER

// Forward declarations for missing functions
static const char* mcp_json_type_name(mcp_json_type_t type) {
    switch (type) {
        case MCP_JSON_NULL: return "null";
        case MCP_JSON_BOOLEAN: return "boolean";
        case MCP_JSON_NUMBER: return "number";
        case MCP_JSON_STRING: return "string";
        case MCP_JSON_ARRAY: return "array";
        case MCP_JSON_OBJECT: return "object";
        default: return "unknown";
    }
}

static char* mcp_json_stringify_pretty(const mcp_json_t* json) {
    // For now, just use the regular stringify function
    return mcp_json_stringify(json);
}

static mcp_json_t* mcp_json_deep_copy(const mcp_json_t* json) {
    if (!json) return NULL;

    // Convert to string and parse back (inefficient but works)
    char* json_str = mcp_json_stringify(json);
    if (!json_str) return NULL;

    mcp_json_t* copy = mcp_json_parse(json_str);
    free(json_str);

    return copy;
}


// Helper functions for JSON operations

/**
 * @brief Get a string from a JSON object property
 *
 * @param json The JSON object
 * @param name The property name
 * @param result Pointer to receive the string value
 * @param required Whether the property is required
 * @return int 0 on success, -1 on failure
 */
static int get_string_from_property(const mcp_json_t* json, const char* name, const char** result, bool required) {
    if (!json || !name || !result) return -1;
    *result = NULL;

    mcp_json_t* value = mcp_json_object_get_property(json, name);
    if (!value) {
        if (required) {
            mcp_log_error("Required property '%s' not found", name);
            return -1;
        }
        return 0; // Not found but not required
    }

    if (!mcp_json_is_string(value)) {
        mcp_log_error("Property '%s' is not a string", name);
        return -1;
    }

    return mcp_json_get_string(value, result);
}

/**
 * @brief Get a string from a JSON array item
 *
 * @param json The JSON array
 * @param index The array index
 * @param result Pointer to receive the string value
 * @return int 0 on success, -1 on failure
 */
static int get_string_from_array_item(const mcp_json_t* json, int index, const char** result) {
    if (!json || index < 0 || !result) return -1;
    *result = NULL;

    mcp_json_t* item = mcp_json_array_get_item(json, index);
    if (!item) {
        mcp_log_error("Array item at index %d not found", index);
        return -1;
    }

    if (!mcp_json_is_string(item)) {
        mcp_log_error("Array item at index %d is not a string", index);
        return -1;
    }

    return mcp_json_get_string(item, result);
}

/**
 * @brief Get a boolean from a JSON object property
 *
 * @param json The JSON object
 * @param name The property name
 * @param result Pointer to receive the boolean value
 * @param default_value Default value to use if property is not found
 * @return int 0 on success, -1 on failure
 */
static int get_boolean_from_property(const mcp_json_t* json, const char* name, bool* result, bool default_value) {
    if (!json || !name || !result) return -1;
    *result = default_value;

    mcp_json_t* value = mcp_json_object_get_property(json, name);
    if (!value) {
        return 0; // Use default value
    }

    if (!mcp_json_is_boolean(value)) {
        mcp_log_error("Property '%s' is not a boolean", name);
        return -1;
    }

    return mcp_json_get_boolean(value, result);
}

/**
 * @brief Get a number from a JSON object property
 *
 * @param json The JSON object
 * @param name The property name
 * @param result Pointer to receive the number value
 * @param default_value Default value to use if property is not found
 * @return int 0 on success, -1 on failure
 */
static int get_number_from_property(const mcp_json_t* json, const char* name, double* result, double default_value) {
    if (!json || !name || !result) return -1;
    *result = default_value;

    mcp_json_t* value = mcp_json_object_get_property(json, name);
    if (!value) {
        return 0; // Use default value
    }

    if (!mcp_json_is_number(value)) {
        mcp_log_error("Property '%s' is not a number", name);
        return -1;
    }

    return mcp_json_get_number(value, result);
}

/**
 * @brief JSON path component type
 */
typedef enum {
    JSON_PATH_OBJECT,  // Object property access (e.g., "foo")
    JSON_PATH_ARRAY    // Array index access (e.g., "[0]")
} json_path_component_type_t;

/**
 * @brief JSON path component
 */
typedef struct {
    json_path_component_type_t type;  // Component type
    union {
        char* property;  // Object property name
        int index;       // Array index
    } value;
} json_path_component_t;

/**
 * @brief JSON path
 */
typedef struct {
    json_path_component_t* components;  // Path components
    size_t component_count;             // Number of components
} json_path_t;

/**
 * @brief Configuration schema
 */
typedef struct {
    const char* schema_json;  // JSON schema as a string
    size_t schema_size;       // Schema size in bytes
} config_schema_t;

/**
 * @brief Complete definition of configuration parser structure
 */
struct kmcp_config_parser {
    char* file_path;                      // Configuration file path
    mcp_json_t* json;                     // Parsed JSON
    kmcp_config_parser_options_t options; // Parser options
    // TODO: Add schema validation in the future
    // mcp_json_schema_t* schema;         // JSON schema for validation
};

/**
 * @brief Parse a JSON path string into components
 *
 * @param path_str JSON path string (e.g., "foo.bar[0].baz")
 * @return json_path_t* Parsed JSON path, or NULL on failure
 */
static json_path_t* parse_json_path(const char* path_str) {
    if (!path_str) return NULL;

    // Count the number of components
    size_t component_count = 1; // At least one component
    for (const char* p = path_str; *p; p++) {
        if (*p == '.' || *p == '[') {
            component_count++;
        }
    }

    // Allocate memory for the path
    json_path_t* path = (json_path_t*)malloc(sizeof(json_path_t));
    if (!path) {
        mcp_log_error("Failed to allocate memory for JSON path");
        return NULL;
    }

    // Allocate memory for the components
    path->components = (json_path_component_t*)malloc(component_count * sizeof(json_path_component_t));
    if (!path->components) {
        mcp_log_error("Failed to allocate memory for JSON path components");
        free(path);
        return NULL;
    }

    // Initialize component count
    path->component_count = 0;

    // Parse the path string
    const char* p = path_str;
    while (*p) {
        // Skip leading dots
        if (*p == '.') {
            p++;
            continue;
        }

        // Check if this is an array index
        if (*p == '[') {
            // Parse array index
            p++; // Skip [
            char* end;
            int index = (int)strtol(p, &end, 10);
            if (end == p || *end != ']') {
                mcp_log_error("Invalid array index in JSON path: %s", path_str);
                // Free memory
                for (size_t i = 0; i < path->component_count; i++) {
                    if (path->components[i].type == JSON_PATH_OBJECT) {
                        free(path->components[i].value.property);
                    }
                }
                free(path->components);
                free(path);
                return NULL;
            }

            // Add array index component
            path->components[path->component_count].type = JSON_PATH_ARRAY;
            path->components[path->component_count].value.index = index;
            path->component_count++;

            // Move to the next character after ]
            p = end + 1;
        } else {
            // Parse object property
            const char* start = p;
            while (*p && *p != '.' && *p != '[') {
                p++;
            }

            // Extract property name
            size_t len = p - start;
            char* property = (char*)malloc(len + 1);
            if (!property) {
                mcp_log_error("Failed to allocate memory for property name");
                // Free memory
                for (size_t i = 0; i < path->component_count; i++) {
                    if (path->components[i].type == JSON_PATH_OBJECT) {
                        free(path->components[i].value.property);
                    }
                }
                free(path->components);
                free(path);
                return NULL;
            }

            // Copy property name
            memcpy(property, start, len);
            property[len] = '\0';

            // Add object property component
            path->components[path->component_count].type = JSON_PATH_OBJECT;
            path->components[path->component_count].value.property = property;
            path->component_count++;
        }
    }

    return path;
}

/**
 * @brief Free a JSON path
 *
 * @param path JSON path to free
 */
static void free_json_path(json_path_t* path) {
    if (!path) return;

    // Free components
    for (size_t i = 0; i < path->component_count; i++) {
        if (path->components[i].type == JSON_PATH_OBJECT) {
            free(path->components[i].value.property);
        }
    }

    // Free component array and path
    free(path->components);
    free(path);
}

/**
 * @brief Get a JSON value by path
 *
 * @param json JSON object or array
 * @param path JSON path
 * @return mcp_json_t* JSON value, or NULL if not found
 */
static mcp_json_t* get_json_by_path(const mcp_json_t* json, const json_path_t* path) {
    if (!json || !path) return NULL;

    // Start with the root JSON object
    const mcp_json_t* current = json;

    // Traverse the path
    for (size_t i = 0; i < path->component_count; i++) {
        if (!current) return NULL;

        // Get the next component based on type
        if (path->components[i].type == JSON_PATH_OBJECT) {
            // Get object property
            if (!mcp_json_is_object(current)) {
                mcp_log_error("Expected object but found %s", mcp_json_type_name(mcp_json_get_type(current)));
                return NULL;
            }

            current = mcp_json_object_get_property(current, path->components[i].value.property);
        } else {
            // Get array item
            if (!mcp_json_is_array(current)) {
                mcp_log_error("Expected array but found %s", mcp_json_type_name(mcp_json_get_type(current)));
                return NULL;
            }

            current = mcp_json_array_get_item(current, path->components[i].value.index);
        }
    }

    return (mcp_json_t*)current;
}

/**
 * @brief Substitute environment variables in a string
 *
 * Replaces ${ENV_VAR} with the value of the environment variable ENV_VAR.
 *
 * @param str String to substitute
 * @return char* New string with substitutions, or NULL on failure
 */
static char* substitute_env_vars(const char* str) {
    if (!str) return NULL;

    // Check if there are any environment variables to substitute
    if (!strchr(str, '$')) {
        return mcp_strdup(str);
    }

    // First pass: calculate the size of the result string
    size_t result_size = 0;
    const char* p = str;
    while (*p) {
        if (*p == '$' && *(p + 1) == '{') {
            // Found an environment variable
            const char* var_start = p + 2;
            const char* var_end = strchr(var_start, '}');
            if (!var_end) {
                // Malformed environment variable, treat as literal
                result_size++;
                p++;
                continue;
            }

            // Extract variable name
            size_t var_name_len = var_end - var_start;
            char* var_name = (char*)malloc(var_name_len + 1);
            if (!var_name) {
                mcp_log_error("Failed to allocate memory for environment variable name");
                return NULL;
            }
            memcpy(var_name, var_start, var_name_len);
            var_name[var_name_len] = '\0';

            // Get environment variable value
            const char* var_value = getenv(var_name);
            free(var_name);

            // Add the length of the value (or 0 if not found)
            result_size += var_value ? strlen(var_value) : 0;

            // Move past the closing brace
            p = var_end + 1;
        } else {
            // Regular character
            result_size++;
            p++;
        }
    }

    // Allocate memory for the result string
    char* result = (char*)malloc(result_size + 1);
    if (!result) {
        mcp_log_error("Failed to allocate memory for substituted string");
        return NULL;
    }

    // Second pass: perform the substitution
    char* r = result;
    p = str;
    while (*p) {
        if (*p == '$' && *(p + 1) == '{') {
            // Found an environment variable
            const char* var_start = p + 2;
            const char* var_end = strchr(var_start, '}');
            if (!var_end) {
                // Malformed environment variable, treat as literal
                *r++ = *p++;
                continue;
            }

            // Extract variable name
            size_t var_name_len = var_end - var_start;
            char* var_name = (char*)malloc(var_name_len + 1);
            if (!var_name) {
                mcp_log_error("Failed to allocate memory for environment variable name");
                free(result);
                return NULL;
            }
            memcpy(var_name, var_start, var_name_len);
            var_name[var_name_len] = '\0';

            // Get environment variable value
            const char* var_value = getenv(var_name);
            free(var_name);

            // Copy the value (if found)
            if (var_value) {
                size_t var_value_len = strlen(var_value);
                memcpy(r, var_value, var_value_len);
                r += var_value_len;
            }

            // Move past the closing brace
            p = var_end + 1;
        } else {
            // Regular character
            *r++ = *p++;
        }
    }

    // Null-terminate the result string
    *r = '\0';

    return result;
}

/**
 * @brief Default configuration schema
 */
static const char* default_config_schema =
    "{"
    "    \"$schema\": \"http://json-schema.org/draft-07/schema#\","
    "    \"type\": \"object\","
    "    \"properties\": {"
    "        \"clientConfig\": {"
    "            \"type\": \"object\","
    "            \"properties\": {"
    "                \"clientName\": { \"type\": \"string\" },"
    "                \"clientVersion\": { \"type\": \"string\" },"
    "                \"useServerManager\": { \"type\": \"boolean\" },"
    "                \"requestTimeoutMs\": { \"type\": \"number\", \"minimum\": 0 }"
    "            }"
    "        },"
    "        \"mcpServers\": {"
    "            \"type\": \"object\","
    "            \"additionalProperties\": {"
    "                \"type\": \"object\","
    "                \"oneOf\": ["
    "                    {"
    "                        \"required\": [\"url\"],"
    "                        \"properties\": {"
    "                            \"url\": { \"type\": \"string\", \"format\": \"uri\" },"
    "                            \"apiKey\": { \"type\": \"string\" }"
    "                        }"
    "                    },"
    "                    {"
    "                        \"required\": [\"command\"],"
    "                        \"properties\": {"
    "                            \"command\": { \"type\": \"string\" },"
    "                            \"args\": {"
    "                                \"type\": \"array\","
    "                                \"items\": { \"type\": \"string\" }"
    "                            },"
    "                            \"env\": {"
    "                                \"type\": \"object\","
    "                                \"additionalProperties\": { \"type\": \"string\" }"
    "                            }"
    "                        }"
    "                    }"
    "                ]"
    "            }"
    "        },"
    "        \"toolAccessControl\": {"
    "            \"type\": \"object\","
    "            \"properties\": {"
    "                \"defaultAllow\": { \"type\": \"boolean\" },"
    "                \"allowedTools\": {"
    "                    \"type\": \"array\","
    "                    \"items\": { \"type\": \"string\" }"
    "                },"
    "                \"disallowedTools\": {"
    "                    \"type\": \"array\","
    "                    \"items\": { \"type\": \"string\" }"
    "                }"
    "            }"
    "        },"
    "        \"profiles\": {"
    "            \"type\": \"object\","
    "            \"additionalProperties\": {"
    "                \"type\": \"object\","
    "                \"properties\": {"
    "                    \"servers\": {"
    "                        \"type\": \"array\","
    "                        \"items\": { \"type\": \"string\" }"
    "                    },"
    "                    \"active\": { \"type\": \"boolean\" },"
    "                    \"description\": { \"type\": \"string\" }"
    "                }"
    "            }"
    "        },"
    "        \"include\": {"
    "            \"type\": \"array\","
    "            \"items\": { \"type\": \"string\" }"
    "        }"
    "    }"
    "}";


/**
 * @brief Create a configuration parser with default options
 */
kmcp_config_parser_t* kmcp_config_parser_create(const char* file_path) {
    // Create default options
    kmcp_config_parser_options_t options;
    memset(&options, 0, sizeof(options));
    options.enable_env_vars = false;
    options.enable_includes = false;
    options.validation = KMCP_CONFIG_VALIDATION_BASIC;
    options.default_profile = NULL;
    options.config_dir = NULL;

    // Create parser with options
    return kmcp_config_parser_create_with_options(file_path, &options);
}

/**
 * @brief Create a configuration parser with options
 */
kmcp_config_parser_t* kmcp_config_parser_create_with_options(const char* file_path, const kmcp_config_parser_options_t* options) {
    if (!file_path) {
        mcp_log_error("Invalid parameter: file_path is NULL");
        return NULL;
    }

    if (!options) {
        mcp_log_error("Invalid parameter: options is NULL");
        return NULL;
    }

    // Allocate memory for the parser
    kmcp_config_parser_t* parser = (kmcp_config_parser_t*)malloc(sizeof(kmcp_config_parser_t));
    if (!parser) {
        mcp_log_error("Failed to allocate memory for configuration parser");
        return NULL;
    }

    // Initialize parser
    memset(parser, 0, sizeof(kmcp_config_parser_t));

    // Copy file path
    parser->file_path = mcp_strdup(file_path);
    if (!parser->file_path) {
        mcp_log_error("Failed to duplicate file path");
        free(parser);
        return NULL;
    }

    // Copy options
    memcpy(&parser->options, options, sizeof(kmcp_config_parser_options_t));

    // Duplicate strings in options
    if (options->default_profile) {
        parser->options.default_profile = mcp_strdup(options->default_profile);
        if (!parser->options.default_profile) {
            mcp_log_error("Failed to duplicate default profile");
            free(parser->file_path);
            free(parser);
            return NULL;
        }
    }

    if (options->config_dir) {
        parser->options.config_dir = mcp_strdup(options->config_dir);
        if (!parser->options.config_dir) {
            mcp_log_error("Failed to duplicate config directory");
            free((void*)parser->options.default_profile);
            free(parser->file_path);
            free(parser);
            return NULL;
        }
    }

    // Read and parse the configuration file, CAN NOT use "rb"
    FILE* file = fopen(file_path, "r");
    if (!file) {
        mcp_log_error("Failed to open configuration file: %s", file_path);
        free((void*)parser->options.config_dir);
        free((void*)parser->options.default_profile);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory for file content
    char* file_content = (char*)malloc(file_size + 1);
    if (!file_content) {
        mcp_log_error("Failed to allocate memory for file content");
        fclose(file);
        free((void*)parser->options.config_dir);
        free((void*)parser->options.default_profile);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    // Read file content
    size_t read_size = fread(file_content, 1, file_size, file);
    fclose(file);

    // Null-terminate the file content
    file_content[read_size] = '\0';

    // Substitute environment variables if enabled
    char* processed_content = file_content;
    if (parser->options.enable_env_vars) {
        processed_content = substitute_env_vars(file_content);
        if (!processed_content) {
            mcp_log_error("Failed to substitute environment variables");
            free(file_content);
            free((void*)parser->options.config_dir);
            free((void*)parser->options.default_profile);
            free(parser->file_path);
            free(parser);
            return NULL;
        }

        // Free original content if we created a new string
        if (processed_content != file_content) {
            free(file_content);
        }
    }

    // Parse JSON
    parser->json = mcp_json_parse(processed_content);
    free(processed_content);

    if (!parser->json) {
        mcp_log_error("Failed to parse configuration file: %s", file_path);
        free((void*)parser->options.config_dir);
        free((void*)parser->options.default_profile);
        free(parser->file_path);
        free(parser);
        return NULL;
    }

    // Process includes if enabled
    if (parser->options.enable_includes) {
        mcp_json_t* includes = mcp_json_object_get_property(parser->json, "include");
        if (includes && mcp_json_is_array(includes)) {
            size_t include_count = mcp_json_array_get_size(includes);
            if (include_count > 0) {
                // Collect include file paths
                const char** include_paths = (const char**)malloc(include_count * sizeof(char*));
                if (!include_paths) {
                    mcp_log_error("Failed to allocate memory for include paths");
                    mcp_json_destroy(parser->json);
                    free((void*)parser->options.config_dir);
                    free((void*)parser->options.default_profile);
                    free(parser->file_path);
                    free(parser);
                    return NULL;
                }

                // Get include file paths
                size_t valid_include_count = 0;
                for (size_t i = 0; i < include_count; i++) {
                    mcp_json_t* include = mcp_json_array_get_item(includes, i < INT_MAX ? (int)i : INT_MAX);
                    if (include && mcp_json_is_string(include)) {
                        const char* include_path;
                        if (mcp_json_get_string(include, &include_path) == 0 && include_path) {
                            include_paths[valid_include_count++] = include_path;
                        }
                    }
                }

                // Merge include files
                if (valid_include_count > 0) {
                    kmcp_error_t result = kmcp_config_parser_merge(parser, include_paths, valid_include_count);
                    if (result != KMCP_SUCCESS) {
                        mcp_log_error("Failed to merge include files: %s", kmcp_error_message(result));
                        free(include_paths);
                        mcp_json_destroy(parser->json);
                        free((void*)parser->options.config_dir);
                        free((void*)parser->options.default_profile);
                        free(parser->file_path);
                        free(parser);
                        return NULL;
                    }
                }

                free(include_paths);
            }
        }
    }

    // Validate configuration if requested
    if (parser->options.validation != KMCP_CONFIG_VALIDATION_NONE) {
        kmcp_error_t result = kmcp_config_parser_validate(parser);
        if (result != KMCP_SUCCESS) {
            mcp_log_error("Configuration validation failed: %s", kmcp_error_message(result));
            if (parser->options.validation == KMCP_CONFIG_VALIDATION_STRICT) {
                mcp_json_destroy(parser->json);
                free((void*)parser->options.config_dir);
                free((void*)parser->options.default_profile);
                free(parser->file_path);
                free(parser);
                return NULL;
            }
        }
    }

    return parser;
}

/**
 * @brief Validate configuration file
 *
 * Validates the configuration file against a schema.
 * The validation level is determined by the parser options.
 *
 * @param parser Configuration parser (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_config_parser_validate(kmcp_config_parser_t* parser) {
    if (!parser) {
        mcp_log_error("Invalid parameter: parser is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // TODO: Implement schema validation
    // For now, just do basic validation

    // Check if the root is an object
    if (!mcp_json_is_object(parser->json)) {
        mcp_log_error("Configuration file is not a valid JSON object");
        return KMCP_ERROR_CONFIG_INVALID;
    }

    // Check if required sections exist based on validation level
    if (parser->options.validation >= KMCP_CONFIG_VALIDATION_BASIC) {
        // Check for mcpServers section
        mcp_json_t* mcp_servers = mcp_json_object_get_property(parser->json, "mcpServers");
        if (!mcp_servers) {
            mcp_log_warn("Configuration file does not contain 'mcpServers' section");
            if (parser->options.validation == KMCP_CONFIG_VALIDATION_STRICT) {
                return KMCP_ERROR_CONFIG_INVALID;
            }
        } else if (!mcp_json_is_object(mcp_servers)) {
            mcp_log_error("'mcpServers' section is not a valid JSON object");
            return KMCP_ERROR_CONFIG_INVALID;
        }
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Merge configuration files
 *
 * Merges multiple configuration files into a single configuration.
 * Later files override settings from earlier files.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param file_paths Array of configuration file paths (must not be NULL)
 * @param file_count Number of configuration files
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_config_parser_merge(
    kmcp_config_parser_t* parser,
    const char** file_paths,
    size_t file_count
) {
    if (!parser || !file_paths || file_count == 0) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Process each file
    for (size_t i = 0; i < file_count; i++) {
        const char* file_path = file_paths[i];
        if (!file_path) {
            mcp_log_warn("Skipping NULL file path at index %zu", i);
            continue;
        }

        // Read and parse the file
        FILE* file = fopen(file_path, "r");
        if (!file) {
            mcp_log_warn("Failed to open include file: %s", file_path);
            continue;
        }

        // Get file size
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (file_size <= 0) {
            mcp_log_warn("Include file is empty: %s", file_path);
            fclose(file);
            continue;
        }

        // Allocate memory for file content
        char* file_content = (char*)malloc(file_size + 1);
        if (!file_content) {
            mcp_log_error("Failed to allocate memory for include file content");
            fclose(file);
            continue;
        }

        // Read file content
        size_t read_size = fread(file_content, 1, file_size, file);
        fclose(file);

        // Null-terminate the file content
        file_content[read_size] = '\0';

        // Substitute environment variables if enabled
        char* processed_content = file_content;
        if (parser->options.enable_env_vars) {
            processed_content = substitute_env_vars(file_content);
            if (!processed_content) {
                mcp_log_error("Failed to substitute environment variables in include file");
                free(file_content);
                continue;
            }

            // Free original content if we created a new string
            if (processed_content != file_content) {
                free(file_content);
            }
        }

        // Parse JSON
        mcp_json_t* include_json = mcp_json_parse(processed_content);
        free(processed_content);

        if (!include_json) {
            mcp_log_warn("Failed to parse include file: %s", file_path);
            continue;
        }

        // Merge with existing configuration
        // TODO: Implement proper JSON merging
        // For now, just merge top-level properties
        if (mcp_json_is_object(include_json)) {
            char** keys = NULL;
            size_t key_count = 0;
            if (mcp_json_object_get_property_names(include_json, &keys, &key_count) == 0) {
                for (size_t j = 0; j < key_count; j++) {
                    const char* key = keys[j];
                    mcp_json_t* value = mcp_json_object_get_property(include_json, key);
                    if (value) {
                        // Create a deep copy of the value
                        mcp_json_t* value_copy = mcp_json_deep_copy(value);
                        if (value_copy) {
                            // Replace existing property or add new one
                            mcp_json_object_set_property(parser->json, key, value_copy);
                        }
                    }
                }

                // Free keys
                for (size_t j = 0; j < key_count; j++) {
                    free(keys[j]);
                }
                free(keys);
            }
        }

        // Free include JSON
        mcp_json_destroy(include_json);
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Get a string value from the configuration
 *
 * Retrieves a string value from the configuration file.
 * Environment variables are substituted if enabled in the parser options.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return const char* Returns the string value, or default_value if not found
 */
const char* kmcp_config_parser_get_string(
    kmcp_config_parser_t* parser,
    const char* path,
    const char* default_value
) {
    if (!parser || !path) {
        return default_value;
    }

    // Parse the path
    json_path_t* json_path = parse_json_path(path);
    if (!json_path) {
        return default_value;
    }

    // Get the value
    mcp_json_t* value = get_json_by_path(parser->json, json_path);
    free_json_path(json_path);

    if (!value || !mcp_json_is_string(value)) {
        return default_value;
    }

    // Get the string value
    const char* str_value = NULL;
    if (mcp_json_get_string(value, &str_value) != 0 || !str_value) {
        return default_value;
    }

    // NOTE: Environment variable substitution is NOT done here to avoid memory leaks.
    // Use kmcp_config_parser_get_string_allocated for substitution.
    return str_value;
}


/**
 * @brief Get a string value from the configuration, allocating memory for substitution.
 *
 * Retrieves a string value from the configuration file.
 * Environment variables are substituted if enabled in the parser options.
 * The caller is responsible for freeing the returned string using free().
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found (will be duplicated if returned)
 * @return char* Returns the allocated string value, or an allocated copy of default_value if not found, or NULL on error.
 */
char* kmcp_config_parser_get_string_allocated(
    kmcp_config_parser_t* parser,
    const char* path,
    const char* default_value
) {
    if (!parser || !path) {
        return default_value ? mcp_strdup(default_value) : NULL;
    }

    // Parse the path
    json_path_t* json_path = parse_json_path(path);
    if (!json_path) {
        return default_value ? mcp_strdup(default_value) : NULL;
    }

    // Get the value
    mcp_json_t* value = get_json_by_path(parser->json, json_path);
    free_json_path(json_path);

    if (!value || !mcp_json_is_string(value)) {
        return default_value ? mcp_strdup(default_value) : NULL;
    }

    // Get the string value
    const char* str_value = NULL;
    if (mcp_json_get_string(value, &str_value) != 0 || !str_value) {
        return default_value ? mcp_strdup(default_value) : NULL;
    }

    // Substitute environment variables if enabled
    if (parser->options.enable_env_vars && strchr(str_value, '$')) {
        char* substituted = substitute_env_vars(str_value);
        // If substitution fails, duplicate the original string
        return substituted ? substituted : mcp_strdup(str_value);
    } else {
        // No substitution needed, just duplicate the original string
        return mcp_strdup(str_value);
    }
}

/**
 * @brief Get a boolean value from the configuration
 *
 * Retrieves a boolean value from the configuration file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return bool Returns the boolean value, or default_value if not found
 */
bool kmcp_config_parser_get_boolean(
    kmcp_config_parser_t* parser,
    const char* path,
    bool default_value
) {
    if (!parser || !path) {
        return default_value;
    }

    // Parse the path
    json_path_t* json_path = parse_json_path(path);
    if (!json_path) {
        return default_value;
    }

    // Get the value
    mcp_json_t* value = get_json_by_path(parser->json, json_path);
    free_json_path(json_path);

    if (!value || !mcp_json_is_boolean(value)) {
        return default_value;
    }

    // Get the boolean value
    bool bool_value = default_value;
    if (mcp_json_get_boolean(value, &bool_value) != 0) {
        return default_value;
    }

    return bool_value;
}

/**
 * @brief Get a number value from the configuration
 *
 * Retrieves a number value from the configuration file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return double Returns the number value, or default_value if not found
 */
double kmcp_config_parser_get_number(
    kmcp_config_parser_t* parser,
    const char* path,
    double default_value
) {
    if (!parser || !path) {
        return default_value;
    }

    // Parse the path
    json_path_t* json_path = parse_json_path(path);
    if (!json_path) {
        return default_value;
    }

    // Get the value
    mcp_json_t* value = get_json_by_path(parser->json, json_path);
    free_json_path(json_path);

    if (!value || !mcp_json_is_number(value)) {
        return default_value;
    }

    // Get the number value
    double num_value = default_value;
    if (mcp_json_get_number(value, &num_value) != 0) {
        return default_value;
    }

    return num_value;
}

/**
 * @brief Get an integer value from the configuration
 *
 * Retrieves an integer value from the configuration file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param path JSON path to the value (must not be NULL)
 * @param default_value Default value to return if the path is not found
 * @return int Returns the integer value, or default_value if not found
 */
int kmcp_config_parser_get_int(
    kmcp_config_parser_t* parser,
    const char* path,
    int default_value
) {
    double num_value = kmcp_config_parser_get_number(parser, path, (double)default_value);
    return (int)num_value;
}

/**
 * @brief Save configuration to a file
 *
 * Saves the current configuration to a file.
 *
 * @param parser Configuration parser (must not be NULL)
 * @param file_path File path to save to (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_config_parser_save(
    kmcp_config_parser_t* parser,
    const char* file_path
) {
    if (!parser || !file_path) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Stringify JSON
    char* json_str = mcp_json_stringify_pretty(parser->json);
    if (!json_str) {
        mcp_log_error("Failed to stringify JSON");
        return KMCP_ERROR_INTERNAL;
    }

    // Open file for writing
    FILE* file = fopen(file_path, "w");
    if (!file) {
        mcp_log_error("Failed to open file for writing: %s", file_path);
        free(json_str);
        return KMCP_ERROR_IO;
    }

    // Write JSON to file
    size_t json_len = strlen(json_str);
    fwrite(json_str, 1, json_len, file);
    fclose(file);
    free(json_str);

    return KMCP_SUCCESS;
}



/**
 * @brief Parse server configurations
 */
kmcp_error_t kmcp_config_parser_get_servers(
    kmcp_config_parser_t* parser,
    kmcp_server_config_t*** servers,
    size_t* server_count
) {
    if (!parser || !servers || !server_count) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *servers = NULL;
    *server_count = 0;

    // Get mcpServers object
    mcp_json_t* mcp_servers = mcp_json_object_get_property(parser->json, "mcpServers");
    if (!mcp_servers) {
        mcp_log_error("mcpServers not found in config file");
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get all keys of mcpServers object
    char** keys = NULL;
    size_t key_count = 0;
    if (mcp_json_object_get_property_names(mcp_servers, &keys, &key_count) != 0 || key_count == 0) {
        mcp_log_error("Failed to get server keys or no servers defined");
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Allocate server configuration array
    *servers = (kmcp_server_config_t**)malloc(key_count * sizeof(kmcp_server_config_t*));
    if (!*servers) {
        mcp_log_error("Failed to allocate memory for server configs");
        // Free keys array
        for (size_t i = 0; i < key_count; i++) {
            free(keys[i]);
        }
        free(keys);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Parse each server configuration
    size_t valid_count = 0;
    for (size_t i = 0; i < key_count; i++) {
        const char* server_name = keys[i];
        mcp_json_t* server_json = mcp_json_object_get_property(mcp_servers, server_name);
        if (!server_json) {
            mcp_log_warn("Server %s is not an object, skipping", server_name);
            continue;
        }

        // Allocate server configuration
        kmcp_server_config_t* config = (kmcp_server_config_t*)malloc(sizeof(kmcp_server_config_t));
        if (!config) {
            mcp_log_error("Failed to allocate memory for server config");
            continue;
        }

        // Initialize configuration
        memset(config, 0, sizeof(kmcp_server_config_t));
        config->name = mcp_strdup(server_name);

        // Check if it's an HTTP server
        const char* url = NULL;
        if (get_string_from_property(server_json, "url", &url, false) == 0 && url) {
            config->url = mcp_strdup(url);
            config->is_http = true;
            mcp_log_info("Server '%s' configured as HTTP server with URL: %s", server_name, url);
        } else {
            // Local process server
            const char* command = NULL;
            if (get_string_from_property(server_json, "command", &command, true) == 0 && command) {
                config->command = mcp_strdup(command);
                mcp_log_info("Server '%s' configured as local process with command: %s", server_name, command);

                // Parse arguments array
                mcp_json_t* args_json = mcp_json_object_get_property(server_json, "args");
                if (args_json && mcp_json_is_array(args_json)) {
                    size_t args_count = mcp_json_array_get_size(args_json);
                    if (args_count > 0) {
                        config->args = (char**)malloc(args_count * sizeof(char*));
                        if (config->args) {
                            config->args_count = 0;
                            for (size_t j = 0; j < args_count; j++) {
                                const char* arg = NULL;
                                if (get_string_from_array_item(args_json, j < INT_MAX ? (int)j : INT_MAX, &arg) == 0 && arg) {
                                    mcp_log_debug("Server '%s' arg[%zu]: %s", server_name, j, arg);
                                    config->args[config->args_count] = mcp_strdup(arg);
                                    config->args_count++;
                                }
                            }
                        }
                    }
                }

                // Parse environment variables object
                mcp_json_t* env_json = mcp_json_object_get_property(server_json, "env");
                if (env_json && mcp_json_is_object(env_json)) {
                    char** env_keys = NULL;
                    size_t env_key_count = 0;
                    if (mcp_json_object_get_property_names(env_json, &env_keys, &env_key_count) == 0 && env_key_count > 0) {
                        config->env = (char**)malloc(env_key_count * sizeof(char*));
                        if (config->env) {
                            config->env_count = 0;
                            for (size_t j = 0; j < env_key_count; j++) {
                                const char* key = env_keys[j];
                                const char* value = NULL;
                                if (key && get_string_from_property(env_json, key, &value, false) == 0 && value) {
                                    // Format: KEY=VALUE
                                    size_t env_len = strlen(key) + strlen(value) + 2; // +2 for '=' and '\0'
                                    char* env_var = (char*)malloc(env_len);
                                    if (env_var) {
                                        snprintf(env_var, env_len, "%s=%s", key, value);
                                        config->env[config->env_count] = env_var;
                                        config->env_count++;
                                    }
                                }
                            }
                        }

                        // Free environment variable keys array
                        for (size_t j = 0; j < env_key_count; j++) {
                            free(env_keys[j]);
                        }
                        free(env_keys);
                    }
                }
            }
        }

        // Add to server configuration array
        (*servers)[valid_count] = config;
        valid_count++;
    }

    // Free keys array
    for (size_t i = 0; i < key_count; i++) {
        free(keys[i]);
    }
    free(keys);

    // Update server count
    *server_count = valid_count;

    return KMCP_SUCCESS;
}

/**
 * @brief Parse client configuration
 */
kmcp_error_t kmcp_config_parser_get_client(
    kmcp_config_parser_t* parser,
    kmcp_client_config_t* config
) {
    if (!parser || !config) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize configuration
    memset(config, 0, sizeof(kmcp_client_config_t));

    // Set default values
    config->timeout_ms = 30000; // Default 30 seconds
    config->use_manager = true;

    // Get clientConfig object
    mcp_json_t* client_config = mcp_json_object_get_property(parser->json, "clientConfig");
    if (!client_config) {
        mcp_log_warn("clientConfig not found in config file, using defaults");
        return 0;
    }

    // Parse client name
    const char* name = NULL;
    if (get_string_from_property(client_config, "clientName", &name, false) == 0 && name) {
        config->name = mcp_strdup(name);
        mcp_log_info("Client name: %s", name);
    } else {
        config->name = mcp_strdup("kmcp-client");
        mcp_log_info("Using default client name: %s", config->name);
    }

    // Parse client version
    const char* version = NULL;
    if (get_string_from_property(client_config, "clientVersion", &version, false) == 0 && version) {
        config->version = mcp_strdup(version);
        mcp_log_info("Client version: %s", version);
    } else {
        config->version = mcp_strdup("1.0.0");
        mcp_log_info("Using default client version: %s", config->version);
    }

    // Parse whether to use server manager
    bool use_manager = config->use_manager;
    if (get_boolean_from_property(client_config, "useServerManager", &use_manager, true) == 0) {
        config->use_manager = use_manager;
        mcp_log_info("Server manager %s", use_manager ? "enabled" : "disabled");
    }

    // Parse request timeout
    double timeout_ms = (double)config->timeout_ms;
    if (get_number_from_property(client_config, "requestTimeoutMs", &timeout_ms, (double)config->timeout_ms) == 0) {
        // Validate timeout value (must be positive)
        if (timeout_ms <= 0) {
            mcp_log_warn("Invalid request timeout value: %.0f ms, using default: %u ms", timeout_ms, config->timeout_ms);
        } else {
            config->timeout_ms = (uint32_t)timeout_ms;
            mcp_log_info("Request timeout: %u ms", config->timeout_ms);
        }
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Parse tool access control configuration
 */
kmcp_error_t kmcp_config_parser_get_access(
    kmcp_config_parser_t* parser,
    kmcp_tool_access_t* access
) {
    if (!parser || !access) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Get toolAccessControl object
    mcp_json_t* access_config = mcp_json_object_get_property(parser->json, "toolAccessControl");
    if (!access_config) {
        mcp_log_warn("toolAccessControl not found in config file");
        return 0;
    }

    // Parse default allow policy
    bool default_allow = true; // Default is true
    if (get_boolean_from_property(access_config, "defaultAllow", &default_allow, true) == 0) {
        mcp_log_info("Tool access control default policy: %s", default_allow ? "allow" : "deny");

        // Update the default allow policy in the access object
        kmcp_tool_access_set_default_policy(access, default_allow);
    }

    // Create tool access control
    // Note: Here we assume access is a pointer to an already created kmcp_tool_access_t object
    // If this is not the case, this code needs to be modified

    // Parse allowed tools list
    mcp_json_t* allowed_tools = mcp_json_object_get_property(access_config, "allowedTools");
    if (allowed_tools && mcp_json_is_array(allowed_tools)) {
        size_t count = mcp_json_array_get_size(allowed_tools);
        for (size_t i = 0; i < count; i++) {
            const char* tool = NULL;
            if (get_string_from_array_item(allowed_tools, (int)i, &tool) == 0 && tool) {
                kmcp_tool_access_add(access, tool, true);
            }
        }
    }

    // Parse disallowed tools list
    mcp_json_t* disallowed_tools = mcp_json_object_get_property(access_config, "disallowedTools");
    if (disallowed_tools && mcp_json_is_array(disallowed_tools)) {
        size_t count = mcp_json_array_get_size(disallowed_tools);
        mcp_log_info("Found %zu disallowed tools", count);
        for (size_t i = 0; i < count; i++) {
            const char* tool = NULL;
            if (get_string_from_array_item(disallowed_tools, (int)i, &tool) == 0 && tool) {
                mcp_log_info("Adding disallowed tool: %s", tool);
                kmcp_error_t result = kmcp_tool_access_add(access, tool, false);
                if (result != KMCP_SUCCESS) {
                    mcp_log_error("Failed to add disallowed tool: %s, error: %d", tool, result);
                }
            } else {
                mcp_log_error("Failed to get disallowed tool at index %zu", i);
            }
        }
    } else {
        mcp_log_warn("disallowedTools not found or not an array");
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Close the configuration parser
 */
void kmcp_config_parser_close(kmcp_config_parser_t* parser) {
    if (!parser) {
        return;
    }

    // Free file path
    if (parser->file_path) {
        free(parser->file_path);
        parser->file_path = NULL;
    }

    // Free JSON
    if (parser->json) {
        mcp_json_destroy(parser->json);
        parser->json = NULL;
    }

    // Free options strings
    if (parser->options.default_profile) {
        free((void*)parser->options.default_profile);
        parser->options.default_profile = NULL;
    }

    if (parser->options.config_dir) {
        free((void*)parser->options.config_dir);
        parser->options.config_dir = NULL;
    }

    // Free parser
    free(parser);
}
