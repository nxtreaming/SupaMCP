#include "mcp_types.h"
#include "mcp_cache.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <limits.h>

#define LRU_K_VALUE 2 // Define K for LRU-K
#define DEFAULT_NUM_LOCKS 16 // Default number of lock stripes

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
    mcp_content_item_t** content;   // Value (array of pointers to copies, malloc'd)
    size_t content_count;           // Number of items in the content array
    time_t expiry_time;             // Absolute expiration time (0 for never expires)
    // LRU-K specific fields
    time_t access_history[LRU_K_VALUE]; // Timestamps of last K accesses (history[0] is latest)
    size_t access_count;            // Number of accesses recorded (up to K)
    // ---
    bool valid;                     // Flag indicating if the entry is currently valid
} mcp_cache_entry_t;

// Internal cache structure
struct mcp_resource_cache {
    mutex_t* locks;                 // Array of mutexes for lock striping
    size_t num_locks;               // Number of locks in the array
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
// Free resources associated with a cache entry and reset LRU-K fields
static void free_cache_entry_contents(mcp_cache_entry_t* entry) {
    if (!entry) return;
    free(entry->uri);
    if (entry->content) {
        for (size_t i = 0; i < entry->content_count; ++i) {
            // mcp_content_item_free frees the item pointed to AND its internal data
            mcp_content_item_free(entry->content[i]);
        }
        free(entry->content); // Free the array of pointers
    }
    entry->uri = NULL;
    entry->content = NULL;
    entry->content_count = 0;
    entry->valid = false;
    // Reset LRU-K fields
    entry->access_count = 0;
    for (int k = 0; k < LRU_K_VALUE; ++k) {
        entry->access_history[k] = 0;
    }
}

/**
 * @internal
 * @brief Gets the appropriate lock for a given URI based on its hash.
 * @param cache The cache instance.
 * @param uri The URI to hash.
 * @return Pointer to the mutex_t responsible for this URI.
 */
static mutex_t* get_lock_for_uri(mcp_resource_cache_t* cache, const char* uri) {
    // If num_locks is 0 or locks is NULL, something is wrong, but return first lock to avoid crash.
    // A real implementation might assert or handle this error more gracefully.
    if (cache->num_locks == 0 || cache->locks == NULL) {
        // This case should ideally not happen if create succeeded.
        // Log an error? For now, return the (non-existent) first lock address.
        // This will likely crash, indicating a setup problem.
         return &cache->locks[0]; // Risky fallback
    }
    unsigned long hash = hash_uri(uri);
    size_t lock_index = hash % cache->num_locks;
    return &cache->locks[lock_index];
}


// --- Public API Implementation ---

mcp_resource_cache_t* mcp_cache_create(size_t capacity, time_t default_ttl_seconds) {
    if (capacity == 0) return NULL;

    mcp_resource_cache_t* cache = (mcp_resource_cache_t*)malloc(sizeof(mcp_resource_cache_t));
    if (!cache) return NULL;

    cache->capacity = capacity;
    cache->count = 0;
    cache->default_ttl_seconds = default_ttl_seconds;
    cache->num_locks = DEFAULT_NUM_LOCKS; // Use default for now
    // calloc initializes memory to zero, which is suitable for our new fields
    // (valid=false, access_count=0, access_history={0})
    cache->entries = (mcp_cache_entry_t*)calloc(capacity, sizeof(mcp_cache_entry_t));
    cache->locks = (mutex_t*)malloc(cache->num_locks * sizeof(mutex_t));

    if (!cache->entries || !cache->locks) { // Check allocations
        free(cache->entries);
        free(cache->locks);
        free(cache);
        return NULL;
    }

    // Initialize all locks
    bool locks_ok = true;
    for (size_t i = 0; i < cache->num_locks; ++i) {
        if (mutex_init(&cache->locks[i]) != 0) {
            // Cleanup already initialized locks on failure
            for (size_t j = 0; j < i; ++j) {
                mutex_destroy(&cache->locks[j]);
            }
            locks_ok = false;
            break;
        }
    }

    if (!locks_ok) {
        free(cache->entries);
        free(cache->locks);
        free(cache);
        return NULL;
    }

    return cache;
}

void mcp_cache_destroy(mcp_resource_cache_t* cache) {
    if (!cache) return;

    // No need to lock during destroy, assuming no other threads are using it.
    // If concurrent destroy is possible, locking all stripes would be needed.

    for (size_t i = 0; i < cache->capacity; ++i) {
        if (cache->entries[i].valid) {
            free_cache_entry_contents(&cache->entries[i]);
        }
    }
    free(cache->entries);

    // Destroy and free locks
    if (cache->locks) {
        for (size_t i = 0; i < cache->num_locks; ++i) {
            mutex_destroy(&cache->locks[i]);
        }
        free(cache->locks);
    }

    free(cache);
}

int mcp_cache_get(mcp_resource_cache_t* cache, const char* uri, mcp_content_item_t*** content, size_t* content_count) {
    if (!cache || !uri || !content || !content_count) return -1;

    *content = NULL;
    *content_count = 0;
    int result = -1; // Default to not found/expired
    PROFILE_START("mcp_cache_get");

    mutex_t* lock = get_lock_for_uri(cache, uri);
    mutex_lock(lock);

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
                    // Use mcp_content_item_copy to create a deep copy of the item pointed to by the internal pointer
                    content_copy_ptrs[i] = mcp_content_item_copy(entry->content[i]);
                    if (!content_copy_ptrs[i]) {
                        copy_error = true;
                        break;
                    }
                    copied_count++;
                }

