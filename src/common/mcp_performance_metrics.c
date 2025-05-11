#include "mcp_performance_metrics.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include "mcp_atom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Define TIME_UTC for Windows if not already defined
#ifdef _WIN32
#ifndef TIME_UTC
#define TIME_UTC 1
#endif
#endif

// Global metrics instance
static mcp_performance_metrics_t g_metrics;
static mcp_mutex_t* g_metrics_mutex = NULL;
static bool g_initialized = false;

// Constants for time conversion
#define MICROSECONDS_PER_SECOND 1000000ULL
#define NANOSECONDS_PER_MICROSECOND 1000ULL

/**
 * @brief Get current time in microseconds with high precision
 *
 * This function uses the most precise clock available on each platform.
 *
 * @return Current time in microseconds
 */
static inline uint64_t get_current_time_us(void) {
    struct timespec ts;
#ifdef _WIN32
    // On Windows, use timespec_get with TIME_UTC
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        // Fallback to less precise time if timespec_get fails
        time_t now = time(NULL);
        ts.tv_sec = now;
        ts.tv_nsec = 0;
    }
#else
    // On POSIX systems, use CLOCK_MONOTONIC for better precision
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        // Fallback to less precise time if clock_gettime fails
        time_t now = time(NULL);
        ts.tv_sec = now;
        ts.tv_nsec = 0;
    }
#endif
    return (uint64_t)ts.tv_sec * MICROSECONDS_PER_SECOND +
           (uint64_t)ts.tv_nsec / NANOSECONDS_PER_MICROSECOND;
}

/**
 * @brief Calculate time difference between two timespec structures in microseconds
 *
 * This function handles potential overflow by using 64-bit arithmetic.
 *
 * @param start Pointer to start timespec
 * @param end Pointer to end timespec
 * @return Time difference in microseconds
 */
static inline uint64_t timespec_diff_us(const struct timespec* start, const struct timespec* end) {
    // Ensure end time is greater than or equal to start time
    if (end->tv_sec < start->tv_sec ||
        (end->tv_sec == start->tv_sec && end->tv_nsec < start->tv_nsec)) {
        return 0;
    }

    uint64_t start_us = (uint64_t)start->tv_sec * MICROSECONDS_PER_SECOND +
                        (uint64_t)start->tv_nsec / NANOSECONDS_PER_MICROSECOND;
    uint64_t end_us = (uint64_t)end->tv_sec * MICROSECONDS_PER_SECOND +
                      (uint64_t)end->tv_nsec / NANOSECONDS_PER_MICROSECOND;

    return end_us - start_us;
}

/**
 * @brief Initialize the performance metrics system
 *
 * This function initializes the global metrics instance and mutex.
 * It is thread-safe and can be called multiple times (subsequent calls are no-ops).
 *
 * @return 0 on success, -1 on failure
 */
