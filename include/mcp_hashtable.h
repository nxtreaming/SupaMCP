#ifndef MCP_HASHTABLE_H
#define MCP_HASHTABLE_H

#include <stddef.h>
#include <stdbool.h>
#include "mcp_cache_aligned.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash function type for calculating hash values.
 *
 * @param key Pointer to the key to hash.
 * @return Unsigned long hash value.
 */
typedef unsigned long (*mcp_hash_func_t)(const void* key);

/**
 * @brief Key comparison function type.
 *
 * @param key1 Pointer to the first key.
 * @param key2 Pointer to the second key.
 * @return True if keys are equal, false otherwise.
 */
typedef bool (*mcp_key_compare_func_t)(const void* key1, const void* key2);

/**
 * @brief Key duplication function type.
 *
 * @param key Pointer to the key to duplicate.
 * @return Pointer to the duplicated key, or NULL on failure.
 */
typedef void* (*mcp_key_dup_func_t)(const void* key);

/**
 * @brief Key free function type.
 *
 * @param key Pointer to the key to free.
 */
typedef void (*mcp_key_free_func_t)(void* key);

/**
 * @brief Value free function type.
 *
 * @param value Pointer to the value to free.
 */
typedef void (*mcp_value_free_func_t)(void* value);

/**
 * @brief Hash table entry structure.
 *
 * Cache-aligned to prevent false sharing in multi-threaded environments.
 */
typedef MCP_CACHE_ALIGNED struct mcp_hashtable_entry {
    void* key;                      /**< Pointer to the key. */
    void* value;                    /**< Pointer to the value. */
    struct mcp_hashtable_entry* next; /**< Pointer to the next entry in the bucket. */
} mcp_hashtable_entry_t;

/**
 * @brief Hash table structure.
 *
 * Cache-aligned to prevent false sharing in multi-threaded environments.
 */
typedef MCP_CACHE_ALIGNED struct mcp_hashtable {
    mcp_hashtable_entry_t** buckets; /**< Array of bucket pointers. */
    size_t capacity;                /**< Number of buckets in the table. */
    size_t size;                    /**< Number of entries in the table. */
    float load_factor_threshold;    /**< Load factor threshold for resizing. */
    mcp_hash_func_t hash_func;      /**< Hash function. */
    mcp_key_compare_func_t key_compare; /**< Key comparison function. */
    mcp_key_dup_func_t key_dup;     /**< Key duplication function. */
    mcp_key_free_func_t key_free;   /**< Key free function. */
    mcp_value_free_func_t value_free; /**< Value free function. */
} mcp_hashtable_t;

/**
 * @brief Creates a new hash table.
 *
 * @param initial_capacity Initial number of buckets (should be a power of 2).
 * @param load_factor_threshold Load factor threshold for resizing (e.g., 0.75).
 * @param hash_func Hash function.
 * @param key_compare Key comparison function.
 * @param key_dup Key duplication function.
 * @param key_free Key free function.
 * @param value_free Value free function.
 * @return Pointer to the created hash table, or NULL on failure.
 */
mcp_hashtable_t* mcp_hashtable_create(
    size_t initial_capacity,
    float load_factor_threshold,
    mcp_hash_func_t hash_func,
    mcp_key_compare_func_t key_compare,
    mcp_key_dup_func_t key_dup,
    mcp_key_free_func_t key_free,
    mcp_value_free_func_t value_free
);

/**
 * @brief Destroys a hash table and frees all associated memory.
 *
 * @param table Pointer to the hash table to destroy.
 */
void mcp_hashtable_destroy(mcp_hashtable_t* table);

/**
 * @brief Inserts or updates a key-value pair in the hash table.
 *
 * @param table Pointer to the hash table.
 * @param key Pointer to the key.
 * @param value Pointer to the value.
 * @return 0 on success, non-zero on failure.
 */
int mcp_hashtable_put(mcp_hashtable_t* table, const void* key, void* value);

/**
 * @brief Retrieves a value from the hash table.
 *
 * @param table Pointer to the hash table.
 * @param key Pointer to the key.
 * @param value_ptr Pointer to a variable that will receive the value pointer.
 * @return 0 if the key was found, non-zero otherwise.
 */
int mcp_hashtable_get(mcp_hashtable_t* table, const void* key, void** value_ptr);

/**
 * @brief Removes a key-value pair from the hash table.
 *
 * @param table Pointer to the hash table.
 * @param key Pointer to the key.
 * @return 0 if the key was found and removed, non-zero otherwise.
 */
int mcp_hashtable_remove(mcp_hashtable_t* table, const void* key);

/**
 * @brief Checks if a key exists in the hash table.
 *
 * @param table Pointer to the hash table.
 * @param key Pointer to the key.
 * @return True if the key exists, false otherwise.
 */
bool mcp_hashtable_contains(mcp_hashtable_t* table, const void* key);

