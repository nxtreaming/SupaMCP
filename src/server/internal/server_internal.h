#ifndef MCP_SERVER_INTERNAL_H
#define MCP_SERVER_INTERNAL_H

// Include our Windows socket compatibility header
#ifdef _WIN32
#include <win_socket_compat.h>
#endif

#include <mcp_server.h>
#include <mcp_json.h>
#include <mcp_arena.h>
#include <mcp_log.h>
#include <mcp_thread_pool.h>
#include <mcp_cache.h>
#include <mcp_rate_limiter.h>
#include <mcp_advanced_rate_limiter.h>
#include <mcp_profiler.h>
#include <mcp_transport.h>
#include <mcp_performance_metrics.h>
#include <mcp_performance_collector.h>
#include "gateway.h"
#include "gateway_pool.h"
#include "mcp_auth.h"
#include "mcp_hashtable.h"
#include "mcp_object_pool.h"
#include "mcp_template_security.h"
#include "mcp_server_template_router.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef _WIN32 // Only include netinet/in.h if not Windows (winsock2.h covers htonl)
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// Default settings (consider moving defaults elsewhere if they grow)
#define DEFAULT_THREAD_POOL_SIZE 4
#define DEFAULT_TASK_QUEUE_SIZE 1024
#define DEFAULT_CACHE_CAPACITY 4096
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
    mcp_rate_limiter_t* rate_limiter;   // Basic rate limiter instance
    mcp_advanced_rate_limiter_t* advanced_rate_limiter; // Advanced rate limiter instance
    bool running;

    // Graceful shutdown support
    volatile int active_requests;       // Counter for active requests
    volatile bool shutting_down;        // Flag indicating server is shutting down
    mcp_mutex_t* shutdown_mutex;        // Mutex for shutdown synchronization
    mcp_cond_t* shutdown_cond;          // Condition variable for shutdown waiting

    // Use hash tables for managing resources, templates, and tools
    mcp_hashtable_t* resources_table;         // Key: URI (string), Value: mcp_resource_t*
    mcp_hashtable_t* resource_templates_table; // Key: URI Template (string), Value: mcp_resource_template_t*
    mcp_hashtable_t* tools_table;             // Key: Tool Name (string), Value: mcp_tool_t*
    mcp_hashtable_t* template_routes_table;   // Key: URI Template (string), Value: template_route_t*

    // Template security
    mcp_template_security_t* template_security; // Template security context

    // Handlers
    mcp_server_resource_handler_t resource_handler;
    void* resource_handler_user_data;
    mcp_server_tool_handler_t tool_handler;
    void* tool_handler_user_data;       /**< User data pointer for the tool handler. */

    // Gateway Backends (Loaded from config)
    mcp_backend_info_t* backends;       /**< Array of configured backend servers. */
    size_t backend_count;               /**< Number of configured backend servers. */
    bool is_gateway_mode;               /**< Flag indicating if gateway mode is enabled. */
    gateway_pool_manager_t* pool_manager; /**< Connection pool manager for gateway mode. */
    mcp_object_pool_t* content_item_pool; /**< Object pool for mcp_content_item_t. */
};

// --- Internal Function Prototypes ---

// From mcp_server_dispatch.c
char* handle_message(mcp_server_t* server, const void* data, size_t size, int* error_code);
// Note: handle_request now needs the auth context
char* handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

// From mcp_server_handlers.c
// Note: All specific handlers now need the auth context
char* handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

// From mcp_server_ping.c
// Note: Ping might not need auth context, but keep signature consistent for now
char* handle_ping_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

// From mcp_performance_collector.c
char* handle_get_performance_metrics_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_reset_performance_metrics_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

// From mcp_server_task.c
typedef struct message_task_data_t message_task_data_t; // Forward declare task data struct
void process_message_task(void* arg);
char* transport_message_callback(void* user_data, const void* data, size_t size, int* error_code);

// From mcp_server_response.c
char* create_error_response(uint64_t id, mcp_error_code_t code, const char* message);
char* create_success_response(uint64_t id, char* result_str); // Takes ownership of result_str


#endif // MCP_SERVER_INTERNAL_H
