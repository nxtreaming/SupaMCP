/**
 * @file websocket_logging_example.c
 * @brief Example demonstrating optimized WebSocket logging usage
 * 
 * This example shows how to use the optimized logging macros in WebSocket
 * transport code for different scenarios and build configurations.
 */
#include "mcp_log.h"
#include "mcp_log_config.h"
#include <stdio.h>

// Example WebSocket client connection function
int example_websocket_connect(const char* host, int port) {
    // Always log important connection events
    mcp_log_ws_info("connecting to %s:%d", host, port);
    
    // Debug information (only in debug builds)
    mcp_log_ws_debug("preparing connection parameters");
    
    // Verbose logging for detailed debugging (only when enabled)
    mcp_log_ws_verbose("setting up SSL context");
    mcp_log_ws_verbose("configuring connection timeout");
    
    // Simulate connection success
    mcp_log_ws_info("connection established successfully");
    
    return 0;
}

// Example message sending function
int example_send_message(const char* message, size_t length) {
    // Always log errors
    if (!message || length == 0) {
        mcp_log_ws_error("invalid message parameters");
        return -1;
    }
    
    // Debug information about message size
    mcp_log_ws_debug("sending message of %zu bytes", length);
    
    // Data content logging (only when data logging enabled)
    if (length > 0 && message[0] == '{') {
        mcp_log_data_verbose("sending JSON: %.*s", (int)length, message);
    } else {
        mcp_log_data_verbose("sending binary data (%zu bytes)", length);
    }
    
    // Verbose operation logging
    mcp_log_ws_verbose("message queued for transmission");
    
    return 0;
}

// Example performance monitoring function
void example_performance_stats(int active_connections, double throughput) {
#if MCP_ENABLE_PERF_LOGS
    // Performance metrics (only when performance logging enabled)
    mcp_log_perf("[WS] active connections: %d, throughput: %.2f msg/sec", 
                 active_connections, throughput);
#else
    (void)active_connections;
    (void)throughput;
#endif
}

// Example error handling function
void example_handle_error(int error_code, const char* context) {
    // Always log errors with context
    mcp_log_ws_error("error %d in %s", error_code, context);
    
    // Additional debug information
    mcp_log_ws_debug("error occurred during %s operation", context);
}

// Example callback function with different logging levels
int example_websocket_callback(int reason, void* user_data) {
    (void)user_data;
    switch (reason) {
        case 1: // Connection established
            mcp_log_ws_info("callback: connection established");
            break;
            
        case 2: // Data received
            mcp_log_ws_verbose("callback: data received");
            break;
            
        case 3: // Connection closed
            mcp_log_ws_info("callback: connection closed");
            break;
            
        case 4: // Error occurred
            mcp_log_ws_error("callback: error occurred");
            break;
            
        default:
            mcp_log_ws_verbose("callback: reason %d", reason);
            break;
    }
    
    return 0;
}

// Example function showing conditional compilation effects
void example_show_logging_config(void) {
    printf("=== WebSocket Logging Configuration ===\n");
    
#if MCP_ENABLE_DEBUG_LOGS
    printf("Debug logging: ENABLED\n");
    mcp_log_debug("This debug message will be shown");
#else
    printf("Debug logging: DISABLED\n");
    // mcp_log_debug calls will be compiled out
#endif

#if MCP_ENABLE_VERBOSE_LOGS
    printf("Verbose logging: ENABLED\n");
    mcp_log_verbose("This verbose message will be shown");
#else
    printf("Verbose logging: DISABLED\n");
    // mcp_log_verbose calls will be compiled out
#endif

#if MCP_ENABLE_DATA_LOGS
    printf("Data logging: ENABLED\n");
    mcp_log_data_verbose("This data message will be shown");
#else
    printf("Data logging: DISABLED\n");
    // mcp_log_data_verbose calls will be compiled out
#endif

#if MCP_ENABLE_PERF_LOGS
    printf("Performance logging: ENABLED\n");
    mcp_log_perf("This performance message will be shown");
#else
    printf("Performance logging: DISABLED\n");
    // mcp_log_perf calls will be compiled out
#endif

    printf("========================================\n");
}

// Example main function demonstrating usage
int main(void) {
    // Initialize logging system
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    
    printf("WebSocket Logging Optimization Example\n");
    printf("======================================\n\n");
    
    // Show current logging configuration
    example_show_logging_config();
    printf("\n");
    
    // Demonstrate different logging scenarios
    printf("=== Connection Example ===\n");
    example_websocket_connect("localhost", 8080);
    printf("\n");
    
    printf("=== Message Sending Example ===\n");
    example_send_message("{\"type\":\"request\",\"id\":1}", 25);
    example_send_message("binary_data", 11);
    printf("\n");
    
    printf("=== Performance Monitoring Example ===\n");
    example_performance_stats(150, 1250.5);
    printf("\n");
    
    printf("=== Error Handling Example ===\n");
    example_handle_error(-1, "connection setup");
    printf("\n");
    
    printf("=== Callback Example ===\n");
    example_websocket_callback(1, NULL);  // Connection established
    example_websocket_callback(2, NULL);  // Data received
    example_websocket_callback(3, NULL);  // Connection closed
    printf("\n");
    
    printf("Example completed. Check the log output above to see\n");
    printf("which messages are displayed based on your build configuration.\n");
    
    // Clean up logging system
    mcp_log_close();
    
    return 0;
}

/*
 * Build Examples:
 * 
 * Development build (all logging enabled):
 * gcc -DMCP_ENABLE_DEBUG_LOGS=1 -DMCP_ENABLE_VERBOSE_LOGS=1 \
 *     -DMCP_ENABLE_DATA_LOGS=1 -DMCP_ENABLE_PERF_LOGS=1 \
 *     websocket_logging_example.c -o example_dev
 * 
 * Production build (minimal logging):
 * gcc -DMCP_ENABLE_DEBUG_LOGS=0 -DMCP_ENABLE_VERBOSE_LOGS=0 \
 *     -DMCP_ENABLE_DATA_LOGS=0 -DMCP_ENABLE_PERF_LOGS=0 \
 *     websocket_logging_example.c -o example_prod
 * 
 * Debug build (debug + performance only):
 * gcc -DMCP_ENABLE_DEBUG_LOGS=1 -DMCP_ENABLE_VERBOSE_LOGS=0 \
 *     -DMCP_ENABLE_DATA_LOGS=0 -DMCP_ENABLE_PERF_LOGS=1 \
 *     websocket_logging_example.c -o example_debug
 */
