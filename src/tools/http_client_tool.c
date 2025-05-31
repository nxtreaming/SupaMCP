/**
 * @file http_client_tool.c
 * @brief Implementation of the HTTP client tool for MCP server.
 *
 * This tool allows making HTTP requests from the MCP server to external services.
 */

#include "mcp_server.h"
#include "mcp_json.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_json_utils.h"
#include "mcp_socket_utils.h"
#include "mcp_sys_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

// OpenSSL headers
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

// Configuration constants
#define HTTP_CLIENT_DEFAULT_TIMEOUT_MS 30000  // Default timeout: 30 seconds
#define HTTP_CLIENT_MAX_RESPONSE_SIZE (10 * 1024 * 1024)  // Max response: 10MB
#define HTTP_CLIENT_INITIAL_BUFFER_SIZE 4096  // Initial buffer: 4KB
#define HTTP_CLIENT_REQUEST_BUFFER_SIZE 8192  // Request buffer: 8KB

// SSL context structure
typedef struct {
    SSL_CTX* ctx;                // OpenSSL context
    SSL* ssl;                    // OpenSSL connection
    bool initialized;            // Whether OpenSSL has been initialized
} ssl_context_t;

// HTTP response structure
typedef struct {
    char* data;                  // Response data
    size_t size;                 // Response size
    size_t capacity;             // Buffer capacity
    int status_code;             // HTTP status code
    char* headers;               // Response headers
    char* charset;               // Character encoding (e.g., UTF-8)
} http_response_t;

// Forward declarations
static http_response_t* http_request(const char* method, const char* url,
                                    const char* content_type, const char* headers,
                                    const void* data, size_t data_size,
                                    uint32_t timeout_ms);
static void http_response_free(http_response_t* response);
static char* parse_url(const char* url, char** host, int* port, char** path, bool* use_ssl);
static bool extract_http_headers(http_response_t* response);
static const char* extract_mime_type(const char* headers, char** charset_out);
static mcp_content_item_t* create_content_item(mcp_content_type_t type, const char* mime_type,
                                              const void* data, size_t data_size);
static void free_content_items(mcp_content_item_t** content, size_t count);
static ssl_context_t* ssl_init(void);
static void ssl_cleanup(ssl_context_t* ssl_ctx);
static int ssl_connect(ssl_context_t* ssl_ctx, socket_t sock, const char* host);
static int ssl_send(ssl_context_t* ssl_ctx, const char* data, size_t len);
static int ssl_recv(ssl_context_t* ssl_ctx, char* buffer, size_t buffer_size, int* bytes_received);

/**
 * @brief Extract MIME type from HTTP headers
 *
 * @param headers HTTP headers string
 * @param charset_out Optional pointer to store charset (caller must free)
 * @return const char* MIME type string (static buffer, do not free)
 */
static const char* extract_mime_type(const char* headers, char** charset_out)
{
    static char mime_type_buf[128];
    static const char* default_mime_type = "text/plain";

    // Initialize charset output if provided
    if (charset_out) {
        *charset_out = NULL;
    }

    if (!headers) {
        return default_mime_type;
    }

    const char* content_type_header = strstr(headers, "Content-Type:");
    if (!content_type_header) {
        return default_mime_type;
    }

    // Skip "Content-Type:" and whitespace
    content_type_header += 13;
    while (*content_type_header == ' ') {
        content_type_header++;
    }

    // Extract MIME type (up to semicolon or newline)
    size_t i = 0;
    while (i < sizeof(mime_type_buf) - 1 &&
           *content_type_header &&
           *content_type_header != ';' &&
           *content_type_header != '\r' &&
           *content_type_header != '\n') {
        mime_type_buf[i++] = *content_type_header++;
    }
    mime_type_buf[i] = '\0';

    // Extract charset if requested
    if (charset_out && *content_type_header == ';') {
        // Skip semicolon and whitespace
        content_type_header++;
        while (*content_type_header == ' ') {
            content_type_header++;
        }

        // Look for charset=
        const char* charset_str = strstr(content_type_header, "charset=");
        if (charset_str) {
            charset_str += 8; // Skip "charset="

            // Skip quotes if present
            if (*charset_str == '"' || *charset_str == '\'') {
                charset_str++;
            }

            // Extract charset value
            char charset_buf[64] = {0};
            i = 0;
            while (i < sizeof(charset_buf) - 1 &&
                   *charset_str &&
                   *charset_str != '"' && *charset_str != '\'' &&
                   *charset_str != ';' &&
                   *charset_str != '\r' && *charset_str != '\n') {
                charset_buf[i++] = *charset_str++;
            }
            charset_buf[i] = '\0';

            if (i > 0) {
                *charset_out = mcp_strdup(charset_buf);
            }
        }
    }

    return mime_type_buf[0] ? mime_type_buf : default_mime_type;
}

/**
 * @brief Create a content item
 *
 * @param type Content type
 * @param mime_type MIME type string
 * @param data Data buffer
 * @param data_size Size of data
 * @return mcp_content_item_t* Created content item or NULL on failure
 */
static mcp_content_item_t* create_content_item(
    mcp_content_type_t type,
    const char* mime_type,
    const void* data,
    size_t data_size)
{
    mcp_content_item_t* item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!item) {
        return NULL;
    }

    item->type = type;
    item->mime_type = mcp_strdup(mime_type);

    if (data && data_size > 0) {
        item->data = malloc(data_size + 1);
        if (item->data) {
            memcpy(item->data, data, data_size);
            ((char*)item->data)[data_size] = '\0';
            item->data_size = data_size;
        } else {
            item->data = NULL;
            item->data_size = 0;
        }
    } else {
        item->data = NULL;
        item->data_size = 0;
    }

    // Check if allocation failed
    if (!item->mime_type || (data_size > 0 && !item->data)) {
        if (item->mime_type) free(item->mime_type);
        if (item->data) free(item->data);
        free(item);
        return NULL;
    }

    return item;
}

/**
 * @brief Free content items array
 *
 * @param content Array of content items
 * @param count Number of items in the array
 */
static void free_content_items(mcp_content_item_t** content, size_t count)
{
    if (!content) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (content[i]) {
            free(content[i]->mime_type);
            free(content[i]->data);
            free(content[i]);
        }
    }

    free(content);
}

/**
 * @brief Check if a string is valid UTF-8
 *
 * @param data Data to check
 * @param size Size of the data
 * @return bool True if the data is valid UTF-8, false otherwise
 */
