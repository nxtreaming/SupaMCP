/**
 * @file test_client_limits.c
 * @brief Test program to find the root cause of client creation limits
 *
 * This test systematically creates clients to find where and why the limit occurs.
 */
#ifdef _WIN32
#include "win_socket_compat.h"
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/resource.h>
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "mcp_server.h"
#include "mcp_sthttp_transport.h"
#include "mcp_sthttp_client_transport.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"
#include "mcp_socket_utils.h"

// Global variables
static volatile bool g_running = true;
static mcp_server_t* g_server = NULL;
static mcp_transport_t* g_server_transport = NULL;

/**
 * @brief Signal handler for graceful shutdown and crash detection
 */
static void signal_handler(int signal) {
    printf("\n[SIGNAL] Received signal %d", signal);
    
    switch (signal) {
        case SIGINT:
            printf(" (SIGINT - Interrupt)\n");
            printf("User requested shutdown, stopping server...\n");
            g_running = false;
            break;
        case SIGTERM:
            printf(" (SIGTERM - Terminate)\n");
            printf("Termination requested, stopping server...\n");
            g_running = false;
            break;
#ifndef _WIN32
        case SIGSEGV:
            printf(" (SIGSEGV - Segmentation fault)\n");
            printf("CRASH: Segmentation fault detected!\n");
            printf("This indicates a memory access violation.\n");
            fflush(stdout);
            exit(1);
            break;
        case SIGABRT:
            printf(" (SIGABRT - Abort)\n");
            printf("CRASH: Program aborted!\n");
            printf("This may be due to an assertion failure or abort() call.\n");
            fflush(stdout);
            exit(1);
            break;
#endif
        default:
            printf(" (Unknown signal)\n");
            printf("Unexpected signal received, stopping server...\n");
            g_running = false;
            break;
    }
    fflush(stdout);
}

/**
 * @brief Create a test client
 */
static mcp_transport_t* create_test_client(int client_id) {
    printf("   [DEBUG] Creating client #%d...\n", client_id);
    fflush(stdout);
    
    mcp_sthttp_client_config_t config = MCP_STHTTP_CLIENT_CONFIG_DEFAULT;
    config.host = "127.0.0.1";
    config.port = 8080;
    config.enable_sse_streams = true;
    
    mcp_transport_t* client = mcp_transport_sthttp_client_create(&config);
    if (client == NULL) {
        printf("   [ERROR] Failed to create client transport #%d\n", client_id);
        fflush(stdout);
        return NULL;
    }
    
    // Start the client
    int start_result = mcp_transport_start(client, NULL, NULL, NULL);
    if (start_result != 0) {
        printf("   [ERROR] Failed to start client transport #%d (result: %d)\n", client_id, start_result);
        fflush(stdout);
        mcp_transport_destroy(client);
        return NULL;
    }
    
    printf("   [SUCCESS] Client #%d created and started\n", client_id);
    fflush(stdout);
    
    return client;
}

/**
 * @brief Test client creation limits
 */
static int test_client_limits(int max_clients) {
    printf("\n=== Testing Client Creation Limits ===\n");
    printf("Target: %d clients\n", max_clients);
    printf("Creating clients one by one...\n\n");
    fflush(stdout);
    
    mcp_transport_t** clients = (mcp_transport_t**)calloc(max_clients, sizeof(mcp_transport_t*));
    if (clients == NULL) {
        printf("ERROR: Failed to allocate client array\n");
        return -1;
    }
    
    int created_count = 0;
    int consecutive_failures = 0;
    
    for (int i = 0; i < max_clients && g_running; i++) {
        // Add delay between client creations
        mcp_sleep_ms(200);
        
        clients[i] = create_test_client(i + 1);
        if (clients[i] != NULL) {
            created_count++;
            consecutive_failures = 0;
            
            // Report progress every 10 clients
            if ((i + 1) % 10 == 0) {
                printf("Progress: %d/%d clients created successfully\n", created_count, i + 1);
                fflush(stdout);
            }
        } else {
            consecutive_failures++;
            printf("WARNING: Failed to create client #%d (consecutive failures: %d)\n", i + 1, consecutive_failures);
            fflush(stdout);
            
            if (consecutive_failures >= 5) {
                printf("ERROR: Too many consecutive failures (%d), stopping test\n", consecutive_failures);
                break;
            }
        }
    }
    
    printf("\n=== Test Results ===\n");
    printf("Successfully created: %d clients\n", created_count);
    printf("Failed attempts: %d\n", max_clients - created_count);
    printf("Consecutive failures before stopping: %d\n", consecutive_failures);
    fflush(stdout);
    
    // Keep clients alive for a short time to test stability
    if (created_count > 0) {
        printf("\nKeeping clients alive for 10 seconds to test stability...\n");
        fflush(stdout);
        
        for (int i = 0; i < 10 && g_running; i++) {
            mcp_sleep_ms(1000);
            printf(".");
            fflush(stdout);
        }
        printf("\n");
    }
    
    // Cleanup clients
    printf("Cleaning up clients...\n");
    fflush(stdout);
    
    for (int i = 0; i < max_clients; i++) {
        if (clients[i] != NULL) {
            mcp_transport_destroy(clients[i]);
            clients[i] = NULL;
        }
    }
    
    free(clients);
    printf("Cleanup completed.\n");
    fflush(stdout);
    
    return created_count;
}

