#include <mcp_server.h>
#include "internal/server_internal.h"
#include <mcp_types.h>
#include <mcp_template_optimized.h>
#include <mcp_log.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "mcp_gateway.h"
#include "mcp_gateway_pool.h"
#include "mcp_arena.h"
#include "mcp_object_pool.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"

/**
 * @brief Default size for the content item object pool
 */
#define CONTENT_ITEM_POOL_INITIAL_CAPACITY 128

/**
 * @brief Default hash table initial capacity
 */
#define DEFAULT_HASHTABLE_INITIAL_CAPACITY 16

/**
 * @brief Default hash table load factor
 */
#define DEFAULT_HASHTABLE_LOAD_FACTOR 0.75f

/**
 * @brief Creates a hash table with string keys and specified value free function
 *
 * @param value_free_func Function to free values stored in the hash table
 * @return Pointer to the created hash table, or NULL on failure
 */
static mcp_hashtable_t* create_string_hashtable(mcp_value_free_func_t value_free_func) {
    return mcp_hashtable_create(
        DEFAULT_HASHTABLE_INITIAL_CAPACITY,
        DEFAULT_HASHTABLE_LOAD_FACTOR,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        value_free_func
    );
}

/**
 * @brief Safely destroys a hash table and sets the pointer to NULL
 *
 * @param table_ptr Pointer to the hash table pointer
 */
static void safe_destroy_hashtable(mcp_hashtable_t** table_ptr) {
    if (table_ptr && *table_ptr) {
        mcp_hashtable_destroy(*table_ptr);
        *table_ptr = NULL;
    }
}

/**
 * @brief Safely destroys a resource and sets the pointer to NULL
 *
 * @param resource_ptr Pointer to the resource pointer
 * @param destroy_func Function to destroy the resource
 */
static void safe_destroy_resource(void** resource_ptr, void (*destroy_func)(void*)) {
    if (resource_ptr && *resource_ptr && destroy_func) {
        destroy_func(*resource_ptr);
        *resource_ptr = NULL;
    }
}

/**
 * @brief Adds an item to a hash table with error handling
 *
 * @param table The hash table
 * @param key The key for the item
 * @param value The value to add
 * @param error_msg Error message to log on failure
 * @param cleanup_func Function to clean up the value on failure
 * @return 0 on success, -1 on failure
 */
static int add_to_hashtable(
    mcp_hashtable_t* table,
    const char* key,
    void* value,
    const char* error_msg,
    void (*cleanup_func)(void*)
) {
    if (!table || !key || !value) {
        if (cleanup_func && value) {
            cleanup_func(value);
        }
        return -1;
    }

    int result = mcp_hashtable_put(table, key, value);
    if (result != 0) {
        if (error_msg) {
            mcp_log_error("%s: %s", error_msg, key);
        }
        if (cleanup_func) {
            cleanup_func(value);
        }
        return -1;
    }

    return 0;
}

/**
 * @brief Cleans up all resources associated with a server
 *
 * @param server The server to clean up
 */
