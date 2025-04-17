#include "kmcp_tool_access.h"
#include "kmcp_config_parser.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_hashtable.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Complete definition of tool access control structure
 */
struct kmcp_tool_access {
    mcp_hashtable_t* tool_map;  // Mapping from tool name to access permission (value 1 means allow, 0 means deny)
    bool default_allow;         // Default allow policy
};

/**
 * @brief Create tool access control
 */
kmcp_tool_access_t* kmcp_tool_access_create(bool default_allow) {
    kmcp_tool_access_t* access = (kmcp_tool_access_t*)malloc(sizeof(kmcp_tool_access_t));
    if (!access) {
        mcp_log_error("Failed to allocate memory for tool access control");
        return NULL;
    }

    // Initialize fields
    access->default_allow = default_allow;

    // Create hash table
    access->tool_map = mcp_hashtable_create(
        16,                             // initial_capacity
        0.75f,                          // load_factor_threshold
        mcp_hashtable_string_hash,      // hash_func
        mcp_hashtable_string_compare,   // key_compare
        mcp_hashtable_string_dup,       // key_dup
        mcp_hashtable_string_free,      // key_free
        free                            // value_free
    );
    if (!access->tool_map) {
        mcp_log_error("Failed to create tool map");
        free(access);
        return NULL;
    }

    return access;
}

/**
 * @brief Add a tool to the access control list
 */
int kmcp_tool_access_add(kmcp_tool_access_t* access, const char* tool_name, bool allow) {
    if (!access || !tool_name) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // Create value (1 means allow, 0 means deny)
    int* value = (int*)malloc(sizeof(int));
    if (!value) {
        mcp_log_error("Failed to allocate memory for tool access value");
        return -1;
    }

    *value = allow ? 1 : 0;

    // Add to hash table
    // Note: If the tool already exists, this will replace the existing value
    // The old value will be freed by mcp_hashtable_put
    if (mcp_hashtable_put(access->tool_map, tool_name, value) != 0) {
        mcp_log_error("Failed to add tool to access control: %s", tool_name);
        free(value);
        return -1;
    }

    return 0;
}

/**
 * @brief Check if a tool is allowed to access
 */
bool kmcp_tool_access_check(kmcp_tool_access_t* access, const char* tool_name) {
    if (!access || !tool_name) {
        // Default deny
        return false;
    }

    // Look up the tool
    void* value = NULL;
    if (mcp_hashtable_get(access->tool_map, tool_name, &value) && value) {
        // Found the tool, return its access permission
        return *((int*)value) == 1;
    }

    // Tool not found, return default policy
    return access->default_allow;
}

/**
 * @brief Load access control list from a configuration file
 */
int kmcp_tool_access_load(kmcp_tool_access_t* access, const char* config_file) {
    if (!access || !config_file) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    kmcp_config_parser_t* parser = kmcp_config_parser_create(config_file);
    if (!parser) {
        mcp_log_error("Failed to create config parser");
        return -1;
    }

    int result = kmcp_config_parser_get_access(parser, access);
    kmcp_config_parser_close(parser);

    return result;
}

/**
 * @brief Helper function to free hash table values
 */
static void free_hash_value(const void* key, void* value, void* user_data) {
    (void)key;       // Unused parameter
    (void)user_data; // Unused parameter
    if (value) {
        free(value);
    }
}

/**
 * @brief Destroy tool access control
 */
void kmcp_tool_access_destroy(kmcp_tool_access_t* access) {
    if (!access) {
        return;
    }

    // Free hash table
    if (access->tool_map) {
        // Free values in hash table (integer pointers)
        mcp_hashtable_foreach(access->tool_map, free_hash_value, NULL);
        mcp_hashtable_destroy(access->tool_map);
    }

    free(access);
}
