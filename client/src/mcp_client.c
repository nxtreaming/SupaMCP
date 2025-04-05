#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mcp_json.h>
#include "mcp_client.h"
#include <mcp_transport.h>
#include <mcp_log.h> // Include log header for LOG_LEVEL_DEBUG

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <time.h>
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
#ifdef _WIN32
    CONDITION_VARIABLE cv;
#else
    pthread_cond_t cv;
#endif
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
#ifdef _WIN32
    CRITICAL_SECTION pending_requests_mutex;
#else
    pthread_mutex_t pending_requests_mutex;
#endif
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
        // If client allocation fails, should we destroy the passed transport?
        // The API doc says client takes ownership, so yes.
        mcp_transport_destroy(transport);
        return NULL;
    }

    // Store config and transport
    client->config = *config; // Copy config struct
    client->transport = transport;
    client->next_id = 1;

    // Initialize synchronization primitives and pending requests map
#ifdef _WIN32
    InitializeCriticalSection(&client->pending_requests_mutex);
#else
    if (pthread_mutex_init(&client->pending_requests_mutex, NULL) != 0) {
        mcp_transport_destroy(transport);
        free(client);
        return NULL;
    }
#endif

    // Initialize hash table
    client->pending_requests_capacity = INITIAL_PENDING_REQUESTS_CAPACITY;
    client->pending_requests_count = 0;
    // Use calloc to zero-initialize the table (marks all slots as empty with id=0)
    client->pending_requests_table = (pending_request_entry_t*)calloc(client->pending_requests_capacity, sizeof(pending_request_entry_t));
    if (client->pending_requests_table == NULL) {
#ifdef _WIN32
        DeleteCriticalSection(&client->pending_requests_mutex);
#else
        pthread_mutex_destroy(&client->pending_requests_mutex);
#endif
        mcp_transport_destroy(transport);
        free(client);
        return NULL;
    }
    // Initialize status for all allocated entries (calloc already sets id to 0)
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
         client->pending_requests_table[i].request.status = PENDING_REQUEST_INVALID;
    }


    // Start the transport's receive mechanism with our internal callbacks
    if (mcp_transport_start(
            client->transport,
            client_receive_callback,
            client,
            client_transport_error_callback
        ) != 0)
    {
        // Cleanup if start fails
        free(client->pending_requests_table);
#ifdef _WIN32
        DeleteCriticalSection(&client->pending_requests_mutex);
#else
        pthread_mutex_destroy(&client->pending_requests_mutex);
#endif
        mcp_client_destroy(client); // Will destroy transport
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
#ifdef _WIN32
    DeleteCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_destroy(&client->pending_requests_mutex);
#endif

    // Free any remaining pending requests (and their condition variables) in the hash table
    for (size_t i = 0; i < client->pending_requests_capacity; ++i) {
        if (client->pending_requests_table[i].id != 0 && client->pending_requests_table[i].request.status != PENDING_REQUEST_INVALID) {
#ifdef _WIN32
            // No explicit destruction needed for CONDITION_VARIABLE? Check docs.
#else
            pthread_cond_destroy(&client->pending_requests_table[i].request.cv);
#endif
            // Free any potentially allocated result/error strings if request timed out or errored?
            // The current logic assigns these pointers back to the caller's stack variables,
            // so the caller is responsible. We only need to destroy the CV here.
        }
    }
    free(client->pending_requests_table);

    free(client);
}


// --- Hash Table Helper Function Implementations ---

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
            fprintf(stderr, "Failed to resize hash table for request %llu.\n", (unsigned long long)id);
            return -1; // Resize failed
        }
        // After resize, capacity has changed, need to recalculate hash/index
    }

    // Find an empty slot for insertion (using the potentially new capacity)
    pending_request_entry_t* entry = find_pending_request_entry(client, id, true);

    if (entry == NULL) {
         fprintf(stderr, "Hash table full or failed to find slot for insert (ID: %llu)\n", (unsigned long long)id);
         return -1; // Should not happen if resizing is implemented or table not full
    }

    if (entry->id == id) {
         fprintf(stderr, "Error: Duplicate request ID found in hash table: %llu\n", (unsigned long long)id);
         // This indicates a logic error (ID reuse before completion) or hash collision issue not handled
         return -1;
    }


    // Found an empty or deleted slot
    entry->id = id;
    entry->request = *request; // Copy the request data
    client->pending_requests_count++;
    return 0;
}