static void cleanup_server_resources(mcp_server_t* server) {
    if (!server) return;

    // Clean up object pool
    safe_destroy_resource((void**)&server->content_item_pool, (void(*)(void*))mcp_object_pool_destroy);

    // Clean up hash tables
    safe_destroy_hashtable(&server->tools_table);
    safe_destroy_hashtable(&server->template_routes_table);
    safe_destroy_resource((void**)&server->template_security, (void(*)(void*))mcp_template_security_destroy);
    safe_destroy_hashtable(&server->resource_templates_table);
    safe_destroy_hashtable(&server->resources_table);

    // Clean up gateway pool
    safe_destroy_resource((void**)&server->pool_manager, (void(*)(void*))gateway_pool_manager_destroy);

    // Clean up rate limiters
    safe_destroy_resource((void**)&server->advanced_rate_limiter, (void(*)(void*))mcp_advanced_rate_limiter_destroy);
    safe_destroy_resource((void**)&server->rate_limiter, (void(*)(void*))mcp_rate_limiter_destroy);

    // Clean up resource cache
    safe_destroy_resource((void**)&server->resource_cache, (void(*)(void*))mcp_cache_destroy);

    // Clean up thread pool
    safe_destroy_resource((void**)&server->thread_pool, (void(*)(void*))mcp_thread_pool_destroy);

    // Clean up synchronization primitives
    safe_destroy_resource((void**)&server->shutdown_mutex, (void(*)(void*))mcp_mutex_destroy);
    safe_destroy_resource((void**)&server->shutdown_cond, (void(*)(void*))mcp_cond_destroy);

    // Clean up configuration strings
    free((void*)server->config.name);
    free((void*)server->config.version);
    free((void*)server->config.description);
    free((void*)server->config.api_key);

    // Clean up prewarm URIs
    if (server->config.prewarm_resource_uris) {
        for (size_t i = 0; i < server->config.prewarm_count; ++i) {
            free(server->config.prewarm_resource_uris[i]);
        }
        free(server->config.prewarm_resource_uris);
    }

    // Reset config pointers/counts
    server->config.prewarm_resource_uris = NULL;
    server->config.prewarm_count = 0;

    // Free gateway backend list
    mcp_free_backend_list(server->backends, server->backend_count);
    server->backends = NULL;
    server->backend_count = 0;

    // Clean up the global template cache
    mcp_template_cache_cleanup();
}

/**
 * @brief Creates an MCP server instance.
 *
 * @param config Pointer to the server configuration
 * @param capabilities Pointer to the server capabilities flags
 * @return Pointer to the created server instance, or NULL on failure
 */
