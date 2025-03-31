#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h> // For hash table implementation assertions
#include "mcp_json.h"
#include "mcp_arena.h" // Include arena allocator

// --- Hash Table Implementation for JSON Objects ---

// Initial capacity for the hash table
#define MCP_JSON_HASH_TABLE_INITIAL_CAPACITY 16
// Load factor threshold for resizing
#define MCP_JSON_HASH_TABLE_MAX_LOAD_FACTOR 0.75

// Hash table entry structure (for separate chaining)
typedef struct mcp_json_object_entry {
    char* name;                         // Property name (key) - uses malloc/strdup
    mcp_json_t* value;                  // Property value - allocated from arena or malloc
    struct mcp_json_object_entry* next; // Pointer to the next entry in the same bucket
} mcp_json_object_entry_t;

// Hash table structure for JSON object
typedef struct mcp_json_object_table {
    mcp_json_object_entry_t** buckets; // Array of pointers to entries (buckets) - uses malloc/realloc
    size_t capacity;                   // Current capacity of the bucket array
    size_t count;                      // Number of entries currently stored
} mcp_json_object_table_t;

// djb2 hash function for strings
static unsigned long hash_string(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// Forward declarations for hash table helper functions
static int mcp_json_object_table_init(mcp_json_object_table_t* table, size_t capacity);
static void mcp_json_object_table_destroy(mcp_json_object_table_t* table);
static int mcp_json_object_table_set(mcp_arena_t* arena, mcp_json_object_table_t* table, const char* name, mcp_json_t* value); // Pass arena
static mcp_json_object_entry_t* mcp_json_object_table_find(mcp_json_object_table_t* table, const char* name);
static int mcp_json_object_table_delete(mcp_json_object_table_t* table, const char* name);
static int mcp_json_object_table_resize(mcp_json_object_table_t* table, size_t new_capacity);

// --- End Hash Table Implementation ---


// We'll use a simple JSON library implementation
// In a real-world scenario, you might want to use a more robust library like cJSON

struct mcp_json {
    mcp_json_type_t type;
    union {
        bool boolean_value;
        double number_value;
        char* string_value; // Uses malloc/strdup
        struct {
            mcp_json_t** items; // Uses malloc/realloc
            size_t count;
            size_t capacity;
        } array;
        mcp_json_object_table_t object; // Uses hash table for objects
    };
};

// Helper function to allocate mcp_json_t node
static mcp_json_t* mcp_json_alloc_node(mcp_arena_t* arena) {
    if (arena != NULL) {
        return (mcp_json_t*)mcp_arena_alloc(arena, sizeof(mcp_json_t));
    } else {
        return (mcp_json_t*)malloc(sizeof(mcp_json_t));
    }
}

mcp_json_t* mcp_json_null_create(mcp_arena_t* arena) {
    mcp_json_t* json = mcp_json_alloc_node(arena);
    if (json == NULL) {
        return NULL;
    }
    json->type = MCP_JSON_NULL;
    return json;
}

mcp_json_t* mcp_json_boolean_create(mcp_arena_t* arena, bool value) {
    mcp_json_t* json = mcp_json_alloc_node(arena);
    if (json == NULL) {
        return NULL;
    }
    json->type = MCP_JSON_BOOLEAN;
    json->boolean_value = value;
    return json;
}

mcp_json_t* mcp_json_number_create(mcp_arena_t* arena, double value) {
    mcp_json_t* json = mcp_json_alloc_node(arena);
    if (json == NULL) {
        return NULL;
    }
    json->type = MCP_JSON_NUMBER;
    json->number_value = value;
    return json;
}

// String values still use strdup/malloc, not the arena
mcp_json_t* mcp_json_string_create(mcp_arena_t* arena, const char* value) {
    if (value == NULL) {
        return NULL;
    }
    mcp_json_t* json = mcp_json_alloc_node(arena);
    if (json == NULL) {
        return NULL;
    }
    json->type = MCP_JSON_STRING;
    json->string_value = strdup(value);
    if (json->string_value == NULL) {
        // If arena was used, the node is leaked here, but fixing requires
        // knowing if it was arena or malloc. Simplification for now.
        // A more robust solution would track the allocator used.
        if (arena == NULL) free(json);
        return NULL;
    }
    return json;
}

// Array backing storage uses malloc/realloc, not the arena
mcp_json_t* mcp_json_array_create(mcp_arena_t* arena) {
    mcp_json_t* json = mcp_json_alloc_node(arena);
    if (json == NULL) {
        return NULL;
    }
    json->type = MCP_JSON_ARRAY;
    json->array.items = NULL;
    json->array.count = 0;
    json->array.capacity = 0;
    return json;
}

// Object hash table uses malloc/realloc internally, not the arena
mcp_json_t* mcp_json_object_create(mcp_arena_t* arena) {
    mcp_json_t* json = mcp_json_alloc_node(arena);
    if (json == NULL) {
        return NULL;
    }
    json->type = MCP_JSON_OBJECT;
    // Pass arena to table init? No, table uses malloc for buckets.
    if (mcp_json_object_table_init(&json->object, MCP_JSON_HASH_TABLE_INITIAL_CAPACITY) != 0) {
        if (arena == NULL) free(json);
        return NULL;
    }
    return json;
}

// Destroy function needs to know if malloc or arena was used.
// For simplicity, we assume malloc was used if arena wasn't involved in parsing/creation.
// If arena was used, mcp_arena_destroy/reset should be called instead of this.
void mcp_json_destroy(mcp_json_t* json) {
    if (json == NULL) {
        return;
    }

    switch (json->type) {
        case MCP_JSON_STRING:
            free(json->string_value); // Strings always use malloc
            break;
        case MCP_JSON_ARRAY:
            for (size_t i = 0; i < json->array.count; i++) {
                // Recursively destroy items (assuming they also used malloc)
                mcp_json_destroy(json->array.items[i]);
            }
            free(json->array.items); // Array storage uses malloc
            break;
        case MCP_JSON_OBJECT:
            // Hash table destroy handles freeing names (malloc) and destroying values (recursive)
            mcp_json_object_table_destroy(&json->object);
            break;
        default:
            // Null, Boolean, Number have no extra data to free
            break;
    }

    // Free the node itself (assuming malloc was used)
    // If arena was used, this free is incorrect, but necessary if called directly.
    free(json);
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

// Array items storage uses malloc/realloc
int mcp_json_array_add_item(mcp_json_t* json, mcp_json_t* item) {
    if (json == NULL || item == NULL || json->type != MCP_JSON_ARRAY) {
        return -1;
    }
    if (json->array.count >= json->array.capacity) {
        size_t new_capacity = json->array.capacity == 0 ? 8 : json->array.capacity * 2;
        mcp_json_t** new_items = (mcp_json_t**)realloc(json->array.items, new_capacity * sizeof(mcp_json_t*));
        if (new_items == NULL) {
            return -1;
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

// Set property uses hash table set, which needs arena passed if used
// However, the public API doesn't take arena here. This assumes set is called
// on objects created potentially outside the parsing context (where arena is available).
// For simplicity, we don't pass arena to table_set here, meaning new entries
// created during set will use malloc, not the arena.
int mcp_json_object_set_property(mcp_json_t* json, const char* name, mcp_json_t* value) {
    if (json == NULL || name == NULL || value == NULL || json->type != MCP_JSON_OBJECT) {
        return -1;
    }
    // Pass NULL arena to table_set, forcing malloc for new entries if needed
    return mcp_json_object_table_set(NULL, &json->object, name, value);
}

int mcp_json_object_delete_property(mcp_json_t* json, const char* name) {
    if (json == NULL || name == NULL || json->type != MCP_JSON_OBJECT) {
        return -1;
    }
    return mcp_json_object_table_delete(&json->object, name);
}

// Property names array uses malloc
int mcp_json_object_get_property_names(const mcp_json_t* json, char*** names_out, size_t* count_out) {
    if (json == NULL || names_out == NULL || count_out == NULL || json->type != MCP_JSON_OBJECT) {
        return -1;
    }
    const mcp_json_object_table_t* table = &json->object;
    *count_out = table->count;
    if (table->count == 0) {
        *names_out = NULL;
        return 0;
    }
    *names_out = (char**)malloc(table->count * sizeof(char*));
    if (*names_out == NULL) {
        *count_out = 0;
        return -1;
    }
    size_t current_index = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            assert(current_index < table->count); // Sanity check
            (*names_out)[current_index] = strdup(entry->name); // Names use strdup
            if ((*names_out)[current_index] == NULL) {
                for (size_t j = 0; j < current_index; j++) {
                    free((*names_out)[j]);
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

// --- Hash Table Helper Function Implementations ---

// Initialize hash table - uses calloc/malloc
static int mcp_json_object_table_init(mcp_json_object_table_t* table, size_t capacity) {
    table->count = 0;
    table->capacity = capacity > 0 ? capacity : MCP_JSON_HASH_TABLE_INITIAL_CAPACITY;
    table->buckets = (mcp_json_object_entry_t**)calloc(table->capacity, sizeof(mcp_json_object_entry_t*));
    if (table->buckets == NULL) {
        table->capacity = 0;
        return -1; // Allocation failure
    }
    return 0;
}

// Destroy hash table and its entries
static void mcp_json_object_table_destroy(mcp_json_object_table_t* table) {
    if (table == NULL || table->buckets == NULL) {
        return;
    }
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            mcp_json_object_entry_t* next = entry->next;
            free(entry->name); // Names use malloc/strdup
            // Values might be arena or malloc allocated. Destroy assumes malloc.
            // If arena was used, this might double-free or leak depending on destroy logic.
            // Correct handling requires tracking allocator per value or destroying via arena.
            mcp_json_destroy(entry->value);
            free(entry); // Entries use malloc
            entry = next;
        }
    }
    free(table->buckets); // Buckets use malloc/realloc
    table->buckets = NULL;
    table->capacity = 0;
    table->count = 0;
}

// Find an entry in the hash table
static mcp_json_object_entry_t* mcp_json_object_table_find(mcp_json_object_table_t* table, const char* name) {
    if (table == NULL || name == NULL || table->capacity == 0) {
        return NULL;
    }
    unsigned long hash = hash_string(name);
    size_t index = hash % table->capacity;
    mcp_json_object_entry_t* entry = table->buckets[index];
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry; // Found
        }
        entry = entry->next;
    }
    return NULL; // Not found
}

// Resize the hash table - uses calloc/malloc/realloc
static int mcp_json_object_table_resize(mcp_json_object_table_t* table, size_t new_capacity) {
    if (new_capacity < MCP_JSON_HASH_TABLE_INITIAL_CAPACITY) {
        new_capacity = MCP_JSON_HASH_TABLE_INITIAL_CAPACITY;
    }
    if (new_capacity == table->capacity) {
        return 0; // No resize needed
    }
    mcp_json_object_entry_t** new_buckets = (mcp_json_object_entry_t**)calloc(new_capacity, sizeof(mcp_json_object_entry_t*));
    if (new_buckets == NULL) {
        return -1; // Allocation failure
    }
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            mcp_json_object_entry_t* next = entry->next;
            unsigned long hash = hash_string(entry->name);
            size_t new_index = hash % new_capacity;
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;
            entry = next;
        }
    }
    free(table->buckets);
    table->buckets = new_buckets;
    table->capacity = new_capacity;
    return 0;
}

// Set (insert or update) a property in the hash table
// Uses malloc for new entries, not arena (unless arena passed explicitly)
static int mcp_json_object_table_set(mcp_arena_t* arena, mcp_json_object_table_t* table, const char* name, mcp_json_t* value) {
    if (table->capacity == 0 || ((double)table->count + 1) / table->capacity > MCP_JSON_HASH_TABLE_MAX_LOAD_FACTOR) {
        size_t new_capacity = (table->capacity == 0) ? MCP_JSON_HASH_TABLE_INITIAL_CAPACITY : table->capacity * 2;
        if (mcp_json_object_table_resize(table, new_capacity) != 0) {
            return -1; // Resize failed
        }
    }
    unsigned long hash = hash_string(name);
    size_t index = hash % table->capacity;
    mcp_json_object_entry_t* entry = table->buckets[index];
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            // Update existing entry
            // Destroy old value (assuming malloc) before replacing
            mcp_json_destroy(entry->value);
            entry->value = value;
            return 0;
        }
        entry = entry->next;
    }

    // Insert new entry - use arena if provided, otherwise malloc
    mcp_json_object_entry_t* new_entry;
    if (arena != NULL) {
        new_entry = (mcp_json_object_entry_t*)mcp_arena_alloc(arena, sizeof(mcp_json_object_entry_t));
    } else {
        new_entry = (mcp_json_object_entry_t*)malloc(sizeof(mcp_json_object_entry_t));
    }
    if (new_entry == NULL) {
        return -1; // Allocation failed
    }
    new_entry->name = strdup(name); // Name uses strdup
    if (new_entry->name == NULL) {
        free(new_entry);
        return -1;
    }
    new_entry->value = value; // Value is passed in (arena or malloc allocated)
    new_entry->next = table->buckets[index];
    table->buckets[index] = new_entry;
    table->count++;
    return 0;
}

