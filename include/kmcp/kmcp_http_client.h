/**
 * @file kmcp_http_client.h
 * @brief HTTP client for communicating with HTTP MCP servers
 */

#ifndef KMCP_HTTP_CLIENT_H
#define KMCP_HTTP_CLIENT_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP client structure
 */
typedef struct kmcp_http_client kmcp_http_client_t;

/**
 * @brief SSL/TLS verification mode
 */
typedef enum kmcp_ssl_verify_mode {
    KMCP_SSL_VERIFY_NONE = 0,     /**< Do not verify server certificate */
    KMCP_SSL_VERIFY_PEER = 1,     /**< Verify server certificate */
    KMCP_SSL_VERIFY_FULL = 2      /**< Verify server certificate and hostname */
} kmcp_ssl_verify_mode_t;

/**
 * @brief HTTP client configuration
 */
typedef struct kmcp_http_client_config {
    const char* base_url;       /**< Base URL (must not be NULL) */
    const char* api_key;        /**< API key, can be NULL */
    int connect_timeout_ms;     /**< Connection timeout in milliseconds (0 for default) */
    int request_timeout_ms;     /**< Request timeout in milliseconds (0 for default) */
    int max_retries;            /**< Maximum number of retries (0 for no retries) */
    int retry_interval_ms;      /**< Interval between retries in milliseconds */

    // SSL/TLS options
    kmcp_ssl_verify_mode_t ssl_verify_mode; /**< SSL verification mode */
    const char* ssl_ca_file;    /**< Path to CA certificate file, can be NULL */
    const char* ssl_cert_file;  /**< Path to client certificate file, can be NULL */
    const char* ssl_key_file;   /**< Path to client private key file, can be NULL */
    const char* ssl_key_password; /**< Password for client private key, can be NULL */
    bool accept_self_signed;    /**< Whether to accept self-signed certificates */
} kmcp_http_client_config_t;

/**
 * @brief Create an HTTP client with default configuration
 *
 * Creates an HTTP client for communicating with an HTTP MCP server.
 *
 * @param base_url Base URL (must not be NULL)
 * @param api_key API key, can be NULL
 * @return kmcp_http_client_t* Returns HTTP client pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the client using kmcp_http_client_close()
 * @see kmcp_http_client_close()
 */
kmcp_http_client_t* kmcp_http_client_create(const char* base_url, const char* api_key);

/**
 * @brief Create an HTTP client with custom configuration
 *
 * Creates an HTTP client for communicating with an HTTP MCP server using custom configuration.
 *
 * @param config Client configuration (must not be NULL)
 * @return kmcp_http_client_t* Returns HTTP client pointer on success, NULL on failure
 *
 * @note The caller is responsible for freeing the client using kmcp_http_client_close()
 * @see kmcp_http_client_close()
 */
kmcp_http_client_t* kmcp_http_client_create_with_config(const kmcp_http_client_config_t* config);

/**
 * @brief Send an HTTP request
 *
 * Sends an HTTP request to the server and returns the response.
 *
 * @param client HTTP client (must not be NULL)
 * @param method HTTP method (GET, POST, PUT, DELETE, etc.) (must not be NULL)
 * @param path Path (relative to base_url) (must not be NULL)
 * @param content_type Content type, can be NULL
 * @param body Request body, can be NULL
 * @param response Pointer to response body, memory allocated by function, caller responsible for freeing
 * @param status Pointer to status code
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the response string using free()
 */
kmcp_error_t kmcp_http_client_send(
    kmcp_http_client_t* client,
    const char* method,
    const char* path,
    const char* content_type,
    const char* body,
    char** response,
    int* status
);

/**
 * @brief Send an HTTP request with timeout
 *
 * Sends an HTTP request to the server and returns the response, with custom timeout.
 *
 * @param client HTTP client (must not be NULL)
 * @param method HTTP method (GET, POST, PUT, DELETE, etc.) (must not be NULL)
 * @param path Path (relative to base_url) (must not be NULL)
 * @param content_type Content type, can be NULL
 * @param body Request body, can be NULL
 * @param response Pointer to response body, memory allocated by function, caller responsible for freeing
 * @param status Pointer to status code
 * @param timeout_ms Request timeout in milliseconds (0 to use client default)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any required parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - KMCP_ERROR_TIMEOUT if the request times out
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the response string using free()
 */
kmcp_error_t kmcp_http_client_send_with_timeout(
    kmcp_http_client_t* client,
    const char* method,
    const char* path,
    const char* content_type,
    const char* body,
    char** response,
    int* status,
    int timeout_ms
);

/**
 * @brief Call a tool
 *
 * Calls a tool on the HTTP server using the HTTP client.
 *
 * @param client HTTP client (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @param params_json Parameter JSON string (must not be NULL)
 * @param result_json Pointer to result JSON string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the result_json string using free()
 */
kmcp_error_t kmcp_http_client_call_tool(
    kmcp_http_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
);

/**
 * @brief Get a resource
 *
 * Retrieves a resource from the HTTP server using the HTTP client.
 *
 * @param client HTTP client (must not be NULL)
 * @param resource_uri Resource URI (must not be NULL)
 * @param content Pointer to content string, memory allocated by function, caller responsible for freeing
 * @param content_type Pointer to content type string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - KMCP_ERROR_RESOURCE_NOT_FOUND if the resource is not found
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the content and content_type strings using free()
 */
kmcp_error_t kmcp_http_client_get_resource(
    kmcp_http_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
);

/**
 * @brief Get supported tools
 *
 * Retrieves a list of tools supported by the HTTP server.
 *
 * @param client HTTP client (must not be NULL)
 * @param tools Pointer to array of tool names, memory allocated by function, caller responsible for freeing
 * @param count Pointer to number of tools
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the tools array and each tool name using free()
 */
kmcp_error_t kmcp_http_get_tools(
    kmcp_http_client_t* client,
    char*** tools,
    size_t* count
);

/**
 * @brief Get supported resources
 *
 * Retrieves a list of resources supported by the HTTP server.
 *
 * @param client HTTP client (must not be NULL)
 * @param resources Pointer to array of resource URIs, memory allocated by function, caller responsible for freeing
 * @param count Pointer to number of resources
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure:
 *         - KMCP_ERROR_INVALID_PARAMETER if any parameter is NULL
 *         - KMCP_ERROR_CONNECTION_FAILED if connection to server fails
 *         - Other error codes for specific failures
 *
 * @note The caller is responsible for freeing the resources array and each resource URI using free()
 */
kmcp_error_t kmcp_http_get_resources(
    kmcp_http_client_t* client,
    char*** resources,
    size_t* count
);

/**
 * @brief Close the HTTP client
 *
 * Closes the HTTP client and frees all associated resources.
 *
 * @param client HTTP client (can be NULL, in which case this function does nothing)
 *
 * @note After calling this function, the client pointer is no longer valid and should not be used.
 */
void kmcp_http_client_close(kmcp_http_client_t* client);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_HTTP_CLIENT_H */
