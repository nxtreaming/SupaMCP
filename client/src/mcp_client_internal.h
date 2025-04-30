#ifndef MCP_CLIENT_INTERNAL_H
#define MCP_CLIENT_INTERNAL_H

#include <mcp_client.h>
#include <mcp_transport.h>
#include <mcp_types.h>
#include <mcp_sync.h>
#include <mcp_arena.h>
#include <mcp_json_message.h>
#include <stddef.h>
#include <stdbool.h>

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

// Forward declarations for internal functions that need to be shared between modules

// From mcp_client_hash_table.c - wrapper functions to expose hash table functionality
pending_request_entry_t* mcp_client_find_pending_request_entry(mcp_client_t* client, uint64_t id, bool find_empty_for_insert);
int mcp_client_add_pending_request_entry(mcp_client_t* client, uint64_t id, pending_request_t* request);
int mcp_client_remove_pending_request_entry(mcp_client_t* client, uint64_t id);

// From mcp_client_request.c
int mcp_client_send_and_wait(
    mcp_client_t* client,
    const char* request_json,
    uint64_t request_id,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
);

int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
);

/**
 * @brief Send a request using HTTP transport and process the response directly.
 *
 * This function is specifically designed for HTTP transport, which uses a synchronous
 * request-response model. It sends the request and processes the response in the same
 * function call, without using the asynchronous callback mechanism used by other transports.
 */
int mcp_client_http_send_request(
    mcp_client_t* client,
    const char* request_json,
    uint64_t request_id,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
);

// From mcp_client_core.c
char* mcp_client_receive_callback(void* user_data, const void* data, size_t size, int* error_code);
void mcp_client_transport_error_callback(void* user_data, int error_code);

// From mcp_arena.h
mcp_arena_t* mcp_arena_get_current(void);
int mcp_arena_init_current_thread(size_t size);
int mcp_arena_reset_current_thread(void);

// From mcp_json_message.h
int mcp_json_parse_resources(const char* json, mcp_resource_t*** resources, size_t* count);
int mcp_json_parse_resource_templates(const char* json, mcp_resource_template_t*** templates, size_t* count);
int mcp_json_parse_content(const char* json, mcp_content_item_t*** content, size_t* count);
int mcp_json_parse_tools(const char* json, mcp_tool_t*** tools, size_t* count);
int mcp_json_parse_tool_result(const char* json, mcp_content_item_t*** content, size_t* count, bool* is_error);
char* mcp_json_format_read_resource_params(const char* uri);
char* mcp_json_format_call_tool_params(const char* name, const char* arguments);

#endif // MCP_CLIENT_INTERNAL_H