mcp_server_t* mcp_server_create(const mcp_server_config_t* config, const mcp_server_capabilities_t* capabilities) {
    if (config == NULL || capabilities == NULL) {
        return NULL;
    }

    // Allocate and zero-initialize server structure
    mcp_server_t* server = (mcp_server_t*)calloc(1, sizeof(mcp_server_t));
    if (server == NULL) {
        return NULL;
    }

    // --- Copy Configuration ---
    // Copy string fields with mcp_strdup
    server->config.name = config->name ? mcp_strdup(config->name) : NULL;
    server->config.version = config->version ? mcp_strdup(config->version) : NULL;
    server->config.description = config->description ? mcp_strdup(config->description) : NULL;
    server->config.api_key = config->api_key ? mcp_strdup(config->api_key) : NULL;

    // Copy numeric/boolean config values with defaults
    server->config.thread_pool_size = config->thread_pool_size > 0 ?
                                     config->thread_pool_size : DEFAULT_THREAD_POOL_SIZE;
    server->config.task_queue_size = config->task_queue_size > 0 ?
                                    config->task_queue_size : DEFAULT_TASK_QUEUE_SIZE;
    server->config.cache_capacity = config->cache_capacity > 0 ?
                                   config->cache_capacity : DEFAULT_CACHE_CAPACITY;
    server->config.cache_default_ttl_seconds = config->cache_default_ttl_seconds > 0 ?
                                              config->cache_default_ttl_seconds : DEFAULT_CACHE_TTL_SECONDS;
    server->config.max_message_size = config->max_message_size > 0 ?
                                     config->max_message_size : DEFAULT_MAX_MESSAGE_SIZE;
    server->config.rate_limit_capacity = config->rate_limit_capacity > 0 ?
                                        config->rate_limit_capacity : DEFAULT_RATE_LIMIT_CAPACITY;

    // Rate limiting settings - keep 0 values if user wants to disable
    server->config.rate_limit_window_seconds = config->rate_limit_window_seconds;
    server->config.rate_limit_max_requests = config->rate_limit_max_requests;

    // Advanced rate limiter settings
    server->config.use_advanced_rate_limiter = config->use_advanced_rate_limiter || true; // Default to true
    server->config.enable_token_bucket = config->enable_token_bucket || true; // Default to true
    server->config.tokens_per_second = config->tokens_per_second > 0 ? config->tokens_per_second : 5.0;
    server->config.max_tokens = config->max_tokens > 0 ? config->max_tokens : 20;

    // Graceful shutdown settings
    server->config.enable_graceful_shutdown = true; // Default to enabled
    server->config.graceful_shutdown_timeout_ms = config->graceful_shutdown_timeout_ms > 0 ?
                                                config->graceful_shutdown_timeout_ms : 5000;

    // Copy prewarm URIs if provided
    if (config->prewarm_resource_uris && config->prewarm_count > 0) {
        server->config.prewarm_count = config->prewarm_count;
        server->config.prewarm_resource_uris = (char**)calloc(config->prewarm_count, sizeof(char*));
        if (!server->config.prewarm_resource_uris) {
            goto create_error_cleanup;
        }

        for (size_t i = 0; i < config->prewarm_count; ++i) {
            if (config->prewarm_resource_uris[i]) {
                server->config.prewarm_resource_uris[i] = mcp_strdup(config->prewarm_resource_uris[i]);
                if (!server->config.prewarm_resource_uris[i]) {
                    goto create_error_cleanup;
                }
            }
        }
    } else {
        server->config.prewarm_resource_uris = NULL;
        server->config.prewarm_count = 0;
    }

    // Check for allocation failures during config string copy
    if ((config->name && !server->config.name) ||
        (config->version && !server->config.version) ||
        (config->description && !server->config.description) ||
        (config->api_key && !server->config.api_key))
    {
        goto create_error_cleanup;
    }
    // --- End Configuration Copy ---

    // Copy capabilities struct
    server->capabilities = *capabilities;

    // Initialize other fields (already zeroed by calloc)
    server->is_gateway_mode = false;
    server->pool_manager = NULL;
    server->resources_table = NULL;
    server->resource_templates_table = NULL;
    server->tools_table = NULL;
    server->content_item_pool = NULL;

    // Initialize graceful shutdown support
    server->active_requests = 0;
    server->shutting_down = false;
    server->shutdown_mutex = mcp_mutex_create();
    server->shutdown_cond = mcp_cond_create();

    // Check if mutex and condition variable creation succeeded
    if (server->shutdown_mutex == NULL || server->shutdown_cond == NULL) {
        mcp_log_error("Failed to create shutdown synchronization primitives");
        goto create_error_cleanup;
    }

    // Initialize the memory pool system if not already initialized
    static int memory_system_initialized = 0;
    if (!memory_system_initialized) {
        // Initialize with 64-byte cache line, 32 pools, 16 size classes
        if (!mcp_memory_pool_system_init(64, 32, 16)) {
            mcp_log_error("Failed to initialize memory pool system");
            goto create_error_cleanup;
        }

        // Initialize the thread cache
        if (!mcp_thread_cache_init()) {
            mcp_log_error("Failed to initialize thread cache");
            goto create_error_cleanup;
        }

        memory_system_initialized = 1;
    }

    // Create the thread pool
    server->thread_pool = mcp_thread_pool_create(
        server->config.thread_pool_size,
        server->config.task_queue_size
    );

    if (server->thread_pool == NULL) {
        mcp_log_error("Failed to create server thread pool");
        goto create_error_cleanup;
    }

    // Create the resource cache if resources are supported
    if (server->capabilities.resources_supported) {
        server->resource_cache = mcp_cache_create(
            server->config.cache_capacity,
            server->config.cache_default_ttl_seconds
        );

        if (server->resource_cache == NULL) {
            mcp_log_error("Failed to create server resource cache");
            goto create_error_cleanup;
        }
    }

    // Create the rate limiter if rate limiting is enabled
    if (server->config.rate_limit_window_seconds > 0 && server->config.rate_limit_max_requests > 0) {
        if (server->config.use_advanced_rate_limiter) {
            // Configure advanced rate limiter
            mcp_advanced_rate_limiter_config_t adv_config = {
                .capacity_hint = server->config.rate_limit_capacity,
                .enable_burst_handling = true,
                .burst_multiplier = 2,
                .burst_window_seconds = 10,
                .enable_dynamic_rules = false,
                .threshold_for_tightening = 0.9,
                .threshold_for_relaxing = 0.3
            };

            // Create advanced rate limiter
            server->advanced_rate_limiter = mcp_advanced_rate_limiter_create(&adv_config);
            if (server->advanced_rate_limiter == NULL) {
                mcp_log_error("Failed to create advanced rate limiter");
                goto create_error_cleanup;
            }

            // Create appropriate rule based on configuration
            mcp_rate_limit_rule_t rule;
            if (server->config.enable_token_bucket) {
                // Use token bucket algorithm
                rule = mcp_advanced_rate_limiter_create_token_bucket_rule(
                    MCP_RATE_LIMIT_KEY_IP,
                    server->config.tokens_per_second,
                    server->config.max_tokens
                );
            } else {
                // Use fixed window algorithm
                rule = mcp_advanced_rate_limiter_create_default_rule(
                    MCP_RATE_LIMIT_KEY_IP,
                    MCP_RATE_LIMIT_FIXED_WINDOW,
                    server->config.rate_limit_window_seconds,
                    server->config.rate_limit_max_requests
                );
            }

            // Add the rule to the rate limiter
            if (!mcp_advanced_rate_limiter_add_rule(server->advanced_rate_limiter, &rule)) {
                mcp_log_error("Failed to add default rate limit rule");
                goto create_error_cleanup;
            }

            mcp_log_info("Advanced rate limiter created with %s algorithm",
                        server->config.enable_token_bucket ? "token bucket" : "fixed window");
        } else {
            // Create basic rate limiter with fixed window algorithm
            server->rate_limiter = mcp_rate_limiter_create(
                server->config.rate_limit_capacity,
                server->config.rate_limit_window_seconds,
                server->config.rate_limit_max_requests
            );

            if (server->rate_limiter == NULL) {
                mcp_log_error("Failed to create server rate limiter");
                goto create_error_cleanup;
            }

            mcp_log_info("Basic rate limiter created with fixed window algorithm");
        }
    }

    // Create the gateway pool manager (always needed for destroy and dispatch logic)
    server->pool_manager = gateway_pool_manager_create();
    if (server->pool_manager == NULL) {
        mcp_log_error("Failed to create gateway pool manager");
        goto create_error_cleanup;
    }

    // --- Create Hash Tables ---
    // Resources Table (Key: URI string, Value: mcp_resource_t*)
    server->resources_table = create_string_hashtable((mcp_value_free_func_t)mcp_resource_free);
    if (server->resources_table == NULL) {
        mcp_log_error("Failed to create resources hash table");
        goto create_error_cleanup;
    }

    // Resource Templates Table (Key: URI Template string, Value: mcp_resource_template_t*)
    server->resource_templates_table = create_string_hashtable((mcp_value_free_func_t)mcp_resource_template_free);
    if (server->resource_templates_table == NULL) {
        mcp_log_error("Failed to create resource templates hash table");
        goto create_error_cleanup;
    }

    // Tools Table (Key: Tool Name string, Value: mcp_tool_t*)
    server->tools_table = create_string_hashtable((mcp_value_free_func_t)mcp_tool_free);
    if (server->tools_table == NULL) {
        mcp_log_error("Failed to create tools hash table");
        goto create_error_cleanup;
    }

    // Template Routes Table (Key: URI Template string, Value: template_route_t*)
    server->template_routes_table = create_string_hashtable((mcp_value_free_func_t)mcp_server_free_template_routes);
    if (server->template_routes_table == NULL) {
        mcp_log_error("Failed to create template routes hash table");
        goto create_error_cleanup;
    }

    // Initialize template security
    server->template_security = mcp_template_security_create();
    if (server->template_security == NULL) {
        mcp_log_error("Failed to create template security context");
        goto create_error_cleanup;
    }
    // --- End Hash Table Creation ---

    // --- Create Content Item Pool ---
    size_t max_pool_cap = 0; // Unlimited
    server->content_item_pool = mcp_object_pool_create(
        sizeof(mcp_content_item_t),
        CONTENT_ITEM_POOL_INITIAL_CAPACITY,
        max_pool_cap
    );

    if (server->content_item_pool == NULL) {
        mcp_log_error("Failed to create content item object pool");
        goto create_error_cleanup;
    }
    // --- End Content Item Pool Creation ---

    return server;

create_error_cleanup:
    // Centralized cleanup for error cases
    if (server) {
        cleanup_server_resources(server);
        free(server);
    }

    return NULL;
}