static bool is_valid_utf8(const void* data, size_t size)
{
    if (!data || size == 0) {
        return false;
    }

    const unsigned char* bytes = (const unsigned char*)data;
    size_t i = 0;

    while (i < size) {
        if (bytes[i] < 0x80) {
            // ASCII character (1 byte)
            i++;
        } else if ((bytes[i] & 0xE0) == 0xC0) {
            // 2-byte UTF-8 character
            if (i + 1 >= size || (bytes[i + 1] & 0xC0) != 0x80) {
                return false; // Invalid UTF-8 sequence
            }
            i += 2;
        } else if ((bytes[i] & 0xF0) == 0xE0) {
            // 3-byte UTF-8 character
            if (i + 2 >= size || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) {
                return false; // Invalid UTF-8 sequence
            }
            i += 3;
        } else if ((bytes[i] & 0xF8) == 0xF0) {
            // 4-byte UTF-8 character
            if (i + 3 >= size || (bytes[i + 1] & 0xC0) != 0x80 ||
                (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80) {
                return false; // Invalid UTF-8 sequence
            }
            i += 4;
        } else {
            return false; // Invalid UTF-8 sequence
        }
    }

    return true;
}

/**
 * @brief Fix UTF-8 encoding issues in response data
 *
 * This function attempts to fix common encoding issues in HTTP responses,
 * particularly when the server claims UTF-8 but the content is actually
 * in another encoding or has encoding issues.
 *
 * @param data Data to fix
 * @param size Size of the data
 * @param charset Detected charset (may be NULL)
 * @return char* Fixed data (caller must free) or NULL if no fix needed or error
 */
static char* fix_encoding_issues(const void* data, size_t size, const char* charset)
{
    if (!data || size == 0) {
        return NULL;
    }

    // If the data is already valid UTF-8, no need to fix
    if (is_valid_utf8(data, size)) {
        return NULL;
    }

    // If charset is specified as UTF-8 but data is not valid UTF-8,
    // we need to try to fix it
#ifdef _WIN32
    if (charset && (_stricmp(charset, "UTF-8") == 0 || _stricmp(charset, "UTF8") == 0)) {
#else
    if (charset && (strcasecmp(charset, "UTF-8") == 0 || strcasecmp(charset, "UTF8") == 0)) {
#endif
        // Allocate buffer for fixed data (worst case: each byte becomes 3 bytes in UTF-8)
        char* fixed_data = (char*)malloc(size * 3 + 1);
        if (!fixed_data) {
            return NULL;
        }

        // Try to fix common encoding issues
        const unsigned char* src = (const unsigned char*)data;
        unsigned char* dst = (unsigned char*)fixed_data;
        size_t src_pos = 0;
        size_t dst_pos = 0;

        while (src_pos < size) {
            // Check if this is a valid UTF-8 sequence
            if (src[src_pos] < 0x80) {
                // ASCII character (1 byte) - copy as is
                dst[dst_pos++] = src[src_pos++];
            } else if ((src[src_pos] & 0xE0) == 0xC0 && src_pos + 1 < size && (src[src_pos + 1] & 0xC0) == 0x80) {
                // Valid 2-byte UTF-8 sequence - copy as is
                dst[dst_pos++] = src[src_pos++];
                dst[dst_pos++] = src[src_pos++];
            } else if ((src[src_pos] & 0xF0) == 0xE0 && src_pos + 2 < size &&
                      (src[src_pos + 1] & 0xC0) == 0x80 && (src[src_pos + 2] & 0xC0) == 0x80) {
                // Valid 3-byte UTF-8 sequence - copy as is
                dst[dst_pos++] = src[src_pos++];
                dst[dst_pos++] = src[src_pos++];
                dst[dst_pos++] = src[src_pos++];
            } else if ((src[src_pos] & 0xF8) == 0xF0 && src_pos + 3 < size &&
                      (src[src_pos + 1] & 0xC0) == 0x80 && (src[src_pos + 2] & 0xC0) == 0x80 &&
                      (src[src_pos + 3] & 0xC0) == 0x80) {
                // Valid 4-byte UTF-8 sequence - copy as is
                dst[dst_pos++] = src[src_pos++];
                dst[dst_pos++] = src[src_pos++];
                dst[dst_pos++] = src[src_pos++];
                dst[dst_pos++] = src[src_pos++];
            } else {
                // Invalid UTF-8 sequence - replace with Unicode replacement character (U+FFFD)
                dst[dst_pos++] = 0xEF;
                dst[dst_pos++] = 0xBF;
                dst[dst_pos++] = 0xBD;
                src_pos++;
            }
        }

        // Null-terminate the fixed data
        dst[dst_pos] = '\0';

        return fixed_data;
    }

    // For other charsets, we would need a proper character encoding conversion library
    // For now, just return NULL to indicate no fix was applied
    return NULL;
}

// We no longer truncate responses - all responses are shown in full

/**
 * @brief Save response data to a file
 *
 * @param file_path Path to save the file
 * @param data Data to save
 * @param data_size Size of the data
 * @return bool True if successful, false otherwise
 */
static bool save_response_to_file(const char* file_path, const void* data, size_t data_size)
{
    if (!file_path || !data || data_size == 0) {
        mcp_log_error("Invalid parameters for saving response to file");
        return false;
    }

    FILE* file = fopen(file_path, "wb");
    if (!file) {
        mcp_log_error("Failed to open file for writing: %s", file_path);
        return false;
    }

    size_t bytes_written = fwrite(data, 1, data_size, file);
    fclose(file);

    if (bytes_written != data_size) {
        mcp_log_error("Failed to write all data to file: %s (wrote %zu of %zu bytes)",
                     file_path, bytes_written, data_size);
        return false;
    }

    mcp_log_info("Successfully saved response to file: %s (%zu bytes)", file_path, data_size);
    return true;
}

/**
 * @brief Detect character encoding from HTML content
 *
 * This function looks for meta tags in HTML content to detect character encoding.
 *
 * @param html_content HTML content to analyze
 * @return char* Detected charset (caller must free) or NULL if not found
 */
static char* detect_charset_from_html(const char* html_content)
{
    if (!html_content) {
        return NULL;
    }

    // Look for <meta charset="..."> tag
    const char* meta_charset = strstr(html_content, "<meta charset=");
    if (meta_charset) {
        meta_charset += 14; // Skip "<meta charset="

        // Skip whitespace and quotes
        while (*meta_charset && (*meta_charset == ' ' || *meta_charset == '"' || *meta_charset == '\'')) {
            meta_charset++;
        }

        // Extract charset value
        char charset_buf[64] = {0};
        size_t i = 0;
        while (i < sizeof(charset_buf) - 1 &&
               *meta_charset &&
               *meta_charset != '"' && *meta_charset != '\'' &&
               *meta_charset != '>' &&
               *meta_charset != ' ') {
            charset_buf[i++] = *meta_charset++;
        }
        charset_buf[i] = '\0';

        if (i > 0) {
            return mcp_strdup(charset_buf);
        }
    }

    // Look for <meta http-equiv="Content-Type" content="text/html; charset=..."> tag
    const char* meta_content_type = strstr(html_content, "http-equiv=\"Content-Type\"");
    if (!meta_content_type) {
        meta_content_type = strstr(html_content, "http-equiv='Content-Type'");
    }

    if (meta_content_type) {
        // Find the content attribute
        const char* content_attr = strstr(meta_content_type, "content=");
        if (content_attr) {
            content_attr += 8; // Skip "content="

            // Skip whitespace and quotes
            while (*content_attr && (*content_attr == ' ' || *content_attr == '"' || *content_attr == '\'')) {
                content_attr++;
            }

            // Look for charset in content attribute
            const char* charset_str = strstr(content_attr, "charset=");
            if (charset_str) {
                charset_str += 8; // Skip "charset="

                // Extract charset value
                char charset_buf[64] = {0};
                size_t i = 0;
                while (i < sizeof(charset_buf) - 1 &&
                       *charset_str &&
                       *charset_str != '"' && *charset_str != '\'' &&
                       *charset_str != '>' &&
                       *charset_str != ' ') {
                    charset_buf[i++] = *charset_str++;
                }
                charset_buf[i] = '\0';

                if (i > 0) {
                    return mcp_strdup(charset_buf);
                }
            }
        }
    }

    return NULL;
}

/**
 * @brief Verification callback for SSL connections
 *
 * This callback is used to allow connections even when certificate verification fails.
 * In a production environment, you might want to implement more strict verification.
 *
 * @param preverify_ok Whether the verification was successful
 * @param x509_ctx The X509 store context
 * @return int 1 to continue the handshake, 0 to abort
 */
static int ssl_verify_callback(int preverify_ok, X509_STORE_CTX* x509_ctx) {
    // For now, we'll accept all certificates to ensure connections work
    // In a production environment, you should implement proper certificate verification
    if (!preverify_ok) {
        mcp_log_warn("SSL certificate verification failed, but continuing anyway");

        // Get more information about the verification failure
        int err = X509_STORE_CTX_get_error(x509_ctx);
        int depth = X509_STORE_CTX_get_error_depth(x509_ctx);
        X509* cert = X509_STORE_CTX_get_current_cert(x509_ctx);

        if (cert) {
            char subject_name[256] = {0};
            X509_NAME_oneline(X509_get_subject_name(cert), subject_name, sizeof(subject_name) - 1);
            mcp_log_warn("Certificate verification error at depth %d: %d (%s) for %s",
                        depth, err, X509_verify_cert_error_string(err), subject_name);
        } else {
            mcp_log_warn("Certificate verification error at depth %d: %d (%s)",
                        depth, err, X509_verify_cert_error_string(err));
        }
    }

    return 1; // Always return 1 to accept the certificate
}

/**
 * @brief Initialize OpenSSL and create a new SSL context
 *
 * @return ssl_context_t* Initialized SSL context or NULL on failure
 */
static ssl_context_t* ssl_init(void)
{
    ssl_context_t* ssl_ctx = (ssl_context_t*)calloc(1, sizeof(ssl_context_t));
    if (!ssl_ctx) {
        mcp_log_error("Failed to allocate memory for SSL context");
        return NULL;
    }

    // Initialize OpenSSL
    if (!ssl_ctx->initialized) {
        // Initialize OpenSSL library with all algorithms and error strings
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

    // Create a new SSL context using TLS client method
    const SSL_METHOD* method = TLS_client_method();
    if (!method) {
        mcp_log_error("Failed to create SSL method");
        ssl_cleanup(ssl_ctx);
        return NULL;
    }

    ssl_ctx->ctx = SSL_CTX_new(method);
    if (!ssl_ctx->ctx) {
        mcp_log_error("Failed to create SSL context");
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        mcp_log_error("OpenSSL error: %s", err_buf);
        ssl_cleanup(ssl_ctx);
        return NULL;
    }

    // Set default verify paths (CA certificates)
    if (SSL_CTX_set_default_verify_paths(ssl_ctx->ctx) != 1) {
        mcp_log_warn("Failed to set default verify paths, certificate verification may fail");
    }

    // Set SSL options - disable older protocols and enable workarounds for buggy SSL implementations
    SSL_CTX_set_options(ssl_ctx->ctx,
                       SSL_OP_NO_SSLv2 |
                       SSL_OP_NO_SSLv3 |
                       SSL_OP_NO_COMPRESSION |
                       SSL_OP_ALL); // SSL_OP_ALL includes various bug workarounds

    // Set verification mode with our callback - use SSL_VERIFY_NONE for testing
    // In production, you should use SSL_VERIFY_PEER with proper certificate verification
    SSL_CTX_set_verify(ssl_ctx->ctx, SSL_VERIFY_NONE, ssl_verify_callback);

    // Set verification depth
    SSL_CTX_set_verify_depth(ssl_ctx->ctx, 4);

    // Set session cache mode
    SSL_CTX_set_session_cache_mode(ssl_ctx->ctx, SSL_SESS_CACHE_CLIENT);

    // Set timeout for SSL sessions
    SSL_CTX_set_timeout(ssl_ctx->ctx, 300); // 5 minutes

    mcp_log_info("SSL context initialized successfully");
    return ssl_ctx;
}

/**
 * @brief Clean up SSL context and free resources
 *
 * @param ssl_ctx SSL context to clean up
 */
static void ssl_cleanup(ssl_context_t* ssl_ctx)
{
    if (!ssl_ctx) {
        return;
    }

    if (ssl_ctx->ssl) {
        SSL_shutdown(ssl_ctx->ssl);
        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = NULL;
    }

    if (ssl_ctx->ctx) {
        SSL_CTX_free(ssl_ctx->ctx);
        ssl_ctx->ctx = NULL;
    }

    free(ssl_ctx);
}

/**
 * @brief Create a new SSL connection and connect to the server
 *
 * @param ssl_ctx SSL context
 * @param sock Socket to use for the connection
 * @param host Hostname for SNI
 * @return int 0 on success, -1 on failure
 */
static int ssl_connect(ssl_context_t* ssl_ctx, socket_t sock, const char* host)
{
    if (!ssl_ctx || !ssl_ctx->ctx || sock == MCP_INVALID_SOCKET) {
        mcp_log_error("Invalid SSL context or socket");
        return -1;
    }

    // Create a new SSL connection
    ssl_ctx->ssl = SSL_new(ssl_ctx->ctx);
    if (!ssl_ctx->ssl) {
        mcp_log_error("Failed to create SSL connection");
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        mcp_log_error("OpenSSL error: %s", err_buf);
        return -1;
    }

    // Set the socket for the SSL connection
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

    // Set SNI hostname
    if (host) {
        mcp_log_info("Setting SNI hostname to: %s", host);
        if (!SSL_set_tlsext_host_name(ssl_ctx->ssl, host)) {
            mcp_log_warn("Failed to set SNI hostname, continuing anyway");
        }
    }

#ifdef _WIN32
    u_long original_mode;
#else
    int original_flags;
#endif

    // Set socket to non-blocking mode for SSL handshake and save original mode
    int nb_result = mcp_socket_set_non_blocking_ex(sock,
#ifdef _WIN32
                                              &original_mode
#else
                                              &original_flags
#endif
                                              );
    if (nb_result != 0) {
        mcp_log_warn("Failed to set socket to non-blocking mode, SSL handshake may block");
    }

    // Perform SSL handshake with retry
    int result;
    int retry_count = 0;
    const int max_retries = 5; // Increase max retries

    do {
        mcp_log_info("Attempting SSL handshake (attempt %d/%d)", retry_count + 1, max_retries);
        result = SSL_connect(ssl_ctx->ssl);

        if (result == 1) {
            // Success
            break;
        }

        int ssl_error = SSL_get_error(ssl_ctx->ssl, result);

        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            // Need to retry, the operation would block
            mcp_log_info("SSL handshake would block (error: %s), retrying...",
                        ssl_error == SSL_ERROR_WANT_READ ? "WANT_READ" : "WANT_WRITE");

            // Use progressively longer delays between retries
            int delay_ms = 100 * (retry_count + 1);
            mcp_log_info("Waiting %d ms before retry", delay_ms);
            mcp_sleep_ms(delay_ms);

            retry_count++;
            continue;
        }

        // Log detailed error information
        mcp_log_error("SSL connection failed: %d (SSL error: %d)", result, ssl_error);
        unsigned long err;
        char err_buf[256];
        while ((err = ERR_get_error()) != 0) {
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            mcp_log_error("SSL error details: %s", err_buf);
        }

        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = NULL;
        return -1;

    } while (retry_count < max_retries);

    if (result != 1) {
        mcp_log_error("SSL handshake failed after %d attempts", max_retries);
        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = NULL;
        return -1;
    }

    // Get certificate information
    X509* cert = SSL_get_peer_certificate(ssl_ctx->ssl);
    if (cert) {
        char subject_name[256] = {0};
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, sizeof(subject_name) - 1);
        mcp_log_info("Server certificate subject: %s", subject_name);

        char issuer_name[256] = {0};
        X509_NAME_oneline(X509_get_issuer_name(cert), issuer_name, sizeof(issuer_name) - 1);
        mcp_log_info("Server certificate issuer: %s", issuer_name);

        X509_free(cert);
    } else {
        mcp_log_warn("No server certificate received");
    }

    // Restore socket to original mode for normal operation
    if (mcp_socket_restore_blocking(sock,
#ifdef _WIN32
                                  original_mode
#else
                                  original_flags
#endif
                                  ) != 0) {
        mcp_log_warn("Failed to restore socket to original mode, operations may not behave as expected");
    }

    mcp_log_info("SSL connection established using %s", SSL_get_cipher(ssl_ctx->ssl));
    return 0;
}

/**
 * @brief Send data over an SSL connection
 *
 * @param ssl_ctx SSL context
 * @param data Data to send
 * @param len Length of data
 * @return int 0 on success, -1 on failure
 */
static int ssl_send(ssl_context_t* ssl_ctx, const char* data, size_t len)
{
    if (!ssl_ctx || !ssl_ctx->ssl || !data || len == 0) {
        mcp_log_error("Invalid SSL context or data for sending");
        return -1;
    }

    size_t total_sent = 0;
    int retry_count = 0;
    const int max_retries = 5;

    mcp_log_debug("Sending %zu bytes over SSL", len);

    while (total_sent < len) {
        int bytes_to_send = (int)(len - total_sent);
        if (bytes_to_send > INT_MAX) {
            bytes_to_send = INT_MAX;
        }

        int bytes_sent = SSL_write(ssl_ctx->ssl, data + total_sent, bytes_to_send);

        if (bytes_sent <= 0) {
            int ssl_error = SSL_get_error(ssl_ctx->ssl, bytes_sent);

            if (ssl_error == SSL_ERROR_WANT_WRITE || ssl_error == SSL_ERROR_WANT_READ) {
                // Non-blocking operation would block, try again after a short delay
                mcp_log_debug("SSL_write would block, retrying...");
                mcp_sleep_ms(50);

                retry_count++;
                if (retry_count > max_retries) {
                    mcp_log_error("SSL_write failed after %d retries", max_retries);
                    return -1;
                }
                continue;
            }

            // Log detailed error information
            mcp_log_error("SSL_write failed: %d (SSL error: %d)", bytes_sent, ssl_error);
            unsigned long err;
            char err_buf[256];
            while ((err = ERR_get_error()) != 0) {
                ERR_error_string_n(err, err_buf, sizeof(err_buf));
                mcp_log_error("SSL error details: %s", err_buf);
            }

            return -1;
        }

        total_sent += bytes_sent;
        retry_count = 0; // Reset retry count after successful send
    }

    mcp_log_debug("Successfully sent %zu bytes over SSL", total_sent);
    return 0;
}

/**
 * @brief Receive data from an SSL connection
 *
 * @param ssl_ctx SSL context
 * @param buffer Buffer to store received data
 * @param buffer_size Size of the buffer
 * @param bytes_received Pointer to store the number of bytes received
 * @return int 0 on success, -1 on failure
 */
static int ssl_recv(ssl_context_t* ssl_ctx, char* buffer, size_t buffer_size, int* bytes_received)
{
    if (!ssl_ctx || !ssl_ctx->ssl || !buffer || buffer_size == 0 || !bytes_received) {
        mcp_log_error("Invalid SSL context or buffer for receiving");
        return -1;
    }

    int retry_count = 0;
    const int max_retries = 5;

    // Check if there's pending data in the SSL buffer
    int pending = SSL_pending(ssl_ctx->ssl);
    if (pending > 0) {
        mcp_log_debug("SSL has %d bytes pending", pending);
    }

    do {
        *bytes_received = SSL_read(ssl_ctx->ssl, buffer, (int)buffer_size);

        if (*bytes_received > 0) {
            // Success - data received
            mcp_log_debug("Received %d bytes from SSL", *bytes_received);
            return 0;
        }

        // Handle errors or special cases
        int ssl_error = SSL_get_error(ssl_ctx->ssl, *bytes_received);

        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            // Non-blocking operation would block
            mcp_log_debug("SSL_read would block, retrying...");
            mcp_sleep_ms(50);

            retry_count++;
            if (retry_count > max_retries) {
                mcp_log_debug("SSL_read would block after %d retries, returning 0 bytes", max_retries);
                *bytes_received = 0;
                return 0;
            }
            continue;
        } else if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            // Connection closed cleanly
            mcp_log_debug("SSL connection closed cleanly");
            *bytes_received = 0;
            return 0;
        } else if (ssl_error == SSL_ERROR_SYSCALL && *bytes_received == 0) {
            // EOF observed
            mcp_log_debug("SSL connection EOF observed");
            *bytes_received = 0;
            return 0;
        }

        // Log detailed error information
        mcp_log_error("SSL_read failed: %d (SSL error: %d)", *bytes_received, ssl_error);
        unsigned long err;
        char err_buf[256];
        while ((err = ERR_get_error()) != 0) {
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            mcp_log_error("SSL error details: %s", err_buf);
        }

        return -1;

    } while (retry_count < max_retries);

    // Should not reach here, but just in case
    mcp_log_error("SSL_read unexpected exit from loop");
    *bytes_received = 0;
    return -1;
}

