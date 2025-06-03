#ifndef SERVER_INTERNAL_H
#define SERVER_INTERNAL_H

#ifdef _WIN32
#include <win_socket_compat.h>
#endif

#include <mcp_server.h>
#include <mcp_server_core.h>
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
#include "mcp_gateway.h"
#include "mcp_gateway_pool.h"
#include "mcp_auth.h"
#include "mcp_hashtable.h"
#include "mcp_object_pool.h"
#include "mcp_template_security.h"
#include "server_template_router.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef _WIN32
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


char* handle_message(mcp_server_t* server, const void* data, size_t size, int* error_code);
// Note: handle_request now needs the auth context
char* handle_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

// Note: All specific handlers now need the auth context
char* handle_list_resources_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_list_resource_templates_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_read_resource_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_list_tools_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_call_tool_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

// Note: Ping might not need auth context, but keep signature consistent for now
char* handle_ping_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

char* handle_get_performance_metrics_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);
char* handle_reset_performance_metrics_request(mcp_server_t* server, mcp_arena_t* arena, const mcp_request_t* request, const mcp_auth_context_t* auth_context, int* error_code);

typedef struct message_task_data_t message_task_data_t; // Forward declare task data struct
void process_message_task(void* arg);
char* transport_message_callback(void* user_data, const void* data, size_t size, int* error_code);

char* create_error_response(uint64_t id, mcp_error_code_t code, const char* message);
char* create_success_response(uint64_t id, char* result_str); // Takes ownership of result_str

#endif // SERVER_INTERNAL_H