                if (!copy_error) {
                    *content = content_copy_ptrs; // Return the array of pointers
                    *content_count = entry->content_count;

                    // --- Update LRU-K history ---
                    // Shift older times
                    for (int k = LRU_K_VALUE - 1; k > 0; --k) {
                        entry->access_history[k] = entry->access_history[k - 1];
                    }
                    // Record current access time
                    entry->access_history[0] = now;
                    // Increment access count (capped at K)
                    if (entry->access_count < LRU_K_VALUE) {
                        entry->access_count++;
                    }
                    // --- End LRU-K update ---

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

    mutex_unlock(lock);
    PROFILE_END("mcp_cache_get");
    return result;
}

int mcp_cache_put(mcp_resource_cache_t* cache, const char* uri, mcp_content_item_t** content, size_t content_count, int ttl_seconds) {
    // Note: 'content' is now mcp_content_item_t** (array of pointers)
    if (!cache || !uri || !content || content_count == 0) return -1;
    PROFILE_START("mcp_cache_put");

    mutex_t* lock = get_lock_for_uri(cache, uri);
    mutex_lock(lock);


    // --- 1. Find Existing Entry or Empty/Eviction Slot ---
    mcp_cache_entry_t* entry = find_cache_entry(cache, uri, true); // Try to find existing or an empty slot

    if (!entry) { // Cache is full (no empty slots found)
        // --- 1a. Cache Full: Implement LRU-K (K=2) Eviction ---
        size_t evict_index = SIZE_MAX;
        time_t oldest_k_minus_1_time = time(NULL) + 1; // Initialize to future time
        time_t oldest_0_time_for_less_than_k = time(NULL) + 1; // Initialize to future time
        bool found_less_than_k = false;

        for (size_t i = 0; i < cache->capacity; ++i) {
            mcp_cache_entry_t* candidate = &cache->entries[i];
            // Consider only valid, non-permanent entries for eviction
            if (!candidate->valid || candidate->expiry_time == 0) {
                continue;
            }

            if (candidate->access_count < LRU_K_VALUE) {
                // Priority 1: Entries accessed less than K times
                if (!found_less_than_k || candidate->access_history[0] < oldest_0_time_for_less_than_k) {
                    oldest_0_time_for_less_than_k = candidate->access_history[0];
                    evict_index = i;
                    found_less_than_k = true;
                }
            } else if (!found_less_than_k) {
                // Priority 2: Entries accessed K times (only if no <K entries found)
                // Compare based on the K-th last access time (index K-1)
                if (candidate->access_history[LRU_K_VALUE - 1] < oldest_k_minus_1_time) {
                    oldest_k_minus_1_time = candidate->access_history[LRU_K_VALUE - 1];
                    evict_index = i;
                }
            }
        }

        if (evict_index == SIZE_MAX) {
            // Should not happen if cache is full of evictable items.
            // Fallback or error? For now, log and maybe evict index 0 as a last resort.
            log_message(LOG_LEVEL_WARN, "LRU-K eviction failed to find a candidate. Evicting index 0.");
            evict_index = 0; // Simple fallback
        }

        entry = &cache->entries[evict_index];
        log_message(LOG_LEVEL_INFO, "Cache full, LRU-K evicting entry at index %zu (URI: %s) for new URI %s",
                evict_index, entry->uri ? entry->uri : "<empty>", uri);
        free_cache_entry_contents(entry); // Free the victim's contents
        // cache->count remains the same as we are replacing a valid entry
        // --- End LRU-K Eviction ---

    } else if (entry->valid) {
        // --- 1b. Overwriting Existing Valid Entry ---
        log_message(LOG_LEVEL_DEBUG, "Overwriting existing cache entry for URI: %s", uri);
        free_cache_entry_contents(entry);
        cache->count--; // Decrement count since we are replacing a valid entry
    }
    // else: Found an invalid slot, no need to decrement count


    // --- 2. Copy URI ---
    entry->uri = mcp_strdup(uri);
    if (!entry->uri) {
        mutex_unlock(lock);
        return -1; // Allocation failure
    }


    // --- 3. Allocate and Copy Content Items ---
    // Allocate space for the array of content item POINTERS
    entry->content = (mcp_content_item_t**)malloc(content_count * sizeof(mcp_content_item_t*));
    if (!entry->content) {
        free(entry->uri);
        entry->uri = NULL;
        mutex_unlock(lock);
        return -1; // Allocation failure
    }

    bool copy_error = false;
    entry->content_count = 0; // Track successful copies
    for (size_t i = 0; i < content_count; ++i) {
        // Deep copy the item pointed to by the input array
        entry->content[i] = mcp_content_item_copy(content[i]);
        if (!entry->content[i]) {
            copy_error = true;
            // Free already copied items in the cache entry's pointer array
            for(size_t j=0; j<i; ++j) mcp_content_item_free(entry->content[j]);
            break;
        }
        entry->content_count++;
    }


    if (copy_error) {
        free_cache_entry_contents(entry); // Frees URI and partially copied content array
        mutex_unlock(lock);
        return -1; // Allocation failure during content copy
    }


    // --- 4. Set Metadata (Valid, Expiry, LRU-K) ---
    entry->valid = true;
    time_t now = time(NULL);
    // Initialize LRU-K history for new entry
    entry->access_history[0] = now;
    for (int k = 1; k < LRU_K_VALUE; ++k) {
        entry->access_history[k] = 0; // Initialize older history slots
    }
    entry->access_count = 1; // First access

    time_t effective_ttl = (ttl_seconds == 0) ? cache->default_ttl_seconds : (time_t)ttl_seconds;
    entry->expiry_time = (effective_ttl < 0) ? 0 : now + effective_ttl; // 0 means never expires


    // --- 5. Update Cache Count (Non-atomic, potential race under high contention) ---
    // Note: cache->count is not protected by the striped lock.
    // For accurate count, a separate atomic counter or global lock would be needed.
    // For now, accept potential inaccuracy in 'count' under high contention.
    // A simple fix might be to use atomic increment/decrement if available.
    // __atomic_fetch_add(&cache->count, 1, __ATOMIC_RELAXED); // Example if using GCC/Clang atomics
    cache->count++; // Non-atomic increment (potential race)

    mutex_unlock(lock);
    PROFILE_END("mcp_cache_put");
    return 0; // Success
}


int mcp_cache_invalidate(mcp_resource_cache_t* cache, const char* uri) {
    if (!cache || !uri) return -1;

    mutex_t* lock = get_lock_for_uri(cache, uri);
    mutex_lock(lock);

    mcp_cache_entry_t* entry = find_cache_entry(cache, uri, false);
    int result = -1;

    if (entry && entry->valid) {
        free_cache_entry_contents(entry);
        // __atomic_fetch_sub(&cache->count, 1, __ATOMIC_RELAXED); // Example atomic decrement
        cache->count--; // Non-atomic decrement (potential race)
        result = 0; // Found and invalidated
    }
    // else: Not found or already invalid

    mutex_unlock(lock);
    return result;
}

size_t mcp_cache_prune_expired(mcp_resource_cache_t* cache) {
    if (!cache || cache->num_locks == 0) return 0;

    // Lock all stripes - order matters to prevent deadlock if another
    // operation needs multiple locks (though current ops only need one).
    // Simple ascending order lock acquisition.
    for (size_t i = 0; i < cache->num_locks; ++i) {
        mutex_lock(&cache->locks[i]);
    }

    size_t removed_count = 0;
    size_t current_valid_count = 0; // Recalculate count while holding all locks
    time_t now = time(NULL);

    for (size_t i = 0; i < cache->capacity; ++i) {
        mcp_cache_entry_t* entry = &cache->entries[i];
        if (entry->valid) {
             if (entry->expiry_time != 0 && now >= entry->expiry_time) {
                free_cache_entry_contents(entry);
                removed_count++;
            } else {
                current_valid_count++; // Count remaining valid entries
            }
        }
    }
    // Update the potentially inaccurate count atomically (or at least while locked)
    cache->count = current_valid_count;

    // Unlock all stripes in reverse order of acquisition
    for (size_t i = 0; i < cache->num_locks; ++i) {
         mutex_unlock(&cache->locks[cache->num_locks - 1 - i]);
    }

    return removed_count;
}
