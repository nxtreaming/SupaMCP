/**
 * @file mcp_cache_lru.c
 * @brief Implementation of resource cache with LRU eviction strategy
 *
 * This file implements a thread-safe resource cache with LRU (Least Recently Used)
 * eviction strategy. The cache stores content items and manages their lifecycle
 * based on access patterns and TTL (Time To Live) settings.
 */

#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_cache.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_hashtable.h"
#include "mcp_rwlock.h"
#include "mcp_object_pool.h"
#include "mcp_list.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/**
 * Structure for a cache entry
 *
 * Each cache entry contains a key (URI), an array of content items,
 * expiration information, and LRU tracking data.
 */
typedef struct {
    char* key;                   // Duplicate of the key (URI) for LRU list
    mcp_content_item_t** content;// Value (array of pointers to copies, malloc'd)
    size_t content_count;        // Number of items in the content array
    time_t expiry_time;          // Absolute expiration time (0 for never expires)
    time_t last_accessed;        // For LRU eviction
    bool is_pooled;              // Flag to indicate if content items are from a pool
    mcp_list_node_t* lru_node;   // Pointer to this entry's node in the LRU list
} mcp_cache_entry_t;

/**
 * Internal cache structure
 *
 * The cache uses a hash table for O(1) lookups and a doubly-linked list
 * for maintaining LRU order. Thread safety is provided by a read-write lock.
 */
struct mcp_resource_cache {
    mcp_rwlock_t* rwlock;        // Read-write lock for thread safety
    mcp_hashtable_t* table;      // Hash table for cache entries
    mcp_list_t* lru_list;        // LRU list for eviction ordering
    size_t capacity;             // Max number of entries
    time_t default_ttl_seconds;  // Default TTL for new entries
    mcp_object_pool_t* pool;     // Object pool for content items
};

// Free function for cache entries (used by hash table)
static void free_cache_entry(void* value) {
    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)value;
    if (!entry)
        return;

    // Free the key duplicate used for LRU list
    free(entry->key);

    // Just free the entry structure and the content array
    // The actual content items are handled separately in cleanup_cache_entry
    if (entry->content) {
        free(entry->content);
    }

    free(entry);
}

mcp_resource_cache_t* mcp_cache_create(size_t capacity, time_t default_ttl_seconds) {
    if (capacity == 0) {
        // Allow zero capacity for testing, but log a warning
        mcp_log_warn("Creating cache with zero capacity. Cache will not store any items.");
    }

    mcp_resource_cache_t* cache = (mcp_resource_cache_t*)malloc(sizeof(mcp_resource_cache_t));
    if (!cache)
        return NULL;

    // Initialize read-write lock
    cache->rwlock = mcp_rwlock_create();
    if (!cache->rwlock) {
        free(cache);
        return NULL;
    }

    // Create hash table for cache entries
    cache->table = mcp_hashtable_create(
        capacity > 0 ? capacity : 1, // Ensure at least 1 bucket
        0.75f,                       // Load factor threshold
        mcp_hashtable_string_hash,   // Hash function
        mcp_hashtable_string_compare,// Key comparison function
        mcp_hashtable_string_dup,    // Key duplication function
        mcp_hashtable_string_free,   // Key free function
        free_cache_entry             // Value free function
    );

    if (!cache->table) {
        mcp_rwlock_free(cache->rwlock);
        free(cache);
        return NULL;
    }

    // Create LRU list
    cache->lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);
    if (!cache->lru_list) {
        mcp_hashtable_destroy(cache->table);
        mcp_rwlock_free(cache->rwlock);
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->default_ttl_seconds = default_ttl_seconds;
    cache->pool = NULL; // Will be set in mcp_cache_get/put

    return cache;
}

// Helper function to clean up a content item
static void cleanup_content_item(mcp_resource_cache_t* cache, mcp_content_item_t* item) {
    if (!cache || !item)
        return;

    // Free the internal data
    free(item->mime_type);
    free(item->data);
    item->mime_type = NULL;
    item->data = NULL;
    item->data_size = 0;

    // If we have a pool, release the item back to it
    if (cache->pool) {
        mcp_object_pool_release(cache->pool, item);
    } else {
        // Otherwise, free it directly
        free(item);
    }
}

