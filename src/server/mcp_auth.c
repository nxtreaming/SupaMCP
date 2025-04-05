#include "mcp_auth.h"
#include "mcp_log.h"
#include "mcp_profiler.h"
#include "mcp_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

// Placeholder: Simple wildcard matching (matches '*' at the end)
// A real implementation might use fnmatch or a more robust pattern matching library.
static bool simple_wildcard_match(const char* pattern, const char* text) {
    if (!pattern || !text) return false; // Handle NULL inputs

    size_t pattern_len = strlen(pattern);
    size_t text_len = strlen(text);

    if (pattern_len == 0) {
        return text_len == 0; // Empty pattern matches only empty text
    }

    if (pattern[pattern_len - 1] == '*') {
        if (pattern_len == 1) return true; // Pattern "*" matches everything
        // Match prefix if pattern ends with '*' (and pattern is longer than just "*")
        return pattern_len - 1 <= text_len &&
               strncmp(pattern, text, pattern_len - 1) == 0;
    } else {
        // Exact match required
        return pattern_len == text_len && strcmp(pattern, text) == 0;
    }
}


/**
 * @brief Verifies client credentials. (Placeholder - Basic Functionality Only)
 * @note This implementation provides basic functionality for MCP_AUTH_NONE and a single
 *       hardcoded API key ("TEST_API_KEY_123") for testing purposes.
 *       A production implementation should replace this with secure credential storage
 *       and potentially more granular permission management.
 */
// Include internal header for server config definition
#include "internal/server_internal.h"

int mcp_auth_verify(mcp_server_t* server, mcp_auth_type_t auth_type, const char* credentials, mcp_auth_context_t** context_out) {
    if (!context_out || !server) return -1; // Added server check
    *context_out = NULL; // Ensure output is NULL on failure

    log_message(LOG_LEVEL_DEBUG, "mcp_auth_verify called. Type: %d", auth_type);

    // --- No Authentication ---
    if (auth_type == MCP_AUTH_NONE && server->config.api_key == NULL) {
        // Only allow AUTH_NONE if no API key is configured on the server
        mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
        if (!context) return -1;
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
        log_message(LOG_LEVEL_DEBUG, "Authenticated as 'anonymous' (MCP_AUTH_NONE allowed).");
        return 0; // Success for AUTH_NONE when no server key is set
    }

    // --- API Key Authentication ---
    if (auth_type == MCP_AUTH_API_KEY) {
        // Check if server has an API key configured
        if (server->config.api_key == NULL || strlen(server->config.api_key) == 0) {
            log_message(LOG_LEVEL_WARN, "API Key authentication requested, but no API key configured on server.");
            return -1; // Fail if key requested but none set on server
        }
        // Check if provided credentials match the configured key
        if (credentials && strcmp(credentials, server->config.api_key) == 0) {
            // API Key matches, create context with full permissions for now
            mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
            if (!context) { log_message(LOG_LEVEL_ERROR, "Failed to allocate auth context."); return -1; }
            context->type = MCP_AUTH_API_KEY;
            context->identifier = mcp_strdup("authenticated_client"); // Generic identifier
            context->expiry = 0; // Non-expiring

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
                 mcp_auth_context_free(context); // Cleanup partial allocation
                 return -1;
            }

            *context_out = context;
            log_message(LOG_LEVEL_DEBUG, "Successfully authenticated client '%s' via configured API Key.", context->identifier);
            return 0; // Success
        } else {
            log_message(LOG_LEVEL_WARN, "API Key authentication failed: Provided key does not match configured key.");
            return -1; // Key mismatch
        }
    }

    // --- Other Auth Types (Not Implemented) ---
    // Fail other authentication types or if conditions not met
    log_message(LOG_LEVEL_WARN, "Authentication failed: Type %d not supported, credentials invalid, or server config mismatch.", auth_type);
    return -1;
}

/**
 * @brief Checks resource access permission using simple wildcard matching.
 */
bool mcp_auth_check_resource_access(const mcp_auth_context_t* context, const char* resource_uri) {
    if (!context || !resource_uri) {
        return false; // Cannot check access without context or URI
    }

    // Check expiry if applicable
    if (context->expiry != 0 && time(NULL) > context->expiry) {
        log_message(LOG_LEVEL_WARN, "Auth context for '%s' expired.", context->identifier ? context->identifier : "unknown");
        return false; // Context expired
    }

    // Check against allowed patterns using simple wildcard matching
    for (size_t i = 0; i < context->allowed_resources_count; ++i) {
        if (context->allowed_resources[i] && simple_wildcard_match(context->allowed_resources[i], resource_uri)) {
            log_message(LOG_LEVEL_DEBUG, "Access granted for '%s' to resource '%s' (match: %s)",
                    context->identifier ? context->identifier : "unknown", resource_uri, context->allowed_resources[i]);
            return true; // Found a matching allowed pattern
        }
    }

    log_message(LOG_LEVEL_INFO, "Access denied for '%s' to resource '%s'. No matching rule found.",
            context->identifier ? context->identifier : "unknown", resource_uri);
    return false; // No matching pattern found
}

/**
 * @brief Checks tool access permission using simple wildcard matching.
 */
bool mcp_auth_check_tool_access(const mcp_auth_context_t* context, const char* tool_name) {
     if (!context || !tool_name) {
        return false; // Cannot check access without context or tool name
    }

    // Check expiry if applicable
    if (context->expiry != 0 && time(NULL) > context->expiry) {
         log_message(LOG_LEVEL_WARN, "Auth context for '%s' expired.", context->identifier ? context->identifier : "unknown");
        return false; // Context expired
    }

    // Check against allowed patterns using simple wildcard matching
    for (size_t i = 0; i < context->allowed_tools_count; ++i) {
        if (context->allowed_tools[i] && simple_wildcard_match(context->allowed_tools[i], tool_name)) {
             log_message(LOG_LEVEL_DEBUG, "Access granted for '%s' to tool '%s' (match: %s)",
                    context->identifier ? context->identifier : "unknown", tool_name, context->allowed_tools[i]);
            return true; // Found a matching allowed pattern
        }
    }

     log_message(LOG_LEVEL_INFO, "Access denied for '%s' to tool '%s'. No matching rule found.",
            context->identifier ? context->identifier : "unknown", tool_name);
    return false; // No matching pattern found
}

/**
 * @brief Frees the authentication context.
 */
void mcp_auth_context_free(mcp_auth_context_t* context) {
    if (!context) {
        return;
    }

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