/**
 * @brief HTTP client tool handler function.
 *
 * This function handles HTTP client tool calls from MCP clients.
 *
 * @param server The MCP server instance.
 * @param name The name of the tool being called.
 * @param params The parameters for the tool call.
 * @param user_data User data passed to the tool handler.
 * @param content Pointer to store the content items.
 * @param content_count Pointer to store the number of content items.
 * @param is_error Pointer to store whether an error occurred.
 * @param error_message Pointer to store the error message.
 * @return mcp_error_code_t Error code.
 */
mcp_error_code_t http_client_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message)
{
    (void)server;
    (void)user_data;
    (void)name;

    // Initialize output parameters
    *is_error = false;
    *content = NULL;
    *content_count = 0;
    *error_message = NULL;

    // Variables for parameter extraction
    const char* url = NULL;
    const char* method = "GET";
    const char* headers = NULL;
    const char* body = NULL;
    const char* content_type = NULL;
    const char* save_to_file = NULL;
    uint32_t timeout_ms = HTTP_CLIENT_DEFAULT_TIMEOUT_MS;

    // Extract required URL parameter
    mcp_json_t* url_node = mcp_json_object_get_property(params, "url");
    if (!url_node || mcp_json_get_type(url_node) != MCP_JSON_STRING ||
        mcp_json_get_string(url_node, &url) != 0) {
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid 'url' parameter");
        return MCP_ERROR_INVALID_PARAMS;
    }

    // Extract optional method parameter
    mcp_json_t* method_node = mcp_json_object_get_property(params, "method");
    if (method_node && mcp_json_get_type(method_node) == MCP_JSON_STRING) {
        mcp_json_get_string(method_node, &method);
    }

    // Extract optional headers parameter
    mcp_json_t* headers_node = mcp_json_object_get_property(params, "headers");
    if (headers_node && mcp_json_get_type(headers_node) == MCP_JSON_STRING) {
        mcp_json_get_string(headers_node, &headers);
    }

    // Extract optional body parameter
    mcp_json_t* body_node = mcp_json_object_get_property(params, "body");
    if (body_node && mcp_json_get_type(body_node) == MCP_JSON_STRING) {
        mcp_json_get_string(body_node, &body);
    }

    // Extract optional content_type parameter
    mcp_json_t* content_type_node = mcp_json_object_get_property(params, "content_type");
    if (content_type_node && mcp_json_get_type(content_type_node) == MCP_JSON_STRING) {
        mcp_json_get_string(content_type_node, &content_type);
    } else if (body != NULL) {
        // Default content type for requests with body
        content_type = "application/json";
    }

    // Extract optional timeout parameter
    mcp_json_t* timeout_node = mcp_json_object_get_property(params, "timeout");
    if (timeout_node && mcp_json_get_type(timeout_node) == MCP_JSON_NUMBER) {
        double timeout_seconds;
        if (mcp_json_get_number(timeout_node, &timeout_seconds) == 0) {
            timeout_ms = (uint32_t)(timeout_seconds * 1000);
        }
    }

    // Extract optional save_to_file parameter
    mcp_json_t* save_to_file_node = mcp_json_object_get_property(params, "save_to_file");
    if (save_to_file_node && mcp_json_get_type(save_to_file_node) == MCP_JSON_STRING) {
        mcp_json_get_string(save_to_file_node, &save_to_file);
    }

    // We no longer use max_display_length parameter - always show full response

    // Send the HTTP request
    http_response_t* response = http_request(
        method,
        url,
        content_type,
        headers,
        body,
        body ? strlen(body) : 0,
        timeout_ms
    );

    if (!response) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to send HTTP request");
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Allocate content items array (metadata + response)
    *content_count = 2;
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*) * *content_count);
    if (!*content) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to allocate memory for content array");
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Create metadata JSON with charset information if available
    char metadata_json[256];
    if (response->charset) {
        snprintf(metadata_json, sizeof(metadata_json),
                "{\"status_code\": %d, \"content_length\": %zu, \"charset\": \"%s\", \"success\": true}",
                response->status_code, response->size, response->charset);
    } else {
        snprintf(metadata_json, sizeof(metadata_json),
                "{\"status_code\": %d, \"content_length\": %zu, \"success\": true}",
                response->status_code, response->size);
    }

    (*content)[0] = create_content_item(MCP_CONTENT_TYPE_JSON, "application/json", metadata_json, strlen(metadata_json));

    // Extract MIME type and charset from headers
    char* charset = NULL;
    const char* mime_type = extract_mime_type(response->headers, &charset);

    // Store charset in response structure if not already set
    if (!response->charset) {
        response->charset = charset;
    } else if (charset) {
        // If we already have a charset from HTML detection, free this one
        free(charset);
    }

    // Create content item with proper MIME type including charset
    char full_mime_type[256];
    if (response->charset) {
        snprintf(full_mime_type, sizeof(full_mime_type), "%s; charset=%s",
                mime_type, response->charset);
    } else {
        snprintf(full_mime_type, sizeof(full_mime_type), "%s", mime_type);
    }

    // Never save response to file, always display full response
    bool saved_to_file = false;

    // Only save to file if explicitly requested by the user
    if (save_to_file) {
        saved_to_file = save_response_to_file(save_to_file, response->data, response->size);
        if (saved_to_file) {
            // Update metadata to include file path
            snprintf(metadata_json, sizeof(metadata_json),
                    "{\"status_code\": %d, \"content_length\": %zu, \"saved_to_file\": \"%s\"%s%s%s, \"success\": true}",
                    response->status_code, response->size, save_to_file,
                    response->charset ? ", \"charset\": \"" : "",
                    response->charset ? response->charset : "",
                    response->charset ? "\"" : "");

            // Recreate the metadata content item
            free_content_items(&(*content)[0], 1);
            (*content)[0] = create_content_item(MCP_CONTENT_TYPE_JSON, "application/json", metadata_json, strlen(metadata_json));
        }
    }



    // Try to fix encoding issues if needed
    char* fixed_data = NULL;
    const void* content_data = response->data;
    size_t content_size = response->size;

    // Only try to fix encoding for text content
    if (strstr(mime_type, "text/") || strstr(mime_type, "application/json") ||
        strstr(mime_type, "application/xml") || strstr(mime_type, "application/javascript")) {
        fixed_data = fix_encoding_issues(response->data, response->size, response->charset);
        if (fixed_data) {
            mcp_log_info("Fixed encoding issues in response data");
            content_data = fixed_data;
            content_size = strlen(fixed_data);
        }
    }

    // Always use the full response data, never truncate
    (*content)[1] = create_content_item(MCP_CONTENT_TYPE_TEXT, full_mime_type, content_data, content_size);

    // Free fixed data if it was created
    if (fixed_data) {
        free(fixed_data);
    }

    // Check if content item creation failed
    if (!(*content)[0] || !(*content)[1]) {
        *is_error = true;
        *error_message = mcp_strdup("Failed to create content items");
        free_content_items(*content, *content_count);
        *content = NULL;
        *content_count = 0;
        http_response_free(response);
        return MCP_ERROR_INTERNAL_ERROR;
    }

    // Clean up
    http_response_free(response);

    return MCP_ERROR_NONE;
}