// Helper function to clean up a cache entry's content items
static void cleanup_cache_entry_content(mcp_resource_cache_t* cache, mcp_cache_entry_t* entry) {
    if (!cache || !entry || !entry->content)
        return;

    for (size_t i = 0; i < entry->content_count; ++i) {
        if (entry->content[i]) {
            cleanup_content_item(cache, entry->content[i]);
        }
    }
}

// Helper function to clean up all entries in the cache
static void cleanup_all_cache_entries(mcp_resource_cache_t* cache) {
    if (!cache || !cache->table)
        return;

    // Iterate through all buckets
    for (size_t i = 0; i < cache->table->capacity; i++) {
        mcp_hashtable_entry_t* bucket_entry = cache->table->buckets[i];
        while (bucket_entry) {
            mcp_hashtable_entry_t* next = bucket_entry->next;
            mcp_cache_entry_t* entry = (mcp_cache_entry_t*)bucket_entry->value;

            // Clean up the entry's content items
            cleanup_cache_entry_content(cache, entry);

            bucket_entry = next;
        }
    }
}

void mcp_cache_destroy(mcp_resource_cache_t* cache) {
    if (!cache)
        return;

    // Clean up all entries first
    cleanup_all_cache_entries(cache);

    // Destroy hash table (which will free all entries)
    if (cache->table) {
        mcp_hashtable_destroy(cache->table);
    }

    // Destroy LRU list
    if (cache->lru_list) {
        mcp_list_destroy(cache->lru_list, NULL); // Don't free data, already handled by hashtable
    }

    // Destroy read-write lock and free cache structure
    mcp_rwlock_free(cache->rwlock);
    free(cache);
}

// Helper function to create a new cache entry
static mcp_cache_entry_t* create_cache_entry(const char* uri, time_t ttl_seconds, mcp_resource_cache_t* cache) {
    if (!uri || !cache)
        return NULL;

    // Create a new cache entry
    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)malloc(sizeof(mcp_cache_entry_t));
    if (!entry)
        return NULL;

    // Duplicate the key for the LRU list
    entry->key = mcp_strdup(uri);
    if (!entry->key) {
        free(entry);
        return NULL;
    }

    // Initialize entry
    entry->content = NULL;
    entry->content_count = 0;
    entry->last_accessed = time(NULL);
    time_t effective_ttl = (ttl_seconds == 0) ? cache->default_ttl_seconds : (time_t)ttl_seconds;
    // 0 means never expires
    entry->expiry_time = (effective_ttl < 0) ? 0 : entry->last_accessed + effective_ttl;
    // Mark as pooled since we're using the pool
    entry->is_pooled = true;
    // Will be set after adding to LRU list
    entry->lru_node = NULL;

    return entry;
}

// Helper function to update LRU position
static void update_lru_position(mcp_resource_cache_t* cache, mcp_cache_entry_t* entry) {
    if (!cache || !entry || !entry->lru_node)
        return;

    // Move the entry to the front of the LRU list (most recently used)
    mcp_list_move_to_front(cache->lru_list, entry->lru_node);
}

// Helper function to clean up a cache entry and its resources
static void free_entry_resources(mcp_resource_cache_t* cache, mcp_cache_entry_t* entry) {
    if (!cache || !entry)
        return;

    // Clean up content items
    cleanup_cache_entry_content(cache, entry);

    // Free the entry's resources
    free(entry->key);
    free(entry->content);
    // The entry itself will be freed by the hash table's free function
}

// Helper function to add an entry to the LRU list and hash table
static int add_entry_to_cache(mcp_resource_cache_t* cache, const char* uri, mcp_cache_entry_t* entry) {
    if (!cache || !uri || !entry)
        return -1;

    // Add entry to LRU list (at the front, as it's most recently used)
    entry->lru_node = mcp_list_push_front(cache->lru_list, entry);
    if (!entry->lru_node) {
        return -1;
    }

    // Add entry to hash table (this will replace any existing entry with the same key)
    // mcp_hashtable_put takes ownership of the 'entry' pointer if successful
    return mcp_hashtable_put(cache->table, uri, entry);
}

