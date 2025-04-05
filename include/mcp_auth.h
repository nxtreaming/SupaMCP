#ifndef MCP_AUTH_H
#define MCP_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h> // For time_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Defines the types of authentication supported.
 */
typedef enum {
    MCP_AUTH_NONE,     /**< No authentication required. */
    MCP_AUTH_API_KEY,  /**< Authentication via a simple API key. */
    MCP_AUTH_TOKEN,    /**< Authentication via a bearer token (e.g., JWT). */
    MCP_AUTH_CERT      /**< Authentication via client certificates (e.g., mTLS). */
} mcp_auth_type_t;

/**
 * @brief Represents the authentication context for a connected client.
 * Contains identity information and permissions after successful authentication.
 */
typedef struct {
    mcp_auth_type_t type;             /**< The type of authentication used. */
    char* identifier;                 /**< Unique identifier for the authenticated entity (e.g., username, client ID). Malloc'd string. */
    time_t expiry;                    /**< Expiration time for the authentication context (0 for non-expiring). */
    char** allowed_resources;         /**< Array of allowed resource URI patterns (e.g., "weather://*", "files:///readonly/*"). Malloc'd array of malloc'd strings. */
    size_t allowed_resources_count;   /**< Number of patterns in allowed_resources. */
    char** allowed_tools;             /**< Array of allowed tool names (e.g., "get_forecast", "admin_*"). Malloc'd array of malloc'd strings. */
    size_t allowed_tools_count;       /**< Number of patterns in allowed_tools. */
    // Add other relevant context fields as needed (e.g., roles, groups)
} mcp_auth_context_t;

/**
 * @brief Verifies client credentials based on the specified authentication type.
 *
 * This function is responsible for validating the provided credentials against a
 * configured store (e.g., database, configuration file, external service).
 * On successful authentication, it allocates and populates an mcp_auth_context_t structure.
 *
 * @param server The server instance (needed to access configuration like API key).
 * @param auth_type The expected authentication type.
 * @param credentials The credentials string provided by the client (e.g., API key, token).
 * @param[out] context Output parameter. On success, points to a newly allocated mcp_auth_context_t.
 *                     The caller is responsible for freeing this context using mcp_auth_context_free().
 *                     On failure, *context is set to NULL.
 * @return 0 for authentication success, -1 for authentication failure (invalid credentials, error, etc.).
 */
// Forward declare server struct
struct mcp_server;

int mcp_auth_verify(struct mcp_server* server, mcp_auth_type_t auth_type, const char* credentials, mcp_auth_context_t** context);

/**
 * @brief Checks if the authenticated client has permission to access a specific resource.
 *
 * This function compares the requested resource URI against the allowed resource patterns
 * stored in the authentication context. It should support wildcard matching (e.g., '*').
 *
 * @param context The authentication context obtained from mcp_auth_verify().
 * @param resource_uri The URI of the resource being requested.
 * @return true if access is permitted, false otherwise (including if context is NULL).
 */
bool mcp_auth_check_resource_access(const mcp_auth_context_t* context, const char* resource_uri);

/**
 * @brief Checks if the authenticated client has permission to call a specific tool.
 *
 * This function compares the requested tool name against the allowed tool patterns
 * stored in the authentication context. It should support wildcard matching (e.g., '*').
 *
 * @param context The authentication context obtained from mcp_auth_verify().
 * @param tool_name The name of the tool being called.
 * @return true if access is permitted, false otherwise (including if context is NULL).
 */
bool mcp_auth_check_tool_access(const mcp_auth_context_t* context, const char* tool_name);

/**
 * @brief Frees the memory allocated for an authentication context structure.
 *
 * This includes freeing the identifier string, the arrays of allowed resources/tools,
 * and each string within those arrays, before finally freeing the context structure itself.
 *
 * @param context The authentication context to free. If NULL, the function does nothing.
 */
void mcp_auth_context_free(mcp_auth_context_t* context);

#ifdef __cplusplus
}
#endif

#endif // MCP_AUTH_H
