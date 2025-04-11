#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mcp_json.h>
#include "mcp_client.h"
#include <mcp_transport.h>
#include <mcp_log.h>
#include "mcp_sync.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include <mcp_json_message.h>
#include <mcp_json_rpc.h>


// Platform specific includes are no longer needed here for sync primitives,
// but keep them for socket types if used elsewhere in the file.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h> // Still needed for timespec
#include <errno.h> // Include for ETIMEDOUT on POSIX
#endif


// Initial capacity for pending requests hash table (must be power of 2)
#define INITIAL_PENDING_REQUESTS_CAPACITY 16
// Max load factor before resizing hash table
#define HASH_TABLE_MAX_LOAD_FACTOR 0.75

// Status for pending requests
typedef enum {
    PENDING_REQUEST_INVALID, // Slot is empty or request was removed
    PENDING_REQUEST_WAITING,
    PENDING_REQUEST_COMPLETED,
    PENDING_REQUEST_ERROR,
    PENDING_REQUEST_TIMEOUT
} pending_request_status_t;

// Structure to hold info about a pending request
typedef struct {
    uint64_t id;
    pending_request_status_t status;
    char** result_ptr;             // Pointer to the result pointer in the caller's stack
    mcp_error_code_t* error_code_ptr; // Pointer to the error code in the caller's stack
    char** error_message_ptr;      // Pointer to the error message pointer in the caller's stack
    mcp_cond_t* cv;                 // Use the abstracted condition variable type (pointer)
} pending_request_t;

// Structure for hash table entry
typedef struct {
    uint64_t id; // 0 indicates empty slot
    pending_request_t request;
} pending_request_entry_t;


/**
 * MCP client structure (Internal definition)
 */
struct mcp_client {
    mcp_client_config_t config;     // Store configuration
    mcp_transport_t* transport;     // Transport handle (owned by client)
    uint64_t next_id;               // Counter for request IDs

    // State for asynchronous response handling
    mcp_mutex_t* pending_requests_mutex; // Use the abstracted mutex type (pointer)
    pending_request_entry_t* pending_requests_table; // Hash table
    size_t pending_requests_capacity; // Current capacity (size) of the hash table
    size_t pending_requests_count;    // Number of active entries in the hash table
};

// --- Hash Table Helper Function Declarations ---
static size_t hash_id(uint64_t id, size_t table_size);
static pending_request_entry_t* find_pending_request_entry(mcp_client_t* client, uint64_t id, bool find_empty_for_insert);
static int add_pending_request_entry(mcp_client_t* client, uint64_t id, pending_request_t* request);
static int remove_pending_request_entry(mcp_client_t* client, uint64_t id);
static int resize_pending_requests_table(mcp_client_t* client);

// Forward declaration for the client's internal receive callback
static char* client_receive_callback(void* user_data, const void* data, size_t size, int* error_code);
// Forward declaration for the client's internal transport error callback
static void client_transport_error_callback(void* user_data, int error_code);


/**
 * @brief Create an MCP client instance.
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport) {
    if (config == NULL || transport == NULL) {
        return NULL; // Config and transport are required
    }

    mcp_client_t* client = (mcp_client_t*)malloc(sizeof(mcp_client_t));
    if (client == NULL) {
        mcp_transport_destroy(transport);
        return NULL;
    }

    // Store config and transport
    client->config = *config; // Copy config struct
    client->transport = transport;
    client->next_id = 1;

    // Initialize synchronization primitives using the abstraction layer
    client->pending_requests_mutex = mcp_mutex_create();
    if (client->pending_requests_mutex == NULL) {
        mcp_log_error("Failed to create pending requests mutex.");
        mcp_transport_destroy(transport);
        free(client);
        return NULL;
    }

    // Initialize hash table
    client->pending_requests_capacity = INITIAL_PENDING_REQUESTS_CAPACITY;
    client->pending_requests_count = 0;
    client->pending_requests_table = (pending_request_entry_t*)calloc(client->pending_requests_capacity, sizeof(pending_request_entry_t));
    if (client->pending_requests_table == NULL) {
        mcp_mutex_destroy(client->pending_requests_mutex); // Use abstracted destroy
        mcp_transport_destroy(transport);
        free(client);
        return NULL;
    }
    // Initialize status and CV pointers for all allocated entries
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
         client->pending_requests_table[i].request.status = PENDING_REQUEST_INVALID;
         client->pending_requests_table[i].request.cv = NULL; // Initialize CV pointer
    }


    // Start the transport's receive mechanism with our internal callbacks
    if (mcp_transport_start(
            client->transport,
            client_receive_callback,
            client,
            client_transport_error_callback
        ) != 0)
    {
        mcp_client_destroy(client); // Will destroy transport and mutex/CS
        return NULL;
    }

    return client;
}

/**
 * Destroy an MCP client
 */
