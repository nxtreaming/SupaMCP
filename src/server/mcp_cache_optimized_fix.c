#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_cache.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_hashtable.h"
#include "mcp_sync.h"
#include "mcp_object_pool.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

// Structure for a cache entry
typedef struct {
    mcp_content_item_t** content;   // Value (array of pointers to copies, malloc'd)
    size_t content_count;           // Number of items in the content array
    time_t expiry_time;             // Absolute expiration time (0 for never expires)
    time_t last_accessed;           // For potential LRU eviction
    bool is_pooled;                 // Flag to indicate if content items are from a pool
} mcp_cache_entry_t;

// Internal cache structure
struct mcp_resource_cache {
    mcp_mutex_t* lock;              // Mutex for thread safety (using abstracted type)
    mcp_hashtable_t* table;         // Hash table for cache entries
    size_t capacity;                // Max number of entries
    time_t default_ttl_seconds;     // Default TTL for new entries
    mcp_object_pool_t* pool;        // Object pool for content items
};

// Free function for cache entries (used by hash table)
static void free_cache_entry(void* value) {
    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)value;
    if (!entry) return;

    // Just free the entry structure and the content array
    // The actual content items will be handled separately
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
    if (!cache) return NULL;

    // Initialize mutex using abstraction
    cache->lock = mcp_mutex_create();
    if (!cache->lock) {
        free(cache);
        return NULL;
    }

    // Create hash table for cache entries
    cache->table = mcp_hashtable_create(
        capacity > 0 ? capacity : 1, // Ensure at least 1 bucket
        0.75f,                         // Load factor threshold
        mcp_hashtable_string_hash,     // Hash function
        mcp_hashtable_string_compare,  // Key comparison function
        mcp_hashtable_string_dup,      // Key duplication function
        mcp_hashtable_string_free,     // Key free function
        free_cache_entry               // Value free function
    );

    if (!cache->table) {
        mcp_mutex_destroy(cache->lock); // Use abstracted destroy
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->default_ttl_seconds = default_ttl_seconds;
    cache->pool = NULL; // Will be set in mcp_cache_get/put

    return cache;
}

// Helper function to properly clean up a cache entry
static void cleanup_cache_entry(mcp_resource_cache_t* cache, mcp_cache_entry_t* entry) {
    if (!cache || !entry) return;

    if (entry->content) {
        for (size_t i = 0; i < entry->content_count; ++i) {
            if (entry->content[i]) {
                // Free the internal data
                free(entry->content[i]->mime_type);
                free(entry->content[i]->data);
                entry->content[i]->mime_type = NULL;
                entry->content[i]->data = NULL;
                entry->content[i]->data_size = 0;

                // If we have a pool, release the item back to it
                if (cache->pool) {
                    mcp_object_pool_release(cache->pool, entry->content[i]);
                } else {
                    // Otherwise, free it directly
                    free(entry->content[i]);
                }
            }
        }
    }
}

// Helper function to clean up all entries in the cache
static void cleanup_all_cache_entries(mcp_resource_cache_t* cache) {
    if (!cache || !cache->table) return;

    // Iterate through all buckets
    for (size_t i = 0; i < cache->table->capacity; i++) {
        mcp_hashtable_entry_t* bucket_entry = cache->table->buckets[i];
        while (bucket_entry) {
            mcp_hashtable_entry_t* next = bucket_entry->next;
            mcp_cache_entry_t* entry = (mcp_cache_entry_t*)bucket_entry->value;

            // Clean up the entry
            cleanup_cache_entry(cache, entry);

            bucket_entry = next;
        }
    }
}

void mcp_cache_destroy(mcp_resource_cache_t* cache) {
    if (!cache) return;

    // Clean up all entries first
    cleanup_all_cache_entries(cache);

    // Destroy hash table (which will free all entries)
    if (cache->table) {
        mcp_hashtable_destroy(cache->table);
    }

    // Destroy mutex and free cache structure using abstraction
    mcp_mutex_destroy(cache->lock);
    free(cache);
}