/**
 * @brief Starts the server and begins processing messages via the transport.
 *
 * @param server Pointer to the initialized server instance
 * @param transport Pointer to the initialized transport handle
 * @return 0 on success, non-zero on failure
 */
int mcp_server_start(mcp_server_t* server, mcp_transport_t* transport) {
    if (server == NULL || transport == NULL) {
        return -1;
    }

    // Store the transport handle and mark server as running
    server->transport = transport;
    server->running = true;

    // --- Cache Pre-warming ---
    if (server->resource_cache &&
        server->resource_handler &&
        server->config.prewarm_resource_uris &&
        server->config.prewarm_count > 0)
    {
        mcp_log_info("Starting cache pre-warming for %zu URIs...", server->config.prewarm_count);

        // Use a temporary arena for handler calls
        mcp_arena_t prewarm_arena;
        mcp_arena_init(&prewarm_arena, MCP_ARENA_DEFAULT_SIZE);

        // Process each URI in the prewarm list
        for (size_t i = 0; i < server->config.prewarm_count; ++i) {
            const char* uri = server->config.prewarm_resource_uris[i];
            if (!uri) continue;

            mcp_log_debug("Pre-warming resource: %s", uri);

            // Variables for resource handler call
            mcp_content_item_t** content = NULL;
            size_t content_count = 0;
            char* error_message = NULL;
            mcp_error_code_t handler_err = MCP_ERROR_INTERNAL_ERROR; // Default error

            // Call the resource handler
            handler_err = server->resource_handler(
                server,
                uri,
                server->resource_handler_user_data,
                &content,
                &content_count,
                &error_message
            );

            if (handler_err == MCP_ERROR_NONE) {
                // Put the fetched content into the cache with infinite TTL (-1)
                int put_err = mcp_cache_put(
                    server->resource_cache,
                    uri,
                    server->content_item_pool,
                    content,
                    content_count,
                    -1
                );

                if (put_err != 0) {
                    mcp_log_warn("Failed to put pre-warmed resource '%s' into cache", uri);
                } else {
                    mcp_log_debug("Successfully pre-warmed and cached resource: %s", uri);
                }

                // Free the content returned by the handler (cache made copies)
                if (content) {
                    for (size_t j = 0; j < content_count; ++j) {
                        mcp_content_item_free(content[j]);
                    }
                    free(content);
                }
            } else {
                mcp_log_warn("Failed to pre-warm resource '%s': Handler error %d (%s)",
                             uri, handler_err, error_message ? error_message : "N/A");
            }

            free(error_message);
            mcp_arena_reset(&prewarm_arena);
        }

        mcp_arena_destroy(&prewarm_arena);
        mcp_log_info("Cache pre-warming finished");
    }
    // --- End Cache Pre-warming ---

    // Start the transport with the message callback
    return mcp_transport_start(
        transport,
        transport_message_callback, // Callback from mcp_server_task.c
        server,                     // Pass server instance as user_data
        NULL                        // No error callback needed from transport for now
    );
}