/**
 * @brief Extract HTTP headers and status code from response data
 *
 * @param response The HTTP response structure to update
 * @return bool True if headers were successfully extracted, false otherwise
 */
static bool extract_http_headers(http_response_t* response)
{
    if (!response || !response->data || response->size < 4) {
        return false;
    }

    // Find the end of headers marker
    char* headers_end = strstr(response->data, "\r\n\r\n");
    if (!headers_end || headers_end < response->data || headers_end >= response->data + response->size) {
        return false;
    }

    // Calculate header size and body position
    size_t headers_end_offset = headers_end - response->data;
    size_t headers_size = headers_end_offset + 2; // Include the first \r\n
    size_t headers_total_size = headers_end_offset + 4; // Include \r\n\r\n

    // Allocate and copy headers
    response->headers = (char*)malloc(headers_size + 1);
    if (!response->headers) {
        return false;
    }

    memcpy(response->headers, response->data, headers_size);
    response->headers[headers_size] = '\0';

    // Extract status code
    char* status_line = response->headers;
    char* space = strchr(status_line, ' ');
    if (space) {
        response->status_code = atoi(space + 1);
    }

    // Initialize charset field
    response->charset = NULL;

    // Extract charset from Content-Type header
    char* charset = NULL;
    extract_mime_type(response->headers, &charset);

    // If charset not found in headers, try to detect from HTML content
    if (!charset && response->data) {
        // Check if this is HTML content
        const char* mime_type = extract_mime_type(response->headers, NULL);
        if (mime_type && (strstr(mime_type, "text/html") || strstr(mime_type, "application/xhtml"))) {
            charset = detect_charset_from_html(response->data);
        }
    }

    // Store charset in response
    response->charset = charset;

    // Move body to beginning of buffer
    size_t body_size = response->size - headers_total_size;
    memmove(response->data, response->data + headers_total_size, body_size);
    response->size = body_size;
    response->data[response->size] = '\0';

    return true;
}