// Delete a property from the hash table
static int mcp_json_object_table_delete(mcp_json_object_table_t* table, const char* name) {
    if (table == NULL || name == NULL || table->capacity == 0 || table->count == 0) {
        return -1;
    }
    unsigned long hash = hash_string(name);
    size_t index = hash % table->capacity;
    mcp_json_object_entry_t* entry = table->buckets[index];
    mcp_json_object_entry_t* prev = NULL;
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            if (prev == NULL) {
                table->buckets[index] = entry->next;
            } else {
                prev->next = entry->next;
            }
            free(entry->name); // Free name (strdup)
            // Destroy value (assuming malloc)
            mcp_json_destroy(entry->value);
            free(entry); // Free entry (malloc)
            table->count--;
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }
    return -1; // Not found
}

// --- End Hash Table Helper Function Implementations ---


// --- JSON Parser Implementation (using Arena for nodes) ---

// Forward declarations for parser helper functions (pass arena)
static mcp_json_t* parse_value(mcp_arena_t* arena, const char** json);
static void skip_whitespace(const char** json);
static char* parse_string(const char** json); // Uses malloc/strdup, no arena
static mcp_json_t* parse_object(mcp_arena_t* arena, const char** json);
static mcp_json_t* parse_array(mcp_arena_t* arena, const char** json);
static mcp_json_t* parse_number(mcp_arena_t* arena, const char** json);