void mcp_client_destroy(mcp_client_t* client) {
    if (client == NULL) {
        return;
    }

    // Transport is stopped and destroyed here
    if (client->transport != NULL) {
        mcp_transport_stop(client->transport); // Ensure it's stopped
        mcp_transport_destroy(client->transport);
        client->transport = NULL;
    }

    // Clean up synchronization primitives and pending requests map
    mcp_mutex_destroy(client->pending_requests_mutex);
    client->pending_requests_mutex = NULL; // Avoid double free

    // Free any remaining pending requests (and their condition variables) in the hash table
    if (client->pending_requests_table != NULL) {
        for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
            if (client->pending_requests_table[i].id != 0 && client->pending_requests_table[i].request.status != PENDING_REQUEST_INVALID) {
                // Destroy the condition variable using the abstraction
                mcp_cond_destroy(client->pending_requests_table[i].request.cv);
            }
        }
        free(client->pending_requests_table);
    }

    free(client);
}

// Simple hash function (using bitwise AND for power-of-2 table size)
static size_t hash_id(uint64_t id, size_t table_size) {
    // Assumes table_size is a power of 2
    return (size_t)(id & (table_size - 1));
}

// Find an entry in the hash table using linear probing
// If find_empty_for_insert is true, returns the first empty/deleted slot if key not found
static pending_request_entry_t* find_pending_request_entry(mcp_client_t* client, uint64_t id, bool find_empty_for_insert) {
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
static int add_pending_request_entry(mcp_client_t* client, uint64_t id, pending_request_t* request) {
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
    pending_request_entry_t* entry = find_pending_request_entry(client, id, true);

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
static int remove_pending_request_entry(mcp_client_t* client, uint64_t id) {
    pending_request_entry_t* entry = find_pending_request_entry(client, id, false);
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

/**
 * @brief Callback invoked by the transport layer when a fatal error occurs (e.g., disconnection).
 *
 * This function iterates through all waiting requests, marks them as errored,
 * and signals their condition variables to wake up the waiting threads.
 */
static void client_transport_error_callback(void* user_data, int transport_error_code) {
    mcp_client_t* client = (mcp_client_t*)user_data;
    if (client == NULL) return;

    mcp_log_info("Transport error detected (code: %d). Notifying waiting requests.", transport_error_code);

    // Lock the mutex to safely access the pending requests table
    mcp_mutex_lock(client->pending_requests_mutex);

    // Iterate through the hash table
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        pending_request_entry_t* entry = &client->pending_requests_table[i];
        // Check if the slot is active and the request is currently waiting
        if (entry->id != 0 && entry->request.status == PENDING_REQUEST_WAITING) {
            // Set error details for the waiting request
            *(entry->request.error_code_ptr) = MCP_ERROR_TRANSPORT_ERROR; // Use a generic transport error
            // Avoid overwriting existing error message if one was somehow set
            if (*(entry->request.error_message_ptr) == NULL) {
                 // Allocate error message using our helper
                 *(entry->request.error_message_ptr) = mcp_strdup("Transport connection error");
                 // If mcp_strdup fails, the message pointer remains NULL.
            }

            // Update status to ERROR
            entry->request.status = PENDING_REQUEST_ERROR;

            // Signal the condition variable to wake up the waiting thread
            if (entry->request.cv) {
                mcp_cond_signal(entry->request.cv);
            }
            // Note: The waiting thread is responsible for removing the entry from the table
        }
    }

    // Unlock the mutex
    mcp_mutex_unlock(client->pending_requests_mutex);
}

// Connect/Disconnect functions are removed as transport is handled at creation/destruction.

/**
 * @brief Internal function to send a request and wait for a response.
 *
 * This function handles the core logic of sending a formatted request,
 * managing the pending request state, waiting for the response via condition
 * variables, and handling timeouts or errors.
 *
 * @param client The client instance.
 * @param request_json The fully formatted JSON request string to send.
 * @param request_id The ID used in the request_json.
 * @param[out] result Pointer to receive the malloc'd result string (ownership transferred).
 * @param[out] error_code Pointer to receive the MCP error code.
 * @param[out] error_message Pointer to receive the malloc'd error message string (ownership transferred).
 * @return 0 on success (check error_code for JSON-RPC errors), -1 on failure.
 */
static int mcp_client_send_and_wait(
    mcp_client_t* client,
    const char* request_json,
    uint64_t request_id,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
     if (client == NULL || request_json == NULL || result == NULL || error_code == NULL || error_message == NULL) {
        return -1;
    }

    if (client->transport == NULL) {
        return -1;
    }

    // Initialize result and error message
    *result = NULL;
    *error_message = NULL;
    *error_code = MCP_ERROR_NONE;

    // Calculate JSON length - excluding null terminator, as required by server
    size_t json_len = strlen(request_json);
    uint32_t net_len = htonl((uint32_t)json_len);

    // Prepare buffers for vectored send
    mcp_buffer_t send_buffers[2];
    send_buffers[0].data = &net_len;
    send_buffers[0].size = sizeof(net_len);
    send_buffers[1].data = request_json;
    send_buffers[1].size = json_len;

    // Send the buffers using vectored I/O
    int send_status = mcp_transport_sendv(client->transport, send_buffers, 2);
    mcp_log_debug("mcp_transport_sendv returned: %d for request ID %llu", send_status, (unsigned long long)request_id);

    if (send_status != 0) {
        mcp_log_error("mcp_transport_sendv failed with status %d", send_status);
        return -1; // Send failed
    }

    // --- Asynchronous Receive Logic ---
    // 1. Prepare pending request structure
    pending_request_t pending_req;
    pending_req.id = request_id; // Use the provided ID
    pending_req.status = PENDING_REQUEST_WAITING;
    pending_req.result_ptr = result;
    pending_req.error_code_ptr = error_code;
    pending_req.error_message_ptr = error_message;
    pending_req.cv = mcp_cond_create();
    if (pending_req.cv == NULL) {
        mcp_log_error("Failed to create condition variable for request %llu.", (unsigned long long)pending_req.id);
        return -1;
    }

    // 2. Add to pending requests map (protected by mutex)
    mcp_mutex_lock(client->pending_requests_mutex);
    int add_status = add_pending_request_entry(client, pending_req.id, &pending_req);
    if (add_status != 0) {
        mcp_mutex_unlock(client->pending_requests_mutex);
        mcp_cond_destroy(pending_req.cv); // Destroy the CV we initialized if add failed
        mcp_log_error("Failed to add request %llu to hash table.\n", (unsigned long long)pending_req.id);
        return -1; // Failed to add to hash table
    }
    mcp_mutex_unlock(client->pending_requests_mutex);

    // 3. Wait for response or timeout
    int wait_result = 0; // 0=signaled, 1=timeout (Windows), ETIMEDOUT=timeout (POSIX), -1=error
    mcp_mutex_lock(client->pending_requests_mutex);
    pending_request_entry_t* req_entry_wrapper = find_pending_request_entry(client, pending_req.id, false);

    if (req_entry_wrapper && req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
        if (client->config.request_timeout_ms > 0) {
            wait_result = mcp_cond_timedwait(req_entry_wrapper->request.cv, client->pending_requests_mutex, client->config.request_timeout_ms);
        } else {
            wait_result = mcp_cond_wait(req_entry_wrapper->request.cv, client->pending_requests_mutex);
        }

        // Update status based on wait_result *before* checking request status
        // Check for timeout using platform-specific or abstraction-defined value
#ifdef ETIMEDOUT // Only check ETIMEDOUT if it's defined (i.e., on POSIX)
        if (wait_result == ETIMEDOUT) {
             req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
             mcp_log_error("mcp_cond_wait/timedwait failed with code: %d (%s)", wait_result, strerror(wait_result));
             // Keep status as WAITING or whatever callback set if error occurred during wait
        }
#else // Windows or other systems where ETIMEDOUT isn't the timeout indicator
        // Assume the abstraction returns a specific value (e.g., 1) for timeout, 0 for success, -1 for error
        if (wait_result == 1) { // Assuming 1 indicates timeout from the abstraction
             req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (wait_result != 0) {
             mcp_log_error("mcp_cond_wait/timedwait failed with code: %d", wait_result);
        }
#endif
        // If wait_result == 0, the request status should have been updated by the callback
    }
    // else: Request was processed/removed before we could wait, or send failed initially.

    // Determine final outcome based on request status
    int final_status = -1; // Default to error
    if (req_entry_wrapper) {
        if(req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) {
            final_status = 0; // Success
        } else if (req_entry_wrapper->request.status == PENDING_REQUEST_TIMEOUT) {
            final_status = -2; // Timeout
        } else {
            final_status = -1; // Error (set by callback or wait error)
        }
    } else {
        // Entry removed before check. Rely on output params set by callback.
        if (*error_code != MCP_ERROR_NONE) final_status = -1;
        else if (*result != NULL) final_status = 0;
        else { mcp_log_error("Request %llu not found and no result/error set.", (unsigned long long)pending_req.id); final_status = -1; }
    }

    // Remove entry from hash table after waiting/timeout/error
    if (req_entry_wrapper) {
        remove_pending_request_entry(client, pending_req.id); // CV destroyed inside remove
    }
    mcp_mutex_unlock(client->pending_requests_mutex);

    // 4. Return status based on final outcome
    if (final_status == -2) { // Timeout case
        mcp_log_error("Request %llu timed out.\n", (unsigned long long)pending_req.id);
        *error_code = MCP_ERROR_TRANSPORT_ERROR;
        *error_message = mcp_strdup("Request timed out");
        return -1;
    } else if (final_status != 0) { // Other error
         mcp_log_error("Error processing response for request %llu.\n", (unsigned long long)pending_req.id);
         if (*error_code != MCP_ERROR_NONE && *error_message == NULL) {
             *error_message = mcp_strdup("Unknown internal error occurred");
         } else if (*error_code == MCP_ERROR_NONE) {
             *error_code = MCP_ERROR_INTERNAL_ERROR;
             *error_message = mcp_strdup("Internal error processing response");
         }
         return -1;
    }

    // Success (final_status == 0)
    return 0;
}

/**
 * Send a request to the MCP server and receive a response (Original version)
 */
static int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params, // Assumed to be JSON string or NULL
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || method == NULL || result == NULL || error_code == NULL || error_message == NULL) {
        return -1;
    }

    // Generate next request ID
    mcp_mutex_lock(client->pending_requests_mutex);
    uint64_t current_id = client->next_id++;
    mcp_mutex_unlock(client->pending_requests_mutex);


    // Create request JSON
    char* request_json = NULL;
    const char* params_to_use = (params != NULL) ? params : "{}";
    request_json = mcp_json_format_request(current_id, method, params_to_use);

    if (request_json == NULL) {
        mcp_log_error("Failed to format request JSON for method '%s'", method);
        return -1;
    }

    // Use the internal send_and_wait function
    int status = mcp_client_send_and_wait(client, request_json, current_id, result, error_code, error_message);

    // Free the formatted request JSON string
    free(request_json);

    return status;
}

/**
 * Sends a pre-formatted request and receives the raw response.
 */
int mcp_client_send_raw_request(
    mcp_client_t* client,
    const char* method, // Still needed for logging/context? Or remove? Keep for now.
    const char* params_json, // The raw JSON params string
    uint64_t id, // Use the provided ID
    char** response_json_out, // Changed name for clarity
    mcp_error_code_t* error_code_out, // Changed name
    char** error_message_out // Changed name
) {
     if (client == NULL || method == NULL || params_json == NULL || response_json_out == NULL || error_code_out == NULL || error_message_out == NULL) {
        return -1;
    }

    // Create the full request JSON string using the provided components
    char* request_json = mcp_json_format_request(id, method, params_json);
    if (request_json == NULL) {
        mcp_log_error("Failed to format raw request JSON for method '%s'", method);
        return -1;
    }

    // Use the internal send_and_wait function
    int status = mcp_client_send_and_wait(client, request_json, id, response_json_out, error_code_out, error_message_out);

    // Free the formatted request JSON string
    free(request_json);

    // send_and_wait already populates the output parameters correctly based on success/error/timeout.
    // Ensure response_json_out is NULL on failure.
    if (status != 0) {
        *response_json_out = NULL;
    }

    return status;
}

/**
 * List resources from the MCP server
 */
int mcp_client_list_resources(
    mcp_client_t* client,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (client == NULL || resources == NULL || count == NULL) {
        return -1;
    }

    // Initialize resources and count
    *resources = NULL;
    *count = 0;

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_resources", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message); // Free error message if send failed
        return -1;
    }

    // Check for JSON-RPC error in the response
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resources: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result); // Free the result JSON containing the error object
        return -1;
    }

    // Parse result if no error
    if (mcp_json_parse_resources(result, resources, count) != 0) {
        mcp_log_error("Failed to parse list_resources response.");
        free(error_message); // Should be NULL here anyway
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message); // Should be NULL
    return 0;
}

