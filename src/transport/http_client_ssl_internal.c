#include "internal/http_client_ssl.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>

/* Include OpenSSL headers */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

/**
 * @brief SSL verification callback
 * 
 * @param preverify_ok Result of previous verification step
 * @param x509_ctx X509 store context
 * @return int 1 to accept the certificate, 0 to reject
 */
static int ssl_verify_callback(int preverify_ok, X509_STORE_CTX* x509_ctx) {
    (void)x509_ctx; /* Unused parameter */
    (void)preverify_ok; /* Unused parameter */
    /* For now, we accept all certificates to ensure connectivity */
    /* In production, you should implement proper certificate verification */
    return 1;
}

/**
 * @brief Initialize SSL library and create a new SSL context
 * 
 * @return http_client_ssl_ctx_t* Initialized SSL context or NULL on failure
 */
http_client_ssl_ctx_t* http_client_ssl_init(void) {
    http_client_ssl_ctx_t* ssl_ctx = (http_client_ssl_ctx_t*)calloc(1, sizeof(http_client_ssl_ctx_t));
    if (!ssl_ctx) {
        mcp_log_error("Failed to allocate memory for SSL context");
        return NULL;
    }

    /* Initialize OpenSSL library */
    if (!ssl_ctx->initialized) {
        if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                            OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
                            OPENSSL_INIT_ADD_ALL_CIPHERS |
                            OPENSSL_INIT_ADD_ALL_DIGESTS, NULL) == 0) {
            mcp_log_error("Failed to initialize OpenSSL");
            free(ssl_ctx);
            return NULL;
        }
        ssl_ctx->initialized = true;
    }

    /* Create a new SSL context using TLS client method */
    const SSL_METHOD* method = TLS_client_method();
    if (!method) {
        mcp_log_error("Failed to create SSL method");
        http_client_ssl_cleanup(ssl_ctx);
        return NULL;
    }

    ssl_ctx->ctx = SSL_CTX_new(method);
    if (!ssl_ctx->ctx) {
        mcp_log_error("Failed to create SSL context");
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        mcp_log_error("OpenSSL error: %s", err_buf);
        http_client_ssl_cleanup(ssl_ctx);
        return NULL;
    }

    /* Set default verify paths (CA certificates) */
    if (SSL_CTX_set_default_verify_paths(ssl_ctx->ctx) != 1) {
        mcp_log_warn("Failed to set default verify paths, certificate verification may fail");
    }

    /* Set SSL options - disable older protocols and enable workarounds for buggy SSL implementations */
    SSL_CTX_set_options(ssl_ctx->ctx,
                       SSL_OP_NO_SSLv2 |
                       SSL_OP_NO_SSLv3 |
                       SSL_OP_NO_COMPRESSION |
                       SSL_OP_ALL); /* SSL_OP_ALL includes various bug workarounds */

    /* Set verification mode with our callback - use SSL_VERIFY_NONE for testing */
    SSL_CTX_set_verify(ssl_ctx->ctx, SSL_VERIFY_NONE, ssl_verify_callback);

    /* Set verification depth */
    SSL_CTX_set_verify_depth(ssl_ctx->ctx, 4);

    /* Set session cache mode */
    SSL_CTX_set_session_cache_mode(ssl_ctx->ctx, SSL_SESS_CACHE_CLIENT);

    /* Set timeout for SSL sessions */
    SSL_CTX_set_timeout(ssl_ctx->ctx, 300); /* 5 minutes */

    mcp_log_info("SSL context initialized successfully");
    return ssl_ctx;
}

/**
 * @brief Create a new SSL connection and connect to the server
 * 
 * @param ssl_ctx SSL context
 * @param sock Socket to use for the connection
 * @param host Hostname for SNI
 * @return int 0 on success, -1 on failure
 */
int http_client_ssl_connect(http_client_ssl_ctx_t* ssl_ctx, socket_t sock, const char* host) {
    if (!ssl_ctx || !ssl_ctx->ctx || sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Invalid SSL context or socket");
        return -1;
    }

    /* Create a new SSL connection */
    ssl_ctx->ssl = SSL_new(ssl_ctx->ctx);
    if (!ssl_ctx->ssl) {
        mcp_log_error("Failed to create SSL connection");
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        mcp_log_error("OpenSSL error: %s", err_buf);
        return -1;
    }

    /* Set the socket for the SSL connection */
    if (SSL_set_fd(ssl_ctx->ssl, (int)sock) != 1) {
        mcp_log_error("Failed to set SSL file descriptor");
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        mcp_log_error("OpenSSL error: %s", err_buf);
        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = NULL;
        return -1;
    }

    /* Set SNI hostname */
    if (host) {
        mcp_log_info("Setting SNI hostname to: %s", host);
        if (!SSL_set_tlsext_host_name(ssl_ctx->ssl, host)) {
            mcp_log_warn("Failed to set SNI hostname, continuing anyway");
        }
    }

    /* Perform SSL handshake */
    int result = SSL_connect(ssl_ctx->ssl);
    if (result != 1) {
        int ssl_error = SSL_get_error(ssl_ctx->ssl, result);
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        mcp_log_error("SSL handshake failed: %s (error code: %d)", err_buf, ssl_error);
        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = NULL;
        return -1;
    }

    mcp_log_info("SSL connection established successfully");
    return 0;
}

/**
 * @brief Read data from an SSL connection
 * 
 * @param ssl_ctx SSL context
 * @param buffer Buffer to store the read data
 * @param size Size of the buffer
 * @return int Number of bytes read, 0 on connection closed, -1 on error
 */
int http_client_ssl_read(http_client_ssl_ctx_t* ssl_ctx, void* buffer, int size) {
    if (!ssl_ctx || !ssl_ctx->ssl || !buffer || size <= 0) {
        mcp_log_error("Invalid parameters for SSL read");
        return -1;
    }

    int bytes_read = SSL_read(ssl_ctx->ssl, buffer, size);
    if (bytes_read <= 0) {
        int ssl_error = SSL_get_error(ssl_ctx->ssl, bytes_read);
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            /* Connection closed cleanly */
            return 0;
        } else if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            /* Operation would block, try again later */
            return -1;
        } else {
            /* Error occurred */
            unsigned long err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            mcp_log_error("SSL read error: %s (error code: %d)", err_buf, ssl_error);
            return -1;
        }
    }

    return bytes_read;
}

/**
 * @brief Write data to an SSL connection
 * 
 * @param ssl_ctx SSL context
 * @param buffer Buffer containing the data to write
 * @param size Size of the data
 * @return int Number of bytes written, -1 on error
 */
int http_client_ssl_write(http_client_ssl_ctx_t* ssl_ctx, const void* buffer, int size) {
    if (!ssl_ctx || !ssl_ctx->ssl || !buffer || size <= 0) {
        mcp_log_error("Invalid parameters for SSL write");
        return -1;
    }

    int bytes_written = SSL_write(ssl_ctx->ssl, buffer, size);
    if (bytes_written <= 0) {
        int ssl_error = SSL_get_error(ssl_ctx->ssl, bytes_written);
        if (ssl_error == SSL_ERROR_WANT_WRITE || ssl_error == SSL_ERROR_WANT_READ) {
            /* Operation would block, try again later */
            return -1;
        } else {
            /* Error occurred */
            unsigned long err = ERR_get_error();
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            mcp_log_error("SSL write error: %s (error code: %d)", err_buf, ssl_error);
            return -1;
        }
    }

    return bytes_written;
}

/**
 * @brief Clean up SSL connection and context
 * 
 * @param ssl_ctx SSL context to clean up
 */
void http_client_ssl_cleanup(http_client_ssl_ctx_t* ssl_ctx) {
    if (!ssl_ctx) {
        return;
    }

    /* Free SSL connection */
    if (ssl_ctx->ssl) {
        SSL_shutdown(ssl_ctx->ssl);
        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = NULL;
    }

    /* Free SSL context */
    if (ssl_ctx->ctx) {
        SSL_CTX_free(ssl_ctx->ctx);
        ssl_ctx->ctx = NULL;
    }

    /* Free SSL context structure */
    free(ssl_ctx);
}
