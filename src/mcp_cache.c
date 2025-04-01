#include "mcp_types.h"
#include "mcp_cache.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN // Exclude less-used parts of windows.h
#include <windows.h>        // Include windows.h AFTER defining WIN32_LEAN_AND_MEAN
// Windows-specific implementation details
typedef CRITICAL_SECTION mutex_t;
#define mutex_init(m) (InitializeCriticalSection(m), 0)
#define mutex_lock(m) (EnterCriticalSection(m), 0)
#define mutex_unlock(m) (LeaveCriticalSection(m), 0)
#define mutex_destroy(m) (DeleteCriticalSection(m), 0)
#else
// POSIX (pthreads) implementation details
typedef pthread_mutex_t mutex_t;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#define mutex_destroy(m) pthread_mutex_destroy(m)
#endif

// Structure for a cache entry
typedef struct {
    char* uri;                      // Key (malloc'd)
    mcp_content_item_t* content;    // Value (array of copies, malloc'd)
    size_t content_count;           // Number of items in the content array
    time_t expiry_time;             // Absolute expiration time (0 for never expires)
    time_t last_accessed;           // For potential LRU eviction (not implemented yet)
    bool valid;                     // Flag indicating if the entry is currently valid
} mcp_cache_entry_t;

// Internal cache structure
struct mcp_resource_cache {
    mutex_t lock;                   // Mutex for thread safety
    mcp_cache_entry_t* entries;     // Hash table (array) of entries
    size_t capacity;                // Max number of entries
    size_t count;                   // Current number of valid entries
    time_t default_ttl_seconds;     // Default TTL for new entries
    // Add fields for eviction strategy if needed (e.g., LRU list pointers)
};

// --- Helper Functions ---

// Simple string hash function (djb2)
static unsigned long hash_uri(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

// Find an entry in the cache table using linear probing
// Returns pointer to the entry if found, or pointer to an empty/invalid slot for insertion
// Returns NULL if table is full and key not found
static mcp_cache_entry_t* find_cache_entry(mcp_resource_cache_t* cache, const char* uri, bool find_empty_for_insert) {
    if (!uri || cache->capacity == 0) return NULL; // Added capacity check

    unsigned long hash = hash_uri(uri);
    size_t index = hash % cache->capacity;
    size_t original_index = index;
    mcp_cache_entry_t* first_invalid_slot = NULL;

    do {
        mcp_cache_entry_t* entry = &cache->entries[index];

        if (entry->valid && entry->uri && strcmp(entry->uri, uri) == 0) {
            // Found the exact key
            return entry;
        } else if (!entry->valid) {
            // Found an invalid slot
            if (find_empty_for_insert) {
                 // If looking for insert slot, return this one immediately
                 // (or the first invalid one if we already passed one)
                return first_invalid_slot ? first_invalid_slot : entry;
            } else if (first_invalid_slot == NULL) {
                 // Remember the first invalid slot encountered while searching
                 first_invalid_slot = entry;
            }
        }
        // else: Collision or valid but different key, continue probing

        index = (index + 1) % cache->capacity; // Move to next slot (wraps around)
    } while (index != original_index);

    // Table is full or key not found after full scan
    // If looking for insert slot, return the first invalid slot we found (if any)
    return find_empty_for_insert ? first_invalid_slot : NULL;
}

// Free resources associated with a cache entry
static void free_cache_entry_contents(mcp_cache_entry_t* entry) {
    if (!entry) return;
    free(entry->uri);
    if (entry->content) {
        for (size_t i = 0; i < entry->content_count; ++i) {
            // Important: mcp_content_item_free frees internal data, not the struct itself
            // when it's part of an array like this.
            mcp_content_item_free(&entry->content[i]);
        }
        free(entry->content); // Free the array of structs
    }
    entry->uri = NULL;
    entry->content = NULL;
    entry->content_count = 0;
    entry->valid = false;
}

// --- Public API Implementation ---

mcp_resource_cache_t* mcp_cache_create(size_t capacity, time_t default_ttl_seconds) {
    if (capacity == 0) return NULL;

    mcp_resource_cache_t* cache = (mcp_resource_cache_t*)malloc(sizeof(mcp_resource_cache_t));
    if (!cache) return NULL;

    cache->capacity = capacity;
    cache->count = 0;
    cache->default_ttl_seconds = default_ttl_seconds;
    cache->entries = (mcp_cache_entry_t*)calloc(capacity, sizeof(mcp_cache_entry_t)); // calloc initializes valid=false

    if (!cache->entries || mutex_init(&cache->lock) != 0) {
        free(cache->entries);
        free(cache);
        return NULL;
    }

    return cache;
}

void mcp_cache_destroy(mcp_resource_cache_t* cache) {
    if (!cache) return;

    mutex_lock(&cache->lock); // Ensure exclusive access during destruction

    for (size_t i = 0; i < cache->capacity; ++i) {
        if (cache->entries[i].valid) {
            free_cache_entry_contents(&cache->entries[i]);
        }
    }
    free(cache->entries);

    mutex_unlock(&cache->lock); // Unlock before destroying mutex
    mutex_destroy(&cache->lock);

    free(cache);
}

int mcp_cache_get(mcp_resource_cache_t* cache, const char* uri, mcp_content_item_t*** content, size_t* content_count) {
    if (!cache || !uri || !content || !content_count) return -1;

    *content = NULL;
    *content_count = 0;
    int result = -1; // Default to not found/expired

    mutex_lock(&cache->lock);

    mcp_cache_entry_t* entry = find_cache_entry(cache, uri, false);

    if (entry && entry->valid) {
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
                    // Use mcp_content_item_copy to create a deep copy of each item struct
                    content_copy_ptrs[i] = mcp_content_item_copy(&entry->content[i]);
                    if (!content_copy_ptrs[i]) {
                        copy_error = true;
                        break;
                    }
                    copied_count++;
                }

                if (!copy_error) {
                    *content = content_copy_ptrs; // Return the array of pointers
                    *content_count = entry->content_count;
                    entry->last_accessed = now; // Update last accessed time
                    result = 0; // Success
                } else {
                    // Free partially copied items on error
                    for (size_t i = 0; i < copied_count; ++i) {
                        mcp_content_item_free(content_copy_ptrs[i]); // Frees internal data AND the struct pointer itself
                    }
                    free(content_copy_ptrs); // Free the array of pointers
                    // result remains -1 (error)
                }
            }
            // else: allocation failure for copy array, result remains -1
        } else {
            // Expired entry - invalidate it
            free_cache_entry_contents(entry);
            cache->count--;
            // result remains -1 (expired)
        }
    }
    // else: entry not found, result remains -1

    mutex_unlock(&cache->lock);
    return result;
}

