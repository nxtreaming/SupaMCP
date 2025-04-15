#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_client.h"
#include <mcp_tcp_pool_transport.h>
#include <mcp_log.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(seconds) Sleep((seconds) * 1000)
#else
#include <unistd.h>
#endif

// Helper function to send a request and print the response
int send_request(mcp_client_t* client, const char* method, const char* params) {
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    
    printf("Sending %s request...\n", method);
    int ret = mcp_client_send_request(client, method, params, &result, &error_code, &error_message);
    
    if (ret != 0) {
        printf("Failed to send request: %d\n", ret);
        return -1;
    }
    
    if (error_code != 0) {
        printf("Error in response: %d - %s\n", error_code, error_message ? error_message : "Unknown error");
        free(error_message);
        free(result);
        return -1;
    }
    
    if (result) {
        printf("Received response: %s\n", result);
        free(result);
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    
    // Parse command line arguments
    const char* host = "localhost";
    uint16_t port = 8080;
    
    if (argc > 1) {
        host = argv[1];
    }
    
    if (argc > 2) {
        port = (uint16_t)atoi(argv[2]);
    }
    
    printf("Connecting to %s:%u with connection pool\n", host, port);
    
    // Create transport with connection pool
    mcp_transport_t* transport = mcp_tcp_pool_transport_create(
        host,           // Host
        port,           // Port
        2,              // Min connections
        10,             // Max connections
        30000,          // Idle timeout (30 seconds)
        5000,           // Connect timeout (5 seconds)
        10000           // Request timeout (10 seconds)
    );
    
    if (!transport) {
        printf("Failed to create transport\n");
        return 1;
    }
    
    // Create client configuration
    mcp_client_config_t config = {
        .request_timeout_ms = 5000 // 5 seconds timeout
    };
    
    // Create client
    mcp_client_t* client = mcp_client_create(&config, transport);
    if (!client) {
        printf("Failed to create client\n");
        mcp_transport_destroy(transport);
        return 1;
    }
    
    // Wait a moment for connection
    sleep(1);
    
    // Send multiple requests to demonstrate connection pooling
    for (int i = 0; i < 5; i++) {
        // Send ping request
        send_request(client, "ping", NULL);
        
        // Send echo request
        char params[100];
        snprintf(params, sizeof(params), "{\"message\":\"Hello, world! (request %d)\"}", i + 1);
        send_request(client, "echo", params);
        
        // Small delay between requests
        sleep(1);
    }
    
    // Clean up
    mcp_client_destroy(client);
    
    printf("Example completed\n");
    return 0;
}
