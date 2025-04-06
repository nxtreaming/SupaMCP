#ifndef MCP_JSON_INTERNAL_H
#define MCP_JSON_INTERNAL_H

#include "mcp_json.h"
#include "mcp_arena.h"
#include "mcp_thread_local.h"
#include "mcp_profiler.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

// --- Hash Table Implementation Details ---

/** @internal Initial capacity for the hash table bucket array. Should be power of 2. */
#define MCP_JSON_HASH_TABLE_INITIAL_CAPACITY 16
/** @internal Load factor threshold. If count/capacity exceeds this, the table resizes. */
#define MCP_JSON_HASH_TABLE_MAX_LOAD_FACTOR 0.75

/**
 * @internal
 * @brief Represents a single key-value entry within a JSON object's hash table bucket.
 */
typedef struct mcp_json_object_entry {
    char* name;                         /**< Property name (key), allocated using mcp_strdup (malloc). */
    mcp_json_t* value;                  /**< Property value (mcp_json_t node), allocated using thread-local arena. */
    struct mcp_json_object_entry* next; /**< Pointer to the next entry in the same bucket (separate chaining). */
} mcp_json_object_entry_t;

/**
 * @internal
 * @brief Hash table structure used internally to store JSON object properties.
 */
typedef struct mcp_json_object_table {
    mcp_json_object_entry_t** buckets; /**< Array of pointers to entries (buckets), allocated using malloc/realloc. */
    size_t capacity;                   /**< Current capacity (number of buckets) of the bucket array. */
    size_t count;                      /**< Number of key-value pairs currently stored in the table. */
} mcp_json_object_table_t;

// --- Internal JSON Node Structure ---

/**
 * @internal
 * @brief Internal structure representing a JSON value.
 */
struct mcp_json {
    mcp_json_type_t type; /**< The type of this JSON value. */
    union {
        bool boolean_value;     /**< Used if type is MCP_JSON_BOOLEAN. */
        double number_value;    /**< Used if type is MCP_JSON_NUMBER. */
        char* string_value;     /**< Used if type is MCP_JSON_STRING. Allocated using malloc/mcp_strdup. */
        struct {
            mcp_json_t** items; /**< Dynamic array of item pointers. Allocated using malloc/realloc. */
            size_t count;       /**< Number of items currently in the array. */
            size_t capacity;    /**< Current allocated capacity of the items array. */
        } array;                /**< Used if type is MCP_JSON_ARRAY. */
        mcp_json_object_table_t object; /**< Hash table for properties. Used if type is MCP_JSON_OBJECT. */
    };
};

// --- Internal Function Prototypes ---

// From mcp_json_hashtable.c
int mcp_json_object_table_init(mcp_json_object_table_t* table, size_t capacity);
void mcp_json_object_table_destroy(mcp_json_object_table_t* table);
int mcp_json_object_table_set(mcp_json_object_table_t* table, const char* name, mcp_json_t* value);
mcp_json_object_entry_t* mcp_json_object_table_find(mcp_json_object_table_t* table, const char* name);
int mcp_json_object_table_delete(mcp_json_object_table_t* table, const char* name);
// Note: resize is static within hashtable.c, no need to declare here unless needed elsewhere

// From mcp_json_parser.c
#define MCP_JSON_MAX_PARSE_DEPTH 100
mcp_json_t* parse_value(const char** json, int depth);
// Other parser helpers are static within parser.c

// From mcp_json_stringifier.c
// stringify_value is static within mcp_json_stringifier.c
// Other stringifier helpers are static within stringifier.c

// From mcp_json.c (or potentially moved)
mcp_json_t* mcp_json_alloc_node(void); // Helper to allocate node from arena

#endif // MCP_JSON_INTERNAL_H