int mcp_cache_get(mcp_resource_cache_t* cache, const char* uri, mcp_object_pool_t* pool, mcp_content_item_t*** content, size_t* content_count) {
    if (!cache || !uri || !pool || !content || !content_count) return -1; // Added pool check

    // Store the pool for future use
    cache->pool = pool;

    *content = NULL;
    *content_count = 0;
    int result = -1; // Default to not found/expired
    PROFILE_START("mcp_cache_get");

    mcp_mutex_lock(cache->lock); // Use abstracted lock

    // Try to get the entry from the hash table
    mcp_cache_entry_t* entry = NULL;
    if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
        time_t now = time(NULL);

        // Check expiration (0 means never expires)
        if (entry->expiry_time == 0 || now < entry->expiry_time) {
            // Cache hit and valid! Create copies for the caller.
            // Allocate array of POINTERS for the caller
            mcp_content_item_t** content_copy_ptrs = (mcp_content_item_t**)malloc(entry->content_count * sizeof(mcp_content_item_t*));
            if (content_copy_ptrs) {
                size_t copied_count = 0;
                bool copy_error = false;

                for (size_t i = 0; i < entry->content_count; ++i) {
                    // Acquire a pooled item and copy data into it
                    content_copy_ptrs[i] = mcp_content_item_acquire_pooled(
                        pool,
                        entry->content[i]->type,
                        entry->content[i]->mime_type,
                        entry->content[i]->data,
                        entry->content[i]->data_size
                    );
                    if (!content_copy_ptrs[i]) {
                        copy_error = true;
                        break; // Exit loop on acquisition/copy failure
                    }
                    copied_count++;
                }

                if (!copy_error) {
                    *content = content_copy_ptrs; // Return the array of pointers to pooled items
                    *content_count = entry->content_count;
                    entry->last_accessed = now; // Update last accessed time
                    result = 0; // Success
                } else {
                    // Release partially acquired/copied items back to pool on error
                    for (size_t i = 0; i < copied_count; ++i) {
                        if (content_copy_ptrs[i]) {
                             // Free internal data/mime_type first
                             free(content_copy_ptrs[i]->mime_type);
                             free(content_copy_ptrs[i]->data);
                             content_copy_ptrs[i]->mime_type = NULL;
                             content_copy_ptrs[i]->data = NULL;
                             content_copy_ptrs[i]->data_size = 0;
                             mcp_object_pool_release(pool, content_copy_ptrs[i]);
                        }
                    }
                    free(content_copy_ptrs); // Free the array of pointers
                    // result remains -1 (error)
                }
            }
            // else: allocation failure for copy array, result remains -1
        } else {
            // Expired entry - remove it from the hash table
            mcp_hashtable_remove(cache->table, uri);
            // result remains -1 (expired)
        }
    }
    // else: entry not found, result remains -1

    mcp_mutex_unlock(cache->lock); // Use abstracted unlock
    PROFILE_END("mcp_cache_get");
    return result;
}

