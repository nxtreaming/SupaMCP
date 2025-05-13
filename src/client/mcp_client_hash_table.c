#include "internal/client_internal.h"
#include <mcp_log.h>
#include <mcp_memory_pool.h>
#include <mcp_thread_cache.h>
#include <mcp_cache_aligned.h>
#include <stdlib.h>
#include <string.h>

// Performance optimization constants
#define HASH_PRIME_1 0xff51afd7ed558ccdULL
#define HASH_PRIME_2 0xc4ceb9fe1a85ec53ULL
#define INITIAL_PROBE_DISTANCE 1
#define MAX_PROBE_DISTANCE 16

// Forward declarations for internal functions
static int resize_pending_requests_table(mcp_client_t* client);

/**
 * @brief Optimized hash function for request IDs
 *
 * This function implements a high-performance hash algorithm based on the
 * MurmurHash3 finalizer. It provides excellent distribution and performance
 * for sequential IDs, which are common in the client request pattern.
 *
 * @param id The request ID to hash
 * @param table_size The size of the hash table (must be a power of 2)
 * @return The hash value (index in the table)
 */
static inline size_t hash_id(uint64_t id, size_t table_size) {
    // Fast path for small IDs (common case)
    if (id < 1000) {
        // Simple hash for small sequential IDs
        // Still mix to avoid direct mapping
        id = (id * 2654435761ULL) ^ (id >> 16);
        return (size_t)(id & (table_size - 1));
    }

    // For larger IDs, use a more thorough mixing function
    // Based on MurmurHash3 finalizer
    id ^= id >> 33;
    id *= HASH_PRIME_1; // Defined constant for better readability
    id ^= id >> 33;
    id *= HASH_PRIME_2; // Defined constant for better readability
    id ^= id >> 33;

    // Assumes table_size is a power of 2
    // This is faster than modulo for power-of-2 sizes
    return (size_t)(id & (table_size - 1));
}

/**
 * @brief Find an entry in the hash table using optimized linear probing
 *
 * This optimized function efficiently searches for an entry with the given ID
 * in the hash table. It uses a cache-friendly probing strategy and early termination
 * to improve performance.
 *
 * If find_empty_for_insert is true, it returns the first empty or deleted slot
 * if the key is not found, which can be used for insertion.
 *
 * @param client The MCP client instance
 * @param id The request ID to find
 * @param find_empty_for_insert Whether to return an empty slot for insertion
 * @return The found entry, or NULL if not found
 */
pending_request_entry_t* mcp_client_find_pending_request_entry(
    mcp_client_t* client,
    uint64_t id,
    bool find_empty_for_insert
) {
    // Fast validation - ID 0 is reserved for empty slots
    if (id == 0 || client == NULL || client->pending_requests_table == NULL) {
        return NULL;
    }

    // Calculate the initial index using the optimized hash function
    const size_t capacity = client->pending_requests_capacity;
    size_t index = hash_id(id, capacity);
    pending_request_entry_t* first_deleted_slot = NULL;

    // Use a reasonable probe limit for better performance
    // This avoids scanning the entire table in pathological cases
    const size_t max_probes = capacity < 1024 ? capacity : MAX_PROBE_DISTANCE;

    // Start with a small probe distance for better cache locality
    size_t probe_distance = INITIAL_PROBE_DISTANCE;
    size_t probe_count = 0;

    // Search for the entry using optimized linear probing
    while (probe_count < max_probes) {
        // Get the current entry
        pending_request_entry_t* entry = &client->pending_requests_table[index];

        // Check for exact match (most common case first for branch prediction)
        if (entry->id == id) {
            return entry;
        }
        // Check for empty slot
        else if (entry->id == 0) {
            // Found an empty slot, key is not in the table
            // Return the first deleted slot if we found one, otherwise the empty slot
            return find_empty_for_insert ?
                   (first_deleted_slot ? first_deleted_slot : entry) : NULL;
        }
        // Check for deleted slot
        else if (entry->request.status == PENDING_REQUEST_INVALID) {
            // Found a deleted slot, remember the first one for potential insertion
            if (find_empty_for_insert && first_deleted_slot == NULL) {
                first_deleted_slot = entry;
            }
        }
        // Collision, continue probing

        // Use quadratic probing for better cache performance
        // This helps avoid clustering while maintaining good locality
        probe_distance += 1;
        index = (index + probe_distance) & (capacity - 1);
        probe_count++;
    }

    // If we're looking for an insertion slot and found a deleted slot, use it
    if (find_empty_for_insert && first_deleted_slot != NULL) {
        return first_deleted_slot;
    }

    // If we've reached the probe limit, do a full table scan as a last resort
    // but only if we're looking for an existing entry (not for insertion)
    if (!find_empty_for_insert && probe_count >= max_probes) {
        mcp_log_debug("Hash table probe limit reached for ID %llu, performing full scan",
                     (unsigned long long)id);

        // Full table scan as a fallback
        for (size_t i = 0; i < capacity; i++) {
            if (client->pending_requests_table[i].id == id) {
                return &client->pending_requests_table[i];
            }
        }
    }

    // Table is full or key not found
    if (probe_count >= max_probes) {
        mcp_log_error("Hash table probe limit reached for ID %llu", (unsigned long long)id);
    }

    return NULL;
}

