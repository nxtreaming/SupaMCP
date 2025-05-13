#include "internal/client_internal.h"
#include <mcp_log.h>
#include <stdlib.h>

// Forward declarations for internal functions
static int resize_pending_requests_table(mcp_client_t* client);

/**
 * @brief Hash function for request IDs
 *
 * This is a simple hash function that uses bitwise AND for power-of-2 table size.
 * For better distribution, we could use a more sophisticated hash function like FNV-1a.
 *
 * @param id The request ID to hash
 * @param table_size The size of the hash table (must be a power of 2)
 * @return The hash value (index in the table)
 */
static size_t hash_id(uint64_t id, size_t table_size) {
    // For better distribution, use a simple mixing function
    // This helps avoid clustering with sequential IDs
    id ^= id >> 33;
    id *= 0xff51afd7ed558ccdULL;
    id ^= id >> 33;
    id *= 0xc4ceb9fe1a85ec53ULL;
    id ^= id >> 33;

    // Assumes table_size is a power of 2
    return (size_t)(id & (table_size - 1));
}

/**
 * @brief Find an entry in the hash table using linear probing
 *
 * This function searches for an entry with the given ID in the hash table.
 * If find_empty_for_insert is true, it returns the first empty or deleted slot
 * if the key is not found, which can be used for insertion.
 *
 * @param client The MCP client instance
 * @param id The request ID to find
 * @param find_empty_for_insert Whether to return an empty slot for insertion
 * @return The found entry, or NULL if not found
 */
pending_request_entry_t* mcp_client_find_pending_request_entry(mcp_client_t* client, uint64_t id, bool find_empty_for_insert) {
    // ID 0 is reserved for empty slots
    if (id == 0)
        return NULL;

    // Calculate the initial index using the hash function
    size_t index = hash_id(id, client->pending_requests_capacity);
    size_t original_index = index;
    pending_request_entry_t* first_deleted_slot = NULL;

    // Use a constant for the maximum number of probes to avoid infinite loops
    // This is a safety measure in case the table is corrupted
    const size_t max_probes = client->pending_requests_capacity;
    size_t probe_count = 0;

    // Search for the entry using linear probing
    do {
        pending_request_entry_t* entry = &client->pending_requests_table[index];

        if (entry->id == id) {
            // Found the exact key
            return entry;
        } else if (entry->id == 0) {
            // Found an empty slot, key is not in the table
            return find_empty_for_insert ? (first_deleted_slot ? first_deleted_slot : entry) : NULL;
        } else if (entry->request.status == PENDING_REQUEST_INVALID) {
            // Found a deleted slot (marked as invalid), remember the first one
            if (find_empty_for_insert && first_deleted_slot == NULL) {
                first_deleted_slot = entry;
            }
        }
        // else: Collision, continue probing

        // Move to next slot (wraps around)
        index = (index + 1) & (client->pending_requests_capacity - 1);

        // Safety check to avoid infinite loops
        if (++probe_count >= max_probes) {
            mcp_log_error("Hash table probe limit reached for ID %llu", (unsigned long long)id);
            break;
        }
    } while (index != original_index);

    // Table is full or key not found after full scan
    return find_empty_for_insert ? first_deleted_slot : NULL;
}

/**
 * @brief Add a request to the hash table
 *
 * This function adds a new pending request to the hash table. It checks if
 * the table needs to be resized before adding the request, and then finds
 * an empty slot for insertion.
 *
 * @param client The MCP client instance
 * @param id The request ID to add
 * @param request The pending request to add
 * @return 0 on success, -1 on failure
 */
int mcp_client_add_pending_request_entry(mcp_client_t* client, uint64_t id, pending_request_t* request) {
    if (client == NULL || request == NULL || id == 0) {
        mcp_log_error("Invalid parameters for adding pending request");
        return -1;
    }

    // Check load factor BEFORE trying to find a slot, resize if needed
    // Note: This check happens under the pending_requests_mutex lock in the calling function
    float load_factor = (float)(client->pending_requests_count + 1) / client->pending_requests_capacity;
    if (load_factor >= HASH_TABLE_MAX_LOAD_FACTOR) {
        if (resize_pending_requests_table(client) != 0) {
            mcp_log_error("Failed to resize hash table for request %llu", (unsigned long long)id);
            return -1;
        }
        // After resize, capacity has changed, need to recalculate hash/index
    }

    // Find an empty slot for insertion (using the potentially new capacity)
    pending_request_entry_t* entry = mcp_client_find_pending_request_entry(client, id, true);
    if (entry == NULL) {
        mcp_log_error("Hash table full or failed to find slot for insert (ID: %llu)", (unsigned long long)id);
        // Should not happen if resizing is implemented or table not full
        return -1;
    }

    if (entry->id == id) {
        mcp_log_error("Error: Duplicate request ID found in hash table: %llu", (unsigned long long)id);
        // This indicates a logic error (ID reuse before completion) or hash collision issue not handled
        return -1;
    }

    // Found an empty or deleted slot
    entry->id = id;
    // Copy the request data (including the created CV pointer)
    entry->request = *request;
    client->pending_requests_count++;

    mcp_log_debug("Added pending request ID %llu to hash table (count: %zu/%zu)",
                 (unsigned long long)id, client->pending_requests_count, client->pending_requests_capacity);

    return 0;
}