/**
 * @brief Stops the server and the associated transport.
 *
 * @param server Pointer to the server instance
 * @return 0 on success, non-zero on failure
 */
int mcp_server_stop(mcp_server_t* server) {
    if (server == NULL) {
        return -1;
    }

    // Check if already stopped
    if (!server->running) {
        mcp_log_debug("Server already stopped");
        return 0;
    }

    // Signal threads to stop
    server->running = false;
    server->shutting_down = true;

    // Stop the transport if it exists
    if (server->transport != NULL) {
        mcp_log_debug("Stopping transport");
        mcp_transport_stop(server->transport);
    }

    // Handle graceful shutdown if enabled
    if (server->config.enable_graceful_shutdown) {
        mcp_log_info("Graceful shutdown initiated, waiting for %d active requests to complete...",
                     server->active_requests);

        // Wait for active requests to complete or timeout
        if (server->active_requests > 0) {
            uint32_t timeout_ms = server->config.graceful_shutdown_timeout_ms > 0 ?
                                server->config.graceful_shutdown_timeout_ms : 1000; // Default 1 second

            // Lock mutex and wait on condition variable
            if (mcp_mutex_lock(server->shutdown_mutex) == 0) {
                bool timed_out = false;

                #ifdef _WIN32
                // Calculate absolute timeout time for Windows
                DWORD start_time = GetTickCount();
                DWORD end_time = start_time + timeout_ms;

                // Wait for active requests to complete or timeout
                while (server->active_requests > 0 && !timed_out) {
                    // Wait in small increments to check for timeout
                    int wait_result = mcp_cond_timedwait(server->shutdown_cond, server->shutdown_mutex, 100);
                    if (wait_result == -2) { // -2 indicates timeout of the wait call
                        DWORD current_time = GetTickCount();
                        if (current_time >= end_time) {
                            timed_out = true;
                        }
                    }
                }
                #else
                // On POSIX, we can use a single timed wait with the full timeout
                int wait_result = mcp_cond_timedwait(server->shutdown_cond, server->shutdown_mutex, timeout_ms);
                if (wait_result == -2) {
                    // -2 indicates timeout
                    timed_out = true;
                }
                #endif

                mcp_mutex_unlock(server->shutdown_mutex);

                // Log the shutdown result
                if (timed_out) {
                    mcp_log_warn("Graceful shutdown timed out after %u ms with %d requests still active",
                                 timeout_ms, server->active_requests);
                } else {
                    mcp_log_info("All requests completed, proceeding with shutdown");
                }
            }
        } else {
            mcp_log_info("No active requests, proceeding with shutdown");
        }
    }

    // Destroy the thread pool (waits for tasks and joins threads)
    if (server->thread_pool != NULL) {
        mcp_thread_pool_destroy(server->thread_pool);
        server->thread_pool = NULL;
    }

    return 0;
}

