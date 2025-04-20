/**
 * @file kmcp_ssl_example.c
 * @brief Example program demonstrating SSL certificate handling in KMCP HTTP client
 */

#include "kmcp_http_client.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Check command line arguments
    if (argc < 2) {
        printf("Usage: %s <https_url> [accept_self_signed] [pinned_pubkey_file]\n", argv[0]);
        printf("  https_url: URL to connect to (must start with https://)\n");
        printf("  accept_self_signed: 1 to accept self-signed certificates, 0 otherwise (default: 0)\n");
        printf("  pinned_pubkey_file: Path to file containing the expected public key for certificate pinning (optional)\n");
        return 1;
    }

    const char* url = argv[1];
    bool accept_self_signed = (argc > 2 && strcmp(argv[2], "1") == 0);
    const char* pinned_pubkey = (argc > 3) ? argv[3] : NULL;

    printf("Testing SSL connection to %s\n", url);
    printf("Accept self-signed certificates: %s\n", accept_self_signed ? "Yes" : "No");
    printf("Certificate pinning: %s\n", pinned_pubkey ? pinned_pubkey : "Disabled");

    // Test SSL certificate verification
    kmcp_error_t result = kmcp_http_test_ssl_certificate(url, accept_self_signed);
    if (result == KMCP_SUCCESS) {
        printf("SSL certificate verification successful\n");
    } else {
        printf("SSL certificate verification failed with error code: %d\n", result);
    }

    // Get SSL certificate information
    char* cert_info = NULL;
    result = kmcp_http_get_ssl_certificate_info(url, &cert_info);
    if (result == KMCP_SUCCESS && cert_info) {
        printf("\nCertificate Information:\n%s\n", cert_info);
    } else {
        printf("\nFailed to get certificate information: %d\n", result);
    }

    // Free certificate information
    if (cert_info) {
        free(cert_info);
    }

    // Create HTTP client with custom configuration
    printf("\nCreating HTTP client with custom configuration...\n");
    kmcp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.base_url = url;
    config.ssl_verify_mode = KMCP_SSL_VERIFY_PEER;
    config.accept_self_signed = accept_self_signed;
    config.pinned_pubkey = pinned_pubkey;

    kmcp_http_client_t* client = kmcp_http_client_create_with_config(&config);
    if (!client) {
        printf("Failed to create HTTP client\n");
        return 1;
    }

    // Send a simple GET request
    printf("Sending GET request to %s...\n", url);
    int status = 0;
    char* response = NULL;
    result = kmcp_http_client_send(
        client,
        "GET",
        "/",
        NULL,
        NULL,
        &response,
        &status
    );

    if (result == KMCP_SUCCESS) {
        printf("Request successful, status code: %d\n", status);
        printf("Response (first 100 chars): %.100s%s\n",
               response, strlen(response) > 100 ? "..." : "");
    } else {
        printf("Request failed with error code: %d\n", result);
    }

    // Free response
    if (response) {
        free(response);
    }

    // Close client
    kmcp_http_client_close(client);

    printf("\nSSL certificate handling example completed\n");
    return 0;
}
