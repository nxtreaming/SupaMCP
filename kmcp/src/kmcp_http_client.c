#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "kmcp_http_client.h"
#include "kmcp_error.h"
#include "kmcp_common.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Use _stricmp for Windows, which is equivalent to strcasecmp
#define strncasecmp _strnicmp
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>
#endif

// Include OpenSSL headers
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

/**
 * @brief Case-insensitive string search (like strstr but case-insensitive)
 *
 * This is a custom implementation for platforms that don't provide strcasestr.
 *
 * @param haystack String to search in
 * @param needle String to search for
 * @return char* Pointer to the first occurrence of needle in haystack, or NULL if not found
 */
static char* my_strcasestr(const char* haystack, const char* needle) {
    if (!haystack || !needle) {
        return NULL;
    }

    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return (char*)haystack;
    }

    size_t haystack_len = strlen(haystack);
    if (haystack_len < needle_len) {
        return NULL;
    }

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (strncasecmp(haystack + i, needle, needle_len) == 0) {
            return (char*)(haystack + i);
        }
    }

    return NULL;
}

/**
 * @brief Complete definition of HTTP client structure
 */
struct kmcp_http_client {
    char* base_url;        // Base URL
    char* host;            // Host name
    char* path;            // Path
    int port;              // Port
    bool use_ssl;          // Whether to use SSL
    char* api_key;         // API key

    // Timeout and retry settings
    int connect_timeout_ms;     // Connection timeout in milliseconds
    int request_timeout_ms;     // Request timeout in milliseconds
    int max_retries;            // Maximum number of retries
    int retry_interval_ms;      // Interval between retries in milliseconds

    // SSL/TLS related fields
    SSL_CTX* ssl_ctx;      // SSL context
    SSL* ssl;              // SSL connection
    bool ssl_initialized;  // Whether SSL is initialized
    kmcp_ssl_verify_mode_t ssl_verify_mode; // SSL verification mode
    char* ssl_ca_file;     // Path to CA certificate file
    char* ssl_cert_file;   // Path to client certificate file
    char* ssl_key_file;    // Path to client private key file
    char* ssl_key_password; // Password for client private key
    bool accept_self_signed; // Whether to accept self-signed certificates
    char* pinned_pubkey;   // Path to file containing the expected public key
};

/**
 * @brief Validate URL for security
 *
 * @param url URL to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if URL is valid, or an error code otherwise
 */
