#ifndef MCP_JSON_H
#define MCP_JSON_H

#include <mcp_types.h>
#include <mcp_arena.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Represents the type of a JSON value.
 */
typedef enum {
    MCP_JSON_NULL,      /**< Represents a JSON null value. */
    MCP_JSON_BOOLEAN,   /**< Represents a JSON boolean value (true or false). */
    MCP_JSON_NUMBER,    /**< Represents a JSON number value (stored as double). */
    MCP_JSON_STRING,    /**< Represents a JSON string value. */
    MCP_JSON_ARRAY,     /**< Represents a JSON array value. */
    MCP_JSON_OBJECT,    /**< Represents a JSON object value. */
} mcp_json_type_t;

/**
 * @brief Opaque handle representing a JSON value.
 */
typedef struct mcp_json mcp_json_t;

/**
 * @brief Creates a JSON null value.
 * @return Pointer to the created JSON value (allocated using the thread-local arena), or NULL on error.
 * @note The returned node's memory is managed by the thread-local arena.
 *       Call mcp_json_destroy() before resetting/destroying the arena if necessary.
 */
mcp_json_t* mcp_json_null_create(void);

/**
 * @brief Creates a JSON boolean value.
 * @param value The boolean value (true or false).
 * @return Pointer to the created JSON value (allocated using the thread-local arena), or NULL on error.
 * @note See mcp_json_null_create() note regarding arena management.
 */
mcp_json_t* mcp_json_boolean_create(bool value);

/**
 * @brief Creates a JSON number value.
 * @param value The numeric value (stored as double).
 * @return Pointer to the created JSON value (allocated using the thread-local arena), or NULL on error.
 * @note See mcp_json_null_create() note regarding arena management.
 */
mcp_json_t* mcp_json_number_create(double value);

/**
 * @brief Creates a JSON string value.
 * @param value The null-terminated string value. This string is *always* duplicated
 *              internally using malloc/strdup. Can be NULL, which results in an error.
 * @return Pointer to the created JSON value (node allocated using the thread-local arena), or NULL on error.
 * @note The internal string copy is freed by mcp_json_destroy().
 *       See mcp_json_null_create() note regarding arena management for the node itself.
 */
mcp_json_t* mcp_json_string_create(const char* value);

/**
 * @brief Creates an empty JSON array value.
 * @return Pointer to the created JSON array value (node allocated using the thread-local arena), or NULL on error.
 * @note Internal storage for array items uses malloc/realloc and is freed by mcp_json_destroy().
 *       See mcp_json_null_create() note regarding arena management for the node itself.
 */
mcp_json_t* mcp_json_array_create(void);

/**
 * @brief Creates an empty JSON object value.
 * @return Pointer to the created JSON object value (node allocated using the thread-local arena), or NULL on error.
 * @note Internal storage for object properties (hash table, keys, values) uses malloc/realloc/strdup
 *       and is freed by mcp_json_destroy().
 *       See mcp_json_null_create() note regarding arena management for the node itself.
 */
mcp_json_t* mcp_json_object_create(void);

/**
 * @brief Parses a JSON string into a tree of mcp_json_t nodes.
 *
 * Uses the calling thread's thread-local arena for allocating mcp_json_t nodes.
 * Internal strings, array storage, and object hash tables *always* use malloc/strdup/realloc.
 * @param json The null-terminated JSON string to parse.
 * @return Pointer to the root mcp_json_t node of the parsed tree, or NULL on parse error or allocation failure.
 * @note The caller must call mcp_json_destroy() on the root node to free
 *       internal malloc'd strings/tables, and *then* reset or destroy the thread-local arena
 *       to free the nodes themselves.
 */
mcp_json_t* mcp_json_parse(const char* json);

/**
 * @brief Converts a JSON value tree back into a JSON string representation.
 *
 * @param json Pointer to the root JSON value node.
 * @return A newly allocated null-terminated JSON string, or NULL on error (e.g., allocation failure).
 * @note The caller is responsible for freeing the returned string using free().
 */
char* mcp_json_stringify(const mcp_json_t* json);

/**
 * @brief Gets the type of a JSON value.
 *
 * @param json Pointer to the JSON value. Must not be NULL.
 * @return The mcp_json_type_t enum value.
 */
mcp_json_type_t mcp_json_get_type(const mcp_json_t* json);

/**
 * @brief Retrieves the boolean value from a JSON boolean node.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_BOOLEAN.
 * @param[out] value Pointer to a bool variable where the value will be stored.
 * @return 0 on success, non-zero if json is NULL or not a boolean type.
 */
int mcp_json_get_boolean(const mcp_json_t* json, bool* value);

/**
 * @brief Retrieves the numeric value from a JSON number node.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_NUMBER.
 * @param[out] value Pointer to a double variable where the value will be stored.
 * @return 0 on success, non-zero if json is NULL or not a number type.
 */
int mcp_json_get_number(const mcp_json_t* json, double* value);