// Helper function to evict the least recently used entry
static bool evict_lru_entry(mcp_resource_cache_t* cache) {
    if (!cache || !cache->lru_list || mcp_list_is_empty(cache->lru_list))
        return false;

    // Get the least recently used entry (from the back of the list)
    mcp_list_node_t* lru_node = cache->lru_list->tail;
    if (!lru_node)
        return false;

    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)lru_node->data;
    if (!entry)
        return false;

    // Get the key from the entry
    const char* key_to_evict = entry->key;
    if (!key_to_evict)
        return false;

    mcp_log_debug("Evicting LRU cache entry with key '%s'", key_to_evict);

    // Remove the entry from the LRU list
    mcp_list_remove(cache->lru_list, lru_node, NULL);

    // Remove the entry from the hash table
    return mcp_hashtable_remove(cache->table, key_to_evict) == 0;
}

int mcp_cache_get(mcp_resource_cache_t* cache, const char* uri, mcp_object_pool_t* pool, mcp_content_item_t*** content, size_t* content_count) {
    if (!cache || !uri || !pool || !content || !content_count)
        return -1;

    // Store the pool for future use
    cache->pool = pool;

    *content = NULL;
    *content_count = 0;
    int result = -1; // Default to not found/expired
    PROFILE_START("mcp_cache_get");

    // Local variables to store entry information outside the lock
    mcp_content_item_t** content_copy_ptrs = NULL;
    size_t entry_content_count = 0;
    bool is_expired = false;
    mcp_cache_entry_t* entry = NULL;
    time_t now = time(NULL);

    // First acquire read lock to check if entry exists and is valid
    mcp_rwlock_read_lock(cache->rwlock);

    // Try to get the entry from the hash table
    if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
        // Check expiration (0 means never expires)
        if (entry->expiry_time == 0 || now < entry->expiry_time) {
            // Cache hit and valid! Store information for later use
            entry_content_count = entry->content_count;
        } else {
            // Entry is expired
            is_expired = true;
        }
    }

    // Release read lock
    mcp_rwlock_read_unlock(cache->rwlock);

    // If entry not found or expired, handle accordingly
    if (!entry) {
        // Entry not found
        PROFILE_END("mcp_cache_get");
        return -1;
    }

    if (is_expired) {
        // Entry is expired, acquire write lock to remove it
        mcp_rwlock_write_lock(cache->rwlock);

        // Double-check that the entry is still in the cache and still expired
        if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
            if (entry->expiry_time != 0 && now >= entry->expiry_time) {
                // Remove from LRU list first
                if (entry->lru_node) {
                    mcp_list_remove(cache->lru_list, entry->lru_node, NULL);
                }
                mcp_hashtable_remove(cache->table, uri);
            }
        }

        mcp_rwlock_write_unlock(cache->rwlock);
        PROFILE_END("mcp_cache_get");
        return -1;
    }

    // Entry is valid, allocate memory for content copies
    content_copy_ptrs = (mcp_content_item_t**)malloc(entry_content_count * sizeof(mcp_content_item_t*));
    if (!content_copy_ptrs) {
        // Memory allocation failed
        PROFILE_END("mcp_cache_get");
        return -1;
    }

    // We need to make a copy of the entry's content for processing outside the lock
    mcp_content_item_t** entry_content_copies = NULL;
    size_t actual_content_count = 0;

    // Acquire read lock to copy entry content information
    mcp_rwlock_read_lock(cache->rwlock);

    // Double-check that the entry is still in the cache and still valid
    if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
        // Check expiration again (0 means never expires)
        if (entry->expiry_time == 0 || now < entry->expiry_time) {
            // Make a temporary copy of the content pointers and count
            actual_content_count = entry->content_count;
            entry_content_copies = (mcp_content_item_t**)malloc(actual_content_count * sizeof(mcp_content_item_t*));

            if (entry_content_copies) {
                // Copy the content pointers
                for (size_t i = 0; i < actual_content_count; ++i) {
                    entry_content_copies[i] = entry->content[i];
                }
            }
        } else {
            // Entry expired between our checks
            is_expired = true;
        }
    } else {
        // Entry was removed between our checks
        entry = NULL;
    }

    // Release read lock
    mcp_rwlock_read_unlock(cache->rwlock);

    // If entry was removed or expired, clean up and return
    if (!entry || is_expired || !entry_content_copies) {
        free(content_copy_ptrs);
        free(entry_content_copies);

        // If entry expired, remove it
        if (entry && is_expired) {
            mcp_rwlock_write_lock(cache->rwlock);

            // Double-check again that the entry is still in the cache and still expired
            if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
                if (entry->expiry_time != 0 && now >= entry->expiry_time) {
                    // Remove from LRU list first
                    if (entry->lru_node) {
                        mcp_list_remove(cache->lru_list, entry->lru_node, NULL);
                    }
                    mcp_hashtable_remove(cache->table, uri);
                }
            }

            mcp_rwlock_write_unlock(cache->rwlock);
        }

        PROFILE_END("mcp_cache_get");
        return -1;
    }

    // Now process the content outside the lock
    size_t copied_count = 0;
    bool copy_error = false;

    // Copy content items
    for (size_t i = 0; i < actual_content_count; ++i) {
        // Validate content item before copying
        if (!entry_content_copies[i]) {
            mcp_log_error("NULL content item at index %zu in cache entry", i);
            copy_error = true;
            break;
        }

        // Validate mime_type before passing to acquire_pooled
        const char* mime_type = entry_content_copies[i]->mime_type ? entry_content_copies[i]->mime_type : "";

        // Acquire a pooled item and copy data into it
        content_copy_ptrs[i] = mcp_content_item_acquire_pooled(
            pool,
            entry_content_copies[i]->type,
            mime_type,
            entry_content_copies[i]->data,
            entry_content_copies[i]->data_size
        );
        if (!content_copy_ptrs[i]) {
            copy_error = true;
            break; // Exit loop on acquisition/copy failure
        }
        copied_count++;
    }

    // Free the temporary copy of content pointers
    free(entry_content_copies);

    if (copy_error) {
        // Release partially acquired/copied items back to pool on error
        for (size_t i = 0; i < copied_count; ++i) {
            if (content_copy_ptrs[i]) {
                cleanup_content_item(cache, content_copy_ptrs[i]);
            }
        }
        free(content_copy_ptrs);
        PROFILE_END("mcp_cache_get");
        return -1;
    }

    // Now acquire write lock to update LRU position
    mcp_rwlock_write_lock(cache->rwlock);

    // Double-check that the entry is still in the cache and still valid
    if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
        // Check expiration again (0 means never expires)
        if (entry->expiry_time == 0 || now < entry->expiry_time) {
            // Return the array of pointers to pooled items
            *content = content_copy_ptrs;
            *content_count = actual_content_count;
            // Update last accessed time
            entry->last_accessed = now;

            // Update LRU position (move to front of list)
            update_lru_position(cache, entry);

            result = 0;
        } else {
            // Entry expired between our checks, remove it
            if (entry->lru_node) {
                mcp_list_remove(cache->lru_list, entry->lru_node, NULL);
            }
            mcp_hashtable_remove(cache->table, uri);

            // Release acquired items back to pool
            for (size_t i = 0; i < copied_count; ++i) {
                if (content_copy_ptrs[i]) {
                    cleanup_content_item(cache, content_copy_ptrs[i]);
                }
            }
            free(content_copy_ptrs);
        }
    } else {
        // Entry was removed between our checks
        // Release acquired items back to pool
        for (size_t i = 0; i < copied_count; ++i) {
            if (content_copy_ptrs[i]) {
                cleanup_content_item(cache, content_copy_ptrs[i]);
            }
        }
        free(content_copy_ptrs);
    }

    // Release write lock
    mcp_rwlock_write_unlock(cache->rwlock);
    PROFILE_END("mcp_cache_get");
    return result;
}