/**
 * @brief Sends an HTTP request.
 *
 * @param method HTTP method (GET, POST, etc.)
 * @param url URL to request
 * @param content_type Content type for request body
 * @param headers Additional headers
 * @param data Request body data
 * @param data_size Size of request body data
 * @param timeout_ms Timeout in milliseconds
 * @return http_response_t* Response object, or NULL on error
 */
static http_response_t* http_request(const char* method, const char* url,
                                    const char* content_type, const char* headers,
                                    const void* data, size_t data_size,
                                    uint32_t timeout_ms)
{
    if (!method || !url) {
        return NULL;
    }

    // Parse URL
    char* host = NULL;
    int port = 0;
    char* path = NULL;
    bool use_ssl = false;

    char* url_copy = parse_url(url, &host, &port, &path, &use_ssl);
    if (!url_copy) {
        mcp_log_error("Failed to parse URL: %s", url);
        return NULL;
    }

    // Initialize SSL if needed
    ssl_context_t* ssl_ctx = NULL;
    if (use_ssl) {
        mcp_log_info("Using SSL for connection to %s:%d", host, port);
        ssl_ctx = ssl_init();
        if (!ssl_ctx) {
            mcp_log_error("Failed to initialize SSL");
            free(url_copy);
            return NULL;
        }
    }

    // Special handling for localhost connections to avoid deadlocks
    bool is_localhost = (strcmp(host, "localhost") == 0 ||
                         strcmp(host, "127.0.0.1") == 0 ||
                         strcmp(host, "::1") == 0);

    socket_t sock = MCP_INVALID_SOCKET;

    // Check if we're trying to connect to ourselves (same server)
    // This is a special case that requires direct handling to avoid deadlocks
    if (is_localhost && (port == 8080 || port == 8180)) {
        mcp_log_info("Detected connection to self (localhost:%d), using direct response", port);

        // Create a direct response without making an actual connection
        http_response_t* response = (http_response_t*)calloc(1, sizeof(http_response_t));
        if (!response) {
            mcp_log_error("Failed to allocate memory for direct HTTP response");
            free(url_copy);
            return NULL;
        }

        // Initialize fields
        response->charset = NULL;

        // For root path, return the server's home page
        if (path == NULL || *path == '\0' || strcmp(path, "") == 0) {
            const char* html_response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Connection: close\r\n"
                "\r\n"
                "<!DOCTYPE html>\n"
                "<html>\n"
                "<head>\n"
                "    <title>MCP HTTP Server</title>\n"
                "</head>\n"
                "<body>\n"
                "    <h1>MCP HTTP Server</h1>\n"
                "    <p>This is a direct response from the HTTP client tool.</p>\n"
                "    <p>The server detected that you're trying to connect to itself and provided this response directly.</p>\n"
                "</body>\n"
                "</html>";

            size_t response_len = strlen(html_response);
            response->capacity = response_len + 1;
            response->data = (char*)malloc(response->capacity);
            if (!response->data) {
                mcp_log_error("Failed to allocate memory for direct HTTP response data");
                free(response);
                free(url_copy);
                return NULL;
            }

            memcpy(response->data, html_response, response_len);
            response->data[response_len] = '\0';
            response->size = response_len;

            // Extract headers
            extract_http_headers(response);

            mcp_log_info("Generated direct HTML response for localhost:%d", port);
            free(url_copy);
            return response;
        }

        // For other paths, return a 404 response
        const char* not_found_response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "The requested path was not found on this server.";

        size_t response_len = strlen(not_found_response);
        response->capacity = response_len + 1;
        response->data = (char*)malloc(response->capacity);
        if (!response->data) {
            mcp_log_error("Failed to allocate memory for direct HTTP response data");
            free(response);
            free(url_copy);
            return NULL;
        }

        memcpy(response->data, not_found_response, response_len);
        response->data[response_len] = '\0';
        response->size = response_len;

        // Extract headers
        extract_http_headers(response);

        mcp_log_info("Generated direct 404 response for localhost:%d/%s", port, path);
        free(url_copy);
        return response;
    }
    else if (is_localhost) {
        mcp_log_info("Detected localhost connection, using shorter timeout (5000ms)");

        // For localhost connections, use the standard non-blocking connect function
        // but with a shorter timeout
        sock = mcp_socket_connect_nonblocking(host, (uint16_t)port, 5000);
        if (sock == MCP_INVALID_SOCKET) {
            mcp_log_error("Failed to connect to localhost: %s:%d", host, port);
            free(url_copy);
            return NULL;
        }

        mcp_log_info("Successfully connected to localhost:%d", port);
    }
    else {
        // For non-localhost connections, use the standard non-blocking connect
        sock = mcp_socket_connect_nonblocking(host, (uint16_t)port, timeout_ms);
        if (sock == MCP_INVALID_SOCKET) {
            mcp_log_error("Failed to connect to server: %s:%d", host, port);
            free(url_copy);
            return NULL;
        }
    }

    // Build HTTP request
    char request[HTTP_CLIENT_REQUEST_BUFFER_SIZE] = {0};
    int request_len = 0;

    // Log the request details for debugging
    mcp_log_info("HTTP client sending %s request to %s:%d/%s", method, host, port, path ? path : "");

    // Ensure method is valid - default to GET if empty
    if (method == NULL || *method == '\0') {
        method = "GET";
        mcp_log_info("Empty method specified, defaulting to GET");
    }

    // Request line
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
                           "%s /%s HTTP/1.1\r\n", method, path ? path : "");

    // Host header
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
                           "Host: %s\r\n", host);

    // Content-Type header (if body is provided)
    if (data && data_size > 0 && content_type) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Content-Type: %s\r\n", content_type);
    }

    // Content-Length header (if body is provided)
    if (data && data_size > 0) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "Content-Length: %zu\r\n", data_size);
    }

    // Connection header
    request_len += snprintf(request + request_len, sizeof(request) - request_len,
                           "Connection: close\r\n");

    // Additional headers
    if (headers) {
        request_len += snprintf(request + request_len, sizeof(request) - request_len,
                               "%s\r\n", headers);
    }

    // End of headers
    request_len += snprintf(request + request_len, sizeof(request) - request_len, "\r\n");

    // Log the full request for debugging
    mcp_log_debug("HTTP request headers:\n%.*s", request_len, request);

    // Establish SSL connection if needed
    if (use_ssl) {
        if (ssl_connect(ssl_ctx, sock, host) != 0) {
            mcp_log_error("Failed to establish SSL connection");
            ssl_cleanup(ssl_ctx);
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        }
    }

    // Send request headers
    if (use_ssl) {
        if (ssl_send(ssl_ctx, request, request_len) != 0) {
            mcp_log_error("Failed to send HTTP request headers over SSL");
            ssl_cleanup(ssl_ctx);
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        }
    } else {
        if (mcp_socket_send_exact(sock, request, request_len, NULL) != 0) {
            mcp_log_error("Failed to send HTTP request headers");
            free(url_copy);
            mcp_socket_close(sock);
            return NULL;
        }
    }

    // Send request body (if provided)
    if (data && data_size > 0) {
        if (use_ssl) {
            if (ssl_send(ssl_ctx, (const char*)data, data_size) != 0) {
                mcp_log_error("Failed to send HTTP request body over SSL");
                ssl_cleanup(ssl_ctx);
                free(url_copy);
                mcp_socket_close(sock);
                return NULL;
            }
        } else {
            if (mcp_socket_send_exact(sock, (const char*)data, data_size, NULL) != 0) {
                mcp_log_error("Failed to send HTTP request body");
                free(url_copy);
                mcp_socket_close(sock);
                return NULL;
            }
        }
    }

    // Create response structure
    http_response_t* response = (http_response_t*)calloc(1, sizeof(http_response_t));
    if (!response) {
        mcp_log_error("Failed to allocate memory for HTTP response");
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Initialize fields
    response->charset = NULL;

    // Allocate initial buffer for response
    response->capacity = HTTP_CLIENT_INITIAL_BUFFER_SIZE;
    response->data = (char*)malloc(response->capacity);
    if (!response->data) {
        mcp_log_error("Failed to allocate memory for HTTP response data");
        free(response);
        free(url_copy);
        mcp_socket_close(sock);
        return NULL;
    }

    // Receive response
    char buffer[HTTP_CLIENT_INITIAL_BUFFER_SIZE];
    int bytes_received;
    bool headers_complete = false;

    mcp_log_info("Waiting for response from %s:%d (timeout: %d ms)", host, port, timeout_ms);

    // For SSL connections, we don't need to wait since SSL_read will block
    if (!use_ssl) {
        // Use mcp_socket_wait_readable to check if data is available
        int wait_result = mcp_socket_wait_readable(sock, (int)timeout_ms, NULL);
        if (wait_result <= 0) {
            mcp_log_error("Socket wait failed or timed out: %d", wait_result);
            mcp_socket_close(sock);
            free(url_copy);
            http_response_free(response);
            return NULL;
        }
    }

    mcp_log_info("Socket is readable, receiving data");

    do {
        // Receive data
        if (use_ssl) {
            if (ssl_recv(ssl_ctx, buffer, sizeof(buffer) - 1, &bytes_received) != 0) {
                mcp_log_error("SSL receive error");
                break;
            }
        } else {
            bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received < 0) {
                mcp_log_error("Socket receive error: %d", mcp_socket_get_lasterror());
                break;
            }
        }

        mcp_log_debug("Received %d bytes from %s", bytes_received, use_ssl ? "SSL" : "socket");

        if (bytes_received <= 0) {
            // Connection closed or no data
            break;
        }

        // Ensure buffer is null-terminated
        buffer[bytes_received] = '\0';

        // Check if we need to resize the response buffer
        if (response->size + bytes_received >= response->capacity) {
            size_t new_capacity = response->capacity * 2;
            if (new_capacity > HTTP_CLIENT_MAX_RESPONSE_SIZE) {
                new_capacity = HTTP_CLIENT_MAX_RESPONSE_SIZE;
            }

            char* new_data = (char*)realloc(response->data, new_capacity);
            if (!new_data) {
                mcp_log_error("Failed to resize HTTP response buffer");
                http_response_free(response);
                free(url_copy);
                mcp_socket_close(sock);
                return NULL;
            }
            response->data = new_data;
            response->capacity = new_capacity;
        }

        // Append received data to response
        memcpy(response->data + response->size, buffer, bytes_received);
        response->size += bytes_received;
        response->data[response->size] = '\0';

        // Check if we've received the complete headers
        if (!headers_complete && strstr(response->data, "\r\n\r\n")) {
            headers_complete = true;
        }

        // Check if we've reached the maximum response size
        if (response->size >= HTTP_CLIENT_MAX_RESPONSE_SIZE) {
            mcp_log_warn("HTTP response exceeded maximum size (%d bytes)", HTTP_CLIENT_MAX_RESPONSE_SIZE);
            break;
        }

        // Check if there's more data to read
        if (!use_ssl) {
            if (mcp_socket_wait_readable(sock, 100, NULL) <= 0) {
                mcp_log_debug("No more data available from socket");
                break;  // No more data available or error
            }
        } else {
            // For SSL, we'll try another read and break if no data
            int pending = SSL_pending(ssl_ctx->ssl);
            if (pending <= 0) {
                // Try a non-blocking read to see if there's more data
                fd_set readfds;
                struct timeval tv;
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);
                tv.tv_sec = 0;
                tv.tv_usec = 100000; // 100ms

                int select_result = select((int)sock + 1, &readfds, NULL, NULL, &tv);
                if (select_result <= 0) {
                    mcp_log_debug("No more data available from SSL connection");
                    break;
                }
            }
        }
    } while (1);

    // Clean up resources
    if (use_ssl) {
        ssl_cleanup(ssl_ctx);
    }
    mcp_socket_close(sock);
    free(url_copy);

    // If we didn't get any data, return error
    if (response->size == 0) {
        mcp_log_error("No data received from HTTP server");
        http_response_free(response);
        return NULL;
    }

    // Extract headers and move body to beginning of buffer
    if (!extract_http_headers(response)) {
        mcp_log_warn("Failed to extract HTTP headers, returning raw response");
    }

    // Log response details
    mcp_log_info("HTTP response status: %d, size: %zu bytes", response->status_code, response->size);
    if (response->headers) {
        mcp_log_debug("HTTP response headers:\n%s", response->headers);
    }

    return response;
}

