#include "internal/json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include "mcp_hashtable.h"

/**
 * @internal
/**
 * @internal
 * @brief Helper function to allocate an mcp_json_t node using the thread-local arena.
 *        Kept in this file as it's used by the create functions below.
 * @return Pointer to the allocated node, or NULL on failure.
 */
mcp_json_t* mcp_json_alloc_node(void) {
    // Always allocate node from the thread-local arena
    mcp_arena_t* arena = mcp_arena_get_current(); // Use correct function name
    if (!arena) {
        // Cannot log here easily if logging itself depends on arena/init
        // fprintf(stderr, "FATAL: Thread-local arena not initialized in mcp_json_alloc_node\n");
        // Consider a dedicated low-level error reporting mechanism if this is critical path
        return NULL;
    }
    return (mcp_json_t*)mcp_arena_alloc(arena, sizeof(mcp_json_t));
}

/**
 * @internal
 * @brief Value free function for the generic hash table when storing mcp_json_t values.
 *        Calls mcp_json_destroy on the value node.
 */
static void mcp_json_hashtable_value_free(void* value) {
    mcp_json_destroy((mcp_json_t*)value);
    // Note: This does NOT free the mcp_json_t node itself if it was arena allocated.
    // It only frees internal data (like string values, array items, nested objects).
}

mcp_json_t* mcp_json_null_create(void) {
    // Allocate the node structure itself using thread-local arena
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        // Arena allocation failed, likely OOM or uninitialized arena
        // Logging might not be safe/possible here.
        return NULL;
    }
    json->type = MCP_JSON_NULL;
    return json;
}

mcp_json_t* mcp_json_boolean_create(bool value) {
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        // Arena allocation failed
        return NULL;
    }
    json->type = MCP_JSON_BOOLEAN;
    json->boolean_value = value;
    return json;
}

mcp_json_t* mcp_json_number_create(double value) {
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        // Arena allocation failed
        return NULL;
    }
    json->type = MCP_JSON_NUMBER;
    json->number_value = value;
    return json;
}

// NOTE: String values *always* use mcp_strdup/malloc for the internal copy.
mcp_json_t* mcp_json_string_create(const char* value) {
    if (value == NULL) {
        return NULL; // Cannot create string from NULL
    }
    // Allocate the node structure using thread-local arena
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        // Arena allocation failed
        return NULL;
    }
    json->type = MCP_JSON_STRING;
    // Duplicate the input string using our helper
    json->string_value = mcp_strdup(value);
    if (json->string_value == NULL) {
        mcp_log_error("mcp_strdup failed for JSON string value.");
        // Node is arena allocated, will be cleaned up by arena reset/destroy.
        // No need to free(json) as it's from the arena.
        return NULL;
    }
    return json;
}

// NOTE: Array backing storage (the array of pointers `items`) *always* uses malloc/realloc.
mcp_json_t* mcp_json_array_create(void) {
    // Allocate the node structure using thread-local arena
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        // Arena allocation failed
        return NULL;
    }
    json->type = MCP_JSON_ARRAY;
    json->array.items = NULL;
    json->array.count = 0;
    json->array.capacity = 0;
    return json;
}

// NOTE: Object hash table structures (buckets, entries, keys) *always* use malloc/realloc/mcp_strdup.
mcp_json_t* mcp_json_object_create(void) {
    // Allocate the node structure using thread-local arena
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        // Arena allocation failed
        return NULL;
    }
    json->type = MCP_JSON_OBJECT;
    // Create a generic hash table for the object properties
    json->object_table = mcp_hashtable_create(
        MCP_JSON_HASH_TABLE_INITIAL_CAPACITY, // Initial capacity
        MCP_JSON_HASH_TABLE_MAX_LOAD_FACTOR,  // Load factor
        mcp_hashtable_string_hash,            // Use standard string hash
        mcp_hashtable_string_compare,         // Use standard string compare
        mcp_hashtable_string_dup,             // Use standard string dup (malloc)
        mcp_hashtable_string_free,            // Use standard string free
        mcp_json_hashtable_value_free         // Use our custom value free function
    );
    if (json->object_table == NULL) {
        mcp_log_error("Failed to create generic hash table for JSON object.");
        // Hashtable creation failed. Node is arena allocated, will be cleaned up by arena reset/destroy.
        return NULL; // Return error
    }
    return json;
}