static char* client_receive_callback(void* user_data, const void* data, size_t size, int* error_code) {
    mcp_client_t* client = (mcp_client_t*)user_data;
    if (client == NULL || data == NULL || size == 0 || error_code == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        return NULL; // No response generated by client callback
    }
    *error_code = 0; // Default success for callback processing itself

    // We expect data to be a null-terminated JSON string from the transport receive thread
    const char* response_json = (const char*)data;
    uint64_t id;
    mcp_error_code_t resp_error_code = MCP_ERROR_NONE;
    char* resp_error_message = NULL;
    char* resp_result = NULL;

    // Parse the response
    if (mcp_json_parse_response(response_json, &id, &resp_error_code, &resp_error_message, &resp_result) != 0) {
        mcp_log_error("Client failed to parse response JSON: %s\n", response_json);
        *error_code = MCP_ERROR_PARSE_ERROR;
        // Cannot signal specific request on parse error, maybe log?
        return NULL;
    }

    // --- Special Handling for ID 0 (Initial Ping/Pong) ---
    if (id == 0) {
        // This is likely the response to the initial ping sent by the receive thread.
        // Ignore it, as it's not tied to a user request.
        mcp_log_debug("Received response for initial ping (ID: 0), ignoring.");
        free(resp_error_message); // Free parsed fields even if ignored
        free(resp_result);
        return NULL; // Don't process further
    }
    // --- End Special Handling ---

    // Find the pending request and signal it
    mcp_mutex_lock(client->pending_requests_mutex);

    // Find the pending request entry in the hash table
    pending_request_entry_t* req_entry_wrapper = find_pending_request_entry(client, id, false);

    if (req_entry_wrapper != NULL && req_entry_wrapper->request.status != PENDING_REQUEST_INVALID) {
        // Found the pending request
        if (req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
            // Store results via pointers
            *(req_entry_wrapper->request.error_code_ptr) = resp_error_code;
            *(req_entry_wrapper->request.error_message_ptr) = resp_error_message; // Transfer ownership
            *(req_entry_wrapper->request.result_ptr) = resp_result;             // Transfer ownership

            // Update status
            req_entry_wrapper->request.status = (resp_error_code == MCP_ERROR_NONE) ? PENDING_REQUEST_COMPLETED : PENDING_REQUEST_ERROR;

            // Signal the waiting thread
            if (req_entry_wrapper->request.cv) {
                mcp_cond_signal(req_entry_wrapper->request.cv);
            }
            // Note: We don't remove the entry here. The waiting thread will remove it after waking up.
        } else {
            // Request already timed out or errored, discard response
            mcp_log_error("Received response for already completed/timed out request %llu\n", (unsigned long long)id);
            free(resp_error_message);
            free(resp_result);
        }
    } else {
        // Response received for an unknown/unexpected ID (and ID is not 0)
        mcp_log_warn("Received response with unexpected ID: %llu", (unsigned long long)id);
        free(resp_error_message);
        free(resp_result);
        *error_code = MCP_ERROR_INVALID_REQUEST; // Set error for the callback itself
    }

    mcp_mutex_unlock(client->pending_requests_mutex);

    return NULL; // Client callback never sends a response back
}