/**
 * @brief Retrieves the string value from a JSON string node.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_STRING.
 * @param[out] value Pointer to a `const char*` variable that will be set to point
 *                   to the internal string data.
 * @return 0 on success, non-zero if json is NULL or not a string type.
 * @note The returned string pointer is valid only as long as the `json` node exists
 *       and has not been destroyed or modified. Do not free the returned pointer.
 */
int mcp_json_get_string(const mcp_json_t* json, const char** value);

/**
 * @brief Gets the number of items currently in a JSON array.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_ARRAY.
 * @return The number of items in the array, or -1 if json is NULL or not an array type.
 */
int mcp_json_array_get_size(const mcp_json_t* json);

/**
 * @brief Retrieves an item from a JSON array by its index.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_ARRAY.
 * @param index The zero-based index of the item to retrieve.
 * @return Pointer to the JSON value at the specified index, or NULL if json is NULL,
 *         not an array type, or the index is out of bounds.
 * @note The returned pointer refers to the item within the array; do not free it directly.
 *       Its lifetime is tied to the parent array.
 */
mcp_json_t* mcp_json_array_get_item(const mcp_json_t* json, int index);

/**
 * @brief Adds a JSON value item to the end of a JSON array.
 *
 * The array takes ownership of the added item. If the item was allocated with malloc,
 * it (and its internal data) will be freed when the parent array is destroyed via
 * mcp_json_destroy(). If the item was allocated using an arena, only its internal
 * malloc'd data (like strings) will be freed by mcp_json_destroy(); the node itself
 * must be freed by the arena. Mixing allocators requires careful management.
 *
 * @param json Pointer to the JSON array value. Must be of type MCP_JSON_ARRAY.
 * @param item Pointer to the JSON value item to add.
 * @return 0 on success, non-zero on error (e.g., NULL input, wrong type, allocation failure).
 */
int mcp_json_array_add_item(mcp_json_t* json, mcp_json_t* item);

/**
 * @brief Checks if a JSON object contains a property with the given name.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_OBJECT.
 * @param name The null-terminated property name (key) to check for.
 * @return True if the property exists, false otherwise or if json is NULL/not an object.
 */
bool mcp_json_object_has_property(const mcp_json_t* json, const char* name);

/**
 * @brief Retrieves a property (value) from a JSON object by its name (key).
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_OBJECT.
 * @param name The null-terminated property name (key) whose value should be retrieved.
 * @return Pointer to the JSON value associated with the name, or NULL if json is NULL,
 *         not an object type, or the property does not exist.
 * @note The returned pointer refers to the value within the object; do not free it directly.
 *       Its lifetime is tied to the parent object.
 */
mcp_json_t* mcp_json_object_get_property(const mcp_json_t* json, const char* name);

/**
 * @brief Sets a property (key-value pair) in a JSON object.
 *
 * If a property with the same name already exists, its old value is destroyed
 * (using mcp_json_destroy) and replaced with the new value. The object takes
 * ownership of the provided `value` node. See mcp_json_array_add_item() note
 * regarding memory management and mixed allocators.
 *
 * @param json Pointer to the JSON object value. Must be of type MCP_JSON_OBJECT.
 * @param name The null-terminated property name (key). The name string is copied internally (using malloc).
 * @param value Pointer to the JSON value to associate with the name.
 * @return 0 on success, non-zero on error (e.g., NULL input, wrong type, allocation failure).
 */
int mcp_json_object_set_property(mcp_json_t* json, const char* name, mcp_json_t* value);

/**
 * @brief Deletes a property (key-value pair) from a JSON object.
 *
 * If the property exists, its associated value is destroyed using mcp_json_destroy().
 *
 * @param json Pointer to the JSON object value. Must be of type MCP_JSON_OBJECT.
 * @param name The null-terminated property name (key) to delete.
 * @return 0 on success (property found and deleted), non-zero if json is NULL,
 *         not an object, or the property does not exist.
 */
int mcp_json_object_delete_property(mcp_json_t* json, const char* name);

/**
 * @brief Retrieves an array of all property names (keys) from a JSON object.
 *
 * @param json Pointer to the JSON object value. Must be of type MCP_JSON_OBJECT.
 * @param[out] names Pointer to a `char**` variable that will receive the newly allocated
 *                   array of property name strings.
 * @param[out] count Pointer to a size_t variable that will receive the number of names
 *                   in the returned array.
 * @return 0 on success, non-zero on error (e.g., NULL input, wrong type, allocation failure).
 * @note The caller is responsible for freeing the memory allocated for the name strings.
 *       This involves freeing each individual string (`names[i]`) and then freeing
 *       the array of pointers (`names`) itself using `free()`.
 */
int mcp_json_object_get_property_names(const mcp_json_t* json, char*** names, size_t* count);

