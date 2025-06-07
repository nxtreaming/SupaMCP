/**
 * @file test_logging_warnings.c
 * @brief Test file to verify that logging optimizations don't produce compiler warnings
 * 
 * This test file simulates the scenarios that were causing compiler warnings
 * and verifies that they are now properly handled.
 */

#include "mcp_log.h"
#include "mcp_log_config.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Test function that simulates the performance logging scenario
void test_performance_logging_warnings(void) {
    printf("Testing performance logging warnings fix...\n");
    
    // Simulate the scenario from mcp_websocket_server_event.c
    time_t last_service_time = 0;
    time_t now = time(NULL);
    int service_count = 100;

    if (difftime(now, last_service_time) >= 60) {
        double elapsed = difftime(now, last_service_time);
#if MCP_ENABLE_PERF_LOGS
        double rate = service_count / elapsed;
        mcp_log_perf("[WS] performance: %.1f service calls/sec", rate);
#else
        // Avoid unused variable warnings when performance logging is disabled
        (void)elapsed;
        (void)service_count;
        printf("Performance logging disabled - no warnings should occur\n");
#endif
    }
}

// Test function that simulates the data logging scenario
void test_data_logging_warnings(void) {
    printf("Testing data logging warnings fix...\n");
    
    // Simulate the scenario from mcp_websocket_client_transport.c
    struct {
        const void* data;
        size_t size;
    } buffers[2];
    
    const char* test_json = "{\"test\":\"message\"}";
    buffers[1].data = test_json;
    buffers[1].size = strlen(test_json);
    
    // This should not produce warnings when MCP_ENABLE_DATA_LOGS is 0
#if MCP_ENABLE_DATA_LOGS
    if (buffers[1].size > 0 && ((const char*)buffers[1].data)[0] == '{') {
        const char* json_data = (const char*)buffers[1].data;
        mcp_log_data_verbose("JSON data in sendv: %.*s", (int)buffers[1].size, json_data);
    }
#else
    printf("Data logging disabled - no warnings should occur\n");
#endif
}

// Test function that simulates message content logging
void test_message_content_logging(void) {
    printf("Testing message content logging warnings fix...\n");
    
    const char* message = "{\"type\":\"test\"}";
    size_t size = strlen(message);

#if MCP_ENABLE_DATA_LOGS
    if (size < 1000) {
        if (size > 0 && message[0] == '{') {
            char debug_buffer[1024] = {0};
            size_t copy_len = size < 1000 ? size : 1000;
            memcpy(debug_buffer, message, copy_len);
            debug_buffer[copy_len] = '\0';
            mcp_log_data_verbose("sending JSON: %s", debug_buffer);
        }
    }
#else
    // Avoid unused variable warning when data logging is disabled
    (void)size;
    (void)message;
    printf("Message content logging disabled - no warnings should occur\n");
#endif
}

// Test function to show current configuration
void show_current_config(void) {
    printf("\n=== Current Logging Configuration ===\n");
    
#if MCP_ENABLE_DEBUG_LOGS
    printf("Debug logs: ENABLED\n");
#else
    printf("Debug logs: DISABLED\n");
#endif

#if MCP_ENABLE_VERBOSE_LOGS
    printf("Verbose logs: ENABLED\n");
#else
    printf("Verbose logs: DISABLED\n");
#endif

#if MCP_ENABLE_DATA_LOGS
    printf("Data logs: ENABLED\n");
#else
    printf("Data logs: DISABLED\n");
#endif

#if MCP_ENABLE_PERF_LOGS
    printf("Performance logs: ENABLED\n");
#else
    printf("Performance logs: DISABLED\n");
#endif

    printf("=====================================\n\n");
}

int main(void) {
    printf("WebSocket Logging Warnings Test\n");
    printf("===============================\n\n");
    
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    
    // Show current configuration
    show_current_config();
    
    // Run tests
    test_performance_logging_warnings();
    printf("\n");
    
    test_data_logging_warnings();
    printf("\n");
    
    test_message_content_logging();
    printf("\n");
    
    printf("All tests completed. If this compiles without warnings,\n");
    printf("the logging optimization fixes are working correctly.\n");
    
    // Clean up
    mcp_log_close();
    
    return 0;
}

/*
 * Compilation test commands:
 * 
 * Test with all logging disabled (should produce no warnings):
 * gcc -Wall -Wextra -DMCP_ENABLE_DEBUG_LOGS=0 -DMCP_ENABLE_VERBOSE_LOGS=0 \
 *     -DMCP_ENABLE_DATA_LOGS=0 -DMCP_ENABLE_PERF_LOGS=0 \
 *     test_logging_warnings.c -o test_warnings_disabled
 * 
 * Test with all logging enabled:
 * gcc -Wall -Wextra -DMCP_ENABLE_DEBUG_LOGS=1 -DMCP_ENABLE_VERBOSE_LOGS=1 \
 *     -DMCP_ENABLE_DATA_LOGS=1 -DMCP_ENABLE_PERF_LOGS=1 \
 *     test_logging_warnings.c -o test_warnings_enabled
 * 
 * Test with mixed configuration:
 * gcc -Wall -Wextra -DMCP_ENABLE_DEBUG_LOGS=1 -DMCP_ENABLE_VERBOSE_LOGS=0 \
 *     -DMCP_ENABLE_DATA_LOGS=0 -DMCP_ENABLE_PERF_LOGS=1 \
 *     test_logging_warnings.c -o test_warnings_mixed
 */
