#include "mcp_json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

// --- Hash Table Implementation for JSON Objects ---
// IMPORTANT: This internal implementation uses malloc/free/mcp_strdup/realloc
//            for its own structures (buckets, entries, keys). Only the mcp_json_t *value* nodes
//            stored in the table are allocated using the thread-local arena.

/**
 * @internal
 * @brief Simple djb2 hash function for strings.
 * @param str Null-terminated string to hash.
 * @return Unsigned long hash value.
 */
static unsigned long hash_string(const char* str) {
    unsigned long hash = 5381; // Initial magic value
    int c;
    // Iterate through the string, updating the hash
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

/** @internal Resizes the hash table's bucket array and rehashes existing entries. Uses malloc/realloc/calloc. */
static int mcp_json_object_table_resize(mcp_json_object_table_t* table, size_t new_capacity) {
    if (new_capacity < MCP_JSON_HASH_TABLE_INITIAL_CAPACITY) {
        new_capacity = MCP_JSON_HASH_TABLE_INITIAL_CAPACITY;
    }
    if (new_capacity == table->capacity) {
        return 0; // No resize needed
    }

    // Allocate new bucket array
    mcp_json_object_entry_t** new_buckets = (mcp_json_object_entry_t**)calloc(new_capacity, sizeof(mcp_json_object_entry_t*));
    if (new_buckets == NULL) {
        return -1; // Allocation failure
    }

    // Rehash all existing entries into the new buckets
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            mcp_json_object_entry_t* next = entry->next; // Store next entry
            // Calculate new index
            unsigned long hash = hash_string(entry->name);
            size_t new_index = hash % new_capacity;
            // Insert entry at the head of the new bucket's list
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;
            entry = next; // Move to the next entry in the old bucket
        }
    }

    // Free the old bucket array and update table properties
    free(table->buckets);
    table->buckets = new_buckets;
    table->capacity = new_capacity;
    return 0;
}


// --- Internal Hash Table Helper Function Implementations ---

/** @internal Initializes the hash table buckets. Uses calloc. */
int mcp_json_object_table_init(mcp_json_object_table_t* table, size_t capacity) {
    table->count = 0;
    table->capacity = capacity > 0 ? capacity : MCP_JSON_HASH_TABLE_INITIAL_CAPACITY;
    // Allocate the bucket array (array of pointers)
    table->buckets = (mcp_json_object_entry_t**)calloc(table->capacity, sizeof(mcp_json_object_entry_t*));
    if (table->buckets == NULL) {
        table->capacity = 0;
        return -1; // Allocation failure
    }
    return 0;
}

/** @internal Frees all memory associated with the hash table. */
void mcp_json_object_table_destroy(mcp_json_object_table_t* table) {
    if (table == NULL || table->buckets == NULL) {
        return;
    }
    // Iterate through each bucket
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        // Iterate through the linked list in the bucket
        while (entry != NULL) {
            mcp_json_object_entry_t* next = entry->next;
            free(entry->name); // Free the duplicated key string
            // Recursively destroy the value node's internal data.
            // IMPORTANT: This assumes the value node itself will be freed elsewhere
            // (either by the caller via free() or by the arena).
            mcp_json_destroy(entry->value);
            // Free the entry structure itself (always allocated with malloc by table_set)
            free(entry);
            entry = next;
        }
    }
    // Free the bucket array itself
    free(table->buckets);
    table->buckets = NULL;
    table->capacity = 0;
    table->count = 0;
}

/** @internal Finds an entry in the hash table by name. */
mcp_json_object_entry_t* mcp_json_object_table_find(mcp_json_object_table_t* table, const char* name) {
    if (table == NULL || name == NULL || table->capacity == 0) {
        return NULL;
    }
    // Calculate hash and initial bucket index
    unsigned long hash = hash_string(name);
    size_t index = hash % table->capacity;
    // Search the linked list at the bucket index
    mcp_json_object_entry_t* entry = table->buckets[index];
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry; // Found
        }
        entry = entry->next;
    }
    return NULL; // Not found
}