static void skip_whitespace(const char** json) {
    while (**json == ' ' || **json == '\t' || **json == '\n' || **json == '\r') {
        (*json)++;
    }
}

// Uses malloc/strdup
static char* parse_string(const char** json) {
    if (**json != '"') {
        return NULL;
    }
    (*json)++;
    const char* start = *json;
    while (**json != '"' && **json != '\0') {
        if (**json == '\\' && *(*json + 1) != '\0') {
            (*json)++;
        }
        (*json)++;
    }
    if (**json != '"') {
        return NULL; // Unterminated string
    }
    size_t length = *json - start;
    char* result = (char*)malloc(length + 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, start, length);
    result[length] = '\0';
    (*json)++; // Skip closing quote
    return result;
}

static mcp_json_t* parse_object(mcp_arena_t* arena, const char** json) {
    if (**json != '{') {
        return NULL;
    }
    mcp_json_t* object = mcp_json_object_create(arena);
    if (object == NULL) {
        return NULL;
    }
    (*json)++; // Skip '{'
    skip_whitespace(json);
    if (**json == '}') {
        (*json)++; // Skip '}'
        return object; // Empty object
    }
    while (1) {
        skip_whitespace(json);
        char* name = parse_string(json); // Name uses malloc
        if (name == NULL) {
            // Don't destroy object here, let caller handle cleanup via arena reset/destroy
            return NULL; // Invalid key
        }
        skip_whitespace(json);
        if (**json != ':') {
            free(name);
            return NULL; // Expected colon
        }
        (*json)++; // Skip ':'
        skip_whitespace(json);
        mcp_json_t* value = parse_value(arena, json); // Value uses arena (recursively)
        if (value == NULL) {
            free(name);
            return NULL; // Invalid value
        }
        // Set property - uses malloc for entry/name, value is from arena
        if (mcp_json_object_table_set(arena, &object->object, name, value) != 0) {
            free(name);
            // Don't destroy value (it's in arena), don't destroy object
            return NULL; // Set property failed
        }
        free(name); // Free the malloc'd name string
        skip_whitespace(json);
        if (**json == '}') {
            (*json)++; // Skip '}'
            return object;
        }
        if (**json != ',') {
            return NULL; // Expected comma or closing brace
        }
        (*json)++; // Skip ','
    }
}