int mcp_cache_put(mcp_resource_cache_t* cache, const char* uri, mcp_object_pool_t* pool, mcp_content_item_t** content, size_t content_count, int ttl_seconds) {
    if (!cache || !uri || !pool || !content || content_count == 0)
        return -1;

    // Validate each content item in the array
    for (size_t i = 0; i < content_count; i++) {
        if (!content[i]) {
            mcp_log_error("NULL content item at index %zu in mcp_cache_put", i);
            return -1;
        }

        // Additional validation for content item fields
        if (!content[i]->data && content[i]->data_size > 0) {
            mcp_log_error("Content item at index %zu has NULL data but non-zero data_size", i);
            return -1;
        }
    }

    // If capacity is zero, don't store anything but return success
    if (cache->capacity == 0) {
        return 0;
    }

    // Store the pool for future use
    cache->pool = pool;

    PROFILE_START("mcp_cache_put");

    // First, prepare the entry and content outside the lock
    mcp_cache_entry_t* entry = create_cache_entry(uri, ttl_seconds, cache);
    if (!entry) {
        PROFILE_END("mcp_cache_put");
        return -1;
    }

    // Allocate space for the array of content item POINTERS
    entry->content = (mcp_content_item_t**)malloc(content_count * sizeof(mcp_content_item_t*));
    if (!entry->content) {
        free(entry->key);
        free(entry);
        PROFILE_END("mcp_cache_put");
        return -1;
    }

    // Acquire pooled items and copy content into them
    bool copy_error = false;
    for (size_t i = 0; i < content_count; ++i) {
        // Validate mime_type before passing to acquire_pooled
        const char* mime_type = content[i]->mime_type ? content[i]->mime_type : "";

        // Acquire a pooled item and copy data into it
        entry->content[i] = mcp_content_item_acquire_pooled(
            pool,
            content[i]->type,
            mime_type,
            content[i]->data,
            content[i]->data_size
        );
        if (!entry->content[i]) {
            copy_error = true;
            // Release already acquired/copied items back to pool
            for(size_t j = 0; j < i; ++j) {
                if (entry->content[j]) {
                    cleanup_content_item(cache, entry->content[j]);
                }
            }
            // Exit loop on acquisition/copy failure
            break;
        }
        // Increment count only after successful copy
        entry->content_count++;
    }

    if (copy_error) {
        // Use free_entry_resources to ensure all resources are properly cleaned up
        free_entry_resources(cache, entry);
        free(entry);
        PROFILE_END("mcp_cache_put");
        return -1;
    }

    // Now that we have prepared the entry, acquire the write lock
    mcp_rwlock_write_lock(cache->rwlock);

    // Check if the key already exists
    mcp_cache_entry_t* existing_entry = NULL;
    bool key_exists = mcp_hashtable_get(cache->table, uri, (void**)&existing_entry) == 0 && existing_entry != NULL;

    // --- LRU Eviction Logic ---
    if (!key_exists && mcp_hashtable_size(cache->table) >= cache->capacity) {
        mcp_log_warn("Cache full (capacity: %zu). Evicting LRU entry to insert '%s'.", cache->capacity, uri);
        if (!evict_lru_entry(cache)) {
            mcp_log_error("Cache full but failed to evict LRU entry.");
            mcp_rwlock_write_unlock(cache->rwlock);
            free_entry_resources(cache, entry);
            free(entry);
            PROFILE_END("mcp_cache_put");
            return -1;
        }
    }
    // --- End LRU Eviction Logic ---

    // If the key exists, we need to remove the old entry from the LRU list
    if (key_exists && existing_entry && existing_entry->lru_node) {
        mcp_list_remove(cache->lru_list, existing_entry->lru_node, NULL);
    }

    // Add entry to LRU list and hash table
    int result = add_entry_to_cache(cache, uri, entry);
    if (result != 0) {
        // Failed to add to cache, clean up the entry
        free_entry_resources(cache, entry);
        free(entry);
    }

    // Release write lock
    mcp_rwlock_write_unlock(cache->rwlock);
    PROFILE_END("mcp_cache_put");
    return result;
}