/**
 * @brief Destroys the server instance and frees associated resources.
 *
 * @param server Pointer to the server instance to destroy
 */
void mcp_server_destroy(mcp_server_t* server) {
    if (server == NULL) {
        return;
    }

    // Ensure server is stopped before destroying
    mcp_log_debug("Stopping server during destroy");
    mcp_server_stop(server);

    // Note: We don't destroy the transport here because it's owned by the caller
    server->transport = NULL;

    // Clean up all server resources
    cleanup_server_resources(server);

    // Note: We don't clean up the thread cache and memory pool system here
    // because they might be used by other servers or clients

    free(server);
}

/**
 * @brief Sets the handler function for processing resource read requests.
 *
 * @param server Pointer to the server instance
 * @param handler The function pointer to the resource handler implementation
 * @param user_data An arbitrary pointer passed back to the handler during calls
 * @return 0 on success, non-zero on failure
 */
int mcp_server_set_resource_handler(mcp_server_t* server, mcp_server_resource_handler_t handler, void* user_data) {
    if (server == NULL) {
        return -1;
    }

    server->resource_handler = handler;
    server->resource_handler_user_data = user_data;
    return 0;
}

/**
 * @brief Registers a template-based resource handler.
 *
 * @param server Pointer to the server instance
 * @param template_uri The template URI pattern
 * @param handler The function pointer to the resource handler implementation
 * @param user_data An arbitrary pointer passed back to the handler during calls
 * @return 0 on success, non-zero on failure
 */
int mcp_server_register_template_handler(mcp_server_t* server, const char* template_uri,
    mcp_server_resource_handler_t handler, void* user_data) {
    if (server == NULL || template_uri == NULL || handler == NULL) {
        return -1;
    }

    // Create the template routes table if it doesn't exist
    if (server->template_routes_table == NULL) {
        server->template_routes_table = create_string_hashtable(
            (mcp_value_free_func_t)mcp_server_free_template_routes
        );

        if (server->template_routes_table == NULL) {
            return -1;
        }
    }

    // Register the template handler using the internal function
    return mcp_server_register_template_handler_internal(server, template_uri, handler, user_data);
}

/**
 * @brief Sets the handler function for processing tool call requests.
 *
 * @param server Pointer to the server instance
 * @param handler The function pointer to the tool handler implementation
 * @param user_data An arbitrary pointer passed back to the handler during calls
 * @return 0 on success, non-zero on failure
 */
