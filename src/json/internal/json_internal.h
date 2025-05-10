#ifndef MCP_JSON_INTERNAL_H
#define MCP_JSON_INTERNAL_H

#include "mcp_json.h"
#include "mcp_arena.h"
#include "mcp_thread_local.h"
#include "mcp_profiler.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

// --- Constants (Keep if needed elsewhere, otherwise remove) ---
/** @internal Initial capacity for the hash table bucket array. Should be power of 2. */
#define MCP_JSON_HASH_TABLE_INITIAL_CAPACITY 32  // Increased from 16 for better performance
/** @internal Load factor threshold. If count/capacity exceeds this, the table resizes. */
#define MCP_JSON_HASH_TABLE_MAX_LOAD_FACTOR 0.75

// --- Internal JSON Node Structure ---
// Note: Old custom hash table structs (mcp_json_object_entry_t, mcp_json_object_table_t) are removed.

/**
 * @internal
 * @brief Internal structure representing a JSON value.
 */

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable:4201)
#endif

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
        mcp_hashtable_t* object_table; /**< Generic hash table for properties. Used if type is MCP_JSON_OBJECT. */
    };
};
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

// --- Internal Function Prototypes ---

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