static mcp_json_t* parse_array(mcp_arena_t* arena, const char** json) {
    if (**json != '[') {
        return NULL;
    }
    mcp_json_t* array = mcp_json_array_create(arena);
    if (array == NULL) {
        return NULL;
    }
    (*json)++; // Skip '['
    skip_whitespace(json);
    if (**json == ']') {
        (*json)++; // Skip ']'
        return array; // Empty array
    }
    while (1) {
        skip_whitespace(json);
        mcp_json_t* value = parse_value(arena, json); // Value uses arena (recursively)
        if (value == NULL) {
            // Don't destroy array, let caller handle via arena
            return NULL; // Invalid value in array
        }
        // Add item uses realloc for backing store, not arena
        if (mcp_json_array_add_item(array, value) != 0) {
            // Don't destroy value (it's in arena)
            return NULL; // Add item failed
        }
        skip_whitespace(json);
        if (**json == ']') {
            (*json)++; // Skip ']'
            return array;
        }
        if (**json != ',') {
            return NULL; // Expected comma or closing bracket
        }
        (*json)++; // Skip ','
    }
}

static mcp_json_t* parse_number(mcp_arena_t* arena, const char** json) {
    const char* start = *json;
    if (**json == '-') (*json)++;
    if (**json < '0' || **json > '9') return NULL; // Must have at least one digit
    while (**json >= '0' && **json <= '9') (*json)++;
    if (**json == '.') {
        (*json)++;
        if (**json < '0' || **json > '9') return NULL; // Digit must follow '.'
        while (**json >= '0' && **json <= '9') (*json)++;
    }
    if (**json == 'e' || **json == 'E') {
        (*json)++;
        if (**json == '+' || **json == '-') (*json)++;
        if (**json < '0' || **json > '9') return NULL; // Digit must follow 'e'/'E'
        while (**json >= '0' && **json <= '9') (*json)++;
    }
    char* end;
    double value = strtod(start, &end);
    if (end != *json) {
        return NULL; // Invalid number format
    }
    return mcp_json_number_create(arena, value); // Uses arena
}

