/**
 * @file mcp_auth.c
 * @brief Implementation of authentication and authorization functionality for MCP server
 */

#include "mcp_auth.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "internal/server_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Creates an auth context with wildcard permissions
 *
 * @param auth_type The authentication type
 * @param identifier The client identifier
 * @param expiry The expiry time (0 for no expiry)
 * @return mcp_auth_context_t* The created auth context, or NULL on failure
 */
static mcp_auth_context_t* create_wildcard_auth_context(mcp_auth_type_t auth_type, const char* identifier, time_t expiry) {
    if (!identifier) {
        mcp_log_error("Invalid identifier for auth context");
        return NULL;
    }

    // Allocate and initialize the context
    mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
    if (!context) {
        mcp_log_error("Failed to allocate auth context");
        return NULL;
    }

    // Set basic properties
    context->type = auth_type;
    context->identifier = mcp_strdup(identifier);
    context->expiry = expiry;

    if (!context->identifier) {
        mcp_log_error("Failed to duplicate identifier");
        mcp_auth_context_free(context);
        return NULL;
    }

    // Allocate and set wildcard resource permissions
    context->allowed_resources_count = 1;
    context->allowed_resources = (char**)malloc(sizeof(char*));
    if (!context->allowed_resources) {
        mcp_log_error("Failed to allocate resources array");
        mcp_auth_context_free(context);
        return NULL;
    }
    context->allowed_resources[0] = mcp_strdup("*"); // Allow all resources

    // Allocate and set wildcard tool permissions
    context->allowed_tools_count = 1;
    context->allowed_tools = (char**)malloc(sizeof(char*));
    if (!context->allowed_tools) {
        mcp_log_error("Failed to allocate tools array");
        mcp_auth_context_free(context);
        return NULL;
    }
    context->allowed_tools[0] = mcp_strdup("*"); // Allow all tools

    // Verify all allocations succeeded
    if (!context->allowed_resources[0] || !context->allowed_tools[0]) {
        mcp_log_error("Failed to allocate permission strings");
        mcp_auth_context_free(context);
        return NULL;
    }

    return context;
}

/**
 * @brief Checks if an auth context has expired
 *
 * @param context The auth context to check
 * @return true if expired, false if not expired or no expiry
 */
static bool is_auth_context_expired(const mcp_auth_context_t* context) {
    if (!context) {
        return true;
    }

    // Check expiry if applicable (0 means no expiry)
    if (context->expiry != 0 && time(NULL) > context->expiry) {
        mcp_log_warn("Auth context for '%s' expired.",
                    context->identifier ? context->identifier : "unknown");
        return true;
    }

    return false;
}

/**
 * @brief Checks access permission using simple wildcard matching
 *
 * @param context The auth context
 * @param item_name The name of the item to check access for
 * @param allowed_items Array of allowed item patterns
 * @param allowed_items_count Count of allowed item patterns
 * @param item_type String describing the type of item (for logging)
 * @return true if access is allowed, false otherwise
 */
static bool check_access(const mcp_auth_context_t* context, const char* item_name,
                         char** allowed_items, size_t allowed_items_count, const char* item_type) {
    if (!context || !item_name || !allowed_items || !item_type) {
        return false;
    }

    // Check if context has expired
    if (is_auth_context_expired(context)) {
        return false;
    }

    // Check against allowed patterns using the utility function
    for (size_t i = 0; i < allowed_items_count; ++i) {
        if (allowed_items[i] && mcp_wildcard_match(allowed_items[i], item_name)) {
            mcp_log_debug("Access granted for '%s' to %s '%s' (match: %s)",
                    context->identifier ? context->identifier : "unknown",
                    item_type, item_name, allowed_items[i]);
            // Found a matching allowed pattern
            return true;
        }
    }

    mcp_log_info("Access denied for '%s' to %s '%s'. No matching rule found.",
            context->identifier ? context->identifier : "unknown", item_type, item_name);
    return false;
}

/**
 * @brief Verifies client credentials. (Placeholder - Basic Functionality Only)
 * @note This implementation provides basic functionality for MCP_AUTH_NONE and a single
 *       hardcoded API key ("TEST_API_KEY_123") for testing purposes.
 *       A production implementation should replace this with secure credential storage
 *       and potentially more granular permission management.
 */