// See header file for detailed explanation of mcp_json_destroy behavior.
void mcp_json_destroy(mcp_json_t* json) {
    if (json == NULL) {
        return;
    }

    // This function only frees internally allocated data using malloc/mcp_strdup/realloc.
    // It does NOT free the mcp_json_t node itself.
    switch (json->type) {
        case MCP_JSON_STRING:
            // Free the duplicated string value
            free(json->string_value);
            json->string_value = NULL; // Prevent double free
            break;
        case MCP_JSON_ARRAY:
            // Recursively destroy items before freeing the item array.
            // This assumes items were added correctly (e.g., if item was arena'd,
            // its internal mallocs are freed here, but node remains until arena clear).
            for (size_t i = 0; i < json->array.count; i++) {
                mcp_json_destroy(json->array.items[i]);
                // If the item node itself was malloc'd, the caller should free it.
                // If added to a malloc'd array, this function doesn't free the item node.
                // This highlights complexity - best practice is consistent allocation.
            }
            // Free the array pointer storage itself (always malloc'd)
            free(json->array.items);
            json->array.items = NULL;
            json->array.count = 0;
            json->array.capacity = 0;
            break;
        case MCP_JSON_OBJECT:
            // Destroy the generic hash table (frees buckets, entries, keys via key_free, and values via value_free)
            mcp_hashtable_destroy(json->object_table);
            json->object_table = NULL; // Prevent double free
            break;
        case MCP_JSON_NULL:
        case MCP_JSON_BOOLEAN:
        case MCP_JSON_NUMBER:
        default:
            // These types have no internally malloc'd data associated with the node.
            break;
    }
    // Note: We don't touch json->type or the value fields here, only pointers we allocated.
}

mcp_json_type_t mcp_json_get_type(const mcp_json_t* json) {
    if (json == NULL) {
        return MCP_JSON_NULL;
    }
    return json->type;
}

int mcp_json_get_boolean(const mcp_json_t* json, bool* value) {
    if (json == NULL || value == NULL || json->type != MCP_JSON_BOOLEAN) {
        return -1;
    }
    *value = json->boolean_value;
    return 0;
}

int mcp_json_get_number(const mcp_json_t* json, double* value) {
    if (json == NULL || value == NULL || json->type != MCP_JSON_NUMBER) {
        return -1;
    }
    *value = json->number_value;
    return 0;
}

int mcp_json_get_string(const mcp_json_t* json, const char** value) {
    if (json == NULL || value == NULL || json->type != MCP_JSON_STRING) {
        return -1;
    }
    *value = json->string_value;
    return 0;
}

int mcp_json_array_get_size(const mcp_json_t* json) {
    if (json == NULL || json->type != MCP_JSON_ARRAY) {
        return -1;
    }
    return (int)json->array.count;
}

mcp_json_t* mcp_json_array_get_item(const mcp_json_t* json, int index) {
    if (json == NULL || json->type != MCP_JSON_ARRAY || index < 0 || (size_t)index >= json->array.count) {
        return NULL;
    }
    return json->array.items[index];
}

// NOTE: Array backing storage (`items` pointer array) uses malloc/realloc.
int mcp_json_array_add_item(mcp_json_t* json, mcp_json_t* item) {
    if (json == NULL || item == NULL || json->type != MCP_JSON_ARRAY) {
        return -1; // Invalid input
    }
    // Resize the internal item pointer array if necessary
    if (json->array.count >= json->array.capacity) {
        size_t new_capacity = json->array.capacity == 0 ? 8 : json->array.capacity * 2;
        // Use realloc for the array of pointers
        mcp_json_t** new_items = (mcp_json_t**)realloc(json->array.items, new_capacity * sizeof(mcp_json_t*));
        if (new_items == NULL) {
            return -1; // Realloc failed
        }
        json->array.items = new_items;
        json->array.capacity = new_capacity;
    }
    json->array.items[json->array.count++] = item;
    return 0;
}

bool mcp_json_object_has_property(const mcp_json_t* json, const char* name) {
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT || json->object_table == NULL) {
        return false;
    }
    // Use the generic hashtable contains function
    return mcp_hashtable_contains(json->object_table, name);
}

mcp_json_t* mcp_json_object_get_property(const mcp_json_t* json, const char* name) {
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT || json->object_table == NULL) {
        return NULL;
    }
    void* value_ptr = NULL;
    // Use the generic hashtable get function
    if (mcp_hashtable_get(json->object_table, name, &value_ptr) == 0) {
        return (mcp_json_t*)value_ptr; // Key found
    }
    return NULL; // Key not found
}

// NOTE: The internal hash table uses malloc/mcp_strdup for keys and entries.
//       The added `value` node's memory is managed by its original allocator (arena or malloc).
//       The object takes ownership in the sense that mcp_json_destroy(object) will
//       call mcp_json_destroy(value) later.
int mcp_json_object_set_property(mcp_json_t* json, const char* name, mcp_json_t* value) {
    if (json == NULL || name == NULL || value == NULL || json->type != MCP_JSON_OBJECT || json->object_table == NULL) {
        return -1; // Invalid input
    }
    // Use the generic hashtable put function
    // Note: mcp_hashtable_put will handle key duplication and freeing old value/key if necessary
    return mcp_hashtable_put(json->object_table, name, value);
}

int mcp_json_object_delete_property(mcp_json_t* json, const char* name) {
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT || json->object_table == NULL) {
        return -1;
    }
    // Use the generic hashtable remove function
    // Note: mcp_hashtable_remove will call key_free and value_free
    // It returns 0 on success, non-zero if key not found.
    return mcp_hashtable_remove(json->object_table, name);
}

// Context struct for the get_property_names callback
typedef struct {
    char** names_array;
    size_t current_index;
    size_t capacity;
    bool error_occurred;
} get_names_context_t;

