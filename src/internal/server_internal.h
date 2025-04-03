#ifndef MCP_SERVER_INTERNAL_H
#define MCP_SERVER_INTERNAL_H

// Ensure winsock2.h is included before windows.h (which might be included by other headers)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <mcp_server.h> // Public API header
#include <mcp_json.h>
#include <mcp_arena.h>
#include <mcp_log.h>
#include <mcp_thread_pool.h>
#include <mcp_cache.h>
#include <mcp_rate_limiter.h>
#include <mcp_profiler.h>
#include <mcp_transport.h> // Needed for transport handle in struct
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h> // For size_t

#ifndef _WIN32 // Only include netinet/in.h if not Windows (winsock2.h covers htonl)
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// Default settings (consider moving defaults elsewhere if they grow)
#define DEFAULT_THREAD_POOL_SIZE 4
#define DEFAULT_TASK_QUEUE_SIZE 1024
#define DEFAULT_CACHE_CAPACITY 128
#define DEFAULT_CACHE_TTL_SECONDS 300 // 5 minutes
#define DEFAULT_MAX_MESSAGE_SIZE (1024 * 1024) // 1MB
#define DEFAULT_RATE_LIMIT_CAPACITY 1024
#define DEFAULT_RATE_LIMIT_WINDOW_SECONDS 60
#define DEFAULT_RATE_LIMIT_MAX_REQUESTS 100

// --- Server Structure Definition ---
// This is the internal definition, the public header should use an opaque pointer.
struct mcp_server {
    mcp_server_config_t config;
    mcp_server_capabilities_t capabilities;
    mcp_transport_t* transport;         // Transport associated via start()
    mcp_thread_pool_t* thread_pool;     // Thread pool for request handling
    mcp_resource_cache_t* resource_cache; // Resource cache
    mcp_rate_limiter_t* rate_limiter;   // Rate limiter instance
    bool running;

    // Resources
    mcp_resource_t** resources;
    size_t resource_count;
    size_t resource_capacity;

    // Resource templates
    mcp_resource_template_t** resource_templates;
    size_t resource_template_count;
    size_t resource_template_capacity;

    // Tools
    mcp_tool_t** tools;
    size_t tool_count;
    size_t tool_capacity;

    // Handlers
    mcp_server_resource_handler_t resource_handler;
    void* resource_handler_user_data;
    mcp_server_tool_handler_t tool_handler;
    void* tool_handler_user_data;       /**< User data pointer for the tool handler. */
};

// --- Internal Function Prototypes ---

// From mcp_server_dispatch.c
char* handle_message(mcp_server_t* server, const void* data, size_t size, int* error_code);
char* handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code);

// From mcp_server_handlers.c
char* handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code);
char* handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code);
char* handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code);
char* handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code);
char* handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, int* error_code);

// From mcp_server_task.c
typedef struct message_task_data_t message_task_data_t; // Forward declare task data struct
void process_message_task(void* arg);
char* transport_message_callback(void* user_data, const void* data, size_t size, int* error_code);

// From mcp_server_response.c
char* create_error_response(uint64_t id, mcp_error_code_t code, const char* message);
char* create_success_response(uint64_t id, char* result_str); // Takes ownership of result_str

#endif // MCP_SERVER_INTERNAL_H