/**
 * @brief Returns the number of entries in the hash table.
 *
 * @param table Pointer to the hash table.
 * @return Number of entries.
 */
size_t mcp_hashtable_size(const mcp_hashtable_t* table);

/**
 * @brief Clears all entries from the hash table.
 *
 * @param table Pointer to the hash table.
 */
void mcp_hashtable_clear(mcp_hashtable_t* table);

/**
 * @brief Iterates through all entries in the hash table.
 *
 * @param table Pointer to the hash table.
 * @param callback Function to call for each entry.
 * @param user_data User data to pass to the callback.
 */
void mcp_hashtable_foreach(
    mcp_hashtable_t* table,
    void (*callback)(const void* key, void* value, void* user_data),
    void* user_data
);

/**
 * @brief String hash function (djb2).
 *
 * @param key Pointer to a null-terminated string.
 * @return Hash value.
 */
unsigned long mcp_hashtable_string_hash(const void* key);

/**
 * @brief String comparison function.
 *
 * @param key1 Pointer to the first string.
 * @param key2 Pointer to the second string.
 * @return True if strings are equal, false otherwise.
 */
bool mcp_hashtable_string_compare(const void* key1, const void* key2);

/**
 * @brief String duplication function.
 *
 * @param key Pointer to the string to duplicate.
 * @return Pointer to the duplicated string, or NULL on failure.
 */
void* mcp_hashtable_string_dup(const void* key);

/**
 * @brief String free function.
 *
 * @param key Pointer to the string to free.
 */
void mcp_hashtable_string_free(void* key);

/**
 * @brief Integer hash function.
 *
 * @param key Pointer to an integer.
 * @return Hash value.
 */
unsigned long mcp_hashtable_int_hash(const void* key);

/**
 * @brief Integer comparison function.
 *
 * @param key1 Pointer to the first integer.
 * @param key2 Pointer to the second integer.
 * @return True if integers are equal, false otherwise.
 */
bool mcp_hashtable_int_compare(const void* key1, const void* key2);

/**
 * @brief Integer duplication function.
 *
 * @param key Pointer to the integer to duplicate.
 * @return Pointer to the duplicated integer, or NULL on failure.
 */
void* mcp_hashtable_int_dup(const void* key);

/**
 * @brief Integer free function.
 *
 * @param key Pointer to the integer to free.
 */
void mcp_hashtable_int_free(void* key);

/**
 * @brief Pointer hash function.
 *
 * @param key Pointer value.
 * @return Hash value.
 */
unsigned long mcp_hashtable_ptr_hash(const void* key);

/**
 * @brief Pointer comparison function.
 *
 * @param key1 First pointer.
 * @param key2 Second pointer.
 * @return True if pointers are equal, false otherwise.
 */
bool mcp_hashtable_ptr_compare(const void* key1, const void* key2);

/**
 * @brief No-op duplication function (returns the pointer as-is).
 *
 * @param key Pointer to duplicate.
 * @return The same pointer.
 */
void* mcp_hashtable_ptr_dup(const void* key);

/**
 * @brief No-op free function.
 *
 * @param key Pointer to free.
 */
void mcp_hashtable_ptr_free(void* key);

/**
 * @brief Batch put operation for multiple key-value pairs.
 *
 * @param table Pointer to the hash table.
 * @param keys Array of pointers to keys.
 * @param values Array of pointers to values.
 * @param count Number of key-value pairs to put.
 * @return Number of successful insertions, or -1 on error.
 */
int mcp_hashtable_put_batch(
    mcp_hashtable_t* table,
    const void** keys,
    void** values,
    size_t count
);

/**
 * @brief Batch get operation for multiple keys.
 *
 * @param table Pointer to the hash table.
 * @param keys Array of pointers to keys.
 * @param values_out Array to receive value pointers.
 * @param count Number of keys to get.
 * @param results_out Optional array to receive result codes (0 for success, -1 for not found).
 * @return Number of successful retrievals.
 */
int mcp_hashtable_get_batch(
    mcp_hashtable_t* table,
    const void** keys,
    void** values_out,
    size_t count,
    int* results_out
);

/**
 * @brief Batch remove operation for multiple keys.
 *
 * @param table Pointer to the hash table.
 * @param keys Array of pointers to keys.
 * @param count Number of keys to remove.
 * @param results_out Optional array to receive result codes (0 for success, -1 for not found).
 * @return Number of successful removals.
 */
int mcp_hashtable_remove_batch(
    mcp_hashtable_t* table,
    const void** keys,
    size_t count,
    int* results_out
);

/**
 * @brief Cleans up thread-local resources used by the hashtable implementation.
 *
 * This function should be called by each thread before it exits to free any
 * thread-local resources allocated by the hashtable implementation, such as
 * memory pools. It only affects resources for the calling thread.
 */
void mcp_hashtable_system_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // MCP_HASHTABLE_H
