#include "mcp_client.h"
#include "mcp_transport_factory.h"
#include "mcp_websocket_transport.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"
#include "mcp_thread_local.h"
#include "mcp_json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Test configuration
#define TEST_HOST "127.0.0.1"
#define TEST_PORT 8080
#define TEST_PATH "/ws"
#define NUM_SMALL_MESSAGES 100
#define NUM_LARGE_MESSAGES 20
#define SMALL_MESSAGE_SIZE 256
#define LARGE_MESSAGE_SIZE 1024

// Global variables
static mcp_client_t* g_client = NULL;
static mcp_transport_t* g_transport = NULL;
static volatile bool g_running = true;

// Signal handler
static void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_running = false;
}

// Generate test message of specified size
static char* generate_test_message(size_t target_size, int id) {
    if (target_size < 20) {
        target_size = 20;
    }
    
    char* message = (char*)malloc(target_size + 1);
    if (!message) {
        return NULL;
    }

    int header_len = snprintf(message, target_size, "TEST_%d_", id);
    if (header_len < 0 || header_len >= target_size - 1) {
        free(message);
        return NULL;
    }

    size_t remaining = target_size - header_len;
    for (size_t i = 0; i < remaining; i++) {
        message[header_len + i] = 'A' + (i % 26);
    }

    message[target_size] = '\0';
    return message;
}

// Create client connection
static bool create_client_connection(const char* host, uint16_t port, const char* path) {
    if (g_client) {
        mcp_client_destroy(g_client);
        g_client = NULL;
    }

    // Create transport configuration
    mcp_transport_config_t transport_config = {0};
    transport_config.ws.host = host;
    transport_config.ws.port = port;
    transport_config.ws.path = path;
    transport_config.ws.use_ssl = 0;
    transport_config.ws.connect_timeout_ms = 5000;

    // Create transport
    g_transport = mcp_transport_factory_create(MCP_TRANSPORT_WS_CLIENT, &transport_config);
    if (!g_transport) {
        printf("Failed to create WebSocket transport\n");
        return false;
    }

    // Create client configuration
    mcp_client_config_t client_config = {
        .request_timeout_ms = 5000
    };

    // Create client
    g_client = mcp_client_create(&client_config, g_transport);
    if (!g_client) {
        printf("Failed to create client\n");
        mcp_transport_destroy(g_transport);
        g_transport = NULL;
        return false;
    }

    printf("Connecting to WebSocket server at %s:%d%s\n", host, port, path);

    // Wait for connection
    int max_wait_attempts = 50; // 5 seconds
    int wait_attempts = 0;

    while (wait_attempts < max_wait_attempts) {
        int connection_state = mcp_client_is_connected(g_client);
        if (connection_state == 1) {
            printf("Connected to server successfully.\n");
            return true;
        }

        mcp_sleep_ms(100);
        wait_attempts++;

        if (wait_attempts % 10 == 0) {
            printf("Waiting for connection... (%d seconds)\n", wait_attempts / 10);
        }
    }

    printf("Failed to connect after %d seconds\n", max_wait_attempts / 10);
    return false;
}

// Send echo request
static bool send_echo_request(const char* message) {
    if (!g_client || mcp_client_is_connected(g_client) != 1) {
        return false;
    }

    // Create escaped JSON string
    char* escaped_message = mcp_json_format_string(message);
    if (!escaped_message) {
        return false;
    }

    // Format parameters
    char params_buffer[2048];
    snprintf(params_buffer, sizeof(params_buffer),
        "{\"name\":\"echo\",\"arguments\":{\"message\":%s}}",
        escaped_message);

    free(escaped_message);

    // Send request
    char* response = NULL;
    mcp_error_code_t error_code = MCP_ERROR_NONE;
    char* error_message = NULL;

    int result = mcp_client_send_request(g_client, "call_tool", params_buffer, 
                                       &response, &error_code, &error_message);

    // Cleanup
    if (error_message) {
        free(error_message);
    }
    if (response) {
        free(response);
    }

    return (result == 0 && error_code == MCP_ERROR_NONE);
}

// Test small messages
static void test_small_messages() {
    printf("Testing %d small messages (%d bytes each)...\n", NUM_SMALL_MESSAGES, SMALL_MESSAGE_SIZE);
    
    clock_t start = clock();
    int successful = 0;
    int failed = 0;
    
    for (int i = 0; i < NUM_SMALL_MESSAGES && g_running; i++) {
        char* message = generate_test_message(SMALL_MESSAGE_SIZE, i);
        if (!message) {
            failed++;
            continue;
        }

        if (send_echo_request(message)) {
            successful++;
        } else {
            failed++;
        }

        free(message);

        // Conservative delay
        if (i % 10 == 0) {
            mcp_sleep_ms(100);
        } else {
            mcp_sleep_ms(50);
        }
    }
    
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("Small messages test completed in %.3f seconds\n", elapsed);
    printf("Successful: %d, Failed: %d\n", successful, failed);
    if (successful > 0) {
        printf("Average time per successful message: %.3f ms\n", (elapsed * 1000) / successful);
    }
}

