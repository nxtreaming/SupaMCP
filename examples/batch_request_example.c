#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp_client.h>
#include <mcp_tcp_client_transport.h>
#include <mcp_log.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(seconds) Sleep((seconds) * 1000)
#else
#include <unistd.h>
#endif

int main(int argc, char* argv[]) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Parse command line arguments
    const char* host = "127.0.0.1";
    uint16_t port = 8080;

    if (argc > 1) {
        host = argv[1];
    }

    if (argc > 2) {
        port = (uint16_t)atoi(argv[2]);
    }

    printf("Connecting to %s:%u\n", host, port);

    // Create transport
    mcp_transport_t* transport = mcp_transport_tcp_client_create(host, port);
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

    printf("Client created successfully\n");

    // Create batch requests
    mcp_batch_request_t requests[3];

    // Request 1: ping
    requests[0].method = "ping";
    requests[0].params = NULL;
    requests[0].id = 101; // Use unique IDs that won't conflict with the initial ping

    // Request 2: list_resources
    requests[1].method = "list_resources";
    requests[1].params = NULL;
    requests[1].id = 102;

    // Request 3: read_resource (assuming a resource exists)
    requests[2].method = "read_resource";
    requests[2].params = "{\"uri\":\"example://hello\"}";
    requests[2].id = 103;

    // Send batch request
    mcp_batch_response_t* responses = NULL;
    size_t response_count = 0;

    printf("Sending batch request with %d requests...\n", 3);
    int result = mcp_client_send_batch_request(client, requests, 3, &responses, &response_count);

    if (result != 0) {
        printf("Failed to send batch request: %d\n", result);
        mcp_client_destroy(client);
        return 1;
    }

    printf("Received %zu responses\n", response_count);

    // Process responses
    for (size_t i = 0; i < response_count; i++) {
        printf("\nResponse %zu (ID: %llu):\n", i + 1, (unsigned long long)responses[i].id);

        if (responses[i].error_code != MCP_ERROR_NONE) {
            printf("  Error: %d - %s\n",
                   responses[i].error_code,
                   responses[i].error_message ? responses[i].error_message : "Unknown error");
        } else {
            printf("  Success: %s\n", responses[i].result ? responses[i].result : "No result");
        }
    }

    // Free responses
    mcp_client_free_batch_responses(responses, response_count);

    // Clean up
    mcp_client_destroy(client);

    printf("Client destroyed\n");
    return 0;
}