/**
 * @brief Remove a request from the hash table
 *
 * This function removes a pending request from the hash table by marking it
 * as invalid. It also destroys the condition variable associated with the request.
 *
 * @param client The MCP client instance
 * @param id The request ID to remove
 * @return 0 on success, -1 if not found or already invalid
 */
int mcp_client_remove_pending_request_entry(mcp_client_t* client, uint64_t id) {
    if (client == NULL || id == 0) {
        return -1;
    }

    pending_request_entry_t* entry = mcp_client_find_pending_request_entry(client, id, false);
    if (entry != NULL && entry->request.status != PENDING_REQUEST_INVALID) {
        // Destroy CV before marking as invalid
        if (entry->request.cv != NULL) {
            mcp_cond_destroy(entry->request.cv);
            entry->request.cv = NULL;
        }

        // Mark as invalid but keep ID for tombstone/probing
        entry->request.status = PENDING_REQUEST_INVALID;
        client->pending_requests_count--;

        mcp_log_debug("Removed pending request ID %llu from hash table (count: %zu/%zu)",
                     (unsigned long long)id, client->pending_requests_count, client->pending_requests_capacity);

        return 0;
    }

    // Not found or already invalid
    return -1;
}

/**
 * @brief Resize the hash table when load factor exceeds the threshold
 *
 * This function creates a new hash table with twice the capacity and rehashes
 * all existing entries into the new table.
 *
 * @param client The MCP client instance
 * @return 0 on success, -1 on failure
 */
static int resize_pending_requests_table(mcp_client_t* client) {
    if (client == NULL) {
        return -1;
    }

    // Calculate new capacity (double the current capacity)
    size_t new_capacity = client->pending_requests_capacity * 2;

    // Ensure capacity doesn't wrap around or become excessively large
    if (new_capacity <= client->pending_requests_capacity) {
        mcp_log_error("Hash table resize failed: new capacity overflow or too large");
        return -1;
    }

    // Allocate new table
    pending_request_entry_t* new_table = (pending_request_entry_t*)calloc(
        new_capacity, sizeof(pending_request_entry_t));

    if (new_table == NULL) {
        mcp_log_error("Hash table resize failed: calloc returned NULL for new capacity %zu", new_capacity);
        return -1;
    }

    // Initialize all entries in the new table (calloc zeros memory, so id is 0)
    for (size_t i = 0; i < new_capacity; ++i) {
        new_table[i].request.status = PENDING_REQUEST_INVALID;
        new_table[i].request.cv = NULL; // Initialize CV pointer
    }

    // Rehash all existing valid entries from the old table
    size_t rehashed_count = 0;
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        pending_request_entry_t* old_entry = &client->pending_requests_table[i];

        // Check if the slot is occupied by a valid, non-deleted request
        if (old_entry->id != 0 && old_entry->request.status != PENDING_REQUEST_INVALID) {
            // Find new position using linear probing in the new table
            size_t index = hash_id(old_entry->id, new_capacity);
            size_t original_index = index;
            size_t probe_count = 0;
            const size_t max_probes = new_capacity; // Safety limit

            do {
                if (new_table[index].id == 0) {
                    // Found empty slot in the new table, copy the entire entry
                    new_table[index] = *old_entry;
                    rehashed_count++;
                    break;
                }

                // Collision in the new table, move to the next slot
                index = (index + 1) & (new_capacity - 1);

                // Safety check to avoid infinite loops
                if (++probe_count >= max_probes) {
                    mcp_log_error("Hash table resize failed: probe limit reached for ID %llu",
                                 (unsigned long long)old_entry->id);
                    free(new_table);
                    return -1;
                }
            } while (index != original_index);

            // If we looped back to the original index, the new table is full
            if (index == original_index) {
                mcp_log_error("Hash table resize failed: Could not find empty slot during rehash for ID %llu",
                             (unsigned long long)old_entry->id);
                free(new_table);
                return -1;
            }
        }
    }

    // Sanity check: ensure all original items were rehashed
    if (rehashed_count != client->pending_requests_count) {
        mcp_log_warn("Hash table resize warning: Rehashed count (%zu) does not match original count (%zu)",
                    rehashed_count, client->pending_requests_count);
        // This might indicate an issue with tracking pending_requests_count or the rehashing logic
        // We'll update the count to match what was actually rehashed
        client->pending_requests_count = rehashed_count;
    }

    // Replace old table with new one
    free(client->pending_requests_table);
    client->pending_requests_table = new_table;
    client->pending_requests_capacity = new_capacity;

    mcp_log_info("Resized pending requests hash table from %zu to %zu (count: %zu)",
                client->pending_requests_capacity / 2, new_capacity, rehashed_count);

    return 0;
}