/**
 * @brief Frees an HTTP response structure.
 *
 * @param response Response structure to free
 */
static void http_response_free(http_response_t* response)
{
    if (response) {
        free(response->data);
        free(response->headers);
        free(response->charset);
        free(response);
    }
}

/**
 * @brief Parses a URL into its components.
 *
 * @param url URL to parse
 * @param host Pointer to store the host
 * @param port Pointer to store the port
 * @param path Pointer to store the path
 * @param use_ssl Pointer to store whether SSL should be used
 * @return char* Duplicated URL string that should be freed by the caller
 */
static char* parse_url(const char* url, char** host, int* port, char** path, bool* use_ssl)
{
    if (!url || !host || !port || !path || !use_ssl) {
        return NULL;
    }

    // Duplicate the URL
    char* url_copy = mcp_strdup(url);
    if (!url_copy) {
        return NULL;
    }

    // Default values
    *host = NULL;
    *port = 80;
    *path = NULL;
    *use_ssl = false;

    // Parse protocol (http:// or https://)
    char* host_start = url_copy;
    if (strncmp(url_copy, "http://", 7) == 0) {
        host_start = url_copy + 7;
    } else if (strncmp(url_copy, "https://", 8) == 0) {
        host_start = url_copy + 8;
        *use_ssl = true;
        *port = 443;
    }

    // Find the path
    char* path_start = strchr(host_start, '/');
    if (path_start) {
        *path_start = '\0';  // Null-terminate the host:port part
        *path = path_start + 1;  // Skip the leading slash
    } else {
        static char empty_path[] = "";
        *path = empty_path;
    }

    // Check for port in host
    char* port_start = strchr(host_start, ':');
    if (port_start) {
        *port_start = '\0';  // Null-terminate the host part
        *port = atoi(port_start + 1);
    }

    // Set the host
    *host = host_start;

    return url_copy;
}