// Remove a request from the hash table (marks as invalid)
static int remove_pending_request_entry(mcp_client_t* client, uint64_t id) {
    pending_request_entry_t* entry = find_pending_request_entry(client, id, false);
    if (entry != NULL && entry->request.status != PENDING_REQUEST_INVALID) {
        // Destroy CV before marking as invalid
#ifndef _WIN32
        pthread_cond_destroy(&entry->request.cv);
#endif
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
        fprintf(stderr, "Hash table resize failed: new capacity overflow or too large.\n");
        return -1;
    }

    pending_request_entry_t* new_table = (pending_request_entry_t*)calloc(
        new_capacity, sizeof(pending_request_entry_t));

    if (new_table == NULL) {
        fprintf(stderr, "Hash table resize failed: calloc returned NULL for new capacity %zu.\n", new_capacity);
        return -1; // Allocation failed
    }

    // Initialize all entries in the new table (calloc zeros memory, so id is 0)
    for (size_t i = 0; i < new_capacity; ++i) {
        new_table[i].request.status = PENDING_REQUEST_INVALID;
        // new_table[i].id = 0; // Already done by calloc
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
                fprintf(stderr, "Hash table resize failed: Could not find empty slot during rehash for ID %llu.\n", (unsigned long long)old_entry->id);
                free(new_table); // Clean up the partially filled new table
                return -1; // Indicate critical failure
            }
        }
    }

    // Sanity check: ensure all original items were rehashed
    if (rehashed_count != client->pending_requests_count) {
         fprintf(stderr, "Hash table resize warning: Rehashed count (%zu) does not match original count (%zu).\n", rehashed_count, client->pending_requests_count);
         // This might indicate an issue with tracking pending_requests_count or the rehashing logic.
         // Proceeding, but this warrants investigation.
    }


    // Replace old table with new one
    free(client->pending_requests_table);
    client->pending_requests_table = new_table;
    client->pending_requests_capacity = new_capacity;

    fprintf(stdout, "Resized pending requests hash table to capacity %zu\n", new_capacity); // Optional: Log resize event
    return 0; // Success
}


// --- Client Internal Transport Error Callback ---

/**
 * @brief Callback invoked by the transport layer when a fatal error occurs (e.g., disconnection).
 *
 * This function iterates through all waiting requests, marks them as errored,
 * and signals their condition variables to wake up the waiting threads.
 */
static void client_transport_error_callback(void* user_data, int transport_error_code) {
    mcp_client_t* client = (mcp_client_t*)user_data;
    if (client == NULL) return;

    fprintf(stderr, "Transport error detected (code: %d). Notifying waiting requests.\n", transport_error_code);

    // Lock the mutex to safely access the pending requests table
#ifdef _WIN32
    EnterCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_lock(&client->pending_requests_mutex);
#endif

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
#ifdef _WIN32
            WakeConditionVariable(&entry->request.cv);
#else
            pthread_cond_signal(&entry->request.cv);
#endif
            // Note: The waiting thread is responsible for removing the entry from the table
        }
    }

    // Unlock the mutex
#ifdef _WIN32
    LeaveCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_unlock(&client->pending_requests_mutex);
#endif
}


// Connect/Disconnect functions are removed as transport is handled at creation/destruction.

/**
 * Send a request to the MCP server and receive a response
 */
