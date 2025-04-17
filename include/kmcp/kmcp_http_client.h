/**
 * @file kmcp_http_client.h
 * @brief HTTP client for communicating with HTTP MCP servers
 */

#ifndef KMCP_HTTP_CLIENT_H
#define KMCP_HTTP_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP client structure
 */
typedef struct kmcp_http_client kmcp_http_client_t;

/**
 * @brief Create an HTTP client
 *
 * @param base_url Base URL
 * @param api_key API key, can be NULL
 * @return kmcp_http_client_t* Returns HTTP client pointer on success, NULL on failure
 */
kmcp_http_client_t* kmcp_http_client_create(const char* base_url, const char* api_key);

/**
 * @brief Send an HTTP request
 *
 * @param client HTTP client
 * @param method HTTP method (GET, POST, PUT, DELETE, etc.)
 * @param path Path (relative to base_url)
 * @param content_type Content type, can be NULL
 * @param body Request body, can be NULL
 * @param response Pointer to response body, memory allocated by function, caller responsible for freeing
 * @param status Pointer to status code
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_http_client_send(
    kmcp_http_client_t* client,
    const char* method,
    const char* path,
    const char* content_type,
    const char* body,
    char** response,
    int* status
);

/**
 * @brief Close the HTTP client
 *
 * @param client HTTP client
 */
void kmcp_http_client_close(kmcp_http_client_t* client);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_HTTP_CLIENT_H */