int mcp_performance_metrics_init(void) {
    // Fast path for already initialized
    if (g_initialized) {
        mcp_log_debug("Performance metrics system already initialized");
        return 0;
    }

    // Initialize mutex for thread safety
    g_metrics_mutex = mcp_mutex_create();
    if (!g_metrics_mutex) {
        mcp_log_error("Failed to create performance metrics mutex");
        return -1;
    }

    // Initialize metrics with zeros
    memset(&g_metrics, 0, sizeof(mcp_performance_metrics_t));

    // Set initial values
    time_t current_time = time(NULL);

    // Set min latency to max value so first request will become the minimum
    MCP_ATOMIC_STORE(g_metrics.min_latency_us, MCP_METRICS_MAX_LATENCY_THRESHOLD);
    MCP_ATOMIC_STORE(g_metrics.start_time, current_time);
    MCP_ATOMIC_STORE(g_metrics.last_reset_time, current_time);

    g_initialized = true;
    mcp_log_info("Performance metrics system initialized successfully");
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

/**
 * @brief Convert performance metrics to JSON format
 *
 * This function generates a JSON representation of the current performance metrics.
 * It includes derived metrics like average latency, error rate, and throughput.
 *
 * @param buffer Buffer to store the JSON string
 * @param size Size of the buffer
 * @return Number of bytes written to the buffer, or -1 on error
 */
int mcp_performance_metrics_to_json(char* buffer, size_t size) {
    // Validate parameters
    if (!g_initialized || !buffer || size < MCP_METRICS_MIN_BUFFER_SIZE) {
        mcp_log_error("Invalid parameters for metrics JSON conversion");
        return -1;
    }

    // Get a snapshot of the metrics to ensure consistency
    mcp_performance_metrics_t* metrics = &g_metrics;

    // Atomic load all metrics at once to get a consistent snapshot
    uint64_t total_requests = MCP_ATOMIC_LOAD(metrics->total_requests);
    uint64_t successful_requests = MCP_ATOMIC_LOAD(metrics->successful_requests);
    uint64_t failed_requests = MCP_ATOMIC_LOAD(metrics->failed_requests);
    uint64_t timeout_requests = MCP_ATOMIC_LOAD(metrics->timeout_requests);
    uint64_t total_latency_us = MCP_ATOMIC_LOAD(metrics->total_latency_us);
    uint64_t min_latency_us = MCP_ATOMIC_LOAD(metrics->min_latency_us);
    uint64_t max_latency_us = MCP_ATOMIC_LOAD(metrics->max_latency_us);
    uint64_t bytes_sent = MCP_ATOMIC_LOAD(metrics->bytes_sent);
    uint64_t bytes_received = MCP_ATOMIC_LOAD(metrics->bytes_received);
    uint64_t active_connections = MCP_ATOMIC_LOAD(metrics->active_connections);
    uint64_t peak_connections = MCP_ATOMIC_LOAD(metrics->peak_connections);
    time_t start_time = MCP_ATOMIC_LOAD(metrics->start_time);

    // Calculate derived metrics
    time_t now = time(NULL);
    double uptime_seconds = difftime(now, start_time);

    // Avoid division by zero
    uint64_t avg_latency_us = (total_requests > 0) ? (total_latency_us / total_requests) : 0;
    double requests_per_second = (uptime_seconds > 0) ? ((double)total_requests / uptime_seconds) : 0;
    double error_rate = (total_requests > 0) ?
        (100.0 * (failed_requests + timeout_requests) / total_requests) : 0.0;
    double bytes_per_second = (uptime_seconds > 0) ?
        ((double)(bytes_sent + bytes_received) / uptime_seconds) : 0;

    // Handle special case for min latency
    if (min_latency_us == MCP_METRICS_MAX_LATENCY_THRESHOLD) {
        min_latency_us = 0; // No requests processed yet
    }

    // Format JSON with platform-independent format specifiers
    int written = snprintf(buffer, size,
        "{\n"
        "  \"timestamp\": %ld,\n"
        "  \"uptime_seconds\": %.2f,\n"
        "  \"requests\": {\n"
#ifdef _WIN32
        "    \"total\": %I64u,\n"
        "    \"successful\": %I64u,\n"
        "    \"failed\": %I64u,\n"
        "    \"timeout\": %I64u,\n"
#else
        "    \"total\": %llu,\n"
        "    \"successful\": %llu,\n"
        "    \"failed\": %llu,\n"
        "    \"timeout\": %llu,\n"
#endif
        "    \"per_second\": %.2f,\n"
        "    \"error_rate_percent\": %.2f\n"
        "  },\n"
        "  \"latency_us\": {\n"
#ifdef _WIN32
        "    \"min\": %I64u,\n"
        "    \"max\": %I64u,\n"
        "    \"avg\": %I64u\n"
#else
        "    \"min\": %llu,\n"
        "    \"max\": %llu,\n"
        "    \"avg\": %llu\n"
#endif
        "  },\n"
        "  \"throughput\": {\n"
#ifdef _WIN32
        "    \"bytes_sent\": %I64u,\n"
        "    \"bytes_received\": %I64u,\n"
#else
        "    \"bytes_sent\": %llu,\n"
        "    \"bytes_received\": %llu,\n"
#endif
        "    \"bytes_per_second\": %.2f\n"
        "  },\n"
        "  \"connections\": {\n"
#ifdef _WIN32
        "    \"active\": %I64u,\n"
        "    \"peak\": %I64u\n"
#else
        "    \"active\": %llu,\n"
        "    \"peak\": %llu\n"
#endif
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
        min_latency_us,
        max_latency_us,
        avg_latency_us,
        bytes_sent,
        bytes_received,
        bytes_per_second,
        active_connections,
        peak_connections
    );

    // Check for buffer overflow
    if (written >= (int)size) {
        mcp_log_error("Buffer too small for metrics JSON (need %d bytes, have %zu)",
                     written, size);
        return -1;
    }

    return written;
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

/**
 * @brief Export performance metrics to a file in JSON format
 *
 * This function generates a JSON representation of the current performance metrics
 * and writes it to the specified file.
 *
 * @param filename Name of the file to export to
 * @return 0 on success, -1 on failure
 */
int mcp_performance_metrics_export(const char* filename) {
    // Validate parameters
    if (!g_initialized || !filename || !*filename) {
        mcp_log_error("Invalid parameters for metrics export");
        return -1;
    }

    // Buffer for JSON data - use the defined constant
    char buffer[MCP_METRICS_DEFAULT_BUFFER_SIZE];

    // Generate JSON representation
    int written = mcp_performance_metrics_to_json(buffer, sizeof(buffer));
    if (written < 0) {
        mcp_log_error("Failed to generate performance metrics JSON");
        return -1;
    }

    // Open file for writing with error handling
    FILE* file = NULL;
#ifdef _MSC_VER
    // Use fopen_s on Windows for security
    errno_t err = fopen_s(&file, filename, "w");
    if (err != 0 || !file) {
        mcp_log_error("Failed to open file for performance metrics export: %s (error %d)",
                     filename, err);
        return -1;
    }
#else
    // Use standard fopen on other platforms
    file = fopen(filename, "w");
    if (!file) {
        mcp_log_error("Failed to open file for performance metrics export: %s (error %d)",
                     filename, errno);
        return -1;
    }
#endif

    // Write JSON data to file
    size_t bytes_written = fwrite(buffer, 1, written, file);

    // Ensure data is flushed to disk
    fflush(file);
    fclose(file);

    // Verify all data was written
    if (bytes_written != (size_t)written) {
        mcp_log_error("Failed to write performance metrics to file: %s (wrote %zu of %d bytes)",
                     filename, bytes_written, written);
        return -1;
    }

    mcp_log_info("Performance metrics exported to %s (%d bytes)", filename, written);
    return 0;
}
