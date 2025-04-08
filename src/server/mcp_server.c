#include <mcp_server.h>
#include "internal/server_internal.h"
#include <mcp_types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gateway.h"
#include "gateway_pool.h"
#include "mcp_arena.h"

// --- Public API Implementation ---

mcp_server_t* mcp_server_create(
    const mcp_server_config_t* config,
    const mcp_server_capabilities_t* capabilities
) {
    if (config == NULL || capabilities == NULL) {
        return NULL;
    }

    mcp_server_t* server = (mcp_server_t*)calloc(1, sizeof(mcp_server_t)); // Use calloc for zero-initialization
    if (server == NULL) {
        return NULL;
    }

    // --- Copy Configuration ---
    // Copy strings using mcp_strdup
    server->config.name = config->name ? mcp_strdup(config->name) : NULL;
    server->config.version = config->version ? mcp_strdup(config->version) : NULL;
    server->config.description = config->description ? mcp_strdup(config->description) : NULL;
    server->config.api_key = config->api_key ? mcp_strdup(config->api_key) : NULL;

    // Copy numeric/boolean config values directly, applying defaults if 0
    server->config.thread_pool_size = config->thread_pool_size > 0 ? config->thread_pool_size : DEFAULT_THREAD_POOL_SIZE;
    server->config.task_queue_size = config->task_queue_size > 0 ? config->task_queue_size : DEFAULT_TASK_QUEUE_SIZE;
    server->config.cache_capacity = config->cache_capacity > 0 ? config->cache_capacity : DEFAULT_CACHE_CAPACITY;
    server->config.cache_default_ttl_seconds = config->cache_default_ttl_seconds > 0 ? config->cache_default_ttl_seconds : DEFAULT_CACHE_TTL_SECONDS;
    server->config.max_message_size = config->max_message_size > 0 ? config->max_message_size : DEFAULT_MAX_MESSAGE_SIZE;
    server->config.rate_limit_capacity = config->rate_limit_capacity > 0 ? config->rate_limit_capacity : DEFAULT_RATE_LIMIT_CAPACITY;
    server->config.rate_limit_window_seconds = config->rate_limit_window_seconds; // Keep 0 if user wants to disable
    server->config.rate_limit_max_requests = config->rate_limit_max_requests;   // Keep 0 if user wants to disable

    // Copy prewarm URIs if provided
    if (config->prewarm_resource_uris && config->prewarm_count > 0) {
        server->config.prewarm_count = config->prewarm_count;
        server->config.prewarm_resource_uris = (char**)malloc(config->prewarm_count * sizeof(char*));
        if (!server->config.prewarm_resource_uris) goto create_error_cleanup;
        memset(server->config.prewarm_resource_uris, 0, config->prewarm_count * sizeof(char*));
        for (size_t i = 0; i < config->prewarm_count; ++i) {
            if (config->prewarm_resource_uris[i]) {
                server->config.prewarm_resource_uris[i] = mcp_strdup(config->prewarm_resource_uris[i]);
                if (!server->config.prewarm_resource_uris[i]) goto create_error_cleanup; // Handle allocation failure
            } else {
                 server->config.prewarm_resource_uris[i] = NULL; // Ensure NULL if source is NULL
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
    server->is_gateway_mode = false; // Explicitly set default

    // Create the thread pool using the final determined values
    server->thread_pool = mcp_thread_pool_create(server->config.thread_pool_size, server->config.task_queue_size);
    if (server->thread_pool == NULL) {
        mcp_log_error("Failed to create server thread pool.");
        goto create_error_cleanup;
    }

    // Create the resource cache if resources are supported, using final determined values
    if (server->capabilities.resources_supported) {
        server->resource_cache = mcp_cache_create(server->config.cache_capacity, server->config.cache_default_ttl_seconds);
        if (server->resource_cache == NULL) {
            mcp_log_error("Failed to create server resource cache.");
            goto create_error_cleanup;
        }
    }

    // Create the rate limiter using final determined values (if enabled)
    if (server->config.rate_limit_window_seconds > 0 && server->config.rate_limit_max_requests > 0) {
         server->rate_limiter = mcp_rate_limiter_create(
             server->config.rate_limit_capacity,
             server->config.rate_limit_window_seconds,
             server->config.rate_limit_max_requests
         );
         if (server->rate_limiter == NULL) {
             mcp_log_error("Failed to create server rate limiter.");
             goto create_error_cleanup;
         }
    }

    // Create the gateway pool manager (always create it, needed for destroy and dispatch logic)
    server->pool_manager = gateway_pool_manager_create();
    if (server->pool_manager == NULL) {
        mcp_log_error("Failed to create gateway pool manager.");
        goto create_error_cleanup;
    }


    return server;

create_error_cleanup:
    // Centralized cleanup using goto
    if (server) {
        if (server->pool_manager) gateway_pool_manager_destroy(server->pool_manager); // Added cleanup
        if (server->rate_limiter) mcp_rate_limiter_destroy(server->rate_limiter);
        if (server->resource_cache) mcp_cache_destroy(server->resource_cache);
        if (server->thread_pool) mcp_thread_pool_destroy(server->thread_pool);
        free((void*)server->config.name);
        free((void*)server->config.version);
        free((void*)server->config.description);
        free((void*)server->config.api_key);
        if (server->config.prewarm_resource_uris) {
             for (size_t i = 0; i < server->config.prewarm_count; ++i) {
                 free(server->config.prewarm_resource_uris[i]);
             }
             free(server->config.prewarm_resource_uris);
        }
        free(server);
    }
    return NULL;
}

int mcp_server_start(
    mcp_server_t* server,
    mcp_transport_t* transport
) {
    if (server == NULL || transport == NULL) {
        return -1;
    }

    server->transport = transport; // Store the transport handle
    server->running = true;

    // --- Cache Pre-warming ---
    if (server->resource_cache && server->resource_handler &&
        server->config.prewarm_resource_uris && server->config.prewarm_count > 0)
    {
        mcp_log_info("Starting cache pre-warming for %zu URIs...", server->config.prewarm_count);
        mcp_arena_t prewarm_arena; // Use a temporary arena for handler calls
        // Initialize with a reasonable size, adjust if needed
        // mcp_arena_init returns void, so we cannot check its return value here.
        // Assume initialization succeeds for now. Error handling within arena_init might be needed if it can fail.
        mcp_arena_init(&prewarm_arena, MCP_ARENA_DEFAULT_SIZE);
        // Proceed with pre-warming loop
        for (size_t i = 0; i < server->config.prewarm_count; ++i) {
            const char* uri = server->config.prewarm_resource_uris[i];
                if (!uri) continue;

                mcp_log_debug("Pre-warming resource: %s", uri);
                mcp_content_item_t** content = NULL;
                size_t content_count = 0;
                char* error_message = NULL;
                // Use the temporary arena for this handler call
                mcp_error_code_t handler_err = MCP_ERROR_INTERNAL_ERROR; // Default error
                if (server->resource_handler) {
                     // Directly call the handler from the server struct
                     handler_err = server->resource_handler(
                        server, uri, server->resource_handler_user_data,
                        &content, &content_count, &error_message
                    );
                } else {
                     mcp_log_error("Resource handler is NULL during pre-warming.");
                     // Handle error appropriately, maybe break or continue
                }


                if (handler_err == MCP_ERROR_NONE) {
                    // Put the fetched content into the cache with infinite TTL (-1 or 0)
                    int put_err = mcp_cache_put(server->resource_cache, uri, content, content_count, -1);
                    if (put_err != 0) {
                        mcp_log_warn("Failed to put pre-warmed resource '%s' into cache.", uri);
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
                free(error_message); // Free error message if any
                mcp_arena_reset(&prewarm_arena); // Reset arena for next iteration
            }
            mcp_arena_destroy(&prewarm_arena); // Destroy temporary arena
            mcp_log_info("Cache pre-warming finished.");
        // Removed the 'else' block corresponding to the removed 'if' check
    }
    // --- End Cache Pre-warming ---


    // Pass the callback from mcp_server_task.c (declared in internal header)
    return mcp_transport_start(
        transport,
        transport_message_callback, // Callback from mcp_server_task.c
        server,                     // Pass server instance as user_data
        NULL                        // No error callback needed from transport for now
    );
}

int mcp_server_stop(mcp_server_t* server) {
    if (server == NULL) {
        return -1;
    }

    server->running = false; // Signal threads to stop (though pool handles this)

    if (server->transport != NULL) {
        // Stop the transport first (e.g., stop accepting connections)
        mcp_transport_stop(server->transport);
    }

    // Destroy the thread pool (waits for tasks and joins threads)
    if (server->thread_pool != NULL) {
        mcp_thread_pool_destroy(server->thread_pool);
        server->thread_pool = NULL; // Mark as destroyed
    }

    return 0;
}

void mcp_server_destroy(mcp_server_t* server) {
    if (server == NULL) {
        return;
    }

    // Ensure stop is called first (idempotent checks inside stop/destroy functions)
    mcp_server_stop(server);

    // Free configuration strings
    free((void*)server->config.name);
    free((void*)server->config.version);
    free((void*)server->config.description);
    free((void*)server->config.api_key);
    // Free prewarm URIs
    if (server->config.prewarm_resource_uris) {
        for (size_t i = 0; i < server->config.prewarm_count; ++i) {
            free(server->config.prewarm_resource_uris[i]); // Free individual strings
        }
        free(server->config.prewarm_resource_uris); // Free the array itself
    }
    // Reset config pointers/counts after freeing
    server->config.prewarm_resource_uris = NULL;
    server->config.prewarm_count = 0;


    // Free gateway backend list
    mcp_free_backend_list(server->backends, server->backend_count);
    server->backends = NULL; // Reset pointer
    server->backend_count = 0; // Reset count

    // Free dynamically allocated resource/template/tool lists and their contents
    for (size_t i = 0; i < server->resource_count; i++) {
        mcp_resource_free(server->resources[i]);
    }
    free(server->resources);
    server->resources = NULL; // Reset pointer
    server->resource_count = 0;
    server->resource_capacity = 0;

    for (size_t i = 0; i < server->resource_template_count; i++) {
        mcp_resource_template_free(server->resource_templates[i]);
    }
    free(server->resource_templates);
    server->resource_templates = NULL; // Reset pointer
    server->resource_template_count = 0;
    server->resource_template_capacity = 0;

    for (size_t i = 0; i < server->tool_count; i++) {
        mcp_tool_free(server->tools[i]);
    }
    free(server->tools);
    server->tools = NULL; // Reset pointer
    server->tool_count = 0;
    server->tool_capacity = 0;

    // Destroy components
    if (server->pool_manager) { // Added destroy call
        gateway_pool_manager_destroy(server->pool_manager);
        server->pool_manager = NULL;
    }
    if (server->rate_limiter) {
        mcp_rate_limiter_destroy(server->rate_limiter);
        server->rate_limiter = NULL;
    }
    if (server->resource_cache) {
        mcp_cache_destroy(server->resource_cache);
        server->resource_cache = NULL;
    }
    if (server->thread_pool) {
        // Should be destroyed by stop, but check just in case
        mcp_thread_pool_destroy(server->thread_pool);
        server->thread_pool = NULL;
    }

    // Finally, free the server struct itself
    free(server);
}

int mcp_server_set_resource_handler(
    mcp_server_t* server,
    mcp_server_resource_handler_t handler,
    void* user_data
) {
    if (server == NULL) {
        return -1;
    }
    server->resource_handler = handler;
    server->resource_handler_user_data = user_data;
    return 0;
}

int mcp_server_set_tool_handler(
    mcp_server_t* server,
    mcp_server_tool_handler_t handler,
    void* user_data
) {
    if (server == NULL) {
        return -1;
    }
    server->tool_handler = handler;
    server->tool_handler_user_data = user_data;
    return 0;
}

// Uses malloc for resource copy
int mcp_server_add_resource(
    mcp_server_t* server,
    const mcp_resource_t* resource
) {
    if (server == NULL || resource == NULL || !server->capabilities.resources_supported) {
        return -1;
    }
    if (server->resource_count >= server->resource_capacity) {
        size_t new_capacity = server->resource_capacity == 0 ? 8 : server->resource_capacity * 2;
        mcp_resource_t** new_resources = (mcp_resource_t**)realloc(server->resources, new_capacity * sizeof(mcp_resource_t*));
        if (new_resources == NULL) return -1;
        server->resources = new_resources;
        server->resource_capacity = new_capacity;
    }
    // Use the create function to make a deep copy
    mcp_resource_t* resource_copy = mcp_resource_create(resource->uri, resource->name, resource->mime_type, resource->description);
    if (resource_copy == NULL) return -1;
    server->resources[server->resource_count++] = resource_copy;
    return 0;
}

// Uses malloc for template copy
int mcp_server_add_resource_template(
    mcp_server_t* server,
    const mcp_resource_template_t* tmpl
) {
    if (server == NULL || tmpl == NULL || !server->capabilities.resources_supported) {
        return -1;
    }
     if (server->resource_template_count >= server->resource_template_capacity) {
        size_t new_capacity = server->resource_template_capacity == 0 ? 8 : server->resource_template_capacity * 2;
        mcp_resource_template_t** new_templates = (mcp_resource_template_t**)realloc(server->resource_templates, new_capacity * sizeof(mcp_resource_template_t*));
        if (new_templates == NULL) return -1;
        server->resource_templates = new_templates;
        server->resource_template_capacity = new_capacity;
    }
    // Use the create function to make a deep copy
    mcp_resource_template_t* template_copy = mcp_resource_template_create(tmpl->uri_template, tmpl->name, tmpl->mime_type, tmpl->description);
    if (template_copy == NULL) return -1;
    server->resource_templates[server->resource_template_count++] = template_copy;
    return 0;
}

// Uses malloc for tool copy
int mcp_server_add_tool(
    mcp_server_t* server,
    const mcp_tool_t* tool
) {
    if (server == NULL || tool == NULL || !server->capabilities.tools_supported) {
        return -1;
    }
    if (server->tool_count >= server->tool_capacity) {
        size_t new_capacity = server->tool_capacity == 0 ? 8 : server->tool_capacity * 2;
        mcp_tool_t** new_tools = (mcp_tool_t**)realloc(server->tools, new_capacity * sizeof(mcp_tool_t*));
        if (new_tools == NULL) return -1;
        server->tools = new_tools;
        server->tool_capacity = new_capacity;
    }
    // Use the create function and add_param to make a deep copy
    mcp_tool_t* tool_copy = mcp_tool_create(tool->name, tool->description);
    if (tool_copy == NULL) return -1;
    // Copy parameters
    for (size_t i = 0; i < tool->input_schema_count; i++) {
        if (mcp_tool_add_param(tool_copy,
                               tool->input_schema[i].name,
                               tool->input_schema[i].type,
                               tool->input_schema[i].description,
                               tool->input_schema[i].required) != 0) {
            mcp_tool_free(tool_copy); // Clean up partially created copy
            return -1;
        }
    }
    server->tools[server->tool_count++] = tool_copy;
    return 0;
}

// --- Deprecated Function ---

// This function is likely unused now, as messages come via the transport callback.
// Keep it for potential direct injection testing, but it's not part of the main flow.
int mcp_server_process_message(
    mcp_server_t* server,
    const void* data,
    size_t size
) {
    if (server == NULL || data == NULL || size == 0) {
        return -1;
    }
    int error_code = 0;
    // Call handle_message from mcp_server_dispatch.c
    char* response = handle_message(server, data, size, &error_code);
    free(response); // Caller doesn't get the response here
    return error_code;
}