/**
 * @internal
 * @brief Inserts or updates a key-value pair in the hash table.
 * Handles resizing if the load factor is exceeded.
 * Destroys the old value if the key already exists.
 * Allocates new entries and duplicates keys using malloc/mcp_strdup.
 * @param table The hash table.
 * @param name The property name (key).
 * @param value The JSON value node (ownership transferred to table logic via mcp_json_destroy).
 * @return 0 on success, -1 on failure.
 */
int mcp_json_object_table_set(mcp_json_object_table_t* table, const char* name, mcp_json_t* value) {
    // Resize if load factor is too high
    if (table->capacity == 0 || ((double)table->count + 1) / table->capacity > MCP_JSON_HASH_TABLE_MAX_LOAD_FACTOR) {
        size_t new_capacity = (table->capacity == 0) ? MCP_JSON_HASH_TABLE_INITIAL_CAPACITY : table->capacity * 2;
        if (mcp_json_object_table_resize(table, new_capacity) != 0) {
            return -1; // Resize failed
        }
    }

    // Find the bucket and check if the key already exists
    unsigned long hash = hash_string(name);
    size_t index = hash % table->capacity;
    mcp_json_object_entry_t* entry = table->buckets[index];
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            // Key exists: Update value
            // Destroy the old value's internal data first
            mcp_json_destroy(entry->value);
            // Let's assume mcp_json_destroy handles internal freeing, and node freeing is caller's job.
            entry->value = value; // Assign the new value pointer
            return 0; // Update successful
        }
        entry = entry->next;
    }

    // Key doesn't exist: Insert new entry
    // Allocate the entry structure itself using malloc
    mcp_json_object_entry_t* new_entry = (mcp_json_object_entry_t*)malloc(sizeof(mcp_json_object_entry_t));
    if (new_entry == NULL) {
         return -1; // Allocation failed
     }
     // Duplicate the key name using our helper (malloc)
     new_entry->name = mcp_strdup(name);
     if (new_entry->name == NULL) {
         free(new_entry); // Free the partially allocated entry
         return -1; // mcp_strdup failed
    }
    new_entry->value = value; // Store the pointer to the value node
    // Insert at the head of the bucket's linked list
    new_entry->next = table->buckets[index];
    table->buckets[index] = new_entry;
    table->count++;
    return 0; // Insert successful
}

/**
 * @internal
 * @brief Deletes a key-value pair from the hash table.
 * Frees the entry structure, the duplicated key name, and calls mcp_json_destroy on the value.
 * @param table The hash table.
 * @param name The key name to delete.
 * @return 0 on success, -1 if key not found or error.
 */
int mcp_json_object_table_delete(mcp_json_object_table_t* table, const char* name) {
    if (table == NULL || name == NULL || table->capacity == 0 || table->count == 0) {
        return -1; // Invalid input or empty table
    }
    // Find the bucket
    unsigned long hash = hash_string(name);
    size_t index = hash % table->capacity;
    mcp_json_object_entry_t* entry = table->buckets[index];
    mcp_json_object_entry_t* prev = NULL;

    // Search the linked list in the bucket
    while (entry != NULL) {
        if (strcmp(entry->name, name) == 0) {
            // Found the entry, remove it from the list
            if (prev == NULL) { // Entry is the head of the list
                table->buckets[index] = entry->next;
            } else {
                prev->next = entry->next;
            }
            // Free allocated resources for the entry
            free(entry->name);          // Free the duplicated key string
            mcp_json_destroy(entry->value); // Free internal data of the value node
            // Note: We don't free the value node itself here, assuming caller/arena handles it.
            free(entry);                // Free the entry structure itself
            table->count--;
            return 0; // Deletion successful
        }
        prev = entry;
        entry = entry->next;
    }
    return -1; // Key not found
}