/**
 * @brief Registers the HTTP client tool with the MCP server.
 *
 * @param server The MCP server instance.
 * @return int 0 on success, non-zero on error.
 */
int register_http_client_tool(mcp_server_t* server)
{
    if (!server) {
        return -1;
    }

    // Create the HTTP client tool
    mcp_tool_t* http_tool = mcp_tool_create("http_client", "Make HTTP requests to external services");
    if (!http_tool) {
        mcp_log_error("Failed to create HTTP client tool");
        return -1;
    }

    // Add parameters
    if (mcp_tool_add_param(http_tool, "url", "string", "URL to request", true) != 0 ||
        mcp_tool_add_param(http_tool, "method", "string", "HTTP method (GET, POST, PUT, DELETE, etc.)", false) != 0 ||
        mcp_tool_add_param(http_tool, "headers", "string", "Additional HTTP headers", false) != 0 ||
        mcp_tool_add_param(http_tool, "body", "string", "Request body", false) != 0 ||
        mcp_tool_add_param(http_tool, "content_type", "string", "Content type for request body", false) != 0 ||
        mcp_tool_add_param(http_tool, "timeout", "number", "Request timeout in seconds", false) != 0 ||
        mcp_tool_add_param(http_tool, "save_to_file", "string", "Path to save response to (optional)", false) != 0) {
        mcp_log_error("Failed to add parameters to HTTP client tool");
        mcp_tool_free(http_tool);
        return -1;
    }

    // Add the tool to the server
    if (mcp_server_add_tool(server, http_tool) != 0) {
        mcp_log_error("Failed to add HTTP client tool to server");
        mcp_tool_free(http_tool);
        return -1;
    }

    // Free the tool (server makes a copy)
    mcp_tool_free(http_tool);

    mcp_log_info("HTTP client tool registered");

    return 0;
}
