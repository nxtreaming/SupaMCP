#include "mcp_rate_limiter.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_sync.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/** @internal Constants for the rate limiter hash table */
#define RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR 2  /**< Initial capacity factor. Capacity = capacity_hint * factor */
#define RATE_LIMIT_HASH_TABLE_MAX_LOAD_FACTOR 0.75  /**< Load factor threshold for resizing */
#define RATE_LIMIT_MIN_CAPACITY 16  /**< Minimum hash table capacity */

/**
 * @internal
 * @brief Represents an entry in the rate limiter hash table.
 */
typedef struct rate_limit_entry {
    char* client_id;                /**< Client identifier (malloc'd string) */
    time_t window_start_time;       /**< Timestamp when the current window started */
    size_t request_count;           /**< Number of requests received in the current window */
    struct rate_limit_entry* next;  /**< Pointer for separate chaining */
} rate_limit_entry_t;

/**
 * @internal
 * @brief Internal structure for the rate limiter.
 */
struct mcp_rate_limiter {
    mcp_mutex_t *lock;              /**< Mutex for thread safety */
    rate_limit_entry_t** buckets;   /**< Hash table buckets */
    size_t capacity;                /**< Current capacity of the hash table */
    size_t count;                   /**< Current number of entries in the table */
    size_t window_seconds;          /**< Duration of the rate limiting window */
    size_t max_requests_per_window; /**< Max requests allowed per window */
};

/**
 * @internal
 * @brief Computes a hash value for a client ID string using the djb2 algorithm.
 *
 * @param str The client ID string to hash
 * @return The computed hash value
 */
static unsigned long hash_client_id(const char* str) {
    if (!str) return 0;

    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash;
}

/**
 * @internal
 * @brief Frees a single rate limit entry and its client ID string.
 *
 * @param entry The entry to free
 */
static void free_rate_limit_entry(rate_limit_entry_t* entry) {
    if (!entry) return;

    free(entry->client_id);
    free(entry);
}

/**
 * @internal
 * @brief Finds or creates an entry for a client ID. Handles table resizing.
 *
 * @param limiter The rate limiter instance
 * @param client_id The client identifier to find or create
 * @param created Optional pointer to a boolean that will be set to true if a new entry was created
 * @return Pointer to the found or created entry, or NULL on error
 */
static rate_limit_entry_t* find_or_create_entry(mcp_rate_limiter_t* limiter, const char* client_id, bool* created) {
    if (!limiter || !client_id) return NULL;
    if (created) *created = false;

    // Resize if load factor is too high
    if (limiter->capacity == 0 || ((double)limiter->count + 1) / limiter->capacity > RATE_LIMIT_HASH_TABLE_MAX_LOAD_FACTOR) {
        size_t new_capacity = (limiter->capacity == 0) ?
            RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR * RATE_LIMIT_MIN_CAPACITY :
            limiter->capacity * 2;

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

    // Find existing entry
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

/**
 * @brief Creates a new rate limiter instance.
 *
 * @param capacity_hint The approximate maximum number of unique client identifiers to track
 * @param window_seconds The time window duration in seconds for rate limiting
 * @param max_requests_per_window The maximum number of requests allowed per client within the window
 * @return A pointer to the newly created rate limiter, or NULL on failure
 */
mcp_rate_limiter_t* mcp_rate_limiter_create(size_t capacity_hint, size_t window_seconds, size_t max_requests_per_window) {
    if (window_seconds == 0 || max_requests_per_window == 0) {
        return NULL; // Invalid parameters
    }

    // Allocate rate limiter structure
    mcp_rate_limiter_t* limiter = (mcp_rate_limiter_t*)malloc(sizeof(mcp_rate_limiter_t));
    if (!limiter) return NULL;

    // Initialize fields
    limiter->capacity = capacity_hint * RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR;
    if (limiter->capacity < RATE_LIMIT_MIN_CAPACITY) {
        limiter->capacity = RATE_LIMIT_MIN_CAPACITY; // Ensure minimum capacity
    }
    limiter->count = 0;
    limiter->window_seconds = window_seconds;
    limiter->max_requests_per_window = max_requests_per_window;

    // Allocate hash table buckets
    limiter->buckets = (rate_limit_entry_t**)calloc(limiter->capacity, sizeof(rate_limit_entry_t*));
    if (!limiter->buckets) {
        free(limiter);
        return NULL;
    }

    // Create mutex for thread safety
    limiter->lock = mcp_mutex_create();
    if (!limiter->lock) {
        free(limiter->buckets);
        free(limiter);
        return NULL;
    }

    return limiter;
}

/**
 * @brief Destroys the rate limiter and frees all associated memory.
 *
 * @param limiter The rate limiter instance to destroy
 */
void mcp_rate_limiter_destroy(mcp_rate_limiter_t* limiter) {
    if (!limiter) return;

    // Lock to ensure no other threads are accessing the limiter
    mcp_mutex_lock(limiter->lock);

    // Free all entries in the hash table
    for (size_t i = 0; i < limiter->capacity; ++i) {
        rate_limit_entry_t* entry = limiter->buckets[i];
        while (entry) {
            rate_limit_entry_t* next = entry->next;
            free_rate_limit_entry(entry);
            entry = next;
        }
    }

    // Free the buckets array
    free(limiter->buckets);

    // Unlock before destroying the mutex
    mcp_mutex_unlock(limiter->lock);
    mcp_mutex_destroy(limiter->lock);

    // Free the limiter structure itself
    free(limiter);
}

/**
 * @brief Checks if a request from a given client identifier is allowed based on the rate limit.
 *
 * This function is thread-safe. It increments the request count for the client
 * if the request is allowed within the current time window.
 *
 * @param limiter The rate limiter instance
 * @param client_id A string uniquely identifying the client (e.g., IP address)
 * @return True if the request is allowed, false if the client has exceeded the rate limit
 */
bool mcp_rate_limiter_check(mcp_rate_limiter_t* limiter, const char* client_id) {
    if (!limiter || !client_id) {
        return false; // Deny if invalid input
    }

    // Lock for thread safety
    mcp_mutex_lock(limiter->lock);

    // Find or create an entry for this client
    bool created = false;
    rate_limit_entry_t* entry = find_or_create_entry(limiter, client_id, &created);
    if (!entry) {
        mcp_mutex_unlock(limiter->lock);
        return false;
    }

    // Get current time
    time_t current_time = time(NULL);
    bool allowed = false;

    // Check if we need to start a new window
    if (created || current_time >= entry->window_start_time + (time_t)limiter->window_seconds) {
        // Start a new window
        entry->window_start_time = current_time;
        entry->request_count = 1; // This is the first request in the new window
        allowed = true;
    } else {
        // We're within the current window, check if we're under the limit
        if (entry->request_count < limiter->max_requests_per_window) {
            entry->request_count++;
            allowed = true;
        } else {
            // Rate limit exceeded
            allowed = false;
        }
    }

    // Unlock and return result
    mcp_mutex_unlock(limiter->lock);
    return allowed;
}
