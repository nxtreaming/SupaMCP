#ifndef MCP_GATEWAY_H
#define MCP_GATEWAY_H

#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_connection_pool.h"
#include <stdint.h>
#include <stdbool.h>

#ifndef _WIN32
#include <regex.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Defines routing rules for a backend server.
 * @note Strings are owned by the parent mcp_backend_info_t.
 *       Compiled regex patterns (if used) are also owned.
 */
typedef struct {
    // Existing prefix/name matching
    char** resource_prefixes;       /**< Array of URI prefixes handled by this backend. */
    size_t resource_prefix_count;   /**< Number of elements in resource_prefixes. */
    char** tool_names;              /**< Array of tool names handled by this backend. */
    size_t tool_name_count;         /**< Number of elements in tool_names. */

    // New regex matching (POSIX only for now)
#ifndef _WIN32
    char** resource_regex_patterns; /**< Array of URI regex patterns handled by this backend. */
    regex_t* compiled_resource_regexes; /**< Array of compiled regex objects (owned by this struct). */
    size_t resource_regex_count;    /**< Number of elements in resource_regex_patterns/compiled_resource_regexes. */
#else
    // Provide dummy fields for Windows to maintain struct size/layout consistency if needed,
    // or just omit them if padding/alignment isn't critical across platforms.
    // Omitting for simplicity now. If issues arise, add dummy pointers/counts.
    void* resource_regex_patterns_dummy; // Avoid unused struct warnings if empty
    void* compiled_resource_regexes_dummy;
    size_t resource_regex_count_dummy;
#endif
    // TODO: Add regex for tool names if needed?
} mcp_backend_routing_t;

/**
 * @brief Holds configuration information for a single backend MCP server.
 * @note Strings and arrays within this struct are typically owned by the struct
 *       and should be freed by a dedicated function (e.g., mcp_backend_info_free).
 */
typedef struct {
    char* name;                     /**< Unique logical name for the backend (e.g., "weather_service"). */
    char* address;                  /**< Connection address (e.g., "tcp://host:port", "stdio:/path/to/exe"). */
    mcp_backend_routing_t routing;  /**< Routing rules for this backend. */
    uint32_t timeout_ms;            /**< Optional request timeout in milliseconds (0 for default). */
    // TODO: Add credentials field (e.g., api_key) later
    // void* credentials;

    // Note: This pool member seems intended for a different pooling mechanism
    // than the gateway_pool_manager. Keeping it for now, but its usage
    // in the context of the gateway needs clarification or removal.
    mcp_connection_pool_t* pool;    /**< Connection pool handle for this backend (managed externally?). */
} mcp_backend_info_t;

/**
 * @brief Frees the memory allocated for an mcp_backend_info_t structure and its contents.
 *
 * Frees the name, address, and the arrays/compiled regexes within the routing structure.
 *
 * @param backend_info Pointer to the backend info structure to free. If NULL, does nothing.
 */
void mcp_backend_info_free(mcp_backend_info_t* backend_info);

/**
 * @brief Loads backend server configurations from a JSON file.
 *
 * Parses the specified JSON file, validates its structure, compiles regex patterns (on POSIX),
 * and allocates an array of mcp_backend_info_t structures representing the
 * configured backends.
 *
 * @param config_path Path to the gateway configuration JSON file.
 * @param[out] backend_list Pointer to a variable that will receive the allocated array
 *                          of mcp_backend_info_t structures. The caller is responsible
 *                          for freeing this array and its contents using
 *                          mcp_free_backend_list() when no longer needed.
 * @param[out] backend_count Pointer to a variable that will receive the number of
 *                           backends loaded from the configuration.
 * @return mcp_error_code_t MCP_ERROR_NONE on success, or an appropriate error code.
 */
mcp_error_code_t load_gateway_config(
    const char* config_path,
    mcp_backend_info_t** backend_list, // Returns array of structs
    size_t* backend_count
);

/**
 * @brief Frees an array of mcp_backend_info_t structures loaded by load_gateway_config.
 *
 * Iterates through the array, calling mcp_backend_info_free() on each element,
 * and then frees the array itself.
 *
 * @param backend_list The array of backend info structures to free.
 * @param backend_count The number of elements in the array.
 */
void mcp_free_backend_list(mcp_backend_info_t* backend_list, size_t backend_count);


#ifdef __cplusplus
}
#endif

#endif /* MCP_GATEWAY_H */
