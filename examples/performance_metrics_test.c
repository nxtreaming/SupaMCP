#include "mcp_performance_metrics.h"
#include "mcp_performance_collector.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep((DWORD)(ms))
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

// Simulate a request with given parameters
void simulate_request(bool success, uint64_t latency_ms, size_t request_size, size_t response_size) {
    // Create a timer
    mcp_performance_timer_t timer = mcp_performance_timer_create();

    // Start collecting metrics for this request
    mcp_performance_collect_request_start(&timer);

    // Simulate processing time
    sleep_ms(latency_ms);

    // End collecting metrics
    mcp_performance_collect_request_end(&timer, success, response_size, request_size);
}

// Simulate a timeout request
void simulate_timeout_request() {
    mcp_performance_collect_request_timeout();
}

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Performance metrics test started");

    // Initialize performance metrics
    if (mcp_performance_metrics_init() != 0) {
        mcp_log_error("Failed to initialize performance metrics");
        return 1;
    }

    // Simulate some successful requests with different latencies
    mcp_log_info("Simulating successful requests...");
    for (int i = 0; i < 10; i++) {
        uint64_t latency = 10 + (rand() % 90); // 10-100ms
        size_t req_size = 100 + (rand() % 900); // 100-1000 bytes
        size_t resp_size = 200 + (rand() % 1800); // 200-2000 bytes

        mcp_log_debug("Request %d: latency=%lums, req_size=%zu, resp_size=%zu",
                     i+1, latency, req_size, resp_size);
        simulate_request(true, latency, req_size, resp_size);
    }

    // Simulate some failed requests
    mcp_log_info("Simulating failed requests...");
    for (int i = 0; i < 3; i++) {
        uint64_t latency = 5 + (rand() % 45); // 5-50ms
        size_t req_size = 50 + (rand() % 450); // 50-500 bytes
        size_t resp_size = 20 + (rand() % 80); // 20-100 bytes (error responses are smaller)

        mcp_log_debug("Failed request %d: latency=%lums, req_size=%zu, resp_size=%zu",
                     i+1, latency, req_size, resp_size);
        simulate_request(false, latency, req_size, resp_size);
    }

    // Simulate some timeout requests
    mcp_log_info("Simulating timeout requests...");
    for (int i = 0; i < 2; i++) {
        mcp_log_debug("Timeout request %d", i+1);
        simulate_timeout_request();
    }

    // Get and display metrics
    char metrics_json[4096];
    if (mcp_performance_get_metrics_json(metrics_json, sizeof(metrics_json)) < 0) {
        mcp_log_error("Failed to get performance metrics");
        return 1;
    }

    mcp_log_info("Performance metrics:");
    printf("\n%s\n", metrics_json);

    // Export metrics to a file
    const char* filename = "performance_metrics.json";
    if (mcp_performance_export_metrics(filename) == 0) {
        mcp_log_info("Performance metrics exported to %s", filename);
    } else {
        mcp_log_error("Failed to export performance metrics");
    }

    // Reset metrics
    mcp_performance_metrics_reset();
    mcp_log_info("Performance metrics reset");

    // Get metrics again after reset
    if (mcp_performance_get_metrics_json(metrics_json, sizeof(metrics_json)) < 0) {
        mcp_log_error("Failed to get performance metrics after reset");
        return 1;
    }

    mcp_log_info("Performance metrics after reset:");
    printf("\n%s\n", metrics_json);

    // Shutdown
    mcp_performance_metrics_shutdown();
    mcp_log_info("Performance metrics test completed");
    mcp_log_close();

    return 0;
}