static int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || method == NULL || result == NULL || error_code == NULL || error_message == NULL) {
        return -1;
    }

    if (client->transport == NULL) {
        return -1;
    }

    // Initialize result and error message
    *result = NULL;
    *error_message = NULL;
    *error_code = MCP_ERROR_NONE;

    // Create request JSON
    char* request_json = NULL;
    if (params != NULL) {
        request_json = mcp_json_format_request(client->next_id, method, params);
    } else {
        request_json = mcp_json_format_request(client->next_id, method, "{}");
    }
    if (request_json == NULL) {
        return -1;
    }

    // Calculate JSON length - excluding null terminator, as required by server
    size_t json_len = strlen(request_json);  // Don't add 1, exclude null terminator
    uint32_t net_len = htonl((uint32_t)json_len); // Convert to network byte order
    
    // Allocate buffer containing length prefix and JSON content
    // Add 1 extra byte for null terminator, but don't include in transmission length
    size_t total_len = sizeof(net_len) + json_len;
    char* send_buffer = (char*)malloc(total_len + 1); // +1 for null terminator space

    if (send_buffer == NULL) {
        free(request_json);
        return -1; // Allocation failed
    }

    // Copy length prefix and JSON data to buffer
    memcpy(send_buffer, &net_len, sizeof(net_len));
    memcpy(send_buffer + sizeof(net_len), request_json, json_len);
    send_buffer[total_len] = '\0'; // Add null terminator (in extra allocated space)

    // DEBUG: Log before sending
    log_message(LOG_LEVEL_DEBUG, "Sending request ID %llu, method '%s', total_len=%zu, prefix=0x%08X (%02X %02X %02X %02X), json_start='%.10s...'",
                (unsigned long long)client->next_id, // Note: ID used here is before incrementing for pending_req
                method,
                total_len,
                net_len,
                (unsigned char)send_buffer[0], (unsigned char)send_buffer[1], (unsigned char)send_buffer[2], (unsigned char)send_buffer[3],
                request_json);

    // Send the combined buffer
    int send_status = mcp_transport_send(client->transport, send_buffer, total_len);
    log_message(LOG_LEVEL_DEBUG, "mcp_transport_send returned: %d", send_status);


    // Clean up buffers
    free(send_buffer);
    free(request_json); // Free original JSON string

    if (send_status != 0) {
        return -1; // Send failed
    }

    // Receive response
    // This is currently blocking and synchronous. A real client might use a
    // separate receive thread or asynchronous I/O, started via mcp_transport_start,
    // which would use a callback to handle responses and match them to requests.
    // For this simple example, we assume a synchronous request-response model
    // and that the transport layer doesn't have its own receive loop running.
    // We also lack timeout handling here based on client->config.request_timeout_ms.
    // char* response_json = NULL; // Unused variable from old sync logic
    // size_t response_size = 0; // Unused variable from old sync logic

    // --- Asynchronous Receive Logic ---
    // 1. Prepare pending request structure
    pending_request_t pending_req;
    pending_req.id = client->next_id++; // Use and increment ID
    pending_req.status = PENDING_REQUEST_WAITING;
    pending_req.result_ptr = result;
    pending_req.error_code_ptr = error_code;
    pending_req.error_message_ptr = error_message;
#ifdef _WIN32
    InitializeConditionVariable(&pending_req.cv);
#else
    if (pthread_cond_init(&pending_req.cv, NULL) != 0) {
        return -1; // Failed to init condition variable
    }
#endif

    // 2. Add to pending requests map (protected by mutex)
#ifdef _WIN32
    EnterCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_lock(&client->pending_requests_mutex);
#endif
    // Add the request to the hash table
    int add_status = add_pending_request_entry(client, pending_req.id, &pending_req);
    if (add_status != 0) {
#ifdef _WIN32
        LeaveCriticalSection(&client->pending_requests_mutex);
        // No explicit CV destruction needed on Windows
#else
        pthread_mutex_unlock(&client->pending_requests_mutex);
        // Destroy the CV we initialized if add failed
        pthread_cond_destroy(&pending_req.cv);
#endif
        fprintf(stderr, "Failed to add request %llu to hash table.\n", (unsigned long long)pending_req.id);
        return -1; // Failed to add to hash table
    }