int mcp_server_set_tool_handler(mcp_server_t* server, mcp_server_tool_handler_t handler, void* user_data) {
    if (server == NULL) {
        return -1;
    }

    server->tool_handler = handler;
    server->tool_handler_user_data = user_data;
    return 0;
}

/**
 * @brief Adds a static resource definition to the server.
 *
 * @param server Pointer to the server instance
 * @param resource Pointer to the resource definition to add
 * @return 0 on success, non-zero on failure
 */
int mcp_server_add_resource(mcp_server_t* server, const mcp_resource_t* resource) {
    if (server == NULL ||
        resource == NULL ||
        resource->uri == NULL ||
        !server->capabilities.resources_supported) {
        return -1;
    }

    // Create a deep copy first, as the hashtable takes ownership
    mcp_resource_t* resource_copy = mcp_resource_create(
        resource->uri,
        resource->name,
        resource->mime_type,
        resource->description
    );

    if (resource_copy == NULL) {
        return -1;
    }

    // Add the copy to the hash table
    return add_to_hashtable(
        server->resources_table,
        resource_copy->uri,
        resource_copy,
        "Failed to add resource to hash table",
        (void(*)(void*))mcp_resource_free
    );
}

/**
 * @brief Adds a resource template definition to the server.
 *
 * @param server Pointer to the server instance
 * @param tmpl Pointer to the resource template definition to add
 * @return 0 on success, non-zero on failure
 */
int mcp_server_add_resource_template(mcp_server_t* server, const mcp_resource_template_t* tmpl) {
    if (server == NULL ||
        tmpl == NULL ||
        tmpl->uri_template == NULL ||
        !server->capabilities.resources_supported) {
        return -1;
    }

    // Create a deep copy first
    mcp_resource_template_t* template_copy = mcp_resource_template_create(
        tmpl->uri_template,
        tmpl->name,
        tmpl->mime_type,
        tmpl->description
    );

    if (template_copy == NULL) {
        return -1;
    }

    // Add the copy to the hash table
    return add_to_hashtable(
        server->resource_templates_table,
        template_copy->uri_template,
        template_copy,
        "Failed to add resource template to hash table",
        (void(*)(void*))mcp_resource_template_free
    );
}

/**
 * @brief Adds a tool definition to the server.
 *
 * @param server Pointer to the server instance
 * @param tool Pointer to the tool definition to add
 * @return 0 on success, non-zero on failure
 */
int mcp_server_add_tool(mcp_server_t* server, const mcp_tool_t* tool) {
    if (server == NULL ||
        tool == NULL ||
        tool->name == NULL ||
        !server->capabilities.tools_supported) {
        return -1;
    }

    // Create a deep copy first
    mcp_tool_t* tool_copy = mcp_tool_create(tool->name, tool->description);
    if (tool_copy == NULL) {
        return -1;
    }

    // Copy parameters
    for (size_t i = 0; i < tool->input_schema_count; i++) {
        if (mcp_tool_add_param(
                tool_copy,
                tool->input_schema[i].name,
                tool->input_schema[i].type,
                tool->input_schema[i].description,
                tool->input_schema[i].required) != 0) {
            mcp_tool_free(tool_copy); // Clean up partially created copy
            return -1;
        }
    }

    // Add the copy to the hash table
    return add_to_hashtable(
        server->tools_table,
        tool_copy->name,
        tool_copy,
        "Failed to add tool to hash table",
        (void(*)(void*))mcp_tool_free
    );
}

/**
 * @brief Manually process a single message received outside the transport mechanism.
 *
 * This function is primarily for testing or scenarios where the transport layer
 * is managed externally.
 *
 * @param server Pointer to the server instance
 * @param data Pointer to the raw message data
 * @param size The size of the message data
 * @return 0 if the message was processed successfully, non-zero on failure
 */
int mcp_server_process_message(mcp_server_t* server, const void* data, size_t size) {
    if (server == NULL || data == NULL || size == 0) {
        return -1;
    }

    // Process the message and get the response
    int error_code = 0;
    char* response = handle_message(server, data, size, &error_code);

    // Free the response (caller doesn't get it in this API)
    free(response);

    return error_code;
}