// Test large messages
static void test_large_messages() {
    printf("Testing %d large messages (%d bytes each)...\n", NUM_LARGE_MESSAGES, LARGE_MESSAGE_SIZE);
    
    clock_t start = clock();
    int successful = 0;
    int failed = 0;
    
    for (int i = 0; i < NUM_LARGE_MESSAGES && g_running; i++) {
        char* message = generate_test_message(LARGE_MESSAGE_SIZE, i);
        if (!message) {
            failed++;
            continue;
        }

        if (send_echo_request(message)) {
            successful++;
        } else {
            failed++;
        }

        free(message);

        // Longer delay for large messages
        mcp_sleep_ms(200);
    }
    
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("Large messages test completed in %.3f seconds\n", elapsed);
    printf("Successful: %d, Failed: %d\n", successful, failed);
    if (successful > 0) {
        printf("Average time per successful message: %.3f ms\n", (elapsed * 1000) / successful);
    }
}

// Test UTF-8 vs ASCII
static void test_utf8_vs_ascii() {
    printf("Testing UTF-8 vs ASCII message performance...\n");
    
    const char* ascii_msg = "ASCII_TEST_Hello_World_ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char* utf8_msg = "UTF8_TEST_Hello_世界_🌍";
    
    clock_t start, end;
    int ascii_successful = 0, utf8_successful = 0;
    
    // Test ASCII messages
    printf("Testing ASCII messages...\n");
    start = clock();
    for (int i = 0; i < 30 && g_running; i++) {
        if (send_echo_request(ascii_msg)) {
            ascii_successful++;
        }
        mcp_sleep_ms(100);
    }
    end = clock();
    double ascii_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Pause between tests
    mcp_sleep_ms(500);
    
    // Test UTF-8 messages
    printf("Testing UTF-8 messages...\n");
    start = clock();
    for (int i = 0; i < 30 && g_running; i++) {
        if (send_echo_request(utf8_msg)) {
            utf8_successful++;
        }
        mcp_sleep_ms(100);
    }
    end = clock();
    double utf8_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    printf("ASCII messages: %d successful, %.3f seconds\n", ascii_successful, ascii_time);
    printf("UTF-8 messages: %d successful, %.3f seconds\n", utf8_successful, utf8_time);
    
    if (ascii_successful > 0 && utf8_successful > 0) {
        double ascii_avg = (ascii_time * 1000) / ascii_successful;
        double utf8_avg = (utf8_time * 1000) / utf8_successful;
        printf("Average ASCII time: %.3f ms\n", ascii_avg);
        printf("Average UTF-8 time: %.3f ms\n", utf8_avg);
    }
}

int main(int argc, char* argv[]) {
    const char* host = TEST_HOST;
    int port = TEST_PORT;
    const char* path = TEST_PATH;

    // Parse command line arguments
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);
    if (argc >= 4) path = argv[3];

    // Show usage if help requested
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("WebSocket Buffer Optimization Test\n");
        printf("Usage: %s [host] [port] [path]\n", argv[0]);
        printf("  host: WebSocket server host (default: %s)\n", TEST_HOST);
        printf("  port: WebSocket server port (default: %d)\n", TEST_PORT);
        printf("  path: WebSocket server path (default: %s)\n", TEST_PATH);
        printf("\nExample: %s 127.0.0.1 8080 /ws\n", argv[0]);
        printf("\nNote: Make sure an MCP WebSocket server is running.\n");
        return 0;
    }

    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena
    if (mcp_arena_init_current_thread(4096) != 0) {
        printf("Failed to initialize thread-local arena\n");
        return 1;
    }

    printf("WebSocket Buffer Optimization Test\n");
    printf("Connecting to MCP server at %s:%d%s\n", host, port, path);

    // Create client connection
    if (!create_client_connection(host, (uint16_t)port, path)) {
        printf("Failed to create client connection\n");
        mcp_arena_destroy_current_thread();
        return 1;
    }

    printf("\nStarting buffer optimization tests...\n\n");

    // Run tests
    if (g_running) {
        test_small_messages();
        printf("\n");
    }

    if (g_running) {
        test_large_messages();
        printf("\n");
    }

    if (g_running) {
        test_utf8_vs_ascii();
        printf("\n");
    }

    printf("All tests completed.\n");

    // Cleanup
    if (g_client) {
        mcp_client_destroy(g_client);
        g_client = NULL;
    }

    mcp_log_close();
    mcp_arena_destroy_current_thread();

    printf("Test program shutdown complete\n");
    return 0;
}