/**
 * @brief Frees memory allocated *internally* by a JSON value node.
 *
 * This function handles freeing:
 * - The duplicated string value within an MCP_JSON_STRING node (allocated via malloc/strdup).
 * - The internal storage (array of pointers) for an MCP_JSON_ARRAY node (allocated via malloc/realloc).
 * - The internal hash table, keys, and associated storage for an MCP_JSON_OBJECT node.
 * - Recursively calls mcp_json_destroy() on child items in arrays and objects.
 *
 * **IMPORTANT Memory Management Notes:**
 * 1.  This function **DOES NOT** free the `mcp_json_t` node itself pointed to by `json`.
 * 2.  This function **DOES NOT** free the `mcp_json_t` node itself pointed to by `json`.
 *     The node's memory is managed by the thread-local arena.
 * 3.  You MUST call `mcp_json_destroy(json)` first to free any internally `malloc`-ed data
 *     (like strings or object keys), and *then* reset or destroy the thread-local arena to free the node itself.
 *
 * @param json Pointer to the JSON value whose internal data should be freed. If NULL, the function does nothing.
 */
void mcp_json_destroy(mcp_json_t* json);

/**
 * @brief Parses a JSON string representing an MCP message (request, response, or notification).
 *
 * Uses the calling thread's thread-local arena for parsing the JSON structure.
 * Note that top-level strings like `method` within the output `message` struct are still `malloc`-ed.
 * @param json The null-terminated JSON string representing the message.
 * @param[out] message Pointer to an `mcp_message_t` structure where the parsed message will be stored.
 *                     The caller typically allocates this structure on the stack.
 * @return 0 on success, non-zero on error (e.g., parse error, invalid message structure).
 * @note On success, the `message` structure will contain pointers to dynamically allocated data
 *       (strings like method, error_message, result/params). The caller MUST call `mcp_message_release_contents(message)`
 *       to free this internal data before the `message` structure goes out of scope or is reused. The parsed JSON tree itself lives in the thread-local arena. */
int mcp_json_parse_message(const char* json, mcp_message_t* message);

/**
 * @brief Converts an MCP message structure into a JSON string representation.
 *
 * @param message Pointer to the `mcp_message_t` structure to stringify.
 * @return A newly allocated null-terminated JSON string, or NULL on error (e.g., allocation failure).
 * @note The caller is responsible for freeing the returned string using `free()`.
 * @note This function uses the thread-local arena for temporary JSON nodes during stringification,
 *       but the final returned string is allocated using `malloc`. */
char* mcp_json_stringify_message(const mcp_message_t* message);

/**
 * @brief Validate if a JSON message conforms to a specified schema.
 * @param json_str JSON string to validate.
 * @param schema_str JSON Schema string.
 * @return 0 for validation success, -1 for validation failure or error.
 * @note This function requires a JSON schema validation library (e.g., cJSON Schema, jsonschema-c).
 *       The implementation details depend on the chosen library.
 */
int mcp_json_validate_schema(const char* json_str, const char* schema_str);

/**
 * @brief JSON Schema cache handle.
 */
typedef struct mcp_json_schema_cache mcp_json_schema_cache_t;

/**
 * @brief Create a new JSON Schema cache.
 *
 * @param capacity Maximum number of schemas to store in the cache (0 for unlimited).
 * @return Pointer to the created cache, or NULL on failure.
 */
mcp_json_schema_cache_t* mcp_json_schema_cache_create(size_t capacity);

/**
 * @brief Destroy a JSON Schema cache.
 *
 * @param cache The cache to destroy.
 */
void mcp_json_schema_cache_destroy(mcp_json_schema_cache_t* cache);

/**
 * @brief Validate JSON against a schema, using the cache.
 *
 * @param cache The schema cache.
 * @param json_str The JSON string to validate.
 * @param schema_str The schema string.
 * @return 0 for validation success, -1 for validation failure or error.
 */
int mcp_json_schema_validate_cached(mcp_json_schema_cache_t* cache, const char* json_str, const char* schema_str);

/**
 * @brief Get cache statistics.
 *
 * @param cache The cache to get statistics for.
 * @param size Pointer to store the current cache size.
 * @param capacity Pointer to store the cache capacity.
 * @param hits Pointer to store the number of cache hits.
 * @param misses Pointer to store the number of cache misses.
 * @return 0 on success, -1 on failure.
 */
int mcp_json_schema_cache_get_stats(mcp_json_schema_cache_t* cache, size_t* size, size_t* capacity, size_t* hits, size_t* misses);

/**
 * @brief Clear all schemas from the cache.
 *
 * @param cache The cache to clear.
 */
void mcp_json_schema_cache_clear(mcp_json_schema_cache_t* cache);

/**
 * @brief Set maximum depth and size limits for JSON parsing.
 * @param max_depth Maximum nesting depth allowed during parsing.
 * @param max_size Maximum JSON string size (in bytes) allowed for parsing.
 * @note These limits help prevent denial-of-service attacks via deeply nested or overly large JSON inputs.
 *       The underlying JSON parser (e.g., cJSON) needs to support these limits.
 */
void mcp_json_set_limits(int max_depth, size_t max_size);

#ifdef __cplusplus
}
#endif

#endif /* MCP_JSON_H */