int mcp_auth_verify(mcp_server_t* server, mcp_auth_type_t auth_type, const char* credentials, mcp_auth_context_t** context_out) {
    if (!context_out || !server) {
        mcp_log_error("Invalid parameters to mcp_auth_verify");
        return -1;
    }

    *context_out = NULL;
    PROFILE_START("mcp_auth_verify");

    mcp_log_debug("mcp_auth_verify called. Type: %d", auth_type);

    // --- No Authentication ---
    if (auth_type == MCP_AUTH_NONE && server->config.api_key == NULL) {
        // Only allow AUTH_NONE if no API key is configured on the server
        mcp_auth_context_t* context = create_wildcard_auth_context(MCP_AUTH_NONE, "anonymous", 0);
        if (!context) {
            mcp_log_error("Failed to create auth context for anonymous access");
            PROFILE_END("mcp_auth_verify");
            return -1;
        }

        *context_out = context;
        mcp_log_debug("Authenticated as 'anonymous' (MCP_AUTH_NONE allowed).");
        PROFILE_END("mcp_auth_verify");
        return 0;
    }

    // --- API Key Authentication ---
    if (auth_type == MCP_AUTH_API_KEY) {
        // Check if server has an API key configured
        if (server->config.api_key == NULL || strlen(server->config.api_key) == 0) {
            mcp_log_warn("API Key authentication requested, but no API key configured on server.");
            // Fail if key requested but none set on server
            PROFILE_END("mcp_auth_verify");
            return -1;
        }

        // Check if provided credentials match the configured key
        if (credentials && strcmp(credentials, server->config.api_key) == 0) {
            // API Key matches, create context with full permissions
            mcp_auth_context_t* context = create_wildcard_auth_context(MCP_AUTH_API_KEY, "authenticated_client", 0);
            if (!context) {
                mcp_log_error("Failed to create auth context for API key authentication");
                PROFILE_END("mcp_auth_verify");
                return -1;
            }

            *context_out = context;
            mcp_log_debug("Successfully authenticated client '%s' via configured API Key.", context->identifier);
            PROFILE_END("mcp_auth_verify");
            return 0;
        } else {
            mcp_log_warn("API Key authentication failed: Provided key does not match configured key.");
            PROFILE_END("mcp_auth_verify");
            return -1;
        }
    }

    // --- Other Auth Types (Not Implemented) ---
    // Fail other authentication types or if conditions not met
    mcp_log_warn("Authentication failed: Type %d not supported, credentials invalid, or server config mismatch.", auth_type);
    PROFILE_END("mcp_auth_verify");
    return -1;
}

/**
 * @brief Checks resource access permission using simple wildcard matching.
 *
 * @param context The auth context
 * @param resource_uri The resource URI to check access for
 * @return true if access is allowed, false otherwise
 */
bool mcp_auth_check_resource_access(const mcp_auth_context_t* context, const char* resource_uri) {
    PROFILE_START("mcp_auth_check_resource_access");
    bool result = check_access(context, resource_uri,
                              context ? context->allowed_resources : NULL,
                              context ? context->allowed_resources_count : 0,
                              "resource");
    PROFILE_END("mcp_auth_check_resource_access");
    return result;
}

/**
 * @brief Checks tool access permission using simple wildcard matching.
 *
 * @param context The auth context
 * @param tool_name The tool name to check access for
 * @return true if access is allowed, false otherwise
 */
bool mcp_auth_check_tool_access(const mcp_auth_context_t* context, const char* tool_name) {
    PROFILE_START("mcp_auth_check_tool_access");
    bool result = check_access(context, tool_name,
                              context ? context->allowed_tools : NULL,
                              context ? context->allowed_tools_count : 0,
                              "tool");
    PROFILE_END("mcp_auth_check_tool_access");
    return result;
}

/**
 * @brief Frees the authentication context.
 *
 * @param context The auth context to free
 */
void mcp_auth_context_free(mcp_auth_context_t* context) {
    if (!context)
        return;

    PROFILE_START("mcp_auth_context_free");

    // Free identifier string
    if (context->identifier) {
        free(context->identifier);
        context->identifier = NULL;
    }

    // Free allowed resources array and its strings
    if (context->allowed_resources) {
        for (size_t i = 0; i < context->allowed_resources_count; ++i) {
            if (context->allowed_resources[i]) {
                free(context->allowed_resources[i]);
                context->allowed_resources[i] = NULL;
            }
        }
        free(context->allowed_resources);
        context->allowed_resources = NULL;
    }
    context->allowed_resources_count = 0;

    // Free allowed tools array and its strings
    if (context->allowed_tools) {
        for (size_t i = 0; i < context->allowed_tools_count; ++i) {
            if (context->allowed_tools[i]) {
                free(context->allowed_tools[i]);
                context->allowed_tools[i] = NULL;
            }
        }
        free(context->allowed_tools);
        context->allowed_tools = NULL;
    }
    context->allowed_tools_count = 0;

    // Free the context structure itself
    free(context);

    PROFILE_END("mcp_auth_context_free");
}