#ifdef _WIN32
    LeaveCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_unlock(&client->pending_requests_mutex);
#endif

    // 3. Send the request (already done above)
    if (send_status != 0) {
    // If send failed, remove the pending request we just added from the hash table
#ifdef _WIN32
        EnterCriticalSection(&client->pending_requests_mutex);
#else
        pthread_mutex_lock(&client->pending_requests_mutex);
#endif
        remove_pending_request_entry(client, pending_req.id); // CV is destroyed inside remove
#ifdef _WIN32
        LeaveCriticalSection(&client->pending_requests_mutex);
#else
        pthread_mutex_unlock(&client->pending_requests_mutex);
#endif
        return -1; // Send failed
    }


    // 4. Wait for response or timeout
    int wait_status = 0;
#ifdef _WIN32
    EnterCriticalSection(&client->pending_requests_mutex);
    // Find the entry in the hash table by ID
    pending_request_entry_t* req_entry_wrapper = find_pending_request_entry(client, pending_req.id, false);

    if (req_entry_wrapper && req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
         if (!SleepConditionVariableCS(&req_entry_wrapper->request.cv, &client->pending_requests_mutex, client->config.request_timeout_ms > 0 ? client->config.request_timeout_ms : INFINITE)) {
             if (GetLastError() == ERROR_TIMEOUT) {
                 wait_status = -2; // Timeout
                 req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
             } else {
                 wait_status = -1; // Wait error
             }
         } else {
             // Signaled successfully
             wait_status = (req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) ? 0 : -1;
         }
    } else if (req_entry_wrapper) {
        // Status changed before we could wait (e.g., error during callback processing)
        wait_status = (req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) ? 0 : -1;
    } else {
        // Request not found - could happen if callback processed it very quickly
        // before we acquired the lock, or if send failed and it was removed.
        // Check the caller's error code/message which might have been set.
        if (*error_code != MCP_ERROR_NONE) {
             wait_status = -1; // Error already set
        } else {
             // This case should ideally not happen if send succeeded. Log it.
             fprintf(stderr, "Request %llu not found in table after send.\n", (unsigned long long)pending_req.id);
             wait_status = -1;
        }
    }

    // Remove entry from hash table after waiting/timeout/error
    if (req_entry_wrapper) {
        remove_pending_request_entry(client, pending_req.id); // CV destroyed inside remove
    }
    LeaveCriticalSection(&client->pending_requests_mutex);

#else // Pthreads
    struct timespec ts;
    if (client->config.request_timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t nsec = ts.tv_nsec + (client->config.request_timeout_ms % 1000) * 1000000;
        ts.tv_sec += (client->config.request_timeout_ms / 1000) + (nsec / 1000000000);
        ts.tv_nsec = nsec % 1000000000;
    }

    pthread_mutex_lock(&client->pending_requests_mutex);
    // Find the entry in the hash table by ID
    pending_request_entry_t* req_entry_wrapper = find_pending_request_entry(client, pending_req.id, false);

    int pthread_wait_ret = 0;
    if (req_entry_wrapper && req_entry_wrapper->request.status == PENDING_REQUEST_WAITING) {
        if (client->config.request_timeout_ms > 0) {
            pthread_wait_ret = pthread_cond_timedwait(&req_entry_wrapper->request.cv, &client->pending_requests_mutex, &ts);
        } else {
            pthread_wait_ret = pthread_cond_wait(&req_entry_wrapper->request.cv, &client->pending_requests_mutex);
        }

        if (pthread_wait_ret == ETIMEDOUT) {
            wait_status = -2; // Timeout
            req_entry_wrapper->request.status = PENDING_REQUEST_TIMEOUT;
        } else if (pthread_wait_ret != 0) {
            wait_status = -1; // Wait error
        } else {
            // Signaled successfully
             wait_status = (req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) ? 0 : -1;
        }
    } else if (req_entry_wrapper) {
         // Status changed before we could wait
         wait_status = (req_entry_wrapper->request.status == PENDING_REQUEST_COMPLETED) ? 0 : -1;
    } else {
        // Request not found - see Windows comments
        if (*error_code != MCP_ERROR_NONE) {
             wait_status = -1; // Error already set
        } else {
             fprintf(stderr, "Request %llu not found in table after send.\n", (unsigned long long)pending_req.id);
             wait_status = -1;
        }
    }

    // Remove entry from hash table after waiting/timeout/error
    if (req_entry_wrapper) {
        remove_pending_request_entry(client, pending_req.id); // CV destroyed inside remove
    }
    pthread_mutex_unlock(&client->pending_requests_mutex);