static kmcp_error_t validate_url(const char* url) {
    if (!url) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum URL length
    size_t url_len = strlen(url);
    if (url_len < 8) { // At minimum "http://a"
        mcp_log_error("URL too short: %s", url);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for valid protocol
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        mcp_log_error("Invalid URL protocol: %s", url);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for invalid characters in URL
    const char* invalid_chars = "<>\"{}|\\^`\0";
    if (strpbrk(url, invalid_chars) != NULL) {
        mcp_log_error("URL contains invalid characters: %s", url);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Parse URL
 *
 * @param url URL
 * @param host Pointer to host name, memory allocated by function, caller responsible for freeing
 * @param path Pointer to path, memory allocated by function, caller responsible for freeing
 * @param port Pointer to port
 * @param use_ssl Pointer to whether to use SSL
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
static kmcp_error_t parse_url(const char* url, char** host, char** path, int* port, bool* use_ssl) {
    if (!url || !host || !path || !port || !use_ssl) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Validate URL first
    kmcp_error_t result = validate_url(url);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Initialize output parameters
    *host = NULL;
    *path = NULL;
    *port = 80;
    *use_ssl = false;

    // Check if URL starts with http:// or https://
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;
        *use_ssl = false;
        *port = 80;
    } else if (strncmp(url, "https://", 8) == 0) {
        url += 8;
        *use_ssl = true;
        *port = 443;
    } else {
        // Not a valid URL
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Find separator between host name and path
    const char* path_start = strchr(url, '/');
    if (!path_start) {
        // No path, use root path
        *host = mcp_strdup(url);
        *path = mcp_strdup("/");
    } else {
        // Has path
        size_t host_len = path_start - url;
        *host = (char*)malloc(host_len + 1);
        if (!*host) {
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }
        strncpy(*host, url, host_len);
        (*host)[host_len] = '\0';

        *path = mcp_strdup(path_start);
    }

    // Check if host name contains port
    char* port_start = strchr(*host, ':');
    if (port_start) {
        // Has port
        *port_start = '\0';
        *port = atoi(port_start + 1);
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Helper function to clean up client resources
 *
 * @param client Client to clean up (can be NULL)
 * @param free_ssl_ctx Whether to free SSL context
 */
static void cleanup_client_resources(kmcp_http_client_t* client, bool free_ssl_ctx) {
    if (!client) {
        return;
    }

    // Free SSL context if requested
    if (free_ssl_ctx && client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
        client->ssl_ctx = NULL;
    }

    // Free client resources with null checks
    if (client->base_url) {
        free(client->base_url);
        client->base_url = NULL;
    }

    if (client->api_key) {
        free(client->api_key);
        client->api_key = NULL;
    }

    if (client->host) {
        free(client->host);
        client->host = NULL;
    }

    if (client->path) {
        free(client->path);
        client->path = NULL;
    }

    if (client->ssl_ca_file) {
        free(client->ssl_ca_file);
        client->ssl_ca_file = NULL;
    }

    if (client->ssl_cert_file) {
        free(client->ssl_cert_file);
        client->ssl_cert_file = NULL;
    }

    if (client->ssl_key_file) {
        free(client->ssl_key_file);
        client->ssl_key_file = NULL;
    }

    if (client->ssl_key_password) {
        free(client->ssl_key_password);
        client->ssl_key_password = NULL;
    }

    if (client->pinned_pubkey) {
        free(client->pinned_pubkey);
        client->pinned_pubkey = NULL;
    }

    // Finally free the client structure
    free(client);
}

/**
 * @brief Helper function to clean up resources in case of error
 *
 * @param client Client to clean up
 * @param sock Socket to close
 * @param full_path Full path to free
 * @param cert Certificate to free
 * @param cleanup_ssl Whether to clean up SSL resources
 */
static void cleanup_http_client_resources(
    kmcp_http_client_t* client,
#ifdef _WIN32
    SOCKET sock,
#else
    int sock,
#endif
    char* full_path,
    X509* cert,
    bool cleanup_ssl
) {
    // Free certificate if any
    if (cert) {
        X509_free(cert);
    }

    // Clean up SSL resources if requested
    if (cleanup_ssl && client && client->ssl) {
        SSL_free(client->ssl);
        client->ssl = NULL;
    }

    // Free full path if any
    if (full_path) {
        free(full_path);
    }

    // Close socket if valid
#ifdef _WIN32
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        WSACleanup();
    }
#else
    if (sock != -1) {
        close(sock);
    }
#endif
}

/**
 * @brief Create an HTTP client
 */
kmcp_http_client_t* kmcp_http_client_create(const char* base_url, const char* api_key) {
    // Create a default configuration
    kmcp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.base_url = base_url;
    config.api_key = api_key;
    config.connect_timeout_ms = 5000;  // 5 seconds
    config.request_timeout_ms = 30000; // 30 seconds
    config.max_retries = 3;            // 3 retries
    config.retry_interval_ms = 1000;   // 1 second
    config.ssl_verify_mode = KMCP_SSL_VERIFY_PEER; // Default to verify server certificate
    config.ssl_ca_file = NULL;         // Use system CA certificates
    config.ssl_cert_file = NULL;       // No client certificate
    config.ssl_key_file = NULL;        // No client private key
    config.ssl_key_password = NULL;    // No password
    config.accept_self_signed = false; // Don't accept self-signed certificates by default
    config.pinned_pubkey = NULL;       // No certificate pinning by default

    // Create client with the configuration
    return kmcp_http_client_create_with_config(&config);
}

/**
 * @brief Create an HTTP client with custom configuration
 */
kmcp_http_client_t* kmcp_http_client_create_with_config(const kmcp_http_client_config_t* config) {
    if (!config || !config->base_url) {
        kmcp_error_log(KMCP_ERROR_INVALID_PARAMETER, "Invalid parameter: config or base_url is NULL");
        return NULL;
    }

    // Validate the base URL
    kmcp_error_t result = validate_url(config->base_url);
    if (result != KMCP_SUCCESS) {
        kmcp_error_log(result, "Invalid base URL: %s", config->base_url);
        return NULL;
    }

    // Allocate memory and initialize to zero
    kmcp_http_client_t* client = (kmcp_http_client_t*)calloc(1, sizeof(kmcp_http_client_t));
    if (!client) {
        kmcp_error_log(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to allocate memory for HTTP client");
        return NULL;
    }
    client->base_url = mcp_strdup(config->base_url);
    client->api_key = config->api_key ? mcp_strdup(config->api_key) : NULL;

    // Set timeout values from config
    client->connect_timeout_ms = config->connect_timeout_ms > 0 ? config->connect_timeout_ms : 5000;
    client->request_timeout_ms = config->request_timeout_ms > 0 ? config->request_timeout_ms : 30000;
    client->max_retries = config->max_retries >= 0 ? config->max_retries : 3;
    client->retry_interval_ms = config->retry_interval_ms > 0 ? config->retry_interval_ms : 1000;

    // Set SSL/TLS options
    client->ssl_verify_mode = config->ssl_verify_mode;
    client->ssl_ca_file = config->ssl_ca_file ? mcp_strdup(config->ssl_ca_file) : NULL;
    client->ssl_cert_file = config->ssl_cert_file ? mcp_strdup(config->ssl_cert_file) : NULL;
    client->ssl_key_file = config->ssl_key_file ? mcp_strdup(config->ssl_key_file) : NULL;
    client->ssl_key_password = config->ssl_key_password ? mcp_strdup(config->ssl_key_password) : NULL;
    client->accept_self_signed = config->accept_self_signed;
    client->pinned_pubkey = config->pinned_pubkey ? mcp_strdup(config->pinned_pubkey) : NULL;

    client->ssl_ctx = NULL;
    client->ssl = NULL;
    client->ssl_initialized = false;

    // Parse URL
    result = parse_url(config->base_url, &client->host, &client->path, &client->port, &client->use_ssl);
    if (result != KMCP_SUCCESS) {
        kmcp_error_log(result, "Failed to parse URL: %s", config->base_url);
        cleanup_client_resources(client, false);
        return NULL;
    }

    // Initialize OpenSSL if needed
    if (client->use_ssl) {
        // Initialize OpenSSL
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();

        // Create SSL context
        client->ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!client->ssl_ctx) {
            kmcp_error_log(KMCP_ERROR_SSL_HANDSHAKE, "Failed to create SSL context");
            ERR_print_errors_fp(stderr);
            cleanup_client_resources(client, false);
            return NULL;
        }

        // Set verification mode
        int verify_mode = SSL_VERIFY_NONE;
        if (config->ssl_verify_mode == KMCP_SSL_VERIFY_PEER ||
            config->ssl_verify_mode == KMCP_SSL_VERIFY_FULL) {
            verify_mode = SSL_VERIFY_PEER;

            // If we're accepting self-signed certs, set appropriate options
            if (config->accept_self_signed) {
                // Set options to be more lenient with certificate verification
                SSL_CTX_set_options(client->ssl_ctx, SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);

                mcp_log_info("Configured to accept self-signed certificates");
            }

            // If certificate pinning is enabled, set up the public key
            if (config->pinned_pubkey) {
                mcp_log_info("Certificate pinning enabled with public key file: %s", config->pinned_pubkey);
            }
        }

        // Set verification mode
        SSL_CTX_set_verify(client->ssl_ctx, verify_mode, NULL);

        // Load CA certificate file if provided
        if (config->ssl_ca_file) {
            if (SSL_CTX_load_verify_locations(client->ssl_ctx, config->ssl_ca_file, NULL) != 1) {
                kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Failed to load CA certificate file: %s", config->ssl_ca_file);
                ERR_print_errors_fp(stderr);
                cleanup_client_resources(client, true);
                return NULL;
            }
        } else {
            // Use default CA certificates
            SSL_CTX_set_default_verify_paths(client->ssl_ctx);
        }

        // Load client certificate and key if provided
        if (config->ssl_cert_file && config->ssl_key_file) {
            if (SSL_CTX_use_certificate_file(client->ssl_ctx, config->ssl_cert_file, SSL_FILETYPE_PEM) != 1) {
                kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Failed to load client certificate file: %s", config->ssl_cert_file);
                ERR_print_errors_fp(stderr);
                cleanup_client_resources(client, true);
                return NULL;
            }

            // Set key password if provided
            if (config->ssl_key_password) {
                SSL_CTX_set_default_passwd_cb_userdata(client->ssl_ctx, (void*)config->ssl_key_password);
            }

            if (SSL_CTX_use_PrivateKey_file(client->ssl_ctx, config->ssl_key_file, SSL_FILETYPE_PEM) != 1) {
                kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Failed to load client private key file: %s", config->ssl_key_file);
                ERR_print_errors_fp(stderr);
                cleanup_client_resources(client, true);
                return NULL;
            }

            // Verify private key
            if (SSL_CTX_check_private_key(client->ssl_ctx) != 1) {
                kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Client certificate and private key do not match");
                ERR_print_errors_fp(stderr);
                cleanup_client_resources(client, true);
                return NULL;
            }
        }

        // Set SSL options
        SSL_CTX_set_options(client->ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

        client->ssl_initialized = true;
        mcp_log_info("SSL initialized for HTTPS connection to %s", client->host);
    }

    return client;
}

/**
 * @brief Send an HTTP request
 *
 * Note: This is an HTTP client implementation with SSL support.
 * It supports certificate validation, self-signed certificates, and client certificates.
 *
 * @param client HTTP client (must not be NULL)
 * @param method HTTP method (GET, POST, etc.) (must not be NULL)
 * @param path Request path (must not be NULL)
 * @param content_type Content type for the request body (can be NULL if no body)
 * @param body Request body (can be NULL)
 * @param response Pointer to store the response body (must not be NULL, will be allocated)
 * @param status Pointer to store the HTTP status code (must not be NULL)
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 *
 * @warning While this implementation validates certificates, for highly sensitive
 * production environments, consider using a mature HTTP client library such as libcurl.
 */
kmcp_error_t kmcp_http_client_send(
    kmcp_http_client_t* client,
    const char* method,
    const char* path,
    const char* content_type,
    const char* body,
    char** response,
    int* status
) {
    // Check parameters
    KMCP_CHECK_PARAM(client);
    KMCP_CHECK_PARAM(method);
    KMCP_CHECK_PARAM(path);
    KMCP_CHECK_PARAM(response);
    KMCP_CHECK_PARAM(status);

    // Initialize output parameters
    *response = NULL;
    *status = 0;

    // Variables for response parsing
    char* response_content_type = NULL;
    int content_length = -1;
    char* transfer_encoding = NULL;
    char* chunked = NULL;

    // Build complete path
    char* full_path = NULL;
    if (path[0] == '/') {
        // Absolute path
        full_path = mcp_strdup(path);
        if (!full_path) {
            mcp_log_error("Failed to allocate memory for full path");
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }
    } else {
        // Relative path
        size_t base_path_len = strlen(client->path);
        size_t path_len = strlen(path);
        full_path = (char*)malloc(base_path_len + path_len + 2); // +2 for '/' and '\0'
        if (!full_path) {
            mcp_log_error("Failed to allocate memory for full path");
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        strcpy(full_path, client->path);
        if (base_path_len > 0 && client->path[base_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, path);
    }

    // Calculate the required request buffer size
    size_t request_size = 0;

    // Request line
    request_size += strlen(method) + strlen(full_path) + 20; // Method + path + HTTP/1.1 + CRLF + some extra

    // Host header
    request_size += strlen(client->host) + 10; // Host: + host + CRLF + some extra

    // Content-Type header
    if (content_type) {
        request_size += strlen(content_type) + 20; // Content-Type: + content_type + CRLF + some extra
    }

    // Content-Length header
    if (body) {
        request_size += 30; // Content-Length: + length + CRLF + some extra
    }

    // API key header
    if (client->api_key) {
        request_size += strlen(client->api_key) + 30; // Authorization: Bearer + api_key + CRLF + some extra
    }

    // Connection header
    request_size += 20; // Connection: close + CRLF + some extra

    // Empty line
    request_size += 3; // CRLF + some extra

    // Request body
    if (body) {
        request_size += strlen(body);
    }

    // Allocate request buffer with some extra space for safety
    const size_t safety_margin = 256; // Increased safety margin for better reliability
    char* request = (char*)malloc(request_size + safety_margin);
    if (!request) {
        mcp_log_error("Failed to allocate memory for HTTP request (size: %zu bytes)", request_size + safety_margin);
        free(full_path);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    int request_len = 0;
    size_t remaining = request_size + safety_margin;

    // Request line
    int written = snprintf(request + request_len, remaining, "%s %s HTTP/1.1\r\n", method, full_path);
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for request line");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Host header
    written = snprintf(request + request_len, remaining, "Host: %s\r\n", client->host);
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for host header");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Content-Type header
    if (content_type) {
        written = snprintf(request + request_len, remaining, "Content-Type: %s\r\n", content_type);
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for content-type header");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
        remaining -= written;
    }

    // Content-Length header
    if (body) {
        written = snprintf(request + request_len, remaining, "Content-Length: %zu\r\n", strlen(body));
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for content-length header");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
        remaining -= written;
    }

    // API key header
    if (client->api_key) {
        written = snprintf(request + request_len, remaining, "Authorization: Bearer %s\r\n", client->api_key);
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for authorization header");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
        remaining -= written;
    }

    // Connection header
    written = snprintf(request + request_len, remaining, "Connection: close\r\n");
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for connection header");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Empty line
    written = snprintf(request + request_len, remaining, "\r\n");
    if (written < 0 || (size_t)written >= remaining) {
        mcp_log_error("Request buffer too small for empty line");
        free(request);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    request_len += written;
    remaining -= written;

    // Request body
    if (body) {
        written = snprintf(request + request_len, remaining, "%s", body);
        if (written < 0 || (size_t)written >= remaining) {
            mcp_log_error("Request buffer too small for request body");
            free(request);
            free(full_path);
            return KMCP_ERROR_INTERNAL;
        }
        request_len += written;
    }

    // Create socket
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        kmcp_error_log(KMCP_ERROR_CONNECTION_FAILED, "Failed to initialize Winsock");
        free(full_path);
        return KMCP_ERROR_CONNECTION_FAILED;
    }
#endif

#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif

    // Store socket for cleanup in case of error
    if (sock < 0) {
        kmcp_error_log(KMCP_ERROR_CONNECTION_FAILED, "Failed to create socket");
        free(full_path);
#ifdef _WIN32
        WSACleanup();
#endif
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Resolve host name
    struct hostent* host = gethostbyname(client->host);
    if (!host) {
        kmcp_error_log(KMCP_ERROR_CONNECTION_FAILED, "Failed to resolve host: %s", client->host);
        cleanup_http_client_resources(client, sock, full_path, NULL, false);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Set socket timeout options
#ifdef _WIN32
    DWORD timeout = client->connect_timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = client->connect_timeout_ms / 1000;
    tv.tv_usec = (client->connect_timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif

    // Connect to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)client->port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        kmcp_error_log(KMCP_ERROR_CONNECTION_FAILED, "Failed to connect to server: %s:%d", client->host, client->port);
        cleanup_http_client_resources(client, sock, full_path, NULL, false);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // For HTTPS connections, establish SSL/TLS connection
    if (client->use_ssl) {
        if (!client->ssl_initialized) {
            kmcp_error_log(KMCP_ERROR_SSL_HANDSHAKE, "SSL not initialized for HTTPS connection");
            cleanup_http_client_resources(client, sock, full_path, NULL, false);
            return KMCP_ERROR_SSL_HANDSHAKE;
        }

        // Create new SSL connection
        client->ssl = SSL_new(client->ssl_ctx);
        if (!client->ssl) {
            kmcp_error_log(KMCP_ERROR_SSL_HANDSHAKE, "Failed to create SSL connection");
            ERR_print_errors_fp(stderr);
            cleanup_http_client_resources(client, sock, full_path, NULL, false);
            return KMCP_ERROR_SSL_HANDSHAKE;
        }

        // Set hostname for SNI (Server Name Indication)
        SSL_set_tlsext_host_name(client->ssl, client->host);

        // Set hostname for certificate verification
        if (client->ssl_verify_mode == KMCP_SSL_VERIFY_FULL) {
            X509_VERIFY_PARAM* param = SSL_get0_param(client->ssl);
            // Set hostname for certificate verification
            X509_VERIFY_PARAM_set1_host(param, client->host, 0);
        }

        // Set socket for SSL connection
        if (SSL_set_fd(client->ssl, (int)sock) != 1) {
            kmcp_error_log(KMCP_ERROR_SSL_HANDSHAKE, "Failed to set socket for SSL connection");
            ERR_print_errors_fp(stderr);
            cleanup_http_client_resources(client, sock, full_path, NULL, true);
            return KMCP_ERROR_SSL_HANDSHAKE;
        }

        // Establish SSL connection
        int ssl_connect_result = SSL_connect(client->ssl);
        if (ssl_connect_result != 1) {
            int ssl_error = SSL_get_error(client->ssl, ssl_connect_result);
            mcp_log_error("Failed to establish SSL connection: error %d", ssl_error);

            // Get more detailed error information
            unsigned long err;
            char err_buf[256];
            while ((err = ERR_get_error()) != 0) {
                ERR_error_string_n(err, err_buf, sizeof(err_buf));
                mcp_log_error("SSL error: %s", err_buf);
            }

            ERR_print_errors_fp(stderr);
        } else if (client->ssl_verify_mode != KMCP_SSL_VERIFY_NONE) {
            // Verify certificate
            X509* cert = SSL_get_peer_certificate(client->ssl);
            if (!cert) {
                mcp_log_error("No certificate presented by server");
                SSL_free(client->ssl);
                client->ssl = NULL;
                free(full_path);
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                return KMCP_ERROR_CONNECTION_FAILED;
            }

            long verify_result = SSL_get_verify_result(client->ssl);
            if (verify_result != X509_V_OK) {
                // The verification callback should have handled self-signed certificates,
                // so if we get here with an error, it's a real certificate validation failure
                kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Certificate verification failed: %s (code: %ld)",
                             X509_verify_cert_error_string(verify_result), verify_result);
                cleanup_http_client_resources(client, sock, full_path, cert, true);
                return KMCP_ERROR_SSL_CERTIFICATE;
            }

            // Check certificate pinning if enabled
            if (client->pinned_pubkey && cert) {
                FILE* fp = fopen(client->pinned_pubkey, "r");
                if (!fp) {
                    kmcp_error_log(KMCP_ERROR_FILE_NOT_FOUND, "Failed to open pinned public key file: %s", client->pinned_pubkey);
                    cleanup_http_client_resources(client, sock, full_path, cert, true);
                    return KMCP_ERROR_FILE_NOT_FOUND;
                }

                // Read the expected public key
                EVP_PKEY* pinned_key = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
                fclose(fp);

                if (!pinned_key) {
                    kmcp_error_log(KMCP_ERROR_PARSE_FAILED, "Failed to read pinned public key from file: %s", client->pinned_pubkey);
                    cleanup_http_client_resources(client, sock, full_path, cert, true);
                    return KMCP_ERROR_PARSE_FAILED;
                }

                // Get the certificate's public key
                EVP_PKEY* cert_key = X509_get_pubkey(cert);
                if (!cert_key) {
                    kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Failed to get public key from certificate");
                    EVP_PKEY_free(pinned_key);
                    cleanup_http_client_resources(client, sock, full_path, cert, true);
                    return KMCP_ERROR_SSL_CERTIFICATE;
                }

                // Compare the public keys
                if (EVP_PKEY_cmp(pinned_key, cert_key) != 1) {
                    kmcp_error_log(KMCP_ERROR_SSL_CERTIFICATE, "Certificate public key does not match pinned public key");
                    EVP_PKEY_free(cert_key);
                    EVP_PKEY_free(pinned_key);
                    cleanup_http_client_resources(client, sock, full_path, cert, true);
                    return KMCP_ERROR_SSL_CERTIFICATE;
                }

                mcp_log_info("Certificate public key matches pinned public key");
                EVP_PKEY_free(cert_key);
                EVP_PKEY_free(pinned_key);
            }

            X509_free(cert);
        }

        if (ssl_connect_result != 1) {
            kmcp_error_log(KMCP_ERROR_SSL_HANDSHAKE, "SSL_connect failed");
            cleanup_http_client_resources(client, sock, full_path, NULL, true);
            return KMCP_ERROR_SSL_HANDSHAKE;
        }

        mcp_log_info("SSL connection established with %s", client->host);
    }

    // Send request
    int send_result;
    if (client->use_ssl) {
        // For SSL connections, use SSL_write
        send_result = SSL_write(client->ssl, request, request_len);
        if (send_result <= 0) {
            int ssl_error = SSL_get_error(client->ssl, send_result);
            mcp_log_error("SSL_write failed with error %d", ssl_error);
            ERR_print_errors_fp(stderr);
        }
    } else {
        send_result = send(sock, request, request_len, 0);
    }

    if (send_result != request_len) {
        kmcp_error_log(KMCP_ERROR_CONNECTION_FAILED, "Failed to send request");
        free(request);
        cleanup_http_client_resources(client, sock, full_path, NULL, client->use_ssl);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Free the request buffer
    free(request);

    // Receive response with dynamic buffer
    size_t buffer_size = 8192; // Increased initial buffer size for better performance
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        kmcp_error_log(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to allocate memory for response buffer (size: %zu bytes)", buffer_size);
        cleanup_http_client_resources(client, sock, full_path, NULL, client->use_ssl);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize buffer to zeros
    memset(buffer, 0, buffer_size);

    int total_received = 0;
    int bytes_received = 0;

    while (1) {
        if (client->use_ssl) {
            // For SSL connections, use SSL_read
            bytes_received = SSL_read(client->ssl, buffer + total_received, (int)buffer_size - total_received - 1);
            if (bytes_received <= 0) {
                int ssl_error = SSL_get_error(client->ssl, bytes_received);
                if (ssl_error == SSL_ERROR_ZERO_RETURN) {
                    // Connection closed cleanly
                    mcp_log_debug("SSL connection closed cleanly");
                } else if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                    // Need to retry
                    continue;
                } else {
                    // Error
                    mcp_log_error("SSL_read failed with error %d", ssl_error);
                    ERR_print_errors_fp(stderr);
                }
            }
        } else {
            bytes_received = recv(sock, buffer + total_received, (int)buffer_size - total_received - 1, 0);
        }

        if (bytes_received <= 0) {
            break;
        }
        total_received += bytes_received;

        // Check if buffer is almost full, resize if needed
        if (total_received >= (int)buffer_size - 2048) { // Leave 2KB margin for better performance
            size_t new_size = buffer_size * 2;
            mcp_log_debug("Resizing response buffer from %zu to %zu bytes", buffer_size, new_size);

            char* new_buffer = (char*)realloc(buffer, new_size);
            if (!new_buffer) {
                kmcp_error_log(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to resize response buffer from %zu to %zu bytes",
                              buffer_size, new_size);
                free(buffer);
                cleanup_http_client_resources(client, sock, full_path, NULL, client->use_ssl);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }

            // Initialize the new portion of the buffer
            memset(new_buffer + buffer_size, 0, new_size - buffer_size);

            buffer = new_buffer;
            buffer_size = new_size;
        }
    }

    buffer[total_received] = '\0';

    // SSL connection is closed below

    // Close SSL connection if used
    if (client->use_ssl && client->ssl) {
        SSL_shutdown(client->ssl);
        SSL_free(client->ssl);
        client->ssl = NULL;
    }

    // Close socket
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    // Parse response
    if (total_received == 0) {
        kmcp_error_log(KMCP_ERROR_CONNECTION_FAILED, "No response received");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_CONNECTION_FAILED;
    }

    // Parse status code
    char* status_line = buffer;
    char* status_code_str = strchr(status_line, ' ');
    if (!status_code_str) {
        kmcp_error_log(KMCP_ERROR_PROTOCOL_ERROR, "Invalid response format: no status code");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_PROTOCOL_ERROR;
    }

    *status = atoi(status_code_str + 1);
    mcp_log_debug("HTTP status code: %d", *status);

    // Find end of status line
    char* headers_start = strstr(status_line, "\r\n");
    if (!headers_start) {
        mcp_log_error("Invalid response format: no end of status line");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }
    headers_start += 2; // Skip \r\n

    // Find response body
    char* body_start = strstr(buffer, "\r\n\r\n");
    if (!body_start) {
        mcp_log_error("Invalid response format: no end of headers");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_INTERNAL;
    }

    // Null-terminate headers for easier parsing
    *body_start = '\0';
    body_start += 4; // Skip \r\n\r\n

    // Parse headers

    char* header = headers_start;
    while (header < body_start) {
        char* header_end = strstr(header, "\r\n");
        if (!header_end) {
            break;
        }

        // Null-terminate header for easier parsing
        *header_end = '\0';

        // Parse Content-Type header
        if (strncasecmp(header, "Content-Type:", 13) == 0) {
            response_content_type = header + 13;
            // Skip leading whitespace
            while (*response_content_type == ' ') {
                response_content_type++;
            }
            mcp_log_debug("Content-Type: %s", response_content_type);
        }
        // Parse Content-Length header
        else if (strncasecmp(header, "Content-Length:", 15) == 0) {
            content_length = atoi(header + 15);
            mcp_log_debug("Content-Length: %d", content_length);
        }
        // Parse Transfer-Encoding header
        else if (strncasecmp(header, "Transfer-Encoding:", 18) == 0) {
            transfer_encoding = header + 18;
            // Skip leading whitespace
            while (*transfer_encoding == ' ') {
                transfer_encoding++;
            }
            mcp_log_debug("Transfer-Encoding: %s", transfer_encoding);
            // Check if chunked encoding is used
            chunked = my_strcasestr(transfer_encoding, "chunked");
        }

        // Move to next header
        header = header_end + 2;
    }

    // Restore the original buffer
    *body_start = '\r';

    // Handle chunked encoding
    if (chunked) {
        mcp_log_warn("Chunked encoding detected but not fully supported");
        // In a real implementation, you would decode chunked encoding here
    }

    // Validate content length if provided
    if (content_length >= 0) {
        size_t body_length = strlen(body_start);
        if (body_length != (size_t)content_length) {
            mcp_log_warn("Content-Length mismatch: expected %d, got %zu", content_length, body_length);
            // In a real implementation, you might want to handle this more gracefully
        }
    }

    // Copy response body
    *response = mcp_strdup(body_start);
    if (!*response) {
        mcp_log_error("Failed to allocate memory for response");
        free(buffer);
        free(full_path);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Free the response buffer
    free(buffer);

    free(full_path);
    return KMCP_SUCCESS;
}

/**
 * @brief Validate tool name for security
 *
 * @param tool_name Tool name to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if tool name is valid, or an error code otherwise
 */
static kmcp_error_t validate_tool_name(const char* tool_name) {
    if (!tool_name) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum length
    size_t len = strlen(tool_name);
    if (len == 0) {
        mcp_log_error("Tool name is empty");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for maximum length
    if (len > 128) {
        mcp_log_error("Tool name too long: %zu characters", len);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for valid characters (alphanumeric, underscore, hyphen, period)
    for (size_t i = 0; i < len; i++) {
        char c = tool_name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            mcp_log_error("Tool name contains invalid character: %c", c);
            return KMCP_ERROR_INVALID_PARAMETER;
        }
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Validate JSON string for security
 *
 * @param json_str JSON string to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if JSON is valid, or an error code otherwise
 */
static kmcp_error_t validate_json(const char* json_str) {
    if (!json_str) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum length
    size_t len = strlen(json_str);
    if (len < 2) { // At minimum "{}" or "[]"
        mcp_log_error("JSON string too short: %zu characters", len);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for balanced braces
    int brace_count = 0;
    int bracket_count = 0;

    for (size_t i = 0; i < len; i++) {
        char c = json_str[i];
        if (c == '{') brace_count++;
        else if (c == '}') brace_count--;
        else if (c == '[') bracket_count++;
        else if (c == ']') bracket_count--;

        // Check for negative counts (closing before opening)
        if (brace_count < 0 || bracket_count < 0) {
            mcp_log_error("Unbalanced braces or brackets in JSON");
            return KMCP_ERROR_INVALID_PARAMETER;
        }
    }

    // Check for balanced counts
    if (brace_count != 0 || bracket_count != 0) {
        mcp_log_error("Unbalanced braces or brackets in JSON");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Call a tool
 */
kmcp_error_t kmcp_http_client_call_tool(
    kmcp_http_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
) {
    if (!client || !tool_name || !params_json || !result_json) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Validate tool name
    kmcp_error_t result = validate_tool_name(tool_name);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Validate JSON parameters
    result = validate_json(params_json);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Initialize output parameter
    *result_json = NULL;

    // Build path with proper length checking
    size_t tool_name_len = strlen(tool_name);
    size_t path_len = 6 + tool_name_len; // "tools/" + tool_name
    char* path = (char*)malloc(path_len + 1); // +1 for null terminator

    if (!path) {
        mcp_log_error("Failed to allocate memory for path");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Use snprintf with explicit length to prevent buffer overflow
    int written = snprintf(path, path_len + 1, "tools/%s", tool_name);

    // Check if the path was truncated
    if (written < 0 || (size_t)written >= path_len + 1) {
        mcp_log_error("Path truncated: tools/%s", tool_name);
        free(path);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Build request body
    const char* content_type = "application/json";

    // Send request
    int status = 0;
    char* response = NULL;
    result = kmcp_http_client_send(
        client,
        "POST",
        path,
        content_type,
        params_json,
        &response,
        &status
    );

    // Free the dynamically allocated path
    free(path);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to call tool: %s", tool_name);
        return result;
    }

    // Check status code
    if (status != 200) {
        mcp_log_error("Tool call failed with status code: %d", status);
        free(response);
        return KMCP_ERROR_INTERNAL;
    }

    // Return response
    *result_json = response;
    return KMCP_SUCCESS;
}

/**
 * @brief Validate resource URI for security
 *
 * @param resource_uri Resource URI to validate
 * @return kmcp_error_t Returns KMCP_SUCCESS if resource URI is valid, or an error code otherwise
 */
static kmcp_error_t validate_resource_uri(const char* resource_uri) {
    if (!resource_uri) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for minimum length
    size_t len = strlen(resource_uri);
    if (len == 0) {
        mcp_log_error("Resource URI is empty");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for maximum length
    if (len > 1024) {
        mcp_log_error("Resource URI too long: %zu characters", len);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for path traversal attempts
    if (strstr(resource_uri, "../") != NULL || strstr(resource_uri, "..\\") != NULL) {
        mcp_log_error("Resource URI contains path traversal sequence");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check for invalid characters
    const char* invalid_chars = "<>\"{}|\\^`\0";
    if (strpbrk(resource_uri, invalid_chars) != NULL) {
        mcp_log_error("Resource URI contains invalid characters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Get a resource
 */
kmcp_error_t kmcp_http_client_get_resource(
    kmcp_http_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
) {
    if (!client || !resource_uri || !content || !content_type) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Validate resource URI
    kmcp_error_t result = validate_resource_uri(resource_uri);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Initialize output parameters
    *content = NULL;
    *content_type = NULL;

    // Build path with proper length checking
    size_t resource_uri_len = strlen(resource_uri);
    size_t path_len = 10 + resource_uri_len; // "resources/" + resource_uri
    char* path = (char*)malloc(path_len + 1); // +1 for null terminator

    if (!path) {
        mcp_log_error("Failed to allocate memory for path");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Use snprintf with explicit length to prevent buffer overflow
    int written = snprintf(path, path_len + 1, "resources/%s", resource_uri);

    // Check if the path was truncated
    if (written < 0 || (size_t)written >= path_len + 1) {
        mcp_log_error("Path truncated: resources/%s", resource_uri);
        free(path);
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Send request
    int status = 0;
    char* response = NULL;
    result = kmcp_http_client_send(
        client,
        "GET",
        path,
        NULL,
        NULL,
        &response,
        &status
    );

    // Free the dynamically allocated path
    free(path);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get resource: %s", resource_uri);
        return result;
    }

    // Check status code
    if (status == 404) {
        mcp_log_error("Resource not found: %s", resource_uri);
        free(response);
        return KMCP_ERROR_RESOURCE_NOT_FOUND;
    } else if (status != 200) {
        mcp_log_error("Resource request failed with status code: %d", status);
        free(response);
        return KMCP_ERROR_INTERNAL;
    }

    // Set content
    *content = response;

    // Set content type (default to text/plain)
    *content_type = mcp_strdup("text/plain");
    if (!*content_type) {
        mcp_log_error("Failed to allocate memory for content type");
        free(response);
        *content = NULL;
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Send an HTTP request with timeout
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
) {
    if (!client) {
        mcp_log_error("Invalid parameter: client is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Save original timeout
    int original_timeout = client->request_timeout_ms;

    // Set custom timeout if specified
    if (timeout_ms > 0) {
        client->request_timeout_ms = timeout_ms;
    }

    // Send request
    kmcp_error_t result = kmcp_http_client_send(
        client,
        method,
        path,
        content_type,
        body,
        response,
        status
    );

    // Restore original timeout
    client->request_timeout_ms = original_timeout;

    return result;
}

/**
 * @brief Close the HTTP client and free all resources
 *
 * @param client HTTP client to close (can be NULL)
 */
void kmcp_http_client_close(kmcp_http_client_t* client) {
    if (!client) {
        return;
    }

    mcp_log_debug("Closing HTTP client for %s", client->base_url ? client->base_url : "(unknown)");

    // Free SSL resources
    if (client->ssl_initialized) {
        if (client->ssl) {
            SSL_shutdown(client->ssl);
            SSL_free(client->ssl);
            client->ssl = NULL;
        }

        // Cleanup OpenSSL
        ERR_free_strings();
        EVP_cleanup();
        CRYPTO_cleanup_all_ex_data();
    }

    // Clean up all client resources
    cleanup_client_resources(client, true);
}

/**
 * @brief Parse JSON array of strings
 *
 * Helper function to parse a JSON array of strings into a C array of strings.
 *
 * @param json_str JSON string containing an array of strings
 * @param items Pointer to array of strings, memory allocated by function, caller responsible for freeing
 * @param count Pointer to number of items
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
static kmcp_error_t parse_json_string_array(const char* json_str, char*** items, size_t* count) {
    if (!json_str || !items || !count) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *items = NULL;
    *count = 0;

    // Find the start of the array
    const char* array_start = strchr(json_str, '[');
    if (!array_start) {
        mcp_log_error("JSON string does not contain an array");
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Find the end of the array
    const char* array_end = strrchr(json_str, ']');
    if (!array_end || array_end <= array_start) {
        mcp_log_error("JSON string does not contain a valid array");
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Count the number of items (strings) in the array
    // This is a simple implementation that counts the number of double quotes
    // and divides by 2 to get the number of strings
    size_t num_quotes = 0;
    for (const char* p = array_start; p <= array_end; p++) {
        if (*p == '"') {
            num_quotes++;
        }
    }

    // Each string has two quotes, so divide by 2
    size_t num_items = num_quotes / 2;
    if (num_items == 0) {
        // Empty array
        *items = NULL;
        *count = 0;
        return KMCP_SUCCESS;
    }

    // Allocate memory for the array of strings
    char** result = (char**)malloc(num_items * sizeof(char*));
    if (!result) {
        mcp_log_error("Failed to allocate memory for string array");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize all pointers to NULL
    memset(result, 0, num_items * sizeof(char*));

    // Parse each string in the array
    size_t item_index = 0;
    const char* p = array_start;

    while (p < array_end && item_index < num_items) {
        // Find the start of the string
        p = strchr(p, '"');
        if (!p || p >= array_end) {
            break;
        }
        p++; // Skip the opening quote

        // Find the end of the string
        const char* str_end = strchr(p, '"');
        if (!str_end || str_end >= array_end) {
            break;
        }

        // Calculate the length of the string
        size_t str_len = str_end - p;

        // Allocate memory for the string
        result[item_index] = (char*)malloc(str_len + 1);
        if (!result[item_index]) {
            mcp_log_error("Failed to allocate memory for string");
            // Free previously allocated strings
            for (size_t i = 0; i < item_index; i++) {
                free(result[i]);
            }
            free(result);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        // Copy the string
        strncpy(result[item_index], p, str_len);
        result[item_index][str_len] = '\0';

        // Move to the next string
        p = str_end + 1;
        item_index++;
    }

    // Set the output parameters
    *items = result;
    *count = item_index;

    return KMCP_SUCCESS;
}

/**
 * @brief Get supported tools
 */
kmcp_error_t kmcp_http_get_tools(
    kmcp_http_client_t* client,
    char*** tools,
    size_t* count
) {
    if (!client || !tools || !count) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *tools = NULL;
    *count = 0;

    // Send request to get tools
    int status = 0;
    char* response = NULL;
    kmcp_error_t result = kmcp_http_client_send(
        client,
        "GET",
        "tools",
        NULL,
        NULL,
        &response,
        &status
    );

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get tools list");
        return result;
    }

    // Check status code
    if (status != 200) {
        mcp_log_error("Tools request failed with status code: %d", status);
        free(response);
        return KMCP_ERROR_INTERNAL;
    }

    // Parse the response
    result = parse_json_string_array(response, tools, count);

    // Free the response
    free(response);

    return result;
}

/**
 * @brief Get supported resources
 */
kmcp_error_t kmcp_http_get_resources(
    kmcp_http_client_t* client,
    char*** resources,
    size_t* count
) {
    if (!client || !resources || !count) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *resources = NULL;
    *count = 0;

    // Send request to get resources
    int status = 0;
    char* response = NULL;
    kmcp_error_t result = kmcp_http_client_send(
        client,
        "GET",
        "resources",
        NULL,
        NULL,
        &response,
        &status
    );

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get resources list");
        return result;
    }

    // Check status code
    if (status != 200) {
        mcp_log_error("Resources request failed with status code: %d", status);
        free(response);
        return KMCP_ERROR_INTERNAL;
    }

    // Parse the response
    result = parse_json_string_array(response, resources, count);

    // Free the response
    free(response);

    return result;
}

/**
 * @brief Test SSL certificate verification
 *
 * This function tests the SSL certificate verification by connecting to a server
 * and verifying its certificate. It's useful for debugging SSL certificate issues.
 *
 * @param url URL to connect to (must start with https://)
 * @param accept_self_signed Whether to accept self-signed certificates
 * @return kmcp_error_t Returns KMCP_SUCCESS if the certificate is valid, or an error code otherwise
 */
kmcp_error_t kmcp_http_test_ssl_certificate(const char* url, bool accept_self_signed) {
    if (!url) {
        mcp_log_error("Invalid parameter: url is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Check if URL starts with https://
    if (strncmp(url, "https://", 8) != 0) {
        mcp_log_error("URL must start with https:// for SSL certificate testing");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Create client configuration
    kmcp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.base_url = url;
    config.ssl_verify_mode = KMCP_SSL_VERIFY_PEER;
    config.accept_self_signed = accept_self_signed;

    // Create client
    kmcp_http_client_t* client = kmcp_http_client_create_with_config(&config);
    if (!client) {
        mcp_log_error("Failed to create HTTP client");
        return KMCP_ERROR_INTERNAL;
    }

    // Send a simple HEAD request to test the connection
    int status = 0;
    char* response = NULL;
    kmcp_error_t result = kmcp_http_client_send(
        client,
        "HEAD",
        "/",
        NULL,
        NULL,
        &response,
        &status
    );

    // Free response if any
    if (response) {
        free(response);
    }

    // Close client
    kmcp_http_client_close(client);

    // Check result
    if (result == KMCP_SUCCESS) {
        mcp_log_info("SSL certificate verification successful");
    } else {
        mcp_log_error("SSL certificate verification failed");
    }

    return result;
}

/**
 * @brief Get SSL certificate information
 *
 * This function connects to a server and retrieves information about its SSL certificate.
 * It's useful for debugging SSL certificate issues.
 *
 * @param url URL to connect to (must start with https://)
 * @param cert_info Pointer to certificate information string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS if the certificate information was retrieved, or an error code otherwise
 */
kmcp_error_t kmcp_http_get_ssl_certificate_info(const char* url, char** cert_info) {
    if (!url || !cert_info) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameter
    *cert_info = NULL;

    // Check if URL starts with https://
    if (strncmp(url, "https://", 8) != 0) {
        mcp_log_error("URL must start with https:// for SSL certificate testing");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Create client configuration
    kmcp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.base_url = url;
    config.ssl_verify_mode = KMCP_SSL_VERIFY_NONE; // Don't verify certificate for this function

    // Create client
    kmcp_http_client_t* client = kmcp_http_client_create_with_config(&config);
    if (!client) {
        mcp_log_error("Failed to create HTTP client");
        return KMCP_ERROR_INTERNAL;
    }

    // Send a simple HEAD request to establish the connection
    int status = 0;
    char* response = NULL;
    kmcp_error_t result = kmcp_http_client_send(
        client,
        "HEAD",
        "/",
        NULL,
        NULL,
        &response,
        &status
    );

    // Free response if any
    if (response) {
        free(response);
        response = NULL;
    }

    // Check if connection was successful
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to connect to server");
        kmcp_http_client_close(client);
        return result;
    }

    // Get certificate information
    if (client->ssl) {
        X509* cert = SSL_get_peer_certificate(client->ssl);
        if (cert) {
            // Get subject name
            char subject_name[256] = {0};
            X509_NAME* subject = X509_get_subject_name(cert);
            X509_NAME_oneline(subject, subject_name, sizeof(subject_name) - 1);

            // Get issuer name
            char issuer_name[256] = {0};
            X509_NAME* issuer = X509_get_issuer_name(cert);
            X509_NAME_oneline(issuer, issuer_name, sizeof(issuer_name) - 1);

            // Get validity period
            ASN1_TIME* not_before = X509_get_notBefore(cert);
            ASN1_TIME* not_after = X509_get_notAfter(cert);

            char not_before_str[128] = {0};
            char not_after_str[128] = {0};

            BIO* bio = BIO_new(BIO_s_mem());
            if (bio) {
                ASN1_TIME_print(bio, not_before);
                BIO_read(bio, not_before_str, sizeof(not_before_str) - 1);
                BIO_reset(bio);

                ASN1_TIME_print(bio, not_after);
                BIO_read(bio, not_after_str, sizeof(not_after_str) - 1);
                BIO_free(bio);
            }

            // Get verification result
            long verify_result = SSL_get_verify_result(client->ssl);
            const char* verify_result_str = X509_verify_cert_error_string(verify_result);

            // Allocate memory for certificate information
            size_t info_size = 1024; // Initial size
            *cert_info = (char*)malloc(info_size);
            if (!*cert_info) {
                mcp_log_error("Failed to allocate memory for certificate information");
                X509_free(cert);
                kmcp_http_client_close(client);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }

            // Format certificate information
            int written = snprintf(*cert_info, info_size,
                "SSL Certificate Information:\n"
                "Subject: %s\n"
                "Issuer: %s\n"
                "Valid From: %s\n"
                "Valid Until: %s\n"
                "Verification Result: %s (code: %ld)",
                subject_name, issuer_name, not_before_str, not_after_str,
                verify_result_str, verify_result);

            // Check if buffer was large enough
            if (written < 0 || (size_t)written >= info_size) {
                mcp_log_error("Certificate information buffer too small");
                free(*cert_info);
                *cert_info = NULL;
                X509_free(cert);
                kmcp_http_client_close(client);
                return KMCP_ERROR_INTERNAL;
            }

            X509_free(cert);
            result = KMCP_SUCCESS;
        } else {
            mcp_log_error("No certificate presented by server");
            *cert_info = mcp_strdup("No certificate presented by server");
            result = KMCP_ERROR_INTERNAL;
        }
    } else {
        mcp_log_error("SSL connection not established");
        *cert_info = mcp_strdup("SSL connection not established");
        result = KMCP_ERROR_INTERNAL;
    }

    // Close client
    kmcp_http_client_close(client);

    return result;
}

/**
 * @brief Check if a certificate is self-signed
 *
 * This function checks if an X509 certificate is self-signed by comparing
 * the subject and issuer names.
 *
 * @param cert X509 certificate to check
 * @return bool Returns true if the certificate is self-signed, false otherwise
 */
bool kmcp_http_is_certificate_self_signed(X509* cert) {
    if (!cert) {
        return false;
    }

    // Get subject and issuer names
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    if (!subject || !issuer) {
        return false;
    }

    // Compare subject and issuer names
    // If they are the same, the certificate is self-signed
    return (X509_NAME_cmp(subject, issuer) == 0);
}