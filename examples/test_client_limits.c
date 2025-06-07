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
#include "mcp_transport_factory.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"
#include "mcp_socket_utils.h"

// Transport protocol types (mapped to factory types)
typedef enum {
    TRANSPORT_STHTTP,    // HTTP Streamable (default)
    TRANSPORT_HTTP,      // Standard HTTP
    TRANSPORT_TCP,       // TCP
    TRANSPORT_WEBSOCKET  // WebSocket
} transport_protocol_t;

// Global variables
static volatile bool g_running = true;
static mcp_server_t* g_server = NULL;
static mcp_transport_t* g_server_transport = NULL;
static transport_protocol_t g_protocol = TRANSPORT_STHTTP;

/**
 * @brief Simple message callback for test clients
 */
static char* test_client_message_callback(void* user_data, const char* message, size_t message_len, int* error_code) {
    (void)user_data;
    (void)message;
    (void)message_len;
    // For testing purposes, we just acknowledge receipt and don't process the message
    // In a real application, this would parse and handle the MCP message
    *error_code = 0;
    return NULL; // No response needed for test clients
}

/**
 * @brief Simple error callback for test clients
 */
static void test_client_error_callback(void* user_data, int error_code) {
    (void)user_data;
    (void)error_code;
    // For testing purposes, we just log the error
    // In a real application, this would handle connection errors, retries, etc.
    // We don't log here to avoid spam during mass testing

}

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
 * @brief Parse transport protocol from string
 */
static transport_protocol_t parse_protocol(const char* protocol_str) {
    if (protocol_str == NULL) {
        return TRANSPORT_STHTTP;
    }

    if (strcmp(protocol_str, "sthttp") == 0 || strcmp(protocol_str, "http-streamable") == 0) {
        return TRANSPORT_STHTTP;
    } else if (strcmp(protocol_str, "http") == 0) {
        return TRANSPORT_HTTP;
    } else if (strcmp(protocol_str, "tcp") == 0) {
        return TRANSPORT_TCP;
    } else if (strcmp(protocol_str, "websocket") == 0 || strcmp(protocol_str, "ws") == 0) {
        return TRANSPORT_WEBSOCKET;
    } else {
        printf("Unknown protocol '%s', using default (sthttp)\n", protocol_str);
        return TRANSPORT_STHTTP;
    }
}

/**
 * @brief Get protocol name string
 */
static const char* get_protocol_name(transport_protocol_t protocol) {
    switch (protocol) {
        case TRANSPORT_STHTTP: return "HTTP Streamable";
        case TRANSPORT_HTTP: return "HTTP";
        case TRANSPORT_TCP: return "TCP";
        case TRANSPORT_WEBSOCKET: return "WebSocket";
        default: return "Unknown";
    }
}

/**
 * @brief Print usage information
 */
static void print_usage(const char* program_name) {
    printf("Usage: %s [max_clients] [protocol]\n", program_name);
    printf("\n");
    printf("Arguments:\n");
    printf("  max_clients  Maximum number of clients to test (default: 100)\n");
    printf("  protocol     Transport protocol to use (default: sthttp)\n");
    printf("\n");
    printf("Supported protocols:\n");
    printf("  sthttp       HTTP Streamable (MCP 2025-03-26) - supports SSE streams\n");
    printf("  http         Standard HTTP - traditional request/response\n");
    printf("  tcp          TCP - raw TCP with length-prefixed framing\n");
    printf("  websocket    WebSocket - full-duplex WebSocket connections\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s 1000                    # Test 1000 HTTP Streamable clients\n", program_name);
    printf("  %s 500 tcp                 # Test 500 TCP clients\n", program_name);
    printf("  %s 2000 websocket          # Test 2000 WebSocket clients\n", program_name);
    printf("  %s 100 http                # Test 100 HTTP clients\n", program_name);
    printf("\n");
}

/**
 * @brief Create server transport based on protocol using factory
 */
static mcp_transport_t* create_server_transport(transport_protocol_t protocol) {
    mcp_transport_config_t config = {0};
    mcp_transport_type_t factory_type;

    switch (protocol) {
        case TRANSPORT_STHTTP:
            factory_type = MCP_TRANSPORT_STHTTP;
            config.sthttp.host = "127.0.0.1";
            config.sthttp.port = 8080;
            config.sthttp.use_ssl = 0;
            config.sthttp.mcp_endpoint = "/mcp";
            config.sthttp.enable_sessions = 0;
            config.sthttp.enable_cors = 1;
            config.sthttp.cors_allow_origin = "*";
            config.sthttp.cors_allow_methods = "GET, POST, OPTIONS, DELETE";
            config.sthttp.cors_allow_headers = "Content-Type, Authorization, Mcp-Session-Id, Last-Event-ID";
            config.sthttp.cors_max_age = 86400;
            config.sthttp.max_sse_clients = 5000;
            config.sthttp.timeout_ms = 30000;
            break;

        case TRANSPORT_HTTP:
            factory_type = MCP_TRANSPORT_HTTP_SERVER;
            config.http.host = "127.0.0.1";
            config.http.port = 8080;
            config.http.use_ssl = 0;
            config.http.timeout_ms = 30000;
            break;

        case TRANSPORT_TCP:
            factory_type = MCP_TRANSPORT_TCP;
            config.tcp.host = "127.0.0.1";
            config.tcp.port = 8080;
            config.tcp.idle_timeout_ms = 0; // no idle timeout
            break;

        case TRANSPORT_WEBSOCKET:
            factory_type = MCP_TRANSPORT_WS_SERVER;
            config.ws.host = "127.0.0.1";
            config.ws.port = 8080;
            config.ws.path = "/ws";
            config.ws.use_ssl = 0;
            config.ws.connect_timeout_ms = 10000;
            break;

        default:
            printf("ERROR: Unsupported server protocol\n");
            return NULL;
    }

    return mcp_transport_factory_create(factory_type, &config);
}