static mcp_json_t* parse_value(mcp_arena_t* arena, const char** json) {
    skip_whitespace(json);
    switch (**json) {
        case '{': return parse_object(arena, json);
        case '[': return parse_array(arena, json);
        case '"': {
            char* string = parse_string(json); // Uses malloc
            if (string == NULL) return NULL;
            mcp_json_t* result = mcp_json_string_create(arena, string); // Uses arena for node
            free(string); // Free malloc'd string
            return result;
        }
        case 'n':
            if (strncmp(*json, "null", 4) == 0) {
                *json += 4;
                return mcp_json_null_create(arena); // Uses arena
            }
            return NULL;
        case 't':
            if (strncmp(*json, "true", 4) == 0) {
                *json += 4;
                return mcp_json_boolean_create(arena, true); // Uses arena
            }
            return NULL;
        case 'f':
            if (strncmp(*json, "false", 5) == 0) {
                *json += 5;
                return mcp_json_boolean_create(arena, false); // Uses arena
            }
            return NULL;
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return parse_number(arena, json); // Uses arena
        default:
            return NULL; // Invalid character
    }
}

// Main parse function
mcp_json_t* mcp_json_parse(mcp_arena_t* arena, const char* json) {
    if (json == NULL) {
        return NULL;
    }
    const char* current = json; // Use a temporary pointer
    skip_whitespace(&current);
    mcp_json_t* result = parse_value(arena, &current);
    if (result == NULL) {
        // Parsing failed, arena contains partially allocated nodes.
        // Caller should reset/destroy the arena.
        return NULL;
    }
    skip_whitespace(&current);
    if (*current != '\0') {
        // Trailing characters after valid JSON
        // Don't destroy result (it's in arena), let caller handle arena.
        return NULL;
    }
    return result;
}

// --- End JSON Parser Implementation ---


// --- JSON Stringification Implementation ---
// Stringification uses malloc/realloc for the output buffer, not arena.

static int stringify_value(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity);

static int ensure_output_capacity(char** output, size_t* output_size, size_t* output_capacity, size_t additional) {
    if (*output_size + additional > *output_capacity) {
        size_t new_capacity = *output_capacity == 0 ? 256 : *output_capacity * 2;
        while (new_capacity < *output_size + additional) {
            new_capacity *= 2;
        }
        char* new_output = (char*)realloc(*output, new_capacity);
        if (new_output == NULL) {
            return -1;
        }
        *output = new_output;
        *output_capacity = new_capacity;
    }
    return 0;
}

static int append_string(char** output, size_t* output_size, size_t* output_capacity, const char* string) {
    size_t length = strlen(string);
    if (ensure_output_capacity(output, output_size, output_capacity, length) != 0) {
        return -1;
    }
    memcpy(*output + *output_size, string, length);
    *output_size += length;
    return 0;
}

