#include "internal/server_internal.h"
#include <mcp_types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gateway.h"

// --- Public API Implementation ---

mcp_server_t* mcp_server_create(
    const mcp_server_config_t* config,
    const mcp_server_capabilities_t* capabilities
) {
    if (config == NULL || capabilities == NULL) {
        return NULL;
    }

    mcp_server_t* server = (mcp_server_t*)malloc(sizeof(mcp_server_t));
    if (server == NULL) {
        return NULL;
    }

    // Copy configuration (uses mcp_strdup)
    server->config.name = config->name ? mcp_strdup(config->name) : NULL;
    server->config.version = config->version ? mcp_strdup(config->version) : NULL;
    server->config.description = config->description ? mcp_strdup(config->description) : NULL;
    server->config.api_key = config->api_key ? mcp_strdup(config->api_key) : NULL; // Copy API key

    // Copy numeric/boolean config values directly
    server->config.thread_pool_size = config->thread_pool_size;
    server->config.task_queue_size = config->task_queue_size;
    server->config.cache_capacity = config->cache_capacity;
    server->config.cache_default_ttl_seconds = config->cache_default_ttl_seconds;
    server->config.max_message_size = config->max_message_size;
    server->config.rate_limit_capacity = config->rate_limit_capacity;
    server->config.rate_limit_window_seconds = config->rate_limit_window_seconds;
    server->config.rate_limit_max_requests = config->rate_limit_max_requests;

    // Copy capabilities struct
    server->capabilities = *capabilities; // Assuming capabilities struct is simple enough for direct copy

    // Initialize other fields
    server->transport = NULL;
    server->running = false;

    server->resources = NULL;
    server->resource_count = 0;
    server->resource_capacity = 0;

    server->resource_templates = NULL;
    server->resource_template_count = 0;
    server->resource_template_capacity = 0;

    server->tools = NULL;
    server->tool_count = 0;
    server->tool_capacity = 0;

    server->resource_handler = NULL;
    server->resource_handler_user_data = NULL;
    server->tool_handler = NULL;
    server->tool_handler_user_data = NULL;
    server->thread_pool = NULL;
    server->resource_cache = NULL;
    server->rate_limiter = NULL;
    server->backends = NULL; // Initialize gateway fields
    server->backend_count = 0; // Initialize gateway fields

    // Check for allocation failures during config copy
    if ((config->name && !server->config.name) ||
        (config->version && !server->config.version) ||
        (config->description && !server->config.description) ||
        (config->api_key && !server->config.api_key))
    {
        goto create_error_cleanup; // Use goto for centralized cleanup
    }

    // Create the thread pool
    size_t pool_size = server->config.thread_pool_size > 0 ? server->config.thread_pool_size : DEFAULT_THREAD_POOL_SIZE;
    size_t queue_size = server->config.task_queue_size > 0 ? server->config.task_queue_size : DEFAULT_TASK_QUEUE_SIZE;
    server->thread_pool = mcp_thread_pool_create(pool_size, queue_size);
    if (server->thread_pool == NULL) {
        fprintf(stderr, "Failed to create server thread pool.\n");
        goto create_error_cleanup;
    }

    // Create the resource cache if resources are supported
    if (server->capabilities.resources_supported) {
        size_t cache_cap = server->config.cache_capacity > 0 ? server->config.cache_capacity : DEFAULT_CACHE_CAPACITY;
        time_t cache_ttl = server->config.cache_default_ttl_seconds > 0 ? server->config.cache_default_ttl_seconds : DEFAULT_CACHE_TTL_SECONDS;
        server->resource_cache = mcp_cache_create(cache_cap, cache_ttl);
        if (server->resource_cache == NULL) {
            fprintf(stderr, "Failed to create server resource cache.\n");
            goto create_error_cleanup;
        }
    }

    // Create the rate limiter if configured
    if (server->config.rate_limit_window_seconds > 0 && server->config.rate_limit_max_requests > 0) {
        size_t rl_cap = server->config.rate_limit_capacity > 0 ? server->config.rate_limit_capacity : DEFAULT_RATE_LIMIT_CAPACITY;
        size_t rl_win = server->config.rate_limit_window_seconds;
        size_t rl_max = server->config.rate_limit_max_requests;
        server->rate_limiter = mcp_rate_limiter_create(rl_cap, rl_win, rl_max);
        if (server->rate_limiter == NULL) {
            fprintf(stderr, "Failed to create server rate limiter.\n");
            goto create_error_cleanup;
        }
    }

    return server;

create_error_cleanup:
    // Centralized cleanup using goto
    if (server) {
        if (server->rate_limiter) mcp_rate_limiter_destroy(server->rate_limiter);
        if (server->resource_cache) mcp_cache_destroy(server->resource_cache);
        if (server->thread_pool) mcp_thread_pool_destroy(server->thread_pool);
        free((void*)server->config.name);
        free((void*)server->config.version);
        free((void*)server->config.description);
        free((void*)server->config.api_key);
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

    // Free gateway backend list
    mcp_free_backend_list(server->backends, server->backend_count);

    // Free dynamically allocated resource/template/tool lists and their contents
    for (size_t i = 0; i < server->resource_count; i++) {
        mcp_resource_free(server->resources[i]); // Frees internal strings and the struct itself
    }
    free(server->resources);

    for (size_t i = 0; i < server->resource_template_count; i++) {
        mcp_resource_template_free(server->resource_templates[i]); // Frees internal strings and the struct itself
    }
    free(server->resource_templates);

    for (size_t i = 0; i < server->tool_count; i++) {
        mcp_tool_free(server->tools[i]); // Frees internal strings, schema, and the struct itself
    }
    free(server->tools);

    // Destroy components (already handled by mcp_server_stop, but good practice for direct destroy calls)
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
