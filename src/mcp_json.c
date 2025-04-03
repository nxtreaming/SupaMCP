#include "mcp_json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

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
    return (mcp_json_t*)mcp_arena_alloc(sizeof(mcp_json_t));
}

// --- Public JSON API Implementation ---

mcp_json_t* mcp_json_null_create(void) {
    // Allocate the node structure itself using thread-local arena
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate JSON null node from arena.");
        return NULL;
    }
    json->type = MCP_JSON_NULL;
    return json;
}

mcp_json_t* mcp_json_boolean_create(bool value) {
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate JSON boolean node from arena.");
        return NULL;
    }
    json->type = MCP_JSON_BOOLEAN;
    json->boolean_value = value;
    return json;
}

mcp_json_t* mcp_json_number_create(double value) {
    mcp_json_t* json = mcp_json_alloc_node();
    if (json == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate JSON number node from arena.");
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
        log_message(LOG_LEVEL_ERROR, "Failed to allocate JSON string node from arena.");
        return NULL;
    }
    json->type = MCP_JSON_STRING;
    // Duplicate the input string using our helper
    json->string_value = mcp_strdup(value);
    if (json->string_value == NULL) {
        // mcp_strdup failed. Node is arena allocated, will be cleaned up by arena reset/destroy.
        log_message(LOG_LEVEL_ERROR, "mcp_strdup failed for JSON string value.");
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
        log_message(LOG_LEVEL_ERROR, "Failed to allocate JSON array node from arena.");
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
        log_message(LOG_LEVEL_ERROR, "Failed to allocate JSON object node from arena.");
        return NULL;
    }
    json->type = MCP_JSON_OBJECT;
    // Initialize the internal hash table (which uses malloc for its parts)
    if (mcp_json_object_table_init(&json->object, MCP_JSON_HASH_TABLE_INITIAL_CAPACITY) != 0) {
        // Table init failed. Node is arena allocated, will be cleaned up by arena reset/destroy.
        log_message(LOG_LEVEL_ERROR, "Failed to initialize JSON object hash table.");
        // No need to free(json) as it's from the arena.
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
            // Destroy the internal hash table (frees buckets, entries, keys, and calls destroy on values)
            mcp_json_object_table_destroy(&json->object);
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
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT) {
        return false;
    }
    // Cast away const for internal find operation (doesn't modify the table)
    return mcp_json_object_table_find((mcp_json_object_table_t*)&json->object, name) != NULL;
}

mcp_json_t* mcp_json_object_get_property(const mcp_json_t* json, const char* name) {
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT) {
        return NULL;
    }
    // Cast away const for internal find operation (doesn't modify the table)
    mcp_json_object_entry_t* entry = mcp_json_object_table_find((mcp_json_object_table_t*)&json->object, name);
    return (entry != NULL) ? entry->value : NULL;
}

// NOTE: The internal hash table uses malloc/mcp_strdup for keys and entries.
//       The added `value` node's memory is managed by its original allocator (arena or malloc).
//       The object takes ownership in the sense that mcp_json_destroy(object) will
//       call mcp_json_destroy(value) later.
int mcp_json_object_set_property(mcp_json_t* json, const char* name, mcp_json_t* value) {
    if (json == NULL || name == NULL || value == NULL || json->type != MCP_JSON_OBJECT) {
        return -1; // Invalid input
    }
    // Pass table directly to internal set function
    return mcp_json_object_table_set(&json->object, name, value);
}

int mcp_json_object_delete_property(mcp_json_t* json, const char* name) {
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT) {
        return -1;
    }
    return mcp_json_object_table_delete(&json->object, name);
}

// NOTE: The returned array of names and the names themselves use malloc/mcp_strdup.
//       The caller is responsible for freeing them as described in the header.
int mcp_json_object_get_property_names(const mcp_json_t* json, char*** names_out, size_t* count_out) {
    if (json == NULL || names_out == NULL || count_out == NULL || json->type != MCP_JSON_OBJECT) {
        if (names_out) *names_out = NULL;
        if (count_out) *count_out = 0;
        return -1; // Invalid input
    }
    const mcp_json_object_table_t* table = &json->object;
    *count_out = table->count;
    if (table->count == 0) {
        *names_out = NULL; // No properties, return NULL array
        return 0;
    }
    // Allocate the array of char pointers using malloc
    *names_out = (char**)malloc(table->count * sizeof(char*));
    if (*names_out == NULL) {
        *count_out = 0;
        return -1; // Allocation failure
    }
    size_t current_index = 0;
    // Iterate through all buckets and entries in the hash table
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            assert(current_index < table->count); // Internal consistency check
            // Duplicate each name string using our helper (malloc)
            (*names_out)[current_index] = mcp_strdup(entry->name);
            if ((*names_out)[current_index] == NULL) {
                // mcp_strdup failed, clean up already duplicated names and the array
                for (size_t j = 0; j < current_index; j++) {
                    free((*names_out)[j]); // Free individual name strings
                }
                free(*names_out);
                *names_out = NULL;
                *count_out = 0;
                return -1;
            }
            current_index++;
            entry = entry->next;
        }
    }
    assert(current_index == table->count); // Ensure we got all names
    return 0;
}

// --- Security Enhancement Functions (Placeholders) ---

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
    log_message(LOG_LEVEL_WARN, "mcp_json_validate_schema: Function not implemented (requires external JSON schema library). Assuming valid.");
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
    log_message(LOG_LEVEL_WARN, "mcp_json_set_limits: Function partially implemented. Max depth is hardcoded (%d), max size limit is not supported by current parser.", MCP_JSON_MAX_PARSE_DEPTH);
}
