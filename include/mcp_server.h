#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <mcp_types.h>
#include <mcp_transport.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration information for an MCP server.
 */
typedef struct {
    const char* name;        /**< Server name (e.g., "my-mcp-server"). */
    const char* version;     /**< Server version string (e.g., "1.0.0"). */
    const char* description; /**< Optional server description. */
    size_t thread_pool_size; /**< Number of worker threads for request handling. Default: 4 if 0. */
    size_t task_queue_size;  /**< Maximum number of pending requests in the queue. Default: 1024 if 0. */
    size_t cache_capacity;   /**< Maximum number of entries in the resource cache. Default: 128 if 0. */
    time_t cache_default_ttl_seconds; /**< Default TTL for cache entries in seconds. Default: 300 (5 min) if 0. */
    size_t max_message_size; /**< Maximum allowed size for incoming messages in bytes. Default: 1MB if 0. */
    // Rate Limiter Config (0 disables rate limiting)
    size_t rate_limit_capacity; /**< Approx max clients to track for rate limiting. Default: 1024 if 0. */
    size_t rate_limit_window_seconds; /**< Time window for rate limit checks. Default: 60 if 0. */
    size_t rate_limit_max_requests; /**< Max requests per client per window. Default: 100 if 0. */
    const char* api_key;     /**< Optional API key required for requests. If NULL or empty, no key is required. */
} mcp_server_config_t;

/**
 * @brief Declares the capabilities supported by the MCP server.
 */
typedef struct {
    bool resources_supported; /**< True if the server supports resource operations. */
    bool tools_supported;     /**< True if the server supports tool operations. */
} mcp_server_capabilities_t;

/**
 * @brief Opaque handle representing an MCP server instance.
 */
typedef struct mcp_server mcp_server_t;

/**
 * @brief Callback function type for handling resource read requests.
 *
 * The implementation of this function is responsible for finding the resource
 * identified by `uri` and returning its content. This function may be called
 * concurrently by multiple server worker threads. Implementations must be thread-safe
 * if they access shared mutable state.
 *
 * @param server Pointer to the server instance handling the request.
 * @param uri The URI of the requested resource.
 * @param user_data The user data pointer provided via mcp_server_set_resource_handler().
 * @param[out] content Pointer to a variable that should receive the allocated array
 *                     of mcp_content_item_t pointers. The handler must allocate this
 *                     array and its items using `malloc`. The server core will free
 *                     the items using `mcp_content_item_free()` and the array using `free()`.
 * @param[out] content_count Pointer to a variable that should receive the number of
 *                           items in the `content` array.
 * @return 0 on success (content found and returned), non-zero on error (e.g., resource
 *         not found, allocation failure). A non-zero return will result in an
 *         appropriate JSON-RPC error response being sent.
 */
typedef int (*mcp_server_resource_handler_t)(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t** content, // Note: Handler must malloc this array and items
    size_t* content_count
);

/**
 * @brief Callback function type for handling tool call requests.
 *
 * The implementation of this function is responsible for executing the tool
 * identified by `name` with the given `params` and returning the result. This function
 * may be called concurrently by multiple server worker threads. Implementations must be
 * thread-safe if they access shared mutable state.
 *
 * @param server Pointer to the server instance handling the request.
 * @param name The name of the tool being called.
 * @param params A JSON string containing the arguments for the tool call.
 * @param user_data The user data pointer provided via mcp_server_set_tool_handler().
 * @param[out] content Pointer to a variable that should receive the allocated array
 *                     of mcp_content_item_t pointers representing the tool's output.
 *                     The handler must allocate this array and its items using `malloc`.
 *                     The server core will free the items using `mcp_content_item_free()`
 *                     and the array using `free()`.
 * @param[out] content_count Pointer to a variable that should receive the number of
 *                           items in the `content` array.
 * @param[out] is_error Pointer to a boolean that the handler should set to true if the
 *                      tool execution itself resulted in an error (even if content is
 *                      returned, e.g., an error message), false otherwise.
 * @return 0 on success (tool executed, content returned), non-zero on error (e.g., tool
 *         not found, internal execution error). A non-zero return will result in an
 *         appropriate JSON-RPC error response being sent.
 */
typedef int (*mcp_server_tool_handler_t)(
    mcp_server_t* server,
    const char* name,
    const char* params, // Note: This is a JSON string
    void* user_data,
    mcp_content_item_t** content, // Note: Handler must malloc this array and items
    size_t* content_count,
    bool* is_error
);

/**
 * @brief Creates an MCP server instance.
 *
 * Allocates and initializes a server handle based on the provided configuration
 * and capabilities. The configuration strings (`name`, `version`, `description`, `api_key`)
 * are copied internally using `mcp_strdup` (malloc). Other configuration values are copied directly.
 * Creates internal resources like the thread pool, cache, and rate limiter.
 *
 * @param config Pointer to the server configuration. Must not be NULL.
 * @param capabilities Pointer to the server capabilities flags. Must not be NULL.
 * @return Pointer to the created server instance, or NULL on allocation failure.
 * @note The caller is responsible for destroying the returned instance using mcp_server_destroy().
 */
