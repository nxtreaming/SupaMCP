#include "mcp_hashtable.h"
#include "mcp_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Helper function to check if a number is a power of 2
static bool is_power_of_two(size_t n) {
    return (n != 0) && ((n & (n - 1)) == 0);
}

// Helper function to get the next power of 2
static size_t next_power_of_two(size_t n) {
    if (n == 0) return 1;
    if (is_power_of_two(n)) return n;

    size_t power = 1;
    while (power < n) {
        power <<= 1;
        // Check for overflow
        if (power == 0) return n; // Return original if overflow
    }
    return power;
}

// Helper function to resize the hash table
static int mcp_hashtable_resize(mcp_hashtable_t* table, size_t new_capacity) {
    if (!table || new_capacity <= table->capacity) {
        return -1;
    }

    // Allocate new buckets array
    mcp_hashtable_entry_t** new_buckets = (mcp_hashtable_entry_t**)calloc(
        new_capacity, sizeof(mcp_hashtable_entry_t*));
    if (!new_buckets) {
        return -1;
    }

    // Rehash all entries
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_hashtable_entry_t* entry = table->buckets[i];
        while (entry) {
            mcp_hashtable_entry_t* next_entry = entry->next; // Store next before modifying entry

            // Calculate new bucket index using bitwise AND for power-of-2 capacity
            size_t new_index = table->hash_func(entry->key) & (new_capacity - 1);

            // Insert at the head of the new bucket
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next_entry; // Move to the stored next
        }
    }

    // Free old buckets array and update table
    free(table->buckets);
    table->buckets = new_buckets;
    table->capacity = new_capacity;

    return 0;
}

mcp_hashtable_t* mcp_hashtable_create(
    size_t initial_capacity,
    float load_factor_threshold,
    mcp_hash_func_t hash_func,
    mcp_key_compare_func_t key_compare,
    mcp_key_dup_func_t key_dup,
    mcp_key_free_func_t key_free,
    mcp_value_free_func_t value_free
) {
    if (!hash_func || !key_compare || !key_dup || !key_free) {
        return NULL;
    }

    // Ensure initial capacity is a power of 2
    if (initial_capacity == 0) {
        initial_capacity = 16; // Default initial capacity
    } else if (!is_power_of_two(initial_capacity)) {
        initial_capacity = next_power_of_two(initial_capacity);
    }

    // Validate load factor threshold
    if (load_factor_threshold <= 0.0f || load_factor_threshold >= 1.0f) {
        load_factor_threshold = 0.75f; // Default load factor threshold
    }

    // Allocate hash table structure
    mcp_hashtable_t* table = (mcp_hashtable_t*)malloc(sizeof(mcp_hashtable_t));
    if (!table) {
        return NULL;
    }

    // Allocate buckets array
    table->buckets = (mcp_hashtable_entry_t**)calloc(
        initial_capacity, sizeof(mcp_hashtable_entry_t*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    // Initialize hash table
    table->capacity = initial_capacity;
    table->size = 0;
    table->load_factor_threshold = load_factor_threshold;
    table->hash_func = hash_func;
    table->key_compare = key_compare;
    table->key_dup = key_dup;
    table->key_free = key_free;
    table->value_free = value_free;

    return table;
}

void mcp_hashtable_destroy(mcp_hashtable_t* table) {
    if (!table) {
        return;
    }

    // Free all entries
    mcp_hashtable_clear(table);

    // Free buckets array and table structure
    free(table->buckets);
    free(table);
}

int mcp_hashtable_put(mcp_hashtable_t* table, const void* key, void* value) {
    if (!table || !key) {
        return -1;
    }

    // --- Check if resize is needed ---
    if ((float)(table->size + 1) / table->capacity > table->load_factor_threshold) {
        if (mcp_hashtable_resize(table, table->capacity * 2) != 0) {
            return -1;
        }
    }


    // --- Calculate bucket index ---
    size_t index = table->hash_func(key) & (table->capacity - 1);


    // --- Check if key already exists ---
    mcp_hashtable_entry_t* entry = table->buckets[index];
    mcp_hashtable_entry_t* prev = NULL;

    while (entry) {
        if (table->key_compare(entry->key, key)) {
            // Key exists, update value
            if (table->value_free) {
                table->value_free(entry->value);
            }
            entry->value = value;
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }


    // --- Key doesn't exist, create and insert new entry ---
    mcp_hashtable_entry_t* new_entry = (mcp_hashtable_entry_t*)malloc(
        sizeof(mcp_hashtable_entry_t));
    if (!new_entry) {
        return -1;
    }

    // Duplicate key
    new_entry->key = table->key_dup(key);
    if (!new_entry->key) {
        free(new_entry);
        return -1;
    }

    // Set value and next pointer
    new_entry->value = value;
    new_entry->next = NULL;

    // Add to bucket
    if (prev) {
        prev->next = new_entry;
    } else {
        table->buckets[index] = new_entry;
    }

    // Increment size
    table->size++;

    return 0;
}

int mcp_hashtable_get(mcp_hashtable_t* table, const void* key, void** value_ptr) {
    if (!table || !key || !value_ptr) {
        return -1;
    }

    // Calculate bucket index using bitwise AND for power-of-2 capacity
    size_t index = table->hash_func(key) & (table->capacity - 1);

    // Search for key
    mcp_hashtable_entry_t* entry = table->buckets[index];
    while (entry) {
        if (table->key_compare(entry->key, key)) {
            *value_ptr = entry->value;
            return 0;
        }
        entry = entry->next;
    }

    // Key not found
    return -1;
}

int mcp_hashtable_remove(mcp_hashtable_t* table, const void* key) {
    if (!table || !key) {
        return -1;
    }

    // --- Calculate bucket index ---
    size_t index = table->hash_func(key) & (table->capacity - 1);

    // --- Search for key ---
    mcp_hashtable_entry_t* entry = table->buckets[index];
    mcp_hashtable_entry_t* prev = NULL;

    while (entry) {
        if (table->key_compare(entry->key, key)) {
            // --- Key found, remove entry ---

            // Update linked list pointers
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[index] = entry->next;
            }

            // Free key and value data
            table->key_free(entry->key);
            if (table->value_free) {
                table->value_free(entry->value);
            }

            // Free entry struct and decrement size
            free(entry);
            table->size--;

            return 0;
        }
        prev = entry;
        entry = entry->next;
    }

    // Key not found
    return -1;
}

bool mcp_hashtable_contains(mcp_hashtable_t* table, const void* key) {
    if (!table || !key) {
        return false;
    }

    // Calculate bucket index using bitwise AND for power-of-2 capacity
    size_t index = table->hash_func(key) & (table->capacity - 1);

    // Search for key
    mcp_hashtable_entry_t* entry = table->buckets[index];
    while (entry) {
        if (table->key_compare(entry->key, key)) {
            return true;
        }
        entry = entry->next;
    }

    // Key not found
    return false;
}

size_t mcp_hashtable_size(const mcp_hashtable_t* table) {
    return table ? table->size : 0;
}

void mcp_hashtable_clear(mcp_hashtable_t* table) {
    if (!table) {
        return;
    }

    // Free all entries
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_hashtable_entry_t* entry = table->buckets[i];
        while (entry) {
            mcp_hashtable_entry_t* next = entry->next;

            // Free key and value
            table->key_free(entry->key);
            if (table->value_free) {
                table->value_free(entry->value);
            }

            // Free entry
            free(entry);
            entry = next;
        }
        table->buckets[i] = NULL;
    }

    // Reset size
    table->size = 0;
}

void mcp_hashtable_foreach(
    mcp_hashtable_t* table,
    void (*callback)(const void* key, void* value, void* user_data),
    void* user_data
) {
    if (!table || !callback) {
        return;
    }

    // Iterate through all buckets
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_hashtable_entry_t* entry = table->buckets[i];
        while (entry) {
            callback(entry->key, entry->value, user_data);
            entry = entry->next;
        }
    }
}