#endif

    // 5. Return status based on wait result
    if (wait_status == -2) { // Timeout case
        fprintf(stderr, "Request %llu timed out.\n", (unsigned long long)pending_req.id);
        *error_code = MCP_ERROR_TRANSPORT_ERROR; // Use a generic transport error for timeout
        // Allocate error message using our helper
        *error_message = mcp_strdup("Request timed out");
        // If mcp_strdup fails, error_message will be NULL, which is acceptable
        return -1; // Return error for timeout
    } else if (wait_status != 0) { // Other wait error or error signaled by callback
         fprintf(stderr, "Error waiting for response for request %llu.\n", (unsigned long long)pending_req.id);
         // error_code and error_message should have been set by the callback
         // (client_receive_callback or client_transport_error_callback) if status is ERROR.
         // Check if an error message was actually allocated by the callback.
         if (*error_code != MCP_ERROR_NONE && *error_message == NULL) {
             // If an error code is set but no message, provide a generic one.
             *error_message = mcp_strdup("Unknown internal error occurred");
         } else if (*error_code == MCP_ERROR_NONE) {
             // This case should ideally not happen if wait_status != 0,
             // but handle it defensively.
             *error_code = MCP_ERROR_INTERNAL_ERROR;
             *error_message = mcp_strdup("Internal error waiting for response");
         }
         // If mcp_strdup failed above, error_message will be NULL.
         return -1; // Return error
    }

    // Success (wait_status == 0), result/error are already populated by callback via pointers
    return 0;
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
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_resources(result, resources, count) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}


// --- Client Internal Receive Callback ---

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
        fprintf(stderr, "Client failed to parse response JSON: %s\n", response_json);
        *error_code = MCP_ERROR_PARSE_ERROR;
        // Cannot signal specific request on parse error, maybe log?
        return NULL;
    }

    // Find the pending request and signal it
#ifdef _WIN32
    EnterCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_lock(&client->pending_requests_mutex);
#endif

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
#ifdef _WIN32
            WakeConditionVariable(&req_entry_wrapper->request.cv);
#else
            pthread_cond_signal(&req_entry_wrapper->request.cv);
#endif
            // Note: We don't remove the entry here. The waiting thread will remove it after waking up.
        } else {
            // Request already timed out or errored, discard response
            fprintf(stderr, "Received response for already completed/timed out request %llu\n", (unsigned long long)id);
            free(resp_error_message);
            free(resp_result);
        }
    } else {
        // Response received for an unknown/unexpected ID
        fprintf(stderr, "Received response with unexpected ID: %llu\n", (unsigned long long)id);
        free(resp_error_message);
        free(resp_result);
        *error_code = MCP_ERROR_INVALID_REQUEST; // Or some other error?
    }

#ifdef _WIN32
    LeaveCriticalSection(&client->pending_requests_mutex);
#else
    pthread_mutex_unlock(&client->pending_requests_mutex);
#endif

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
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_resource_templates(result, templates, count) != 0) {
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
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_content(result, content, count) != 0) {
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
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_tools(result, tools, count) != 0) {
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
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_tool_result(result, content, count, is_error) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    // Free the allocated strings before returning
    free(result);
    free(error_message);
    return 0;
}

// Free functions moved to mcp_types.c
