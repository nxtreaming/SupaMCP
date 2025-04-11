#include "mcp_rate_limiter.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_sync.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/** @internal Initial capacity factor for the hash table. Capacity = capacity_hint * factor. */
#define RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR 2
/** @internal Load factor threshold for the hash table. */
#define RATE_LIMIT_HASH_TABLE_MAX_LOAD_FACTOR 0.75

/**
 * @internal
 * @brief Represents an entry in the rate limiter hash table.
 */
typedef struct rate_limit_entry {
    char* client_id;                /**< Client identifier (malloc'd string). */
    time_t window_start_time;       /**< Timestamp when the current window started. */
    size_t request_count;           /**< Number of requests received in the current window. */
    struct rate_limit_entry* next;  /**< Pointer for separate chaining. */
} rate_limit_entry_t;

/**
 * @internal
 * @brief Internal structure for the rate limiter.
 */
struct mcp_rate_limiter {
    mcp_mutex_t *lock;              /**< Mutex for thread safety. */
    rate_limit_entry_t** buckets;   /**< Hash table buckets. */
    size_t capacity;                /**< Current capacity of the hash table. */
    size_t count;                   /**< Current number of entries in the table. */
    size_t window_seconds;          /**< Duration of the rate limiting window. */
    size_t max_requests_per_window; /**< Max requests allowed per window. */
};

/** @internal Simple string hash function (djb2). */
static unsigned long hash_client_id(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

/** @internal Frees a single rate limit entry and its client ID string. */
static void free_rate_limit_entry(rate_limit_entry_t* entry) {
    if (!entry) return;
    free(entry->client_id);
    free(entry);
}

/** @internal Finds or creates an entry for a client ID. Handles table resizing. */
static rate_limit_entry_t* find_or_create_entry(mcp_rate_limiter_t* limiter, const char* client_id, bool* created) {
    if (!limiter || !client_id) return NULL;
    if (created) *created = false;

    // Resize if load factor is too high
    if (limiter->capacity == 0 || ((double)limiter->count + 1) / limiter->capacity > RATE_LIMIT_HASH_TABLE_MAX_LOAD_FACTOR) {
        size_t new_capacity = (limiter->capacity == 0) ? RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR * 16 : limiter->capacity * 2; // Start with 16*factor if empty
        rate_limit_entry_t** new_buckets = (rate_limit_entry_t**)calloc(new_capacity, sizeof(rate_limit_entry_t*));
        if (!new_buckets) return NULL; // Resize allocation failed

        // Rehash existing entries
        for (size_t i = 0; i < limiter->capacity; ++i) {
            rate_limit_entry_t* entry = limiter->buckets[i];
            while (entry) {
                rate_limit_entry_t* next = entry->next;
                unsigned long new_hash = hash_client_id(entry->client_id);
                size_t new_index = new_hash % new_capacity;
                entry->next = new_buckets[new_index];
                new_buckets[new_index] = entry;
                entry = next;
            }
        }
        free(limiter->buckets);
        limiter->buckets = new_buckets;
        limiter->capacity = new_capacity;
    }

    // Find existing or insert new
    unsigned long hash = hash_client_id(client_id);
    size_t index = hash % limiter->capacity;
    rate_limit_entry_t* entry = limiter->buckets[index];
    rate_limit_entry_t* prev = NULL;

    while (entry) {
        if (strcmp(entry->client_id, client_id) == 0) {
            return entry; // Found existing entry
        }
        prev = entry;
        entry = entry->next;
    }

    // Not found, create a new entry
    rate_limit_entry_t* new_entry = (rate_limit_entry_t*)malloc(sizeof(rate_limit_entry_t));
    if (!new_entry) return NULL;

    new_entry->client_id = mcp_strdup(client_id);
    if (!new_entry->client_id) {
        free(new_entry);
        return NULL;
    }
    new_entry->window_start_time = 0; // Will be set on first check
    new_entry->request_count = 0;
    new_entry->next = NULL;

    // Add to bucket list
    if (prev == NULL) {
        limiter->buckets[index] = new_entry;
    } else {
        prev->next = new_entry;
    }
    limiter->count++;
    if (created) *created = true;
    return new_entry;
}

mcp_rate_limiter_t* mcp_rate_limiter_create(size_t capacity_hint, size_t window_seconds, size_t max_requests_per_window) {
    if (window_seconds == 0 || max_requests_per_window == 0) {
        return NULL; // Invalid parameters
    }

    mcp_rate_limiter_t* limiter = (mcp_rate_limiter_t*)malloc(sizeof(mcp_rate_limiter_t));
    if (!limiter) return NULL;

    limiter->capacity = capacity_hint * RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR;
    if (limiter->capacity < 16) limiter->capacity = 16; // Minimum capacity
    limiter->count = 0;
    limiter->window_seconds = window_seconds;
    limiter->max_requests_per_window = max_requests_per_window;

    limiter->buckets = (rate_limit_entry_t**)calloc(limiter->capacity, sizeof(rate_limit_entry_t*));
    if (!limiter->buckets) {
        free(limiter);
        return NULL;
    }

    limiter->lock = mcp_mutex_create();
    if (!limiter->lock) {
        free(limiter->buckets);
        free(limiter);
        return NULL;
    }

    return limiter;
}

void mcp_rate_limiter_destroy(mcp_rate_limiter_t* limiter) {
    if (!limiter) return;

    mcp_mutex_lock(limiter->lock); // Lock before destroying
    for (size_t i = 0; i < limiter->capacity; ++i) {
        rate_limit_entry_t* entry = limiter->buckets[i];
        while (entry) {
            rate_limit_entry_t* next = entry->next;
            free_rate_limit_entry(entry);
            entry = next;
        }
    }
    free(limiter->buckets);
    mcp_mutex_unlock(limiter->lock); // Unlock before destroying
    mcp_mutex_destroy(limiter->lock);
    free(limiter);
}

bool mcp_rate_limiter_check(mcp_rate_limiter_t* limiter, const char* client_id) {
    if (!limiter || !client_id) return false; // Deny if invalid input

    mcp_mutex_lock(limiter->lock);

    bool created = false;
    rate_limit_entry_t* entry = find_or_create_entry(limiter, client_id, &created);
    if (!entry) {
        mcp_mutex_unlock(limiter->lock);
        fprintf(stderr, "Rate limiter failed to find/create entry for %s\n", client_id);
        return false; // Internal error, deny request
    }

    time_t current_time = time(NULL);
    bool allowed = false;

    // Check if the window needs resetting
    if (created || current_time >= entry->window_start_time + (time_t)limiter->window_seconds) { // Cast window_seconds
        entry->window_start_time = current_time;
        entry->request_count = 1; // Start count for the new window
        allowed = true;
    } else {
        // Within the current window, check count
        if (entry->request_count < limiter->max_requests_per_window) {
            entry->request_count++;
            allowed = true;
        } else {
            // Limit exceeded
            allowed = false;
        }
    }

    mcp_mutex_unlock(limiter->lock);
    return allowed;
}
