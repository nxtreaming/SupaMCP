#include "mcp_client_internal.h"
#include <mcp_log.h>
#include <stdlib.h>

// Initial capacity for pending requests hash table (must be power of 2)
#define INITIAL_PENDING_REQUESTS_CAPACITY 16
// Max load factor before resizing hash table
#define HASH_TABLE_MAX_LOAD_FACTOR 0.75

// Forward declarations for internal functions
static int resize_pending_requests_table(mcp_client_t* client);

// Simple hash function (using bitwise AND for power-of-2 table size)
static size_t hash_id(uint64_t id, size_t table_size) {
    // Assumes table_size is a power of 2
    return (size_t)(id & (table_size - 1));
}

// Find an entry in the hash table using linear probing
// If find_empty_for_insert is true, returns the first empty/deleted slot if key not found
pending_request_entry_t* mcp_client_find_pending_request_entry(mcp_client_t* client, uint64_t id, bool find_empty_for_insert) {
    if (id == 0) return NULL; // ID 0 is reserved for empty slots

    size_t index = hash_id(id, client->pending_requests_capacity);
    size_t original_index = index;
    pending_request_entry_t* first_deleted_slot = NULL;

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

        index = (index + 1) & (client->pending_requests_capacity - 1); // Move to next slot (wraps around)
    } while (index != original_index);

    // Table is full or key not found after full scan
    return find_empty_for_insert ? first_deleted_slot : NULL;
}

// Add a request to the hash table
int mcp_client_add_pending_request_entry(mcp_client_t* client, uint64_t id, pending_request_t* request) {
    // Check load factor BEFORE trying to find a slot, resize if needed
    // Note: This check happens under the pending_requests_mutex lock in the calling function
    // Check load factor *after* potentially adding this new element
    float load_factor = (float)(client->pending_requests_count + 1) / client->pending_requests_capacity;
    if (load_factor >= HASH_TABLE_MAX_LOAD_FACTOR) {
        if (resize_pending_requests_table(client) != 0) {
            mcp_log_error("Failed to resize hash table for request %llu.\n", (unsigned long long)id);
            return -1; // Resize failed
        }
        // After resize, capacity has changed, need to recalculate hash/index
    }

    // Find an empty slot for insertion (using the potentially new capacity)
    pending_request_entry_t* entry = mcp_client_find_pending_request_entry(client, id, true);

    if (entry == NULL) {
         mcp_log_error("Hash table full or failed to find slot for insert (ID: %llu)\n", (unsigned long long)id);
         return -1; // Should not happen if resizing is implemented or table not full
    }

    if (entry->id == id) {
         mcp_log_error("Error: Duplicate request ID found in hash table: %llu\n", (unsigned long long)id);
         // This indicates a logic error (ID reuse before completion) or hash collision issue not handled
         return -1;
    }

    // Found an empty or deleted slot
    entry->id = id;
    entry->request = *request; // Copy the request data (including the created CV pointer)
    client->pending_requests_count++;
    return 0;
}

// Remove a request from the hash table (marks as invalid)
int mcp_client_remove_pending_request_entry(mcp_client_t* client, uint64_t id) {
    pending_request_entry_t* entry = mcp_client_find_pending_request_entry(client, id, false);
    if (entry != NULL && entry->request.status != PENDING_REQUEST_INVALID) {
        // Destroy CV before marking as invalid
        mcp_cond_destroy(entry->request.cv);
        entry->request.cv = NULL; // Avoid dangling pointer
        entry->request.status = PENDING_REQUEST_INVALID;
        // entry->id = 0; // Keep ID for tombstone/probing, or set to a special deleted marker if needed
        client->pending_requests_count--;
        return 0;
    }
    return -1; // Not found or already invalid
}

// Resize the hash table when load factor exceeds the threshold
static int resize_pending_requests_table(mcp_client_t* client) {
    size_t new_capacity = client->pending_requests_capacity * 2;
    // Ensure capacity doesn't wrap around or become excessively large
    if (new_capacity <= client->pending_requests_capacity) {
        mcp_log_error("Hash table resize failed: new capacity overflow or too large.\n");
        return -1;
    }

    pending_request_entry_t* new_table = (pending_request_entry_t*)calloc(
        new_capacity, sizeof(pending_request_entry_t));

    if (new_table == NULL) {
        mcp_log_error("Hash table resize failed: calloc returned NULL for new capacity %zu.\n", new_capacity);
        return -1; // Allocation failed
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

            do {
                if (new_table[index].id == 0) {
                    // Found empty slot in the new table
                    new_table[index] = *old_entry; // Copy the entire entry
                    rehashed_count++;
                    break; // Move to the next entry in the old table
                }
                // Collision in the new table, move to the next slot
                index = (index + 1) & (new_capacity - 1); // Wrap around using bitwise AND
            } while (index != original_index);

            // If we looped back to the original index, the new table is full.
            // This should not happen if the load factor is managed correctly (<1.0)
            // and the new capacity is larger.
            if (index == original_index) {
                mcp_log_error ("Hash table resize failed: Could not find empty slot during rehash for ID %llu.\n", (unsigned long long)old_entry->id);
                free(new_table); // Clean up the partially filled new table
                return -1; // Indicate critical failure
            }
        }
    }

    // Sanity check: ensure all original items were rehashed
    if (rehashed_count != client->pending_requests_count) {
         mcp_log_error("Hash table resize warning: Rehashed count (%zu) does not match original count (%zu).\n", rehashed_count, client->pending_requests_count);
         // This might indicate an issue with tracking pending_requests_count or the rehashing logic.
         // Proceeding, but this warrants investigation.
    }

    // Replace old table with new one
    free(client->pending_requests_table);
    client->pending_requests_table = new_table;
    client->pending_requests_capacity = new_capacity;

    mcp_log_info("Resized pending requests hash table to capacity %zu\n", new_capacity); // Optional: Log resize event
    return 0; // Success
}
