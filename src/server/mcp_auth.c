#include "mcp_auth.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "internal/server_internal.h"

/**
 * @brief Verifies client credentials. (Placeholder - Basic Functionality Only)
 * @note This implementation provides basic functionality for MCP_AUTH_NONE and a single
 *       hardcoded API key ("TEST_API_KEY_123") for testing purposes.
 *       A production implementation should replace this with secure credential storage
 *       and potentially more granular permission management.
 */
int mcp_auth_verify(mcp_server_t* server, mcp_auth_type_t auth_type, const char* credentials, mcp_auth_context_t** context_out) {
    if (!context_out || !server)
        return -1;
    *context_out = NULL; // Ensure output is NULL on failure

    mcp_log_debug("mcp_auth_verify called. Type: %d", auth_type);

    // --- No Authentication ---
    if (auth_type == MCP_AUTH_NONE && server->config.api_key == NULL) {
        // Only allow AUTH_NONE if no API key is configured on the server
        mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
        if (!context)
            return -1;
        context->type = MCP_AUTH_NONE;
        context->identifier = mcp_strdup("anonymous");
        context->allowed_resources = (char**)malloc(sizeof(char*));
        if (!context->allowed_resources) {
            mcp_auth_context_free(context); // Cleanup on allocation failure
            return -1;
        }
        context->allowed_resources_count = 1;
        context->allowed_resources[0] = mcp_strdup("*"); // Allow all resources

        context->allowed_tools = (char**)malloc(sizeof(char*));
        if (!context->allowed_tools) {
            mcp_auth_context_free(context); // Cleanup on allocation failure
            return -1;
        }
        context->allowed_tools_count = 1;
        context->allowed_tools[0] = mcp_strdup("*"); // Allow all tools

        if (!context->identifier || !context->allowed_resources || !context->allowed_resources[0] ||
            !context->allowed_tools || !context->allowed_tools[0]) {
            mcp_auth_context_free(context); // Cleanup on allocation failure
            return -1;
        }
        *context_out = context;
        mcp_log_debug("Authenticated as 'anonymous' (MCP_AUTH_NONE allowed).");
        return 0;
    }

    // --- API Key Authentication ---
    if (auth_type == MCP_AUTH_API_KEY) {
        // Check if server has an API key configured
        if (server->config.api_key == NULL || strlen(server->config.api_key) == 0) {
            mcp_log_warn("API Key authentication requested, but no API key configured on server.");
            // Fail if key requested but none set on server
            return -1;
        }
        // Check if provided credentials match the configured key
        if (credentials && strcmp(credentials, server->config.api_key) == 0) {
            // API Key matches, create context with full permissions for now
            mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
            if (!context) { mcp_log_error("Failed to allocate auth context."); return -1; }
            context->type = MCP_AUTH_API_KEY;
            // Generic identifier
            context->identifier = mcp_strdup("authenticated_client");
            // Non-expiring
            context->expiry = 0;

            // Grant full permissions ('*')
            context->allowed_resources_count = 1;
            context->allowed_resources = (char**)malloc(context->allowed_resources_count * sizeof(char*));
            if (!context->allowed_resources) { mcp_auth_context_free(context); return -1; }
            context->allowed_resources[0] = mcp_strdup("*");

            context->allowed_tools_count = 1;
            context->allowed_tools = (char**)malloc(context->allowed_tools_count * sizeof(char*));
            if (!context->allowed_tools) { mcp_auth_context_free(context); return -1; }
            context->allowed_tools[0] = mcp_strdup("*");

            // Check allocations
            if (!context->identifier || !context->allowed_resources[0] || !context->allowed_tools[0]) {
                // Cleanup partial allocation
                mcp_auth_context_free(context);
                return -1;
            }

            *context_out = context;
            mcp_log_debug("Successfully authenticated client '%s' via configured API Key.", context->identifier);
            return 0; // Success
        } else {
            mcp_log_warn("API Key authentication failed: Provided key does not match configured key.");
            return -1; // Key mismatch
        }
    }

    // --- Other Auth Types (Not Implemented) ---
    // Fail other authentication types or if conditions not met
    mcp_log_warn("Authentication failed: Type %d not supported, credentials invalid, or server config mismatch.", auth_type);
    return -1;
}

/**
 * @brief Checks resource access permission using simple wildcard matching.
 */
bool mcp_auth_check_resource_access(const mcp_auth_context_t* context, const char* resource_uri) {
    if (!context || !resource_uri)
        return false;

    // Check expiry if applicable
    if (context->expiry != 0 && time(NULL) > context->expiry) {
        mcp_log_warn("Auth context for '%s' expired.", context->identifier ? context->identifier : "unknown");
        // Context expired
        return false;
    }

    // Check against allowed patterns using the utility function
    for (size_t i = 0; i < context->allowed_resources_count; ++i) {
        if (context->allowed_resources[i] && mcp_wildcard_match(context->allowed_resources[i], resource_uri)) {
            mcp_log_debug("Access granted for '%s' to resource '%s' (match: %s)",
                    context->identifier ? context->identifier : "unknown", resource_uri, context->allowed_resources[i]);
            // Found a matching allowed pattern
            return true;
        }
    }

    mcp_log_info("Access denied for '%s' to resource '%s'. No matching rule found.",
            context->identifier ? context->identifier : "unknown", resource_uri);
    // No matching pattern found
    return false;
}

/**
 * @brief Checks tool access permission using simple wildcard matching.
 */
bool mcp_auth_check_tool_access(const mcp_auth_context_t* context, const char* tool_name) {
    if (!context || !tool_name)
        return false;

    // Check expiry if applicable
    if (context->expiry != 0 && time(NULL) > context->expiry) {
        mcp_log_warn("Auth context for '%s' expired.", context->identifier ? context->identifier : "unknown");
        // Context expired
        return false;
    }

    // Check against allowed patterns using the utility function
    for (size_t i = 0; i < context->allowed_tools_count; ++i) {
        if (context->allowed_tools[i] && mcp_wildcard_match(context->allowed_tools[i], tool_name)) {
            mcp_log_debug("Access granted for '%s' to tool '%s' (match: %s)",
                    context->identifier ? context->identifier : "unknown", tool_name, context->allowed_tools[i]);
            // Found a matching allowed pattern
            return true;
        }
    }

    mcp_log_info("Access denied for '%s' to tool '%s'. No matching rule found.",
            context->identifier ? context->identifier : "unknown", tool_name);
    // No matching pattern found
    return false;
}

/**
 * @brief Frees the authentication context.
 */
void mcp_auth_context_free(mcp_auth_context_t* context) {
    if (!context)
        return;

    // Free identifier string
    free(context->identifier);

    // Free allowed resources array and its strings
    if (context->allowed_resources) {
        for (size_t i = 0; i < context->allowed_resources_count; ++i) {
            free(context->allowed_resources[i]); // Free each string
        }
        free(context->allowed_resources); // Free the array of pointers
    }

    // Free allowed tools array and its strings
    if (context->allowed_tools) {
        for (size_t i = 0; i < context->allowed_tools_count; ++i) {
            free(context->allowed_tools[i]); // Free each string
        }
        free(context->allowed_tools); // Free the array of pointers
    }

    // Free the context structure itself
    free(context);
}