/**
 * @brief Create a test client based on protocol using factory
 */
static mcp_transport_t* create_test_client(int client_id, transport_protocol_t protocol) {
    printf("   [DEBUG] Creating %s client #%d...\n", get_protocol_name(protocol), client_id);
    fflush(stdout);

    mcp_transport_config_t config = {0};
    mcp_transport_type_t factory_type;

    switch (protocol) {
        case TRANSPORT_STHTTP:
            factory_type = MCP_TRANSPORT_STHTTP_CLIENT;
            config.sthttp_client.host = "127.0.0.1";
            config.sthttp_client.port = 8080;
            config.sthttp_client.use_ssl = 0;
            config.sthttp_client.mcp_endpoint = "/mcp";
            config.sthttp_client.connect_timeout_ms = 10000;
            config.sthttp_client.request_timeout_ms = 30000;
            config.sthttp_client.enable_sessions = 1;
            config.sthttp_client.enable_sse_streams = 1;
            config.sthttp_client.auto_reconnect_sse = 1;
            break;

        case TRANSPORT_HTTP:
            factory_type = MCP_TRANSPORT_HTTP_CLIENT;
            config.http_client.host = "127.0.0.1";
            config.http_client.port = 8080;
            config.http_client.use_ssl = 0;
            config.http_client.timeout_ms = 30000;
            break;

        case TRANSPORT_TCP:
            factory_type = MCP_TRANSPORT_TCP_CLIENT;
            config.tcp.host = "127.0.0.1";
            config.tcp.port = 8080;
            break;

        case TRANSPORT_WEBSOCKET:
            factory_type = MCP_TRANSPORT_WS_CLIENT;
            config.ws.host = "127.0.0.1";
            config.ws.port = 8080;
            config.ws.path = "/ws";
            config.ws.use_ssl = 0;
            config.ws.connect_timeout_ms = 10000;
            break;

        default:
            printf("   [ERROR] Unsupported client protocol\n");
            fflush(stdout);
            return NULL;
    }

    mcp_transport_t* client = mcp_transport_factory_create(factory_type, &config);
    if (client == NULL) {
        printf("   [ERROR] Failed to create %s client transport #%d\n", get_protocol_name(protocol), client_id);
        fflush(stdout);
        return NULL;
    }

    // Start the client with proper callbacks
    int start_result = mcp_transport_start(client, test_client_message_callback, NULL, test_client_error_callback);
    if (start_result != 0) {
        printf("   [ERROR] Failed to start %s client transport #%d (result: %d)\n",
               get_protocol_name(protocol), client_id, start_result);
        fflush(stdout);
        mcp_transport_destroy(client);
        return NULL;
    }

    printf("   [SUCCESS] %s client #%d created and started\n", get_protocol_name(protocol), client_id);
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
        
        clients[i] = create_test_client(i + 1, g_protocol);
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
    printf("This test finds the root cause of client creation limits.\n");
    printf("Supports multiple transport protocols: sthttp, http, tcp, websocket\n\n");
    fflush(stdout);

    // Parse command line arguments
    int target_clients = 100; // Default
    const char* protocol_str = "sthttp"; // Default protocol

    if (argc > 1) {
        target_clients = atoi(argv[1]);
        if (target_clients <= 0) {
            printf("ERROR: Invalid client count: %s\n\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (argc > 2) {
        protocol_str = argv[2];
    }

    // Parse and set protocol
    g_protocol = parse_protocol(protocol_str);
    printf("Using transport protocol: %s\n", get_protocol_name(g_protocol));
    printf("Target clients: %d\n\n", target_clients);
    
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
    
    // Create server transport based on protocol
    g_server_transport = create_server_transport(g_protocol);
    if (g_server_transport == NULL) {
        printf("ERROR: Failed to create %s server transport\n", get_protocol_name(g_protocol));
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
    
    printf("%s server started successfully on 127.0.0.1:8080\n", get_protocol_name(g_protocol));
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