int mcp_cache_put(mcp_resource_cache_t* cache, const char* uri, mcp_object_pool_t* pool, mcp_content_item_t** content, size_t content_count, int ttl_seconds) {
    // Note: 'content' is now mcp_content_item_t** (array of pointers)
    if (!cache || !uri || !pool || !content || content_count == 0) return -1; // Added pool check

    // If capacity is zero, don't store anything but return success
    if (cache->capacity == 0) {
        return 0;
    }

    // Store the pool for future use
    cache->pool = pool;

    PROFILE_START("mcp_cache_put");

    mcp_mutex_lock(cache->lock); // Use abstracted lock

    // --- Eviction Logic: Evict First Found strategy ---
    if (mcp_hashtable_size(cache->table) >= cache->capacity && !mcp_hashtable_contains(cache->table, uri)) {
        mcp_log_warn("Cache full (capacity: %zu). Evicting an entry to insert '%s'.", cache->capacity, uri);
        bool evicted = false;
        // Iterate through buckets to find the first entry to evict
        // Note: Accessing table->buckets directly breaks encapsulation.
        // A better hashtable API would provide an eviction mechanism.
        for (size_t i = 0; i < cache->table->capacity; ++i) {
            mcp_hashtable_entry_t* bucket_entry = cache->table->buckets[i];
            if (bucket_entry != NULL) {
                // Found an entry to evict (first entry in the first non-empty bucket)
                const char* key_to_evict = (const char*)bucket_entry->key;
                mcp_log_debug("Evicting cache entry with key '%s'", key_to_evict);
                // Remove function handles freeing the key and value
                mcp_hashtable_remove(cache->table, key_to_evict);
                evicted = true;
                break; // Evict only one entry
            }
        }
        if (!evicted) {
            // Should not happen if size >= capacity > 0, but handle defensively
            mcp_log_error("Cache full but failed to find an entry to evict.");
            mcp_mutex_unlock(cache->lock);
            PROFILE_END("mcp_cache_put");
            return -1;
        }
    }
    // --- End Eviction Logic ---

    // If the key *does* exist, mcp_hashtable_put will replace it automatically.
    // If the cache was not full, or if we successfully evicted an entry, proceed to create the new entry.

    // Create a new cache entry
    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)malloc(sizeof(mcp_cache_entry_t));
    if (!entry) {
        mcp_mutex_unlock(cache->lock); // Use abstracted unlock
        PROFILE_END("mcp_cache_put");
        return -1; // Allocation failure
    }

    // Allocate space for the array of content item POINTERS
    entry->content = (mcp_content_item_t**)malloc(content_count * sizeof(mcp_content_item_t*));
    if (!entry->content) {
        free(entry);
        mcp_mutex_unlock(cache->lock); // Use abstracted unlock
        PROFILE_END("mcp_cache_put");
        return -1; // Allocation failure
    }

    // Initialize entry
    entry->content_count = 0; // Will be incremented as items are copied
    entry->last_accessed = time(NULL);
    time_t effective_ttl = (ttl_seconds == 0) ? cache->default_ttl_seconds : (time_t)ttl_seconds;
    entry->expiry_time = (effective_ttl < 0) ? 0 : entry->last_accessed + effective_ttl; // 0 means never expires
    entry->is_pooled = true; // Mark as pooled since we're using the pool

    // Acquire pooled items and copy content into them
    bool copy_error = false;
    for (size_t i = 0; i < content_count; ++i) {
        // Acquire a pooled item and copy data into it
        entry->content[i] = mcp_content_item_acquire_pooled(
            pool,
            content[i]->type,
            content[i]->mime_type,
            content[i]->data,
            content[i]->data_size
        );
        if (!entry->content[i]) {
            copy_error = true;
            // Release already acquired/copied items back to pool
            for(size_t j = 0; j < i; ++j) {
                 if (entry->content[j]) {
                     // Free internal data/mime_type first
                     free(entry->content[j]->mime_type);
                     free(entry->content[j]->data);
                     entry->content[j]->mime_type = NULL;
                     entry->content[j]->data = NULL;
                     entry->content[j]->data_size = 0;
                     mcp_object_pool_release(pool, entry->content[j]);
                 }
            }
            break; // Exit loop on acquisition/copy failure
        }
        entry->content_count++; // Increment count only after successful copy
    }

    if (copy_error) {
        free(entry->content); // Free the array of pointers
        free(entry);          // Free the entry struct
        mcp_mutex_unlock(cache->lock); // Use abstracted unlock
        PROFILE_END("mcp_cache_put");
        return -1; // Allocation failure during content copy
    }

    // Add entry to hash table (this will replace any existing entry with the same key)
    // mcp_hashtable_put takes ownership of the 'entry' pointer if successful.
    // If it fails, we need to free the entry and its contents.
    int result = mcp_hashtable_put(cache->table, uri, entry);
    if (result != 0) {
        // hashtable put failed, free the entry we created
        for (size_t i = 0; i < entry->content_count; ++i) {
            if (entry->content[i]) {
                // Free internal data/mime_type first
                free(entry->content[i]->mime_type);
                free(entry->content[i]->data);
                entry->content[i]->mime_type = NULL;
                entry->content[i]->data = NULL;
                entry->content[i]->data_size = 0;
                mcp_object_pool_release(pool, entry->content[i]);
            }
        }
        free(entry->content); // Free the array of pointers
        free(entry);          // Free the entry struct
    }

    mcp_mutex_unlock(cache->lock); // Use abstracted unlock
    PROFILE_END("mcp_cache_put");
    return result;
}

int mcp_cache_invalidate(mcp_resource_cache_t* cache, const char* uri) {
    if (!cache || !uri) return -1;

    mcp_mutex_lock(cache->lock); // Use abstracted lock

    // Get the entry first so we can clean it up properly
    mcp_cache_entry_t* entry = NULL;
    if (mcp_hashtable_get(cache->table, uri, (void**)&entry) == 0 && entry != NULL) {
        // Clean up the entry
        cleanup_cache_entry(cache, entry);

        // Now remove it from the hash table
        // mcp_hashtable_remove calls the value free function (free_cache_entry)
        int result = mcp_hashtable_remove(cache->table, uri);
        mcp_mutex_unlock(cache->lock); // Use abstracted unlock
        return result;
    }

    mcp_mutex_unlock(cache->lock); // Use abstracted unlock
    return -1; // Entry not found
}

