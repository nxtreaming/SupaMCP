#ifndef MCP_SERVER_CORE_H
#define MCP_SERVER_CORE_H

#include <mcp_server.h>
#include <mcp_thread_pool.h>
#include <mcp_cache.h>
#include <mcp_rate_limiter.h>
#include <mcp_advanced_rate_limiter.h>
#include <mcp_sync.h>
#include <mcp_hashtable.h>
#include <mcp_object_pool.h>
#include <mcp_gateway.h>
#include <mcp_gateway_pool.h>
#include <mcp_template_security.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MCP server instance structure.
 * 
 * This structure contains all the state and configuration for an MCP server instance.
 * This is the complete definition that was previously internal, now made available
 * for server applications that need direct access to server internals.
 */
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

#ifdef __cplusplus
}
#endif

#endif /* MCP_SERVER_CORE_H */
