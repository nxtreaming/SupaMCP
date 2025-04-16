#include "mcp_performance_metrics.h"
#include "mcp_log.h"
#include "mcp_server.h"
#include <string.h>

// Helper function to initialize performance metrics collection
static int init_performance_metrics(void) {
    static bool initialized = false;

    if (!initialized) {
        if (mcp_performance_metrics_init() != 0) {
            mcp_log_error("Failed to initialize performance metrics");
            return -1;
        }
        initialized = true;
        mcp_log_info("Performance metrics collection initialized");
    }

    return 0;
}

// Function to be called before processing a request
void mcp_performance_collect_request_start(mcp_performance_timer_t* timer) {
    if (init_performance_metrics() != 0) {
        return;
    }

    if (timer) {
        mcp_performance_timer_start(timer);
    }

    // Increment active connections
    mcp_performance_metrics_update_connections(1);
}

// Function to be called after processing a request
void mcp_performance_collect_request_end(mcp_performance_timer_t* timer, bool success,
                                        size_t bytes_sent, size_t bytes_received) {
    if (!timer || !timer->running) {
        return;
    }

    uint64_t latency_us = mcp_performance_timer_stop(timer);

    // Record request metrics
    mcp_performance_metrics_record_request(success, latency_us, bytes_sent, bytes_received);

    // Decrement active connections
    mcp_performance_metrics_update_connections(-1);
}

// Function to be called when a request times out
void mcp_performance_collect_request_timeout(void) {
    if (init_performance_metrics() != 0) {
        return;
    }

    mcp_performance_metrics_record_timeout();
}

// Function to be called periodically to export metrics
int mcp_performance_export_metrics(const char* filename) {
    if (init_performance_metrics() != 0) {
        return -1;
    }

    return mcp_performance_metrics_export(filename);
}

// Function to get current performance metrics as JSON
int mcp_performance_get_metrics_json(char* buffer, size_t size) {
    if (init_performance_metrics() != 0) {
        return -1;
    }

    return mcp_performance_metrics_to_json(buffer, size);
}


