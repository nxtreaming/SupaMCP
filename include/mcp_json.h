#ifndef MCP_JSON_H
#define MCP_JSON_H

#include "mcp_types.h"
#include "mcp_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * JSON value type
 */
typedef enum {
    MCP_JSON_NULL,
    MCP_JSON_BOOLEAN,
    MCP_JSON_NUMBER,
    MCP_JSON_STRING,
    MCP_JSON_ARRAY,
    MCP_JSON_OBJECT,
} mcp_json_type_t;

/**
 * JSON value
 */
typedef struct mcp_json mcp_json_t;

/**
 * Create a null JSON value
 * @param arena Arena to allocate from (optional, uses malloc if NULL)
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_null_create(mcp_arena_t* arena);

/**
 * Create a boolean JSON value
 * 
 * @param arena Arena to allocate from (optional, uses malloc if NULL)
 * @param value Boolean value
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_boolean_create(mcp_arena_t* arena, bool value);

/**
 * Create a number JSON value
 * 
 * @param arena Arena to allocate from (optional, uses malloc if NULL)
 * @param value Number value
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_number_create(mcp_arena_t* arena, double value);

/**
 * Create a string JSON value
 * 
 * @param arena Arena to allocate from (optional, uses malloc if NULL)
 * @param value String value (will be duplicated using malloc/strdup, not arena)
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_string_create(mcp_arena_t* arena, const char* value);

/**
 * Create an array JSON value
 * @param arena Arena to allocate from (optional, uses malloc if NULL)
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_array_create(mcp_arena_t* arena);

/**
 * Create an object JSON value
 * @param arena Arena to allocate from (optional, uses malloc if NULL)
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_object_create(mcp_arena_t* arena);

/**
 * Parse a JSON string using an optional arena for allocations.
 * NOTE: String values within the parsed JSON will still use malloc/strdup.
 *       Array/Object backing storage will use malloc/realloc.
 *       Only the mcp_json_t nodes themselves are allocated from the arena.
 *
 * @param arena Arena to allocate mcp_json_t nodes from (optional, uses malloc if NULL)
 * @param json JSON string
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_parse(mcp_arena_t* arena, const char* json);

/**
 * Stringify a JSON value
 * 
 * @param json JSON value
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_stringify(const mcp_json_t* json);

/**
 * Get the type of a JSON value
 * 
 * @param json JSON value
 * @return JSON type
 */
mcp_json_type_t mcp_json_get_type(const mcp_json_t* json);

/**
 * Get a boolean value from a JSON value
 * 
 * @param json JSON value
 * @param value Output boolean value
 * @return 0 on success, non-zero on error
 */
int mcp_json_get_boolean(const mcp_json_t* json, bool* value);

/**
 * Get a number value from a JSON value
 * 
 * @param json JSON value
 * @param value Output number value
 * @return 0 on success, non-zero on error
 */
int mcp_json_get_number(const mcp_json_t* json, double* value);

/**
 * Get a string value from a JSON value
 * 
 * @param json JSON value
 * @param value Output string value
 * @return 0 on success, non-zero on error
 */
int mcp_json_get_string(const mcp_json_t* json, const char** value);

/**
 * Get the size of a JSON array
 * 
 * @param json JSON value
 * @return Size of the array or -1 on error
 */
int mcp_json_array_get_size(const mcp_json_t* json);

/**
 * Get an item from a JSON array
 * 
 * @param json JSON value
 * @param index Index of the item
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_array_get_item(const mcp_json_t* json, int index);

/**
 * Add an item to a JSON array
 * 
 * @param json JSON value
 * @param item Item to add
 * @return 0 on success, non-zero on error
 */
int mcp_json_array_add_item(mcp_json_t* json, mcp_json_t* item);

/**
 * Check if a JSON object has a property
 * 
 * @param json JSON value
 * @param name Property name
 * @return true if the property exists, false otherwise
 */
bool mcp_json_object_has_property(const mcp_json_t* json, const char* name);

/**
 * Get a property from a JSON object
 * 
 * @param json JSON value
 * @param name Property name
 * @return JSON value or NULL on error
 */
mcp_json_t* mcp_json_object_get_property(const mcp_json_t* json, const char* name);

/**
 * Set a property in a JSON object
 * 
 * @param json JSON value
 * @param name Property name
 * @param value Property value
 * @return 0 on success, non-zero on error
 */
int mcp_json_object_set_property(mcp_json_t* json, const char* name, mcp_json_t* value);

/**
 * Delete a property from a JSON object
 * 
 * @param json JSON value
 * @param name Property name
 * @return 0 on success, non-zero on error
 */
int mcp_json_object_delete_property(mcp_json_t* json, const char* name);

/**
 * Get all property names from a JSON object
 * 
 * @param json JSON value
 * @param names Output property names (must be freed by the caller)
 * @param count Output property count
 * @return 0 on success, non-zero on error
 */
int mcp_json_object_get_property_names(const mcp_json_t* json, char*** names, size_t* count);

/**
 * @brief Destroys the internal data of a JSON value (strings, arrays, objects).
 *
 * IMPORTANT: This function ONLY frees data allocated internally by the JSON
 * library using malloc/strdup/realloc (e.g., string values, array/object storage).
 * It DOES NOT free the top-level mcp_json_t node itself pointed to by `json`.
 *
 * - If the JSON tree was created using an mcp_arena_t (e.g., via
 *   mcp_json_parse(arena, ...)), you MUST free the entire tree by calling
 *   mcp_arena_reset() or mcp_arena_destroy() on the arena. Calling
 *   mcp_json_destroy on an arena-allocated tree may lead to errors.
 * - If the JSON value was created using malloc (e.g., via
 *   mcp_json_parse(NULL, ...) or mcp_json_*_create(NULL, ...)), you should
 *   call mcp_json_destroy() first to free its internal contents, and then
 *   call free() on the `json` pointer itself.
 *
 * @param json The JSON value whose internal data should be freed.
 */
void mcp_json_destroy(mcp_json_t* json);

/**
 * Parse a message from JSON
 * 
 * @param json JSON string
 * @param arena Arena to allocate parsed JSON nodes from (optional, uses malloc if NULL)
 * @param json JSON string
 * @param message Output message (strings inside message still use malloc/strdup)
 * @return 0 on success, non-zero on error
 */
int mcp_json_parse_message(mcp_arena_t* arena, const char* json, mcp_message_t* message);

/**
 * Stringify a message to JSON
 * 
 * @param message Message
 * @return JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_stringify_message(const mcp_message_t* message);

#ifdef __cplusplus
}
#endif

#endif /* MCP_JSON_H */