int mcp_cache_invalidate(mcp_resource_cache_t* cache, const char* uri) {
    if (!cache || !uri)
        return -1;

    int result = -1;

    // Acquire write lock - exclusive access for invalidation
    mcp_rwlock_write_lock(cache->rwlock);

    // Get the entry first so we can clean it up properly
    mcp_cache_entry_t* entry = NULL;
    if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
        // Remove from LRU list first
        if (entry->lru_node) {
            mcp_list_remove(cache->lru_list, entry->lru_node, NULL);
        }

        // Clean up the entry's content items
        cleanup_cache_entry_content(cache, entry);

        // Now remove it from the hash table
        // mcp_hashtable_remove calls the value free function (free_cache_entry)
        result = mcp_hashtable_remove(cache->table, uri);
    }

    // Always release write lock before returning
    mcp_rwlock_write_unlock(cache->rwlock);
    return result;
}

/**
 * Structure to hold all the data needed for the expired keys callback
 *
 * This structure is used to pass data to and from the callback function
 * that collects expired keys during cache pruning.
 */
typedef struct {
    time_t now;                        // Current time for expiration check
    char** keys_to_remove;             // Array of keys to remove
    mcp_cache_entry_t** entries_to_cleanup; // Array of entries to clean up
    size_t* keys_count;                // Pointer to count of keys
    size_t* keys_capacity;             // Pointer to capacity of keys array
    size_t* removed_count;             // Pointer to count of removed entries
    int* error_flag;                   // Pointer to error flag
    mcp_resource_cache_t* cache;       // Pointer to the cache
} expired_keys_data_t;