// Callback function for mcp_hashtable_foreach used in get_property_names
static void collect_name_callback(const void* key, void* value, void* user_data) {
    (void)value; // Value is unused in this callback
    get_names_context_t* ctx = (get_names_context_t*)user_data;

    if (ctx->error_occurred) {
        return; // Stop processing if an error already occurred
    }

    if (ctx->current_index < ctx->capacity) {
        // Duplicate the key string (which is const char*) using mcp_strdup
        ctx->names_array[ctx->current_index] = mcp_strdup((const char*)key);
        if (ctx->names_array[ctx->current_index] == NULL) {
            mcp_log_error("mcp_strdup failed while collecting property names.");
            ctx->error_occurred = true;
            // Cleanup will happen after the foreach call returns
        }
        ctx->current_index++;
    } else {
        // This case should ideally not happen if size calculation is correct
        mcp_log_error("Hash table size mismatch during name collection (index %zu >= capacity %zu).",
                      ctx->current_index, ctx->capacity);
        ctx->error_occurred = true;
    }
}

// NOTE: The returned array of names and the names themselves use malloc/mcp_strdup.
//       The caller is responsible for freeing them as described in the header.
int mcp_json_object_get_property_names(const mcp_json_t* json, char*** names_out, size_t* count_out) {
    if (json == NULL || names_out == NULL || count_out == NULL || json->type != MCP_JSON_OBJECT) {
        if (names_out) *names_out = NULL;
        if (count_out) *count_out = 0;
        return -1; // Invalid input
    }

    mcp_hashtable_t* table = json->object_table;
    if (table == NULL) { // Check if table exists
         if (names_out) *names_out = NULL;
         if (count_out) *count_out = 0;
         return 0; // Treat as empty object
    }

    *count_out = mcp_hashtable_size(table);
    if (*count_out == 0) {
        *names_out = NULL; // No properties, return NULL array
        return 0;
    }

    // Allocate the array of char pointers using malloc
    *names_out = (char**)malloc(*count_out * sizeof(char*));
    if (*names_out == NULL) {
        *count_out = 0;
        return -1; // Allocation failure
    }

    // Initialize context for the callback
    get_names_context_t context;
    context.names_array = *names_out;
    context.current_index = 0;
    context.capacity = *count_out;
    context.error_occurred = false;

    // Iterate using the generic foreach, passing the callback function pointer
    mcp_hashtable_foreach(table, collect_name_callback, &context);

    // Check if an error occurred during iteration
    if (context.error_occurred) {
        // Clean up partially allocated names array
        for (size_t j = 0; j < context.current_index; j++) {
            // Check if the pointer is not NULL before freeing, in case mcp_strdup failed on the last one
            if (context.names_array[j] != NULL) {
                free(context.names_array[j]);
            }
        }
        free(*names_out);
        *names_out = NULL;
        *count_out = 0;
        return -1;
    }

    // Ensure we collected the expected number of names
    assert(context.current_index == *count_out);

    return 0;
}

/**
 * @brief Validate if a JSON message conforms to a specified schema. (Placeholder)
 * @note Requires integration with a JSON schema validation library. This function
 *       cannot be fully implemented without adding such a dependency (e.g., jsonschema-c).
 */
int mcp_json_validate_schema(const char* json_str, const char* schema_str) {
    // Placeholder implementation: Returns success without validation.
    // A real implementation requires integrating an external library.
    (void)json_str;   // Mark as unused
    (void)schema_str; // Mark as unused
    mcp_log_warn("mcp_json_validate_schema: Function not implemented (requires external JSON schema library). Assuming valid.");
    // Example steps if using a library 'libjsonschema':
    // 1. schema = libjsonschema_parse(schema_str); if (!schema) return -1;
    // 2. json_doc = libjsonschema_parse_json(json_str); if (!json_doc) { libjsonschema_free(schema); return -1; }
    // 3. result = libjsonschema_validate(schema, json_doc);
    // 4. libjsonschema_free(schema); libjsonschema_free(json_doc);
    // 5. return (result == VALID) ? 0 : -1;
    return 0; // Placeholder: Assume valid
}

/**
 * @brief Set maximum depth and size limits for JSON parsing. (Placeholder)
 * @note The current custom parser only supports a hardcoded depth limit.
 *       Implementing a size limit requires modifying the parser itself.
 */
void mcp_json_set_limits(int max_depth, size_t max_size) {
    // Placeholder implementation: Notes limitations.
    // The current parser uses MCP_JSON_MAX_PARSE_DEPTH (hardcoded).
    // A size limit (max_size) is not supported by this simple parser.
    // A more robust parser library (like cJSON with hooks, or others) might support these.
    (void)max_depth; // Mark as unused - depth is currently hardcoded
    (void)max_size;  // Mark as unused - size limit not implemented
    mcp_log_warn("mcp_json_set_limits: Function partially implemented. Max depth is hardcoded (%d), max size limit is not supported by current parser.", MCP_JSON_MAX_PARSE_DEPTH);
}
