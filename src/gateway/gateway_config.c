#include "gateway.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <regex.h>
#endif

// Helper function to free string arrays
static void free_string_array(char** arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

#ifndef _WIN32
// Helper function to free compiled regex objects (POSIX only)
static void free_compiled_regexes(regex_t* regex_arr, size_t count) {
    if (!regex_arr) return;
    for (size_t i = 0; i < count; i++) {
        regfree(&regex_arr[i]); // Free each compiled regex
    }
    free(regex_arr);
}
#endif

// Implementation of mcp_backend_info_free
void mcp_backend_info_free(mcp_backend_info_t* backend_info) {
    if (!backend_info) return;

    free(backend_info->name);
    free(backend_info->address);

    // Free routing arrays
    free_string_array(backend_info->routing.resource_prefixes, backend_info->routing.resource_prefix_count);
    free_string_array(backend_info->routing.tool_names, backend_info->routing.tool_name_count);

#ifndef _WIN32
    // Free regex related arrays (POSIX only)
    free_string_array(backend_info->routing.resource_regex_patterns, backend_info->routing.resource_regex_count);
    free_compiled_regexes(backend_info->routing.compiled_resource_regexes, backend_info->routing.resource_regex_count);
#endif

    // Note: Does not free the struct itself if part of an array
    // Reset memory to avoid dangling pointers if misused
    memset(backend_info, 0, sizeof(mcp_backend_info_t));
}

// Implementation of mcp_free_backend_list
void mcp_free_backend_list(mcp_backend_info_t* backend_list, size_t backend_count) {
    if (!backend_list) return;
    for (size_t i = 0; i < backend_count; i++) {
        mcp_backend_info_free(&backend_list[i]);
    }
    free(backend_list);
}

// Helper to parse a JSON string array into a char**
static mcp_error_code_t parse_string_array(mcp_json_t* json_array, char*** out_array, size_t* out_count) {
    if (!json_array || mcp_json_get_type(json_array) != MCP_JSON_ARRAY) {
        *out_array = NULL;
        *out_count = 0;
        return MCP_ERROR_NONE; // Not an error if the array is missing/null
    }

    size_t count = mcp_json_array_get_size(json_array);
    if (count == 0) {
        *out_array = NULL;
        *out_count = 0;
        return MCP_ERROR_NONE;
    }

    char** result_array = (char**)malloc(count * sizeof(char*));
    if (!result_array) {
        return MCP_ERROR_INTERNAL_ERROR; // Allocation failure
    }
    memset(result_array, 0, count * sizeof(char*)); // Initialize pointers to NULL

    for (size_t i = 0; i < count; i++) {
        mcp_json_t* item = mcp_json_array_get_item(json_array, (int)i);
        const char* str_val = NULL;
        if (!item || mcp_json_get_type(item) != MCP_JSON_STRING || mcp_json_get_string(item, &str_val) != 0 || !str_val) {
            mcp_log_error("Gateway config: Expected string in array at index %zu", i);
            free_string_array(result_array, i); // Free partially allocated array
            return MCP_ERROR_PARSE_ERROR;
        }
        result_array[i] = mcp_strdup(str_val);
        if (!result_array[i]) {
            mcp_log_error("Gateway config: Failed to duplicate string '%s'", str_val);
            free_string_array(result_array, i); // Free partially allocated array
            return MCP_ERROR_INTERNAL_ERROR;
        }
    }

    *out_array = result_array;
    *out_count = count;
    return MCP_ERROR_NONE;
}

#ifndef _WIN32
// Helper to parse and compile regex patterns (POSIX only)
static mcp_error_code_t parse_and_compile_regex_array(
    mcp_json_t* json_array,
    char*** out_patterns,
    regex_t** out_compiled_regexes,
    size_t* out_count)
{
    *out_patterns = NULL;
    *out_compiled_regexes = NULL;
    *out_count = 0;

    if (!json_array || mcp_json_get_type(json_array) != MCP_JSON_ARRAY) {
        return MCP_ERROR_NONE; // Missing array is not an error
    }

    size_t count = mcp_json_array_get_size(json_array);
    if (count == 0) {
        return MCP_ERROR_NONE;
    }

    char** patterns = (char**)malloc(count * sizeof(char*));
    regex_t* compiled = (regex_t*)malloc(count * sizeof(regex_t));
    if (!patterns || !compiled) {
        free(patterns);
        free(compiled);
        return MCP_ERROR_INTERNAL_ERROR;
    }
    memset(patterns, 0, count * sizeof(char*));
    // No need to memset compiled regex_t array

    for (size_t i = 0; i < count; i++) {
        mcp_json_t* item = mcp_json_array_get_item(json_array, (int)i);
        const char* pattern_str = NULL;
        if (!item || mcp_json_get_type(item) != MCP_JSON_STRING || mcp_json_get_string(item, &pattern_str) != 0 || !pattern_str) {
            mcp_log_error("Gateway config: Expected regex pattern string at index %zu", i);
            free_string_array(patterns, i);
            free_compiled_regexes(compiled, i); // Free already compiled ones
            return MCP_ERROR_PARSE_ERROR;
        }

        patterns[i] = mcp_strdup(pattern_str);
        if (!patterns[i]) {
            mcp_log_error("Gateway config: Failed to duplicate regex pattern '%s'", pattern_str);
            free_string_array(patterns, i);
            free_compiled_regexes(compiled, i);
            return MCP_ERROR_INTERNAL_ERROR;
        }

        // Compile the regex (using POSIX Extended Regex syntax)
        int reg_err = regcomp(&compiled[i], pattern_str, REG_EXTENDED | REG_NOSUB); // REG_NOSUB for efficiency if only matching
        if (reg_err != 0) {
            char err_buf[256];
            regerror(reg_err, &compiled[i], err_buf, sizeof(err_buf));
            mcp_log_error("Gateway config: Failed to compile regex '%s': %s", pattern_str, err_buf);
            free(patterns[i]); // Free the duplicated pattern string
            free_string_array(patterns, i); // Free previous patterns
            free_compiled_regexes(compiled, i); // Free previous compiled regexes
            return MCP_ERROR_PARSE_ERROR;
        }
    }

    *out_patterns = patterns;
    *out_compiled_regexes = compiled;
    *out_count = count;
    return MCP_ERROR_NONE;
}
#endif // !_WIN32


// Implementation of load_gateway_config
mcp_error_code_t load_gateway_config(
    const char* config_path,
    mcp_backend_info_t** out_backend_list,
    size_t* out_backend_count)
{
    if (!config_path || !out_backend_list || !out_backend_count) {
        return MCP_ERROR_INVALID_PARAMS;
    }

    *out_backend_list = NULL;
    *out_backend_count = 0;
    mcp_error_code_t err = MCP_ERROR_NONE;
    char* file_content = NULL;
    mcp_json_t* root_json = NULL;
    mcp_backend_info_t* temp_list = NULL; // Temporary list during parsing
    size_t backend_capacity = 0;
    size_t backend_count = 0;

    // 1. Read file content
    FILE* fp = fopen(config_path, "rb"); // Use "rb" for cross-platform consistency
    if (!fp) {
        mcp_log_error("Failed to open gateway config file: %s", config_path);
        return MCP_ERROR_INVALID_REQUEST; // Treat file not found/readable as invalid request context
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        mcp_log_warn("Gateway config file is empty or invalid size: %s", config_path);
        fclose(fp);
        return MCP_ERROR_NONE; // Empty config is not an error, just results in 0 backends
    }

    file_content = (char*)malloc(file_size + 1);
    if (!file_content) {
        mcp_log_error("Failed to allocate memory to read gateway config file: %s", config_path);
        fclose(fp);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    size_t bytes_read = fread(file_content, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        mcp_log_error("Failed to read entire gateway config file: %s", config_path);
        free(file_content);
        return MCP_ERROR_INTERNAL_ERROR;
    }
    file_content[file_size] = '\0'; // Null-terminate

    // 2. Parse JSON
    root_json = mcp_json_parse(file_content);
    free(file_content); // Free the raw file content buffer
    file_content = NULL;

    if (!root_json) {
        mcp_log_error("Failed to parse gateway config JSON: %s", config_path);
        return MCP_ERROR_PARSE_ERROR;
    }

    if (mcp_json_get_type(root_json) != MCP_JSON_ARRAY) {
        mcp_log_error("Gateway config: Root element must be an array: %s", config_path);
        // mcp_json_destroy(root_json); // Assuming arena cleanup handles this
        return MCP_ERROR_PARSE_ERROR;
    }

    // 3. Iterate and parse each backend object
    size_t num_backends_in_json = mcp_json_array_get_size(root_json);
    if (num_backends_in_json == 0) {
        mcp_log_info("Gateway config file contains an empty array. No backends loaded.");
        // mcp_json_destroy(root_json);
        return MCP_ERROR_NONE; // Valid empty config
    }

    // Allocate temporary list
    backend_capacity = num_backends_in_json;
    temp_list = (mcp_backend_info_t*)malloc(backend_capacity * sizeof(mcp_backend_info_t));
    if (!temp_list) {
        mcp_log_error("Gateway config: Failed to allocate temporary backend list.");
        // mcp_json_destroy(root_json);
        return MCP_ERROR_INTERNAL_ERROR;
    }
    memset(temp_list, 0, backend_capacity * sizeof(mcp_backend_info_t)); // Zero initialize

    for (size_t i = 0; i < num_backends_in_json; i++) {
        mcp_json_t* backend_obj = mcp_json_array_get_item(root_json, (int)i);
        if (!backend_obj || mcp_json_get_type(backend_obj) != MCP_JSON_OBJECT) {
            mcp_log_error("Gateway config: Array element at index %zu is not an object.", i);
            err = MCP_ERROR_PARSE_ERROR;
            goto cleanup_error;
        }

        mcp_backend_info_t* current_backend = &temp_list[backend_count]; // Point to next slot

        // --- Parse required fields ---
        mcp_json_t* name_node = mcp_json_object_get_property(backend_obj, "name");
        mcp_json_t* address_node = mcp_json_object_get_property(backend_obj, "address");
        mcp_json_t* routing_node = mcp_json_object_get_property(backend_obj, "routing");

        const char* name_str = NULL;
        const char* address_str = NULL;

        if (!name_node || mcp_json_get_type(name_node) != MCP_JSON_STRING || mcp_json_get_string(name_node, &name_str) != 0 || !name_str) {
            mcp_log_error("Gateway config: Backend at index %zu missing or invalid 'name' string.", i);
            err = MCP_ERROR_PARSE_ERROR; goto cleanup_error;
        }
        if (!address_node || mcp_json_get_type(address_node) != MCP_JSON_STRING || mcp_json_get_string(address_node, &address_str) != 0 || !address_str) {
            mcp_log_error("Gateway config: Backend '%s' missing or invalid 'address' string.", name_str ? name_str : "?");
            err = MCP_ERROR_PARSE_ERROR; goto cleanup_error;
        }
        if (!routing_node || mcp_json_get_type(routing_node) != MCP_JSON_OBJECT) {
            mcp_log_error("Gateway config: Backend '%s' missing or invalid 'routing' object.", name_str);
            err = MCP_ERROR_PARSE_ERROR; goto cleanup_error;
        }

        // Duplicate strings
        current_backend->name = mcp_strdup(name_str);
        current_backend->address = mcp_strdup(address_str);
        if (!current_backend->name || !current_backend->address) {
            mcp_log_error("Gateway config: Failed to duplicate name/address for backend '%s'.", name_str);
            err = MCP_ERROR_INTERNAL_ERROR; goto cleanup_error;
        }

        // --- Parse routing object ---
        mcp_json_t* prefixes_node = mcp_json_object_get_property(routing_node, "resource_prefixes");
        mcp_json_t* tools_node = mcp_json_object_get_property(routing_node, "tool_names");
        mcp_json_t* regexes_node = mcp_json_object_get_property(routing_node, "resource_regexes"); // New field

        err = parse_string_array(prefixes_node, &current_backend->routing.resource_prefixes, &current_backend->routing.resource_prefix_count);
        if (err != MCP_ERROR_NONE) {
             mcp_log_error("Gateway config: Failed to parse 'resource_prefixes' for backend '%s'.", name_str);
             goto cleanup_error;
        }
        err = parse_string_array(tools_node, &current_backend->routing.tool_names, &current_backend->routing.tool_name_count);
        if (err != MCP_ERROR_NONE) {
             mcp_log_error("Gateway config: Failed to parse 'tool_names' for backend '%s'.", name_str);
             goto cleanup_error;
        }

#ifndef _WIN32
        // Parse and compile regexes (POSIX only)
        err = parse_and_compile_regex_array(regexes_node,
                                            &current_backend->routing.resource_regex_patterns,
                                            &current_backend->routing.compiled_resource_regexes,
                                            &current_backend->routing.resource_regex_count);
        if (err != MCP_ERROR_NONE) {
             mcp_log_error("Gateway config: Failed to parse or compile 'resource_regexes' for backend '%s'.", name_str);
             goto cleanup_error;
        }
#else
        // On Windows, ignore the regexes node but log if present
        if (regexes_node && mcp_json_get_type(regexes_node) == MCP_JSON_ARRAY) {
            mcp_log_warn("Gateway config: 'resource_regexes' found for backend '%s' but regex routing is not supported on Windows. Ignoring.", name_str);
        }
        // Explicitly initialize Windows dummy fields to NULL/0
        current_backend->routing.resource_regex_patterns_dummy = NULL;
        current_backend->routing.compiled_resource_regexes_dummy = NULL;
        current_backend->routing.resource_regex_count_dummy = 0;
#endif


        // --- Parse optional fields ---
        mcp_json_t* timeout_node = mcp_json_object_get_property(backend_obj, "timeout_ms");
        double timeout_val = 0;
        if (timeout_node && mcp_json_get_type(timeout_node) == MCP_JSON_NUMBER && mcp_json_get_number(timeout_node, &timeout_val) == 0) {
            current_backend->timeout_ms = (timeout_val > 0) ? (uint32_t)timeout_val : 0;
        } else {
            current_backend->timeout_ms = 0; // Default
        }

        // TODO: Parse credentials later

        backend_count++; // Successfully parsed one backend

        // Realloc not needed as we allocated exact size initially
    }

    // 4. Success - Assign results
    *out_backend_list = temp_list;
    *out_backend_count = backend_count;
    // mcp_json_destroy(root_json); // Assuming arena cleanup
    return MCP_ERROR_NONE;

cleanup_error:
    // Free partially populated temp list
    mcp_free_backend_list(temp_list, backend_count); // Frees structs up to backend_count
    // mcp_json_destroy(root_json); // Assuming arena cleanup
    return err;
}