int mcp_cache_put(mcp_resource_cache_t* cache, const char* uri, const mcp_content_item_t* content, size_t content_count, int ttl_seconds) {
    if (!cache || !uri || !content || content_count == 0) return -1;

    mutex_lock(&cache->lock);

    mcp_cache_entry_t* entry = find_cache_entry(cache, uri, true);

    if (!entry) {
        // Cache is full, need eviction strategy.
        // Simple strategy: Evict the entry at the hash index (overwrite).
        // TODO: Implement LRU or other strategy if needed.
        unsigned long hash = hash_uri(uri);
        size_t index = hash % cache->capacity;
        entry = &cache->entries[index];
        fprintf(stdout, "Cache full, evicting entry at index %zu for URI %s\n", index, uri); // Log eviction
        free_cache_entry_contents(entry); // Free the old entry's contents
        // Note: cache->count is not decremented here because we are replacing an invalid entry
    } else if (entry->valid) {
        // Overwriting existing valid entry
        free_cache_entry_contents(entry);
        cache->count--; // Decrement count since we are replacing a valid entry
    }
    // else: Found an invalid slot, no need to decrement count

    // Copy URI
    entry->uri = mcp_strdup(uri);
    if (!entry->uri) {
        mutex_unlock(&cache->lock);
        return -1; // Allocation failure
    }

    // Allocate space for the array of content item STRUCTS
    entry->content = (mcp_content_item_t*)malloc(content_count * sizeof(mcp_content_item_t));
    if (!entry->content) {
        free(entry->uri);
        entry->uri = NULL;
        mutex_unlock(&cache->lock);
        return -1; // Allocation failure
    }

    bool copy_error = false;
    entry->content_count = 0; // Track successful copies
    for (size_t i = 0; i < content_count; ++i) {
        // Deep copy each item from the input array into our cache entry's array
        entry->content[i].type = content[i].type;
        entry->content[i].mime_type = content[i].mime_type ? mcp_strdup(content[i].mime_type) : NULL;
        entry->content[i].data_size = content[i].data_size;
        if (content[i].data && content[i].data_size > 0) {
            entry->content[i].data = malloc(content[i].data_size);
            if (!entry->content[i].data) {
                copy_error = true;
                // Free already copied items in the cache entry
                for(size_t j=0; j<i; ++j) mcp_content_item_free(&entry->content[j]);
                break;
            }
            memcpy(entry->content[i].data, content[i].data, content[i].data_size);
        } else {
            entry->content[i].data = NULL;
        }
         // Check for allocation errors during copy
        if ((content[i].mime_type && !entry->content[i].mime_type) ||
            (content[i].data && !entry->content[i].data && content[i].data_size > 0)) { // Check data_size too
            copy_error = true;
            mcp_content_item_free(&entry->content[i]); // Free partially copied item
             // Free already copied items in the cache entry
            for(size_t j=0; j<i; ++j) mcp_content_item_free(&entry->content[j]);
            break;
        }
        entry->content_count++;
    }


    if (copy_error) {
        free_cache_entry_contents(entry); // Frees URI and partially copied content array
        mutex_unlock(&cache->lock);
        return -1; // Allocation failure during content copy
    }

    // Set metadata
    entry->valid = true;
    entry->last_accessed = time(NULL);
    time_t effective_ttl = (ttl_seconds == 0) ? cache->default_ttl_seconds : (time_t)ttl_seconds;
    entry->expiry_time = (effective_ttl < 0) ? 0 : entry->last_accessed + effective_ttl; // 0 means never expires

    cache->count++; // Increment count for the new valid entry

    mutex_unlock(&cache->lock);
    return 0; // Success
}


int mcp_cache_invalidate(mcp_resource_cache_t* cache, const char* uri) {
    if (!cache || !uri) return -1;

    mutex_lock(&cache->lock);

    mcp_cache_entry_t* entry = find_cache_entry(cache, uri, false);
    int result = -1;

    if (entry && entry->valid) {
        free_cache_entry_contents(entry);
        cache->count--;
        result = 0; // Found and invalidated
    }
    // else: Not found or already invalid

    mutex_unlock(&cache->lock);
    return result;
}

size_t mcp_cache_prune_expired(mcp_resource_cache_t* cache) {
    if (!cache) return 0;

    mutex_lock(&cache->lock);

    size_t removed_count = 0;
    time_t now = time(NULL);

    for (size_t i = 0; i < cache->capacity; ++i) {
        mcp_cache_entry_t* entry = &cache->entries[i];
        if (entry->valid && entry->expiry_time != 0 && now >= entry->expiry_time) {
            free_cache_entry_contents(entry);
            cache->count--;
            removed_count++;
        }
    }

    mutex_unlock(&cache->lock);
    return removed_count;
}
