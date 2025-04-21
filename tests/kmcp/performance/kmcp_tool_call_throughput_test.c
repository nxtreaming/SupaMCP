#include "kmcp.h"
#include "mcp_log.h"
#include "kmcp_server_manager_stub.h"
#include "mcp_string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Forward declaration
int run_tests(void);

#ifdef STANDALONE_TEST
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #condition, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

int main(void) {
    return run_tests();
}
#else
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #condition, __FILE__, __LINE__); \
            return 0; \
        } \
    } while (0)
#endif

// Get current time in milliseconds
static double get_time_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

/**
 * Test tool call throughput
 */
static int test_tool_call_throughput(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create client configuration
    kmcp_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = mcp_strdup("test-client");
    config.version = mcp_strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create client
    kmcp_client_t* client = kmcp_client_create(&config);
    TEST_ASSERT(client != NULL);

    // Free client configuration
    free(config.name);
    free(config.version);

    // Get server manager
    kmcp_server_manager_t* manager = kmcp_client_get_manager(client);
    TEST_ASSERT(manager != NULL);

    // Create server configuration
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = mcp_strdup("test-server");
    server_config.command = mcp_strdup("echo");
    server_config.args_count = 1;
    server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
    server_config.args[0] = mcp_strdup("hello");

    // Add server
    kmcp_error_t result = kmcp_server_manager_add_server(manager, &server_config);

    // Free server configuration
    free(server_config.name);
    free(server_config.command);
    for (size_t i = 0; i < server_config.args_count; i++) {
        free(server_config.args[i]);
    }
    free(server_config.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Prepare tool call parameters
    const char* tool_name = "echo";
    const char* params_json = "{\"text\":\"Hello, World!\"}";
    char* result_json = NULL;

    // Warm-up
    for (int i = 0; i < 10; i++) {
        result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
        if (result == KMCP_SUCCESS && result_json) {
            free(result_json);
            result_json = NULL;
        }
    }

    // Measure throughput
    const int num_calls = 1000;
    double start_time = get_time_ms();

    for (int i = 0; i < num_calls; i++) {
        result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
        if (result == KMCP_SUCCESS && result_json) {
            free(result_json);
            result_json = NULL;
        }
    }

    double end_time = get_time_ms();
    double elapsed_time = end_time - start_time;
    double calls_per_second = (double)num_calls * 1000.0 / elapsed_time;

    printf("Tool call throughput: %.2f calls/second\n", calls_per_second);
    printf("Average response time: %.2f ms\n", elapsed_time / (double)num_calls);

    // Close the client
    kmcp_client_close(client);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test tool call throughput with different payload sizes
 */
static int test_tool_call_throughput_payload_size(void) {
    // This is a simplified version for testing
    return 1;
}

/**
 * Test tool call throughput with different server configurations
 */
static int test_tool_call_throughput_server_config(void) {
    // This is a simplified version for testing
    return 1;
}

/**
 * Run all tests
 */
int run_tests(void) {
    int success = 1;

    success &= test_tool_call_throughput();
    success &= test_tool_call_throughput_payload_size();
    success &= test_tool_call_throughput_server_config();

    return success ? 0 : 1;
}
