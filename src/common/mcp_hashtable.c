#include "mcp_hashtable.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_memory_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Helper function to allocate memory from pool if available, or fallback to malloc
static void* hashtable_alloc(size_t size) {
    if (mcp_memory_pool_system_is_initialized()) {
        return mcp_pool_alloc(size);
    } else {
        return malloc(size);
    }
}

// Helper function to free memory allocated with hashtable_alloc
static void hashtable_free(void* ptr) {
    if (!ptr) return;

    if (mcp_memory_pool_system_is_initialized()) {
        mcp_pool_free(ptr);
    } else {
        free(ptr);
    }
}

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
    mcp_hashtable_entry_t** new_buckets = (mcp_hashtable_entry_t**)hashtable_alloc(
        new_capacity * sizeof(mcp_hashtable_entry_t*));
    if (!new_buckets) {
        return -1;
    }

    // Initialize all buckets to NULL
    memset(new_buckets, 0, new_capacity * sizeof(mcp_hashtable_entry_t*));

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
    hashtable_free(table->buckets);
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
    mcp_hashtable_t* table = (mcp_hashtable_t*)hashtable_alloc(sizeof(mcp_hashtable_t));
    if (!table) {
        return NULL;
    }

    // Allocate buckets array
    table->buckets = (mcp_hashtable_entry_t**)hashtable_alloc(
        initial_capacity * sizeof(mcp_hashtable_entry_t*));
    if (!table->buckets) {
        hashtable_free(table);
        return NULL;
    }

    // Initialize buckets to NULL
    memset(table->buckets, 0, initial_capacity * sizeof(mcp_hashtable_entry_t*));

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
    hashtable_free(table->buckets);
    hashtable_free(table);
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
    mcp_hashtable_entry_t* new_entry = (mcp_hashtable_entry_t*)hashtable_alloc(
        sizeof(mcp_hashtable_entry_t));
    if (!new_entry) {
        return -1;
    }

    // Duplicate key
    new_entry->key = table->key_dup(key);
    if (!new_entry->key) {
        hashtable_free(new_entry);
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
            hashtable_free(entry);
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
                // Add explicit cast to avoid warnings on Windows
                ((void (*)(void*))table->value_free)(entry->value);
            }

            // Free entry
            hashtable_free(entry);
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

// String hash function (FNV-1a)
unsigned long mcp_hashtable_string_hash(const void* key) {
    const unsigned char* str = (const unsigned char*)key;
    unsigned long hash = 2166136261UL; // FNV offset basis

    // Process 4 bytes at a time for better performance
    while (*str) {
        hash ^= *str++;
        hash *= 16777619UL; // FNV prime
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
    // MurmurHash-inspired integer hash
    unsigned int k = *(const unsigned int*)key;

    k ^= k >> 16;
    k *= 0x85ebca6b;
    k ^= k >> 13;
    k *= 0xc2b2ae35;
    k ^= k >> 16;

    return (unsigned long)k;
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
    // MurmurHash-inspired pointer hash
    uintptr_t ptr_val = (uintptr_t)key;

    // Mix the bits to distribute them better
    ptr_val ^= ptr_val >> 16;
    ptr_val *= 0x85ebca6b;
    ptr_val ^= ptr_val >> 13;
    ptr_val *= 0xc2b2ae35;
    ptr_val ^= ptr_val >> 16;

    return (unsigned long)ptr_val;
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

// Batch put operation
int mcp_hashtable_put_batch(
    mcp_hashtable_t* table,
    const void** keys,
    void** values,
    size_t count
) {
    if (!table || !keys || !values || count == 0) {
        return -1;
    }

    int success_count = 0;

    // Check if we need to resize the table
    if ((float)(table->size + count) / table->capacity > table->load_factor_threshold) {
        size_t new_capacity = table->capacity;
        while ((float)(table->size + count) / new_capacity > table->load_factor_threshold) {
            new_capacity *= 2;
        }
        if (mcp_hashtable_resize(table, new_capacity) != 0) {
            // Continue with the current capacity if resize fails
        }
    }

    // Process each key-value pair
    for (size_t i = 0; i < count; i++) {
        if (mcp_hashtable_put(table, keys[i], values[i]) == 0) {
            success_count++;
        }
    }

    return success_count;
}

// Batch get operation
int mcp_hashtable_get_batch(
    mcp_hashtable_t* table,
    const void** keys,
    void** values_out,
    size_t count,
    int* results_out
) {
    if (!table || !keys || !values_out || count == 0) {
        return 0;
    }

    int success_count = 0;

    // Process each key
    for (size_t i = 0; i < count; i++) {
        int result = mcp_hashtable_get(table, keys[i], &values_out[i]);

        if (results_out) {
            results_out[i] = result;
        }

        if (result == 0) {
            success_count++;
        }
    }

    return success_count;
}

// Batch remove operation
int mcp_hashtable_remove_batch(
    mcp_hashtable_t* table,
    const void** keys,
    size_t count,
    int* results_out
) {
    if (!table || !keys || count == 0) {
        return 0;
    }

    int success_count = 0;

    // Process each key
    for (size_t i = 0; i < count; i++) {
        int result = mcp_hashtable_remove(table, keys[i]);

        if (results_out) {
            results_out[i] = result;
        }

        if (result == 0) {
            success_count++;
        }
    }

    return success_count;
}