// Callback function for mcp_hashtable_foreach to collect expired keys
static void collect_expired_keys(const void* key, void* value, void* user_data) {
    const char* uri = (const char*)key;
    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)value;
    expired_keys_data_t* data = (expired_keys_data_t*)user_data;

    // Validate inputs
    if (!uri || !entry || !data) {
        mcp_log_error("Invalid parameters in collect_expired_keys");
        return;
    }

    if (entry->expiry_time != 0 && data->now >= entry->expiry_time) {
        // This entry is expired, add its key and entry to our list
        if (*(data->keys_count) >= *(data->keys_capacity)) {
            // Resize the keys array if needed
            size_t new_capacity = *(data->keys_capacity) * 2;
            if (new_capacity == 0)
                new_capacity = 16; // Handle initial zero capacity

            // Resize keys array
            char** new_keys = (char**)realloc(data->keys_to_remove, new_capacity * sizeof(char*));
            if (!new_keys) {
                mcp_log_error("Failed to realloc keys_to_remove in prune_expired");
                // Set error flag to indicate memory allocation failure
                *(data->error_flag) = 1;
                return; // Allocation failed, skip this key
            }
            data->keys_to_remove = new_keys;

            // Resize entries array
            mcp_cache_entry_t** new_entries = (mcp_cache_entry_t**)realloc(
                data->entries_to_cleanup, new_capacity * sizeof(mcp_cache_entry_t*));
            if (!new_entries) {
                mcp_log_error("Failed to realloc entries_to_cleanup in prune_expired");
                // Set error flag to indicate memory allocation failure
                *(data->error_flag) = 1;
                return; // Allocation failed, skip this key
            }
            data->entries_to_cleanup = new_entries;

            // Initialize the newly allocated memory to NULL
            for (size_t i = *(data->keys_capacity); i < new_capacity; i++) {
                data->keys_to_remove[i] = NULL;
                data->entries_to_cleanup[i] = NULL;
            }

            *(data->keys_capacity) = new_capacity;
        }

        // Duplicate the key (will be freed after removal)
        char* key_dup = mcp_strdup(uri);
        if (!key_dup) {
            mcp_log_error("Failed to duplicate key in collect_expired_keys");
            // Set error flag to indicate memory allocation failure
            *(data->error_flag) = 1;
            return; // Memory allocation failed
        }

        // Store the key and entry
        data->keys_to_remove[*(data->keys_count)] = key_dup;
        data->entries_to_cleanup[*(data->keys_count)] = entry;

        // Increment counters
        (*(data->keys_count))++;
        (*(data->removed_count))++;
    }
}