/**
 * @brief Add a request to the hash table with optimized performance
 *
 * This optimized function efficiently adds a new pending request to the hash table.
 * It checks if the table needs to be resized before adding the request, and then
 * finds an empty slot for insertion using cache-friendly algorithms.
 *
 * @param client The MCP client instance
 * @param id The request ID to add
 * @param request The pending request to add
 * @return 0 on success, -1 on failure
 */
int mcp_client_add_pending_request_entry(
    mcp_client_t* client,
    uint64_t id,
    pending_request_t* request
) {
    // Fast validation of input parameters
    if (client == NULL || request == NULL || id == 0) {
        mcp_log_error("Invalid parameters for adding pending request");
        return -1;
    }

    // Check if the table is getting too full and needs resizing
    // This is critical for maintaining good performance as the table fills up
    const size_t new_count = client->pending_requests_count + 1;
    const size_t capacity = client->pending_requests_capacity;

    // Calculate load factor - use integer math for better performance
    // We multiply by 100 to get a percentage without floating point
    const size_t load_percentage = (new_count * 100) / capacity;

    // Check if we need to resize (load factor >= 70%)
    if (load_percentage >= HASH_TABLE_MAX_LOAD_FACTOR * 100) {
        mcp_log_debug("Hash table load factor %zu%% exceeds threshold, resizing",
                     load_percentage);

        // Try to resize the table
        if (resize_pending_requests_table(client) != 0) {
            mcp_log_error("Failed to resize hash table for request %llu",
                         (unsigned long long)id);
            return -1;
        }

        // After resize, capacity has changed - no need to recalculate anything here
        // as find_pending_request_entry will use the new capacity
    }

    // Find an empty slot for insertion using our optimized find function
    pending_request_entry_t* entry = mcp_client_find_pending_request_entry(client, id, true);

    // Handle error cases
    if (entry == NULL) {
        mcp_log_error("Hash table full or failed to find slot for request %llu",
                     (unsigned long long)id);
        return -1;
    }

    // Check for duplicate ID (should never happen with proper ID generation)
    if (entry->id == id) {
        mcp_log_error("Duplicate request ID found in hash table: %llu",
                     (unsigned long long)id);
        return -1;
    }

    // Found an empty or deleted slot - initialize it
    entry->id = id;

    // Copy the request data (including the created CV pointer)
    // Use memcpy for better performance with larger structures
    memcpy(&entry->request, request, sizeof(pending_request_t));

    // Update the count
    client->pending_requests_count++;

    // Log at debug level
    if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
        mcp_log_debug("Added request %llu to hash table (count: %zu/%zu, load: %zu%%)",
                     (unsigned long long)id,
                     client->pending_requests_count,
                     client->pending_requests_capacity,
                     (client->pending_requests_count * 100) / client->pending_requests_capacity);
    }

    return 0;
}

/**
 * @brief Remove a request from the hash table with optimized performance
 *
 * This optimized function efficiently removes a pending request from the hash table
 * by marking it as invalid. It also properly cleans up the condition variable
 * associated with the request.
 *
 * @param client The MCP client instance
 * @param id The request ID to remove
 * @return 0 on success, -1 if not found or already invalid
 */
int mcp_client_remove_pending_request_entry(mcp_client_t* client, uint64_t id) {
    // Fast validation of input parameters
    if (client == NULL || id == 0 || client->pending_requests_table == NULL) {
        return -1;
    }

    // Find the entry using our optimized find function
    pending_request_entry_t* entry = mcp_client_find_pending_request_entry(client, id, false);

    // Process the entry if found and valid
    if (entry != NULL && entry->request.status != PENDING_REQUEST_INVALID) {
        // Destroy condition variable before marking as invalid
        if (entry->request.cv != NULL) {
            mcp_cond_destroy(entry->request.cv);
            entry->request.cv = NULL;
        }

        // Mark as invalid but keep ID for tombstone/probing
        // This is important for the linear probing algorithm to work correctly
        entry->request.status = PENDING_REQUEST_INVALID;

        // Update the count
        client->pending_requests_count--;

        // Log at debug level
        if (mcp_log_get_level() >= MCP_LOG_LEVEL_DEBUG) {
            const size_t load_percentage =
                client->pending_requests_count > 0 ?
                (client->pending_requests_count * 100) / client->pending_requests_capacity : 0;

            mcp_log_debug("Removed request %llu from hash table (count: %zu/%zu, load: %zu%%)",
                         (unsigned long long)id,
                         client->pending_requests_count,
                         client->pending_requests_capacity,
                         load_percentage);
        }

        return 0;
    }

    // Entry not found or already invalid
    if (entry == NULL) {
        mcp_log_debug("Request %llu not found in hash table", (unsigned long long)id);
    } else {
        mcp_log_debug("Request %llu already marked as invalid", (unsigned long long)id);
    }

    return -1;
}