/**
 * List resource templates from the MCP server
 */
int mcp_client_list_resource_templates(
    mcp_client_t* client,
    mcp_resource_template_t*** templates,
    size_t* count
) {
    if (client == NULL || templates == NULL || count == NULL) {
        return -1;
    }

    // Initialize templates and count
    *templates = NULL;
    *count = 0;

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_resource_templates", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        mcp_log_error("Server returned error for list_resource_templates: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_resource_templates(result, templates, count) != 0) {
         mcp_log_error("Failed to parse list_resource_templates response.");
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

/**
 * Read a resource from the MCP server
 */
int mcp_client_read_resource(
    mcp_client_t* client,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (client == NULL || uri == NULL || content == NULL || count == NULL) {
        return -1;
    }

    // Initialize content and count
    *content = NULL;
    *count = 0;

    // Create params
    char* params = mcp_json_format_read_resource_params(uri);
    if (params == NULL) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "read_resource", params, &result, &error_code, &error_message) != 0) {
        free(params);
        free(error_message);
        return -1;
    }
    free(params);

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
         mcp_log_error("Server returned error for read_resource: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_content(result, content, count) != 0) {
         mcp_log_error("Failed to parse read_resource response.");
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

/**
 * List tools from the MCP server
 */
int mcp_client_list_tools(
    mcp_client_t* client,
    mcp_tool_t*** tools,
    size_t* count
) {
    if (client == NULL || tools == NULL || count == NULL) {
        return -1;
    }

    // Initialize tools and count
    *tools = NULL;
    *count = 0;

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_tools", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
         mcp_log_error("Server returned error for list_tools: %d (%s)", error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_tools(result, tools, count) != 0) {
         mcp_log_error("Failed to parse list_tools response.");
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

/**
 * Call a tool on the MCP server
 */
int mcp_client_call_tool(
    mcp_client_t* client,
    const char* name,
    const char* arguments,
    mcp_content_item_t*** content,
    size_t* count,
    bool* is_error
) {
    if (client == NULL || name == NULL || content == NULL || count == NULL || is_error == NULL) {
        return -1;
    }

    // Initialize content, count, and is_error
    *content = NULL;
    *count = 0;
    *is_error = false;

    // Create params
    char* params = mcp_json_format_call_tool_params(name, arguments);
    if (params == NULL) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "call_tool", params, &result, &error_code, &error_message) != 0) {
        free(params);
        free(error_message);
        return -1;
    }
    free(params);

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
         mcp_log_error("Server returned error for call_tool '%s': %d (%s)", name, error_code, error_message ? error_message : "N/A");
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_tool_result(result, content, count, is_error) != 0) {
         mcp_log_error("Failed to parse call_tool response for tool '%s'.", name);
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}