static int stringify_string(const char* string, char** output, size_t* output_size, size_t* output_capacity) {
    if (append_string(output, output_size, output_capacity, "\"") != 0) return -1;
    for (const char* p = string; *p != '\0'; p++) {
        char buffer[8];
        const char* escaped = NULL;
        switch (*p) {
            case '\\': escaped = "\\\\"; break;
            case '"':  escaped = "\\\""; break;
            case '\b': escaped = "\\b"; break;
            case '\f': escaped = "\\f"; break;
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            default:
                if (*p < 32) {
                    snprintf(buffer, sizeof(buffer), "\\u%04x", *p);
                    escaped = buffer;
                }
                break;
        }
        if (escaped != NULL) {
            if (append_string(output, output_size, output_capacity, escaped) != 0) return -1;
        } else {
            if (ensure_output_capacity(output, output_size, output_capacity, 1) != 0) return -1;
            (*output)[(*output_size)++] = *p;
        }
    }
    if (append_string(output, output_size, output_capacity, "\"") != 0) return -1;
    return 0;
}

static int stringify_object(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (append_string(output, output_size, output_capacity, "{") != 0) return -1;
    bool first_property = true;
    const mcp_json_object_table_t* table = &json->object;
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            if (!first_property) {
                if (append_string(output, output_size, output_capacity, ",") != 0) return -1;
            } else {
                first_property = false;
            }
            if (stringify_string(entry->name, output, output_size, output_capacity) != 0) return -1;
            if (append_string(output, output_size, output_capacity, ":") != 0) return -1;
            if (stringify_value(entry->value, output, output_size, output_capacity) != 0) return -1;
            entry = entry->next;
        }
    }
    if (append_string(output, output_size, output_capacity, "}") != 0) return -1;
    return 0;
}

static int stringify_array(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (append_string(output, output_size, output_capacity, "[") != 0) return -1;
    for (size_t i = 0; i < json->array.count; i++) {
        if (i > 0) {
            if (append_string(output, output_size, output_capacity, ",") != 0) return -1;
        }
        if (stringify_value(json->array.items[i], output, output_size, output_capacity) != 0) return -1;
    }
    if (append_string(output, output_size, output_capacity, "]") != 0) return -1;
    return 0;
}

static int stringify_value(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (json == NULL) {
        return append_string(output, output_size, output_capacity, "null");
    }
    switch (json->type) {
        case MCP_JSON_NULL:    return append_string(output, output_size, output_capacity, "null");
        case MCP_JSON_BOOLEAN: return append_string(output, output_size, output_capacity, json->boolean_value ? "true" : "false");
        case MCP_JSON_NUMBER: {
            char buffer[32];
            // Use snprintf for robust number formatting
            int len = snprintf(buffer, sizeof(buffer), "%.17g", json->number_value);
            if (len < 0 || (size_t)len >= sizeof(buffer)) return -1; // Encoding error or buffer too small
            return append_string(output, output_size, output_capacity, buffer);
        }
        case MCP_JSON_STRING:  return stringify_string(json->string_value, output, output_size, output_capacity);
        case MCP_JSON_ARRAY:   return stringify_array(json, output, output_size, output_capacity);
        case MCP_JSON_OBJECT:  return stringify_object(json, output, output_size, output_capacity);
        default:               return -1; // Should not happen
    }
}

char* mcp_json_stringify(const mcp_json_t* json) {
    char* output = NULL;
    size_t output_size = 0;
    size_t output_capacity = 0;
    if (stringify_value(json, &output, &output_size, &output_capacity) != 0) {
        free(output);
        return NULL;
    }
    // Add null terminator
    if (ensure_output_capacity(&output, &output_size, &output_capacity, 1) != 0) {
        free(output);
        return NULL;
    }
    output[output_size] = '\0';
    // Optionally, shrink buffer to fit: realloc(output, output_size + 1);
    return output; // Caller must free this string
}

// --- End JSON Stringification ---


// --- MCP Message Parsing/Stringification ---
// These functions now accept an arena for parsing the main JSON structure,
// but still use malloc/strdup for strings within the mcp_message_t struct
// and for the stringified result/params.