/**
 * @brief Resize the hash table when load factor exceeds the threshold
 *
 * This optimized function efficiently creates a new hash table with twice the capacity
 * and rehashes all existing entries into the new table using cache-friendly algorithms.
 *
 * @param client The MCP client instance
 * @return 0 on success, -1 on failure
 */
static int resize_pending_requests_table(mcp_client_t* client) {
    // Fast validation
    if (client == NULL || client->pending_requests_table == NULL) {
        return -1;
    }

    // Calculate new capacity (double the current capacity)
    const size_t old_capacity = client->pending_requests_capacity;
    const size_t new_capacity = old_capacity * 2;

    // Ensure capacity doesn't wrap around or become excessively large
    if (new_capacity <= old_capacity || new_capacity > (1024 * 1024)) {
        mcp_log_error("Hash table resize failed: new capacity %zu invalid", new_capacity);
        return -1;
    }

    mcp_log_info("Resizing hash table from %zu to %zu entries", old_capacity, new_capacity);

    // Allocate new table using thread cache for better performance
    pending_request_entry_t* new_table = (pending_request_entry_t*)mcp_thread_cache_alloc(
        new_capacity * sizeof(pending_request_entry_t));

    if (new_table == NULL) {
        // Fall back to standard allocation if thread cache fails
        new_table = (pending_request_entry_t*)calloc(new_capacity, sizeof(pending_request_entry_t));

        if (new_table == NULL) {
            mcp_log_error("Hash table resize failed: memory allocation failed for %zu entries",
                         new_capacity);
            return -1;
        }
    } else {
        // Thread cache doesn't zero memory, so we need to do it manually
        memset(new_table, 0, new_capacity * sizeof(pending_request_entry_t));
    }

    // Initialize all entries in the new table
    for (size_t i = 0; i < new_capacity; ++i) {
        new_table[i].id = 0; // Mark as empty
        new_table[i].request.status = PENDING_REQUEST_INVALID;
        new_table[i].request.cv = NULL;
    }

    // Rehash all existing valid entries from the old table
    size_t rehashed_count = 0;
    const pending_request_entry_t* const old_table = client->pending_requests_table;

    // Process entries in chunks for better cache locality
    const size_t chunk_size = 16; // Process 16 entries at a time

    for (size_t chunk_start = 0; chunk_start < old_capacity; chunk_start += chunk_size) {
        // Calculate end of this chunk (capped at old_capacity)
        const size_t chunk_end = (chunk_start + chunk_size < old_capacity) ?
                                chunk_start + chunk_size : old_capacity;

        // Process all entries in this chunk
        for (size_t i = chunk_start; i < chunk_end; ++i) {
            const pending_request_entry_t* old_entry = &old_table[i];

            // Skip empty or deleted entries
            if (old_entry->id == 0 || old_entry->request.status == PENDING_REQUEST_INVALID) {
                continue;
            }

            // Find new position using our optimized hash function
            size_t index = hash_id(old_entry->id, new_capacity);
            size_t probe_distance = INITIAL_PROBE_DISTANCE;
            size_t probe_count = 0;

            // Use a reasonable probe limit
            const size_t max_probes = new_capacity < 1024 ? new_capacity : 64;

            // Find an empty slot in the new table
            while (probe_count < max_probes) {
                if (new_table[index].id == 0) {
                    // Found empty slot, copy the entry
                    new_table[index] = *old_entry;
                    rehashed_count++;
                    break;
                }

                // Collision, use quadratic probing for better distribution
                probe_distance += 1;
                index = (index + probe_distance) & (new_capacity - 1);
                probe_count++;
            }

            // Handle probe limit reached
            if (probe_count >= max_probes) {
                mcp_log_error("Hash table resize failed: probe limit reached for ID %llu",
                             (unsigned long long)old_entry->id);

                // Clean up and return error
                if (new_table != NULL) {
                    mcp_thread_cache_free(new_table, new_capacity * sizeof(pending_request_entry_t));
                }
                return -1;
            }
        }
    }

    // Verify all entries were rehashed correctly
    if (rehashed_count != client->pending_requests_count) {
        mcp_log_warn("Hash table resize: rehashed %zu entries, expected %zu",
                    rehashed_count, client->pending_requests_count);

        // Update count to match what was actually rehashed
        client->pending_requests_count = rehashed_count;
    }

    // Replace old table with new one
    mcp_thread_cache_free(client->pending_requests_table,
                         old_capacity * sizeof(pending_request_entry_t));

    client->pending_requests_table = new_table;
    client->pending_requests_capacity = new_capacity;

    mcp_log_info("Successfully resized hash table from %zu to %zu entries (count: %zu)",
                old_capacity, new_capacity, rehashed_count);

    return 0;
}