// Create a struct to hold all the data needed for the callback
typedef struct {
    time_t now;
    char** keys_to_remove;
    mcp_cache_entry_t** entries_to_cleanup; // Added entries to cleanup
    size_t* keys_count;
    size_t* keys_capacity;
    size_t* removed_count;
    mcp_resource_cache_t* cache; // Added cache pointer for cleanup
} expired_keys_data_t;

// Callback function for mcp_hashtable_foreach to collect expired keys
static void collect_expired_keys(const void* key, void* value, void* user_data) {
    const char* uri = (const char*)key;
    mcp_cache_entry_t* entry = (mcp_cache_entry_t*)value;
    expired_keys_data_t* data = (expired_keys_data_t*)user_data;

    if (entry->expiry_time != 0 && data->now >= entry->expiry_time) {
        // This entry is expired, add its key and entry to our list
        if (*(data->keys_count) >= *(data->keys_capacity)) {
            // Resize the keys array if needed
            size_t new_capacity = *(data->keys_capacity) * 2;
            if (new_capacity == 0) new_capacity = 16; // Handle initial zero capacity
            char** new_keys = (char**)realloc(data->keys_to_remove, new_capacity * sizeof(char*));
            if (!new_keys) {
                mcp_log_error("Failed to realloc keys_to_remove in prune_expired");
                return; // Allocation failed, skip this key
            }
            data->keys_to_remove = new_keys;
            *(data->keys_capacity) = new_capacity;
        }

        // Duplicate the key (will be freed after removal)
        data->keys_to_remove[*(data->keys_count)] = mcp_strdup(uri);
        if (data->keys_to_remove[*(data->keys_count)]) {
            // Store the entry pointer in the user data for cleanup
            if (*(data->keys_count) < *(data->keys_capacity)) {
                data->entries_to_cleanup[*(data->keys_count)] = entry;
            }
            (*(data->keys_count))++;
            (*(data->removed_count))++;
        }
    }
}

size_t mcp_cache_prune_expired(mcp_resource_cache_t* cache) {
    if (!cache) return 0;

    mcp_mutex_lock(cache->lock); // Use abstracted lock

    size_t removed_count = 0;
    time_t now = time(NULL);

    // We need to collect keys to remove since we can't modify the hash table during iteration
    char** keys_to_remove = NULL;
    mcp_cache_entry_t** entries_to_cleanup = NULL;
    size_t keys_count = 0;
    size_t keys_capacity = 16; // Initial capacity

    keys_to_remove = (char**)malloc(keys_capacity * sizeof(char*));
    if (!keys_to_remove) {
        mcp_log_error("Failed to allocate initial keys_to_remove in prune_expired");
        mcp_mutex_unlock(cache->lock); // Use abstracted unlock
        return 0;
    }

    entries_to_cleanup = (mcp_cache_entry_t**)malloc(keys_capacity * sizeof(mcp_cache_entry_t*));
    if (!entries_to_cleanup) {
        mcp_log_error("Failed to allocate entries_to_cleanup in prune_expired");
        free(keys_to_remove);
        mcp_mutex_unlock(cache->lock); // Use abstracted unlock
        return 0;
    }

    // Create the data structure for the callback
    expired_keys_data_t callback_data = {
        .now = now,
        .keys_to_remove = keys_to_remove,
        .entries_to_cleanup = entries_to_cleanup,
        .keys_count = &keys_count,
        .keys_capacity = &keys_capacity,
        .removed_count = &removed_count,
        .cache = cache
    };

    // Iterate through all entries to find expired ones
    mcp_hashtable_foreach(cache->table, collect_expired_keys, &callback_data);

    // Update keys_to_remove pointer in case realloc changed it
    keys_to_remove = callback_data.keys_to_remove;
    entries_to_cleanup = callback_data.entries_to_cleanup;

    // Clean up and remove all collected expired entries
    for (size_t i = 0; i < keys_count; i++) {
        if (keys_to_remove[i] && entries_to_cleanup[i]) {
            // Clean up the entry first
            cleanup_cache_entry(cache, entries_to_cleanup[i]);

            // Now remove it from the hash table
            mcp_hashtable_remove(cache->table, keys_to_remove[i]);
            free(keys_to_remove[i]); // Free the duplicated key string
        }
    }

    // Free the arrays
    free(keys_to_remove);
    free(entries_to_cleanup);

    mcp_mutex_unlock(cache->lock); // Use abstracted unlock
    return removed_count;
}