mcp_server_t* mcp_server_create(
    const mcp_server_config_t* config,
    const mcp_server_capabilities_t* capabilities
);

/**
 * @brief Starts the server and begins processing messages via the transport.
 *
 * Associates the server with the given transport and initiates the transport's
 * message processing loop (e.g., starts listening for connections or reading
 * from stdio). Sets the server's internal transport pointer.
 *
 * @param server Pointer to the initialized server instance. Must not be NULL.
 * @param transport Pointer to the initialized transport handle to use for communication. Must not be NULL.
 *                  The server does *not* take ownership of the transport; the caller
 *                  is responsible for managing the transport's lifecycle separately.
 * @return 0 on success, non-zero if the transport fails to start.
 * @note This function is not thread-safe with respect to other server operations.
 */
int mcp_server_start(
    mcp_server_t* server,
    mcp_transport_t* transport
);

/**
 * @brief Stops the server and the associated transport.
 *
 * Signals the server to stop processing, stops the associated transport,
 * and initiates the shutdown of the internal thread pool (waiting for tasks).
 *
 * @param server Pointer to the server instance. Must not be NULL.
 * @return 0 on success, non-zero if the transport fails to stop.
 * @note This function is not thread-safe with respect to other server operations.
 *       It should only be called once during shutdown.
 */
int mcp_server_stop(mcp_server_t* server);

/**
 * @brief Destroys the server instance and frees associated resources.
 *
 * Implicitly calls mcp_server_stop() if the server is running. Frees internally
 * copied configuration strings, destroys the thread pool, cache, and rate limiter,
 * and frees any added resources, templates, or tools.
 * Does *not* destroy the transport handle originally passed to mcp_server_start().
 *
 * @param server Pointer to the server instance to destroy. If NULL, the function does nothing.
 * @note This function is not thread-safe. Ensure no other threads are accessing the server
 *       instance during or after this call.
 */
void mcp_server_destroy(mcp_server_t* server);

/**
 * @brief Sets the handler function for processing resource read requests.
 *
 * @param server Pointer to the server instance.
 * @param handler The function pointer to the resource handler implementation.
 * @param user_data An arbitrary pointer passed back to the handler during calls.
 * @return 0 on success, non-zero if server is NULL.
 */
int mcp_server_set_resource_handler(
    mcp_server_t* server,
    mcp_server_resource_handler_t handler,
    void* user_data
);

/**
 * @brief Sets the handler function for processing tool call requests.
 *
 * @param server Pointer to the server instance.
 * @param handler The function pointer to the tool handler implementation.
 * @param user_data An arbitrary pointer passed back to the handler during calls.
 * @return 0 on success, non-zero if server is NULL.
 */
int mcp_server_set_tool_handler(
    mcp_server_t* server,
    mcp_server_tool_handler_t handler,
    void* user_data
);

/**
 * @brief Adds a static resource definition to the server.
 *
 * The server makes an internal copy of the resource definition. This is used
 * by the default handler for 'list_resources'.
 *
 * @param server Pointer to the server instance.
 * @param resource Pointer to the resource definition to add.
 * @return 0 on success, non-zero on error (e.g., NULL input, memory allocation failure,
 *         or if resources are not supported by the server capabilities).
 */
int mcp_server_add_resource(
    mcp_server_t* server,
    const mcp_resource_t* resource
);

/**
 * @brief Adds a resource template definition to the server.
 *
 * The server makes an internal copy of the template definition. This is used
 * by the default handler for 'list_resource_templates'.
 *
 * @param server Pointer to the server instance.
 * @param tmpl Pointer to the resource template definition to add.
 * @return 0 on success, non-zero on error (e.g., NULL input, memory allocation failure,
 *         or if resources are not supported by the server capabilities).
 */
int mcp_server_add_resource_template(
    mcp_server_t* server,
    const mcp_resource_template_t* tmpl
);

/**
 * @brief Adds a tool definition to the server.
 *
 * The server makes an internal copy of the tool definition, including its parameters.
 * This is used by the default handler for 'list_tools'.
 *
 * @param server Pointer to the server instance.
 * @param tool Pointer to the tool definition to add.
 * @return 0 on success, non-zero on error (e.g., NULL input, memory allocation failure,
 *         or if tools are not supported by the server capabilities).
 */
int mcp_server_add_tool(
    mcp_server_t* server,
    const mcp_tool_t* tool
);

/**
 * @brief Manually process a single message received outside the transport mechanism.
 *
 * This function is primarily for testing or scenarios where the transport layer
 * is managed externally. It parses and handles a single message string.
 * Any response generated is typically sent back via the transport associated
 * during mcp_server_start, not returned directly by this function.
 *
 * @param server Pointer to the server instance.
 * @param data Pointer to the raw message data (expected to be a null-terminated JSON string).
 * @param size The size of the message data (excluding null terminator).
 * @return 0 if the message was processed (even if it was an error response),
 *         non-zero if initial parameter validation failed.
 * @note This function is generally not needed when using mcp_server_start() with a transport.
 */
int mcp_server_process_message(
    mcp_server_t* server,
    const void* data,
    size_t size
);

#ifdef __cplusplus
}
#endif

#endif /* MCP_SERVER_H */