/**
 * @brief Main function
 */
int main(int argc, char* argv[]) {
    printf("=== MCP Client Limits Test ===\n");
    printf("This test finds the root cause of client creation limits.\n\n");
    fflush(stdout);
    
    // Parse command line arguments
    int target_clients = 100; // Default
    if (argc > 1) {
        target_clients = atoi(argv[1]);
        if (target_clients <= 0) {
            printf("Invalid client count: %s\n", argv[1]);
            return 1;
        }
    }
    
    // Set up signal handlers for crash detection
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGSEGV, signal_handler);  // Segmentation fault
    signal(SIGABRT, signal_handler);  // Abort signal
#endif
    
    printf("Signal handlers installed for crash detection\n");
    fflush(stdout);
    
    // Initialize socket system
    if (mcp_socket_init() != 0) {
        printf("ERROR: Failed to initialize socket system\n");
        return 1;
    }
    
    // Create server transport
    mcp_sthttp_config_t config = MCP_STHTTP_CONFIG_DEFAULT;
    config.host = "127.0.0.1";
    config.port = 8080;
    config.mcp_endpoint = "/mcp";
    config.enable_sessions = false;
    
    g_server_transport = mcp_transport_sthttp_create(&config);
    if (g_server_transport == NULL) {
        printf("ERROR: Failed to create server transport\n");
        mcp_socket_cleanup();
        return 1;
    }
    
    // Create and start server
    mcp_server_config_t server_config = {0};
    mcp_server_capabilities_t server_capabilities = {0};

    g_server = mcp_server_create(&server_config, &server_capabilities);
    if (g_server == NULL) {
        printf("ERROR: Failed to create server\n");
        mcp_transport_destroy(g_server_transport);
        mcp_socket_cleanup();
        return 1;
    }
    
    if (mcp_server_start(g_server, g_server_transport) != 0) {
        printf("ERROR: Failed to start server\n");
        mcp_server_destroy(g_server);
        mcp_transport_destroy(g_server_transport);
        mcp_socket_cleanup();
        return 1;
    }
    
    printf("Server started successfully on %s:%d\n", config.host, config.port);
    printf("Testing with up to %d clients...\n\n", target_clients);
    fflush(stdout);
    
    // Wait a moment for server to be ready
    mcp_sleep_ms(1000);
    
    // Run the test
    int result = test_client_limits(target_clients);
    
    printf("\n=== Final Results ===\n");
    if (result > 0) {
        printf("SUCCESS: Created %d clients\n", result);
        if (result >= target_clients) {
            printf("All target clients created successfully!\n");
        } else {
            printf("Reached limit at %d clients\n", result);
        }
    } else {
        printf("FAILURE: Could not create any clients\n");
    }
    fflush(stdout);
    
    // Shutdown
    printf("\nShutting down test server...\n");
    fflush(stdout);
    
    if (g_server) {
        mcp_server_stop(g_server);
        mcp_server_destroy(g_server);
    }
    
    if (g_server_transport) {
        mcp_transport_destroy(g_server_transport);
    }

    mcp_socket_cleanup();
    
    printf("Test completed.\n");
    return result > 0 ? 0 : 1;
}
