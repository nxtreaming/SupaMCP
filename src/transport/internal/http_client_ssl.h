#ifndef HTTP_CLIENT_SSL_H
#define HTTP_CLIENT_SSL_H

#include "mcp_socket_utils.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SSL context structure for HTTP client
 */
typedef struct http_client_ssl_ctx {
    void* ssl;       /* SSL connection object (SSL*) */
    void* ctx;       /* SSL context object (SSL_CTX*) */
    bool initialized; /* Whether OpenSSL has been initialized */
} http_client_ssl_ctx_t;

/**
 * @brief Initialize SSL library and create a new SSL context
 * 
 * @return http_client_ssl_ctx_t* Initialized SSL context or NULL on failure
 */
http_client_ssl_ctx_t* http_client_ssl_init(void);

/**
 * @brief Create a new SSL connection and connect to the server
 * 
 * @param ssl_ctx SSL context
 * @param sock Socket to use for the connection
 * @param host Hostname for SNI
 * @return int 0 on success, -1 on failure
 */
int http_client_ssl_connect(http_client_ssl_ctx_t* ssl_ctx, socket_t sock, const char* host);

/**
 * @brief Read data from an SSL connection
 * 
 * @param ssl_ctx SSL context
 * @param buffer Buffer to store the read data
 * @param size Size of the buffer
 * @return int Number of bytes read, 0 on connection closed, -1 on error
 */
int http_client_ssl_read(http_client_ssl_ctx_t* ssl_ctx, void* buffer, int size);

/**
 * @brief Write data to an SSL connection
 * 
 * @param ssl_ctx SSL context
 * @param buffer Buffer containing the data to write
 * @param size Size of the data
 * @return int Number of bytes written, -1 on error
 */
int http_client_ssl_write(http_client_ssl_ctx_t* ssl_ctx, const void* buffer, int size);

/**
 * @brief Clean up SSL connection and context
 * 
 * @param ssl_ctx SSL context to clean up
 */
void http_client_ssl_cleanup(http_client_ssl_ctx_t* ssl_ctx);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_SSL_H */
