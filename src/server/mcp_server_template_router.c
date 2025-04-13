#include "internal/server_internal.h"
#include "mcp_template.h"
#include "mcp_template_optimized.h"
#include "mcp_hashtable.h"
#include "mcp_string_utils.h"
#include "mcp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Structure to hold template routing information
 */
typedef struct {
    char* template_uri;                 /**< The template URI pattern */
    mcp_server_resource_handler_t handler; /**< The handler function for this template */
    void* user_data;                    /**< User data to pass to the handler */
} template_route_t;

/**
 * @brief Creates a new template route
 *
 * @param template_uri The template URI pattern
 * @param handler The handler function for this template
 * @param user_data User data to pass to the handler
 * @return A newly allocated template route, or NULL on error
 */
static template_route_t* template_route_create(const char* template_uri, mcp_server_resource_handler_t handler, void* user_data) {
    if (template_uri == NULL || handler == NULL) {
        return NULL;
    }

    template_route_t* route = (template_route_t*)malloc(sizeof(template_route_t));
    if (route == NULL) {
        return NULL;
    }

    route->template_uri = mcp_strdup(template_uri);
    if (route->template_uri == NULL) {
        free(route);
        return NULL;
    }

    route->handler = handler;
    route->user_data = user_data;

    return route;
}

/**
 * @brief Frees a template route
 *
 * @param route The route to free
 */
static void template_route_free(template_route_t* route) {
    if (route == NULL) {
        return;
    }

    free(route->template_uri);
    free(route);
}

/**
 * @brief Structure to hold template route matching context
 */
typedef struct {
    const char* uri;              /**< The URI to match */
    template_route_t* match;      /**< The matching route, if found */
    mcp_json_t* params;           /**< The extracted parameters, if a match is found */
} template_match_context_t;

/**
 * @brief Callback function for finding a matching template route
 */
static void template_route_match_callback(const void* key, void* value, void* user_data) {
    (void)key; // Unused
    template_route_t* route = (template_route_t*)value;
    template_match_context_t* context = (template_match_context_t*)user_data;

    // If we already found a match, skip
    if (context->match != NULL) {
        return;
    }

    // Check if the URI matches the template using the optimized function
    if (mcp_template_matches_optimized(context->uri, route->template_uri)) {
        // Extract parameters from the URI using the optimized function
        context->params = mcp_template_extract_params_optimized(context->uri, route->template_uri);
        context->match = route;
    }
}

/**
 * @brief Finds a template route that matches the given URI
 *
 * @param server The server instance
 * @param uri The URI to match
 * @param params_out Pointer to receive the extracted parameters
 * @return The matching template route, or NULL if no match is found
 */
template_route_t* mcp_server_find_template_route(mcp_server_t* server, const char* uri, mcp_json_t** params_out) {
    if (server == NULL || uri == NULL || params_out == NULL) {
        return NULL;
    }

    *params_out = NULL;

    // If there's no template routes table, return NULL
    if (server->template_routes_table == NULL) {
        return NULL;
    }

    // Create a context for the template route matching
    template_match_context_t context = {
        .uri = uri,
        .match = NULL,
        .params = NULL
    };

    // Iterate through all template routes
    mcp_hashtable_foreach(server->template_routes_table, template_route_match_callback, &context);

    // If a match was found, set the output parameters
    if (context.match != NULL) {
        *params_out = context.params;
        return context.match;
    }

    return NULL;
}

/**
 * @brief Internal function to register a template-based resource handler
 *
 * @param server The server instance
 * @param template_uri The template URI pattern
 * @param handler The handler function for this template
 * @param user_data User data to pass to the handler
 * @return 0 on success, non-zero on error
 */
int mcp_server_register_template_handler_internal(
    mcp_server_t* server,
    const char* template_uri,
    mcp_server_resource_handler_t handler,
    void* user_data
) {
    if (server == NULL || template_uri == NULL || handler == NULL) {
        return -1;
    }

    // Create the template routes table if it doesn't exist
    if (server->template_routes_table == NULL) {
        server->template_routes_table = mcp_hashtable_create(64, 0.75f, mcp_hashtable_string_hash, mcp_hashtable_string_compare,
                                                          mcp_hashtable_string_dup, mcp_hashtable_string_free,
                                                          (mcp_value_free_func_t)template_route_free);
        if (server->template_routes_table == NULL) {
            return -1;
        }
    }

    // Create a new template route
    template_route_t* route = template_route_create(template_uri, handler, user_data);
    if (route == NULL) {
        return -1;
    }

    // Add the route to the table
    if (mcp_hashtable_put(server->template_routes_table, route->template_uri, route) != 0) {
        template_route_free(route);
        return -1;
    }

    return 0;
}

/**
 * @brief Callback function to free template routes when the server is destroyed
 */
void mcp_server_free_template_routes(const void* key, void* value, void* user_data) {
    (void)key;
    (void)user_data;
    template_route_t* route = (template_route_t*)value;
    template_route_free(route);
}

/**
 * @brief Structure to hold template parameters for a resource handler
 */
typedef struct {
    mcp_json_t* params;         /**< The extracted parameters */
    mcp_server_resource_handler_t original_handler; /**< The original handler */
    void* original_user_data;   /**< The original user data */
} template_handler_context_t;

/**
 * @brief Wrapper function for template-based resource handlers
 *
 * This function wraps the original resource handler and provides the
 * extracted parameters as part of the user_data.
 */
mcp_error_code_t template_handler_wrapper(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    template_handler_context_t* context = (template_handler_context_t*)user_data;
    if (context == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Internal error: template handler context is NULL");
        }
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Call the original handler with the original user data
    return context->original_handler(
        server,
        uri,
        context->original_user_data,
        content,
        content_count,
        error_message
    );
}

/**
 * @brief Handles a resource request using template-based routing
 *
 * @param server The server instance
 * @param uri The URI to handle
 * @param content Pointer to receive the content items
 * @param content_count Pointer to receive the number of content items
 * @param error_message Pointer to receive an error message
 * @return An error code
 */
mcp_error_code_t mcp_server_handle_template_resource(
    mcp_server_t* server,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    if (server == NULL || uri == NULL || content == NULL || content_count == NULL) {
        if (error_message) {
            *error_message = mcp_strdup("Invalid parameters");
        }
        return MCP_ERROR_INVALID_PARAMS;
    }

    // Initialize output parameters
    *content = NULL;
    *content_count = 0;
    if (error_message) {
        *error_message = NULL;
    }

    // Find a matching template route
    mcp_json_t* params = NULL;
    template_route_t* route = mcp_server_find_template_route(server, uri, &params);
    if (route == NULL) {
        // No matching template found
        return MCP_ERROR_RESOURCE_NOT_FOUND;
    }

    // Create a context for the template handler
    template_handler_context_t context = {
        .params = params,
        .original_handler = route->handler,
        .original_user_data = route->user_data
    };

    // Call the template handler wrapper
    mcp_error_code_t result = template_handler_wrapper(
        server,
        uri,
        &context,
        content,
        content_count,
        error_message
    );

    // Clean up
    if (params) {
        mcp_json_destroy(params);
    }

    return result;
}