size_t mcp_cache_prune_expired(mcp_resource_cache_t* cache) {
    if (!cache)
        return 0;

    size_t removed_count = 0;
    time_t now = time(NULL);

    // We need to collect keys to remove since we can't modify the hash table during iteration
    char** keys_to_remove = NULL;
    mcp_cache_entry_t** entries_to_cleanup = NULL;
    size_t keys_count = 0;
    size_t keys_capacity = 16;
    int error = 0;
    int lock_acquired = 0;

    // Allocate memory for keys and entries outside the lock
    keys_to_remove = (char**)calloc(keys_capacity, sizeof(char*));
    if (!keys_to_remove) {
        mcp_log_error("Failed to allocate initial keys_to_remove in prune_expired");
        goto cleanup;
    }

    entries_to_cleanup = (mcp_cache_entry_t**)calloc(keys_capacity, sizeof(mcp_cache_entry_t*));
    if (!entries_to_cleanup) {
        mcp_log_error("Failed to allocate entries_to_cleanup in prune_expired");
        goto cleanup;
    }

    // Using calloc ensures all pointers are initialized to NULL

    // Create the data structure for the callback
    expired_keys_data_t callback_data = {
        .now = now,
        .keys_to_remove = keys_to_remove,
        .entries_to_cleanup = entries_to_cleanup,
        .keys_count = &keys_count,
        .keys_capacity = &keys_capacity,
        .removed_count = &removed_count,
        .error_flag = &error,
        .cache = cache
    };

    // Acquire write lock - exclusive access for pruning
    mcp_rwlock_write_lock(cache->rwlock);
    lock_acquired = 1;

    // Iterate through all entries to find expired ones
    mcp_hashtable_foreach(cache->table, collect_expired_keys, &callback_data);

    // If memory allocation failed during callback, clean up and return
    if (error) {
        mcp_log_error("Memory allocation failed during cache pruning");
        goto cleanup;
    }

    // Update keys_to_remove pointer in case realloc changed it
    keys_to_remove = callback_data.keys_to_remove;
    entries_to_cleanup = callback_data.entries_to_cleanup;

    // Clean up and remove all collected expired entries
    for (size_t i = 0; i < keys_count; i++) {
        if (keys_to_remove[i] && entries_to_cleanup[i]) {
            // Remove from LRU list first
            if (entries_to_cleanup[i]->lru_node) {
                if (mcp_list_remove(cache->lru_list, entries_to_cleanup[i]->lru_node, NULL) != 0) {
                    // Error removing from LRU list, but continue with other entries
                    mcp_log_error("Failed to remove entry from LRU list in prune_expired");
                    error = 1;
                    // Don't continue - still try to clean up content and remove from hash table
                }
            }

            // Clean up the entry's content items
            cleanup_cache_entry_content(cache, entries_to_cleanup[i]);

            // Now remove it from the hash table
            if (mcp_hashtable_remove(cache->table, keys_to_remove[i]) != 0) {
                // Error removing from hash table, but continue with other entries
                mcp_log_error("Failed to remove entry from hash table in prune_expired");
                error = 1;
            }
        }

        // Always free the key string if it exists, regardless of other errors
        if (keys_to_remove[i]) {
            free(keys_to_remove[i]);
            keys_to_remove[i] = NULL; // Set to NULL after freeing to prevent double-free
        }
    }

cleanup:
    // Always release the lock if it was acquired
    if (lock_acquired) {
        mcp_rwlock_write_unlock(cache->rwlock);
    }

    // Free the arrays if they were allocated
    if (keys_to_remove) {
        // Free any keys that were allocated but not freed in the loop
        for (size_t i = 0; i < keys_count; i++) {
            if (keys_to_remove[i]) {
                free(keys_to_remove[i]);
            }
        }
        free(keys_to_remove);
    }

    if (entries_to_cleanup) {
        free(entries_to_cleanup);
    }

    // If there was an error, log it but still return the count of removed entries
    if (error) {
        mcp_log_warn("Some errors occurred during cache pruning, but %zu entries were removed", removed_count);
    }

    return removed_count;
}
