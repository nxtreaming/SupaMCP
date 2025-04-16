#include "mcp_performance_metrics.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global metrics instance
static mcp_performance_metrics_t g_metrics;
static mcp_mutex_t* g_metrics_mutex = NULL;
static bool g_initialized = false;

// Helper function to get current time in microseconds
static uint64_t get_current_time_us() {
    struct timespec ts;
#ifdef _WIN32
    timespec_get(&ts, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// Helper function to calculate time difference in microseconds
static uint64_t timespec_diff_us(struct timespec* start, struct timespec* end) {
    uint64_t start_us = (uint64_t)start->tv_sec * 1000000 + (uint64_t)start->tv_nsec / 1000;
    uint64_t end_us = (uint64_t)end->tv_sec * 1000000 + (uint64_t)end->tv_nsec / 1000;
    return end_us - start_us;
}

int mcp_performance_metrics_init(void) {
    if (g_initialized) {
        mcp_log_warn("Performance metrics system already initialized");
        return 0;
    }

    // Initialize mutex
    g_metrics_mutex = mcp_mutex_create();
    if (!g_metrics_mutex) {
        mcp_log_error("Failed to create performance metrics mutex");
        return -1;
    }

    // Initialize metrics
    memset(&g_metrics, 0, sizeof(mcp_performance_metrics_t));

    // Set initial values
    MCP_ATOMIC_STORE(g_metrics.min_latency_us, UINT64_MAX);
    MCP_ATOMIC_STORE(g_metrics.start_time, time(NULL));
    MCP_ATOMIC_STORE(g_metrics.last_reset_time, time(NULL));

    g_initialized = true;
    mcp_log_info("Performance metrics system initialized");
    return 0;
}

void mcp_performance_metrics_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    mcp_mutex_destroy(g_metrics_mutex);
    g_metrics_mutex = NULL;
    g_initialized = false;
    mcp_log_info("Performance metrics system shutdown");
}

mcp_performance_metrics_t* mcp_performance_metrics_get_instance(void) {
    if (!g_initialized) {
        mcp_log_warn("Performance metrics system not initialized");
        return NULL;
    }
    return &g_metrics;
}

void mcp_performance_metrics_reset(void) {
    if (!g_initialized) {
        mcp_log_warn("Performance metrics system not initialized");
        return;
    }

    mcp_mutex_lock(g_metrics_mutex);

    // Reset all counters
    MCP_ATOMIC_STORE(g_metrics.total_requests, 0);
    MCP_ATOMIC_STORE(g_metrics.successful_requests, 0);
    MCP_ATOMIC_STORE(g_metrics.failed_requests, 0);
    MCP_ATOMIC_STORE(g_metrics.timeout_requests, 0);
    MCP_ATOMIC_STORE(g_metrics.total_latency_us, 0);
    MCP_ATOMIC_STORE(g_metrics.min_latency_us, UINT64_MAX);
    MCP_ATOMIC_STORE(g_metrics.max_latency_us, 0);
    MCP_ATOMIC_STORE(g_metrics.bytes_sent, 0);
    MCP_ATOMIC_STORE(g_metrics.bytes_received, 0);
    // Don't reset active_connections as it represents current state
    MCP_ATOMIC_STORE(g_metrics.peak_connections, MCP_ATOMIC_LOAD(g_metrics.active_connections));
    MCP_ATOMIC_STORE(g_metrics.last_reset_time, time(NULL));

    mcp_mutex_unlock(g_metrics_mutex);
    mcp_log_info("Performance metrics reset");
}

void mcp_performance_metrics_record_request(bool success, uint64_t latency_us,
                                           uint64_t bytes_sent, uint64_t bytes_received) {
    if (!g_initialized) {
        mcp_log_warn("Performance metrics system not initialized");
        return;
    }

    // Increment total requests
    MCP_ATOMIC_ADD(g_metrics.total_requests, 1);

    // Update success/failure counters
    if (success) {
        MCP_ATOMIC_ADD(g_metrics.successful_requests, 1);
    } else {
        MCP_ATOMIC_ADD(g_metrics.failed_requests, 1);
    }

    // Update latency metrics
    MCP_ATOMIC_ADD(g_metrics.total_latency_us, latency_us);

    // Update min latency (using compare-and-swap)
    uint64_t current_min = MCP_ATOMIC_LOAD(g_metrics.min_latency_us);
    while (latency_us < current_min &&
           !MCP_ATOMIC_COMPARE_EXCHANGE(g_metrics.min_latency_us, current_min, latency_us)) {
        // If CAS failed, current_min has been updated with the latest value, try again
        current_min = MCP_ATOMIC_LOAD(g_metrics.min_latency_us);
    }

    // Update max latency (using compare-and-swap)
    uint64_t current_max = MCP_ATOMIC_LOAD(g_metrics.max_latency_us);
    while (latency_us > current_max &&
           !MCP_ATOMIC_COMPARE_EXCHANGE(g_metrics.max_latency_us, current_max, latency_us)) {
        // If CAS failed, current_max has been updated with the latest value, try again
        current_max = MCP_ATOMIC_LOAD(g_metrics.max_latency_us);
    }

    // Update byte counters
    MCP_ATOMIC_ADD(g_metrics.bytes_sent, bytes_sent);
    MCP_ATOMIC_ADD(g_metrics.bytes_received, bytes_received);
}

void mcp_performance_metrics_record_timeout(void) {
    if (!g_initialized) {
        mcp_log_warn("Performance metrics system not initialized");
        return;
    }

    MCP_ATOMIC_ADD(g_metrics.total_requests, 1);
    MCP_ATOMIC_ADD(g_metrics.timeout_requests, 1);
}

void mcp_performance_metrics_update_connections(int delta) {
    if (!g_initialized) {
        mcp_log_warn("Performance metrics system not initialized");
        return;
    }

    // Update active connections
    uint64_t new_count;
    if (delta > 0) {
        new_count = MCP_ATOMIC_ADD(g_metrics.active_connections, delta) + delta;
    } else {
        // Ensure we don't go below zero
        uint64_t abs_delta = (uint64_t)(-delta);
        uint64_t current = MCP_ATOMIC_LOAD(g_metrics.active_connections);

        if (current >= abs_delta) {
            new_count = MCP_ATOMIC_ADD(g_metrics.active_connections, -((int64_t)abs_delta)) - abs_delta;
        } else {
            MCP_ATOMIC_STORE(g_metrics.active_connections, 0);
            new_count = 0;
        }
    }

    // Update peak connections if needed
    uint64_t current_peak = MCP_ATOMIC_LOAD(g_metrics.peak_connections);
    while (new_count > current_peak &&
           !MCP_ATOMIC_COMPARE_EXCHANGE(g_metrics.peak_connections, current_peak, new_count)) {
        // If CAS failed, current_peak has been updated with the latest value, try again
        current_peak = MCP_ATOMIC_LOAD(g_metrics.peak_connections);
    }
}

mcp_performance_timer_t mcp_performance_timer_create(void) {
    mcp_performance_timer_t timer;
    timer.running = false;
    memset(&timer.start_time, 0, sizeof(struct timespec));
    return timer;
}

void mcp_performance_timer_start(mcp_performance_timer_t* timer) {
    if (!timer) {
        return;
    }

#ifdef _WIN32
    timespec_get(&timer->start_time, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &timer->start_time);
#endif
    timer->running = true;
}

uint64_t mcp_performance_timer_stop(mcp_performance_timer_t* timer) {
    if (!timer || !timer->running) {
        return 0;
    }

    struct timespec end_time;
#ifdef _WIN32
    timespec_get(&end_time, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, &end_time);
#endif

    uint64_t elapsed_us = timespec_diff_us(&timer->start_time, &end_time);
    timer->running = false;
    return elapsed_us;
}

int mcp_performance_metrics_to_json(char* buffer, size_t size) {
    if (!g_initialized || !buffer || size == 0) {
        return -1;
    }

    mcp_performance_metrics_t* metrics = &g_metrics;

    // Calculate derived metrics
    uint64_t total_requests = MCP_ATOMIC_LOAD(metrics->total_requests);
    uint64_t total_latency_us = MCP_ATOMIC_LOAD(metrics->total_latency_us);
    uint64_t avg_latency_us = (total_requests > 0) ? (total_latency_us / total_requests) : 0;

    time_t now = time(NULL);
    time_t start_time = MCP_ATOMIC_LOAD(metrics->start_time);
    double uptime_seconds = difftime(now, start_time);
    double requests_per_second = (uptime_seconds > 0) ? ((double)total_requests / uptime_seconds) : 0;

    uint64_t successful_requests = MCP_ATOMIC_LOAD(metrics->successful_requests);
    uint64_t failed_requests = MCP_ATOMIC_LOAD(metrics->failed_requests);
    uint64_t timeout_requests = MCP_ATOMIC_LOAD(metrics->timeout_requests);
    double error_rate = (total_requests > 0) ?
        (100.0 * (failed_requests + timeout_requests) / total_requests) : 0.0;

    uint64_t bytes_sent = MCP_ATOMIC_LOAD(metrics->bytes_sent);
    uint64_t bytes_received = MCP_ATOMIC_LOAD(metrics->bytes_received);
    double bytes_per_second = (uptime_seconds > 0) ?
        ((double)(bytes_sent + bytes_received) / uptime_seconds) : 0;

    // Format JSON
    int written = snprintf(buffer, size,
        "{\n"
        "  \"timestamp\": %ld,\n"
        "  \"uptime_seconds\": %.2f,\n"
        "  \"requests\": {\n"
        "    \"total\": %I64u,\n"
        "    \"successful\": %I64u,\n"
        "    \"failed\": %I64u,\n"
        "    \"timeout\": %I64u,\n"
        "    \"per_second\": %.2f,\n"
        "    \"error_rate_percent\": %.2f\n"
        "  },\n"
        "  \"latency_us\": {\n"
        "    \"min\": %I64u,\n"
        "    \"max\": %I64u,\n"
        "    \"avg\": %I64u\n"
        "  },\n"
        "  \"throughput\": {\n"
        "    \"bytes_sent\": %I64u,\n"
        "    \"bytes_received\": %I64u,\n"
        "    \"bytes_per_second\": %.2f\n"
        "  },\n"
        "  \"connections\": {\n"
        "    \"active\": %I64u,\n"
        "    \"peak\": %I64u\n"
        "  }\n"
        "}",
        (long)now,
        uptime_seconds,
        total_requests,
        successful_requests,
        failed_requests,
        timeout_requests,
        requests_per_second,
        error_rate,
        MCP_ATOMIC_LOAD(metrics->min_latency_us) == UINT64_MAX ? 0 : MCP_ATOMIC_LOAD(metrics->min_latency_us),
        MCP_ATOMIC_LOAD(metrics->max_latency_us),
        avg_latency_us,
        bytes_sent,
        bytes_received,
        bytes_per_second,
        MCP_ATOMIC_LOAD(metrics->active_connections),
        MCP_ATOMIC_LOAD(metrics->peak_connections)
    );

    return (written >= (int)size) ? -1 : written;
}

uint64_t mcp_performance_metrics_get_avg_latency(void) {
    if (!g_initialized) {
        return 0;
    }

    uint64_t total_requests = MCP_ATOMIC_LOAD(g_metrics.total_requests);
    if (total_requests == 0) {
        return 0;
    }

    uint64_t total_latency_us = MCP_ATOMIC_LOAD(g_metrics.total_latency_us);
    return total_latency_us / total_requests;
}

double mcp_performance_metrics_get_throughput(void) {
    if (!g_initialized) {
        return 0.0;
    }

    uint64_t total_requests = MCP_ATOMIC_LOAD(g_metrics.total_requests);
    time_t start_time = MCP_ATOMIC_LOAD(g_metrics.start_time);
    time_t now = time(NULL);
    double uptime_seconds = difftime(now, start_time);

    if (uptime_seconds <= 0) {
        return 0.0;
    }

    return (double)total_requests / uptime_seconds;
}

double mcp_performance_metrics_get_error_rate(void) {
    if (!g_initialized) {
        return 0.0;
    }

    uint64_t total_requests = MCP_ATOMIC_LOAD(g_metrics.total_requests);
    if (total_requests == 0) {
        return 0.0;
    }

    uint64_t failed_requests = MCP_ATOMIC_LOAD(g_metrics.failed_requests);
    uint64_t timeout_requests = MCP_ATOMIC_LOAD(g_metrics.timeout_requests);
    return 100.0 * (double)(failed_requests + timeout_requests) / (double)total_requests;
}

int mcp_performance_metrics_export(const char* filename) {
    if (!g_initialized || !filename) {
        return -1;
    }

    // Buffer for JSON data
    char buffer[4096];
    int written = mcp_performance_metrics_to_json(buffer, sizeof(buffer));
    if (written < 0) {
        mcp_log_error("Failed to generate performance metrics JSON");
        return -1;
    }

    // Open file for writing
    FILE* file = fopen(filename, "w");
    if (!file) {
        mcp_log_error("Failed to open file for performance metrics export: %s", filename);
        return -1;
    }

    // Write JSON data to file
    size_t bytes_written = fwrite(buffer, 1, written, file);
    fclose(file);

    if (bytes_written != (size_t)written) {
        mcp_log_error("Failed to write performance metrics to file: %s", filename);
        return -1;
    }

    mcp_log_info("Performance metrics exported to %s", filename);
    return 0;
}