// String hash function (djb2)
unsigned long mcp_hashtable_string_hash(const void* key) {
    const char* str = (const char*)key;
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
}

// String comparison function
bool mcp_hashtable_string_compare(const void* key1, const void* key2) {
    return strcmp((const char*)key1, (const char*)key2) == 0;
}

// String duplication function
void* mcp_hashtable_string_dup(const void* key) {
    return mcp_strdup((const char*)key);
}

// String free function
void mcp_hashtable_string_free(void* key) {
    free(key);
}

// Integer hash function
unsigned long mcp_hashtable_int_hash(const void* key) {
    // FNV-1a hash for integers
    unsigned long hash = 2166136261UL; // FNV offset basis
    const unsigned char* bytes = (const unsigned char*)key;
    size_t size = sizeof(int);

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 16777619UL; // FNV prime
    }

    return hash;
}

// Integer comparison function
bool mcp_hashtable_int_compare(const void* key1, const void* key2) {
    return *(const int*)key1 == *(const int*)key2;
}

// Integer duplication function
void* mcp_hashtable_int_dup(const void* key) {
    int* dup = (int*)malloc(sizeof(int));
    if (dup) {
        *dup = *(const int*)key;
    }
    return dup;
}

// Integer free function
void mcp_hashtable_int_free(void* key) {
    free(key);
}

// Pointer hash function
unsigned long mcp_hashtable_ptr_hash(const void* key) {
    // Simple hash for pointer values
    // Use XOR of upper and lower bits to avoid truncation on 64-bit systems
    uintptr_t ptr_val = (uintptr_t)key;
    unsigned long hash = (unsigned long)(ptr_val & 0xFFFFFFFFUL);

    // On 64-bit systems, also include the upper bits
    #if UINTPTR_MAX > 0xFFFFFFFFUL
    hash ^= (unsigned long)((ptr_val >> 32) & 0xFFFFFFFFUL);
    #endif

    return hash;
}

// Pointer comparison function
bool mcp_hashtable_ptr_compare(const void* key1, const void* key2) {
    return key1 == key2;
}

// No-op duplication function
void* mcp_hashtable_ptr_dup(const void* key) {
    return (void*)key;
}

// No-op free function
void mcp_hashtable_ptr_free(void* key) {
    (void)key; // Unused parameter
}
