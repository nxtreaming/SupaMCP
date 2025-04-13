#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp_client.h>
#include <mcp_json.h>
#include <mcp_log.h>
#include <mcp_tcp_transport.h>
#include <mcp_thread_local.h>

int main(int argc, char** argv) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Starting template client test");

    // Initialize thread-local arena
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) { // 1MB arena
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    // Create a TCP transport for the client
    mcp_transport_t* transport = mcp_transport_tcp_create("127.0.0.1", 8080, 0);
    if (transport == NULL) {
        mcp_log_error("Failed to create TCP transport");
        return 1;
    }

    // Create client configuration
    mcp_client_config_t config = {0};
    config.request_timeout_ms = 50000; // 50 seconds timeout

    // Create a client
    mcp_client_t* client = mcp_client_create(&config, transport);
    if (client == NULL) {
        mcp_log_error("Failed to create client");
        mcp_transport_destroy(transport);
        return 1;
    }

    // Test cases
    const char* test_uris[] = {
        "example://john",
        "example://john/profile",
        "example://john/posts/42",
        "example://john/settings/theme-dark"
    };
    const int test_count = sizeof(test_uris) / sizeof(test_uris[0]);

    // Run tests
    for (int i = 0; i < test_count; i++) {
        printf("\nTest %d: %s\n", i + 1, test_uris[i]);

        // Read the resource
        mcp_content_item_t** content = NULL;
        size_t content_count = 0;

        int result = mcp_client_read_resource(client, test_uris[i], &content, &content_count);

        if (result == 0) {
            printf("Success! Received %zu content items\n", content_count);

            // Print content
            for (size_t j = 0; j < content_count; j++) {
                printf("Content %zu:\n", j + 1);
                printf("  Type: %d\n", content[j]->type);
                printf("  MIME: %s\n", content[j]->mime_type);
                printf("  Size: %zu\n", content[j]->data_size);

                if (content[j]->type == MCP_CONTENT_TYPE_TEXT) {
                    printf("  Data: %s\n", (char*)content[j]->data);
                }
            }

            // Free content
            for (size_t j = 0; j < content_count; j++) {
                free(content[j]->mime_type);
                free(content[j]->data);
                free(content[j]);
            }
            free(content);
        } else {
            printf("Error: Failed to read resource\n");
        }
    }

    // Clean up
    mcp_client_destroy(client); // This will also destroy the transport
    mcp_arena_destroy_current_thread();
    mcp_log_close();

    return 0;
}