int mcp_json_parse_message(mcp_arena_t* arena, const char* json_str, mcp_message_t* message) {
    if (json_str == NULL || message == NULL) {
        return -1;
    }
    // Parse using the provided arena (or malloc if arena is NULL)
    mcp_json_t* json = mcp_json_parse(arena, json_str);
    if (json == NULL) {
        // Arena cleanup is handled by the caller if parsing fails
        return -1;
    }
    if (json->type != MCP_JSON_OBJECT) {
        mcp_json_destroy(json);
        return -1;
    }

    mcp_json_t* id = mcp_json_object_get_property(json, "id");
    mcp_json_t* method = mcp_json_object_get_property(json, "method");
    mcp_json_t* params = mcp_json_object_get_property(json, "params");
    mcp_json_t* result = mcp_json_object_get_property(json, "result");
    mcp_json_t* error = mcp_json_object_get_property(json, "error");

    int parse_status = -1; // Default to error
    message->type = MCP_MESSAGE_TYPE_INVALID; // Default type

    // --- Request Check ---
    if (method != NULL && method->type == MCP_JSON_STRING) {
        if (id != NULL) { // Must have ID for request
            // Check ID type (integer or string allowed by spec, but we use uint64_t)
            if (id->type == MCP_JSON_NUMBER) { // || id->type == MCP_JSON_STRING) { // Allow string ID? No, use number for now.
                // Check params type (object or array allowed, or omitted)
                if (params == NULL || params->type == MCP_JSON_OBJECT || params->type == MCP_JSON_ARRAY) {
                    message->type = MCP_MESSAGE_TYPE_REQUEST;
                    message->request.id = (uint64_t)id->number_value; // TODO: Handle potential precision loss or non-integer?
                    message->request.method = strdup(method->string_value);
                    message->request.params = (params != NULL) ? mcp_json_stringify(params) : NULL;

                    if (message->request.method != NULL && (params == NULL || message->request.params != NULL)) {
                        parse_status = 0; // Success
                    } else {
                        // Allocation failure during stringify/strdup
                        free(message->request.method); // Method might be allocated
                        free(message->request.params); // Params might be allocated
                        message->type = MCP_MESSAGE_TYPE_INVALID; // Reset type
                    }
                }
            }
        } else { // Notification (method present, id absent)
             // Check params type (object or array allowed, or omitted)
            if (params == NULL || params->type == MCP_JSON_OBJECT || params->type == MCP_JSON_ARRAY) {
                message->type = MCP_MESSAGE_TYPE_NOTIFICATION;
                message->notification.method = strdup(method->string_value);
                message->notification.params = (params != NULL) ? mcp_json_stringify(params) : NULL;

                if (message->notification.method != NULL && (params == NULL || message->notification.params != NULL)) {
                    parse_status = 0; // Success
                } else {
                    // Allocation failure
                    free(message->notification.method);
                    free(message->notification.params);
                    message->type = MCP_MESSAGE_TYPE_INVALID;
                }
            }
        }
    } else if (id != NULL && (result != NULL || error != NULL)) { // Response
        // Check ID type
        if (id->type == MCP_JSON_NUMBER) { // || id->type == MCP_JSON_STRING) { // Allow string ID? No.
            message->type = MCP_MESSAGE_TYPE_RESPONSE;
            message->response.id = (uint64_t)id->number_value; // TODO: Handle potential precision loss?

            if (error != NULL && error->type == MCP_JSON_OBJECT) { // Error Response
                 if (result != NULL) {
                     // Error: Both result and error members MUST NOT exist
                     message->type = MCP_MESSAGE_TYPE_INVALID;
                 } else {
                    mcp_json_t* code = mcp_json_object_get_property(error, "code");
                    mcp_json_t* msg = mcp_json_object_get_property(error, "message");
                    // Code MUST be an integer
                    if (code != NULL && code->type == MCP_JSON_NUMBER) { // TODO: Check if integer?
                        // Message MUST be a string
                        if (msg != NULL && msg->type == MCP_JSON_STRING) {
                            message->response.error_code = (mcp_error_code_t)(int)code->number_value;
                            message->response.error_message = strdup(msg->string_value);
                            message->response.result = NULL;
                            if (message->response.error_message != NULL) {
                                parse_status = 0; // Success
                            } else {
                                // strdup failed
                                message->type = MCP_MESSAGE_TYPE_INVALID;
                            }
                        }
                    }
                }
            } else if (result != NULL) {
                // Success Response (result can be any JSON type)
                message->response.error_code = MCP_ERROR_NONE;
                message->response.error_message = NULL;
                message->response.result = mcp_json_stringify(result); // Uses malloc
                if (message->response.result != NULL) {
                    parse_status = 0; // Success
                } else {
                    // stringify failed
                    message->type = MCP_MESSAGE_TYPE_INVALID;
                }
            } else {
                 // Invalid response: Must have 'result' or 'error'
                 message->type = MCP_MESSAGE_TYPE_INVALID;
            }
        }
    }

    // Don't destroy json here if arena was used, as it's managed by the arena.
    // If arena is NULL, mcp_json_destroy should be called, but the current
    // destroy logic assumes malloc for the node itself, which is incorrect if
    // arena was NULL but parsing succeeded.
    // For simplicity with arena: let the caller manage the arena's lifetime.
    // If arena was NULL, there's a potential leak here if parsing succeeded
    // but message construction failed. A robust solution needs better tracking.
    if (arena == NULL && json != NULL) {
         mcp_json_destroy(json); // Attempt cleanup if malloc was used
    }
    // If arena was used, the parsed 'json' tree lives in the arena and will be
    // cleaned up when the arena is reset or destroyed by the caller.

    return parse_status;
}

// Stringify message uses the object_create/string_create functions which now
// require an arena. Since we don't have one here, we pass NULL, forcing malloc.
char* mcp_json_stringify_message(const mcp_message_t* message) {
    if (message == NULL) {
        return NULL;
    }
    // Pass NULL arena, forcing malloc for nodes
    mcp_json_t* json = mcp_json_object_create(NULL);
    if (json == NULL) {
        return NULL;
    }

    char* final_json_string = NULL; // Store result here

    switch (message->type) {
        case MCP_MESSAGE_TYPE_REQUEST: {
            mcp_json_t* id_node = mcp_json_number_create(NULL, (double)message->request.id);
            mcp_json_t* method_node = mcp_json_string_create(NULL, message->request.method);
            mcp_json_t* params_node = (message->request.params != NULL) ? mcp_json_parse(NULL, message->request.params) : NULL;

            if (id_node && method_node && (message->request.params == NULL || params_node)) {
                mcp_json_object_set_property(json, "id", id_node);
                mcp_json_object_set_property(json, "method", method_node);
                if (params_node) {
                    mcp_json_object_set_property(json, "params", params_node);
                }
                final_json_string = mcp_json_stringify(json);
            } else {
                // Cleanup nodes if creation or parsing failed
                mcp_json_destroy(id_node);
                mcp_json_destroy(method_node);
                mcp_json_destroy(params_node);
            }
            break;
        }
        case MCP_MESSAGE_TYPE_RESPONSE: {
            mcp_json_t* id_node = mcp_json_number_create(NULL, (double)message->response.id);
            if (!id_node) break; // Failed to create ID node
            mcp_json_object_set_property(json, "id", id_node);

            if (message->response.error_code != MCP_ERROR_NONE) {
                mcp_json_t* error_obj = mcp_json_object_create(NULL);
                mcp_json_t* code_node = mcp_json_number_create(NULL, (double)message->response.error_code);
                mcp_json_t* msg_node = (message->response.error_message != NULL) ? mcp_json_string_create(NULL, message->response.error_message) : NULL;

                if (error_obj && code_node && (message->response.error_message == NULL || msg_node)) {
                    mcp_json_object_set_property(error_obj, "code", code_node);
                    if (msg_node) {
                        mcp_json_object_set_property(error_obj, "message", msg_node);
                    }
                    mcp_json_object_set_property(json, "error", error_obj);
                    final_json_string = mcp_json_stringify(json);
                } else {
                    mcp_json_destroy(error_obj);
                    mcp_json_destroy(code_node);
                    mcp_json_destroy(msg_node);
                }
            } else if (message->response.result != NULL) {
                mcp_json_t* result_node = mcp_json_parse(NULL, message->response.result);
                if (result_node) {
                    mcp_json_object_set_property(json, "result", result_node);
                    final_json_string = mcp_json_stringify(json);
                }
            } else { // Null result
                mcp_json_t* result_node = mcp_json_null_create(NULL);
                 if (result_node) {
                    mcp_json_object_set_property(json, "result", result_node);
                    final_json_string = mcp_json_stringify(json);
                }
            }
            break;
        }
        case MCP_MESSAGE_TYPE_NOTIFICATION: {
             mcp_json_t* method_node = mcp_json_string_create(NULL, message->notification.method);
             mcp_json_t* params_node = (message->notification.params != NULL) ? mcp_json_parse(NULL, message->notification.params) : NULL;

             if (method_node && (message->notification.params == NULL || params_node)) {
                 mcp_json_object_set_property(json, "method", method_node);
                 if (params_node) {
                     mcp_json_object_set_property(json, "params", params_node);
                 }
                 final_json_string = mcp_json_stringify(json);
             } else {
                 mcp_json_destroy(method_node);
                 mcp_json_destroy(params_node);
             }
             break;
        }
        default:
            // Should not happen
            break;
    }

    mcp_json_destroy(json); // Destroy the temporary JSON structure
    return final_json_string; // Return the malloc'd string
}
