#ifndef MCP_PERFORMANCE_METRICS_H
#define MCP_PERFORMANCE_METRICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "mcp_atom.h"
#include "mcp_cache_aligned.h"

// Constants for performance metrics
#define MCP_METRICS_DEFAULT_BUFFER_SIZE 4096
#define MCP_METRICS_MIN_BUFFER_SIZE 1024
#define MCP_METRICS_MAX_LATENCY_THRESHOLD UINT64_MAX

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Performance metrics collection structure
 *
 * This structure is cache-aligned to prevent false sharing in multi-threaded environments.
 * Each group of related metrics is placed on separate cache lines.
 */
typedef struct mcp_performance_metrics {
    // Request metrics - first cache line
    MCP_ATOMIC_TYPE(uint64_t) total_requests;          /**< Total number of requests processed */
    MCP_ATOMIC_TYPE(uint64_t) successful_requests;     /**< Number of successful requests */
    MCP_ATOMIC_TYPE(uint64_t) failed_requests;         /**< Number of failed requests */
    MCP_ATOMIC_TYPE(uint64_t) timeout_requests;        /**< Number of timed out requests */
    MCP_CACHE_PADDING(1);                              /**< Padding to align to cache line */

    // Latency metrics (in microseconds) - second cache line
    MCP_ATOMIC_TYPE(uint64_t) total_latency_us;        /**< Total latency of all requests */
    MCP_ATOMIC_TYPE(uint64_t) min_latency_us;          /**< Minimum request latency */
    MCP_ATOMIC_TYPE(uint64_t) max_latency_us;          /**< Maximum request latency */
    MCP_CACHE_PADDING(2);                              /**< Padding to align to cache line */

    // Throughput metrics - third cache line
    MCP_ATOMIC_TYPE(uint64_t) bytes_sent;              /**< Total bytes sent */
    MCP_ATOMIC_TYPE(uint64_t) bytes_received;          /**< Total bytes received */
    MCP_CACHE_PADDING(3);                              /**< Padding to align to cache line */

    // Resource metrics - fourth cache line
    MCP_ATOMIC_TYPE(uint64_t) active_connections;      /**< Current number of active connections */
    MCP_ATOMIC_TYPE(uint64_t) peak_connections;        /**< Peak number of active connections */
    MCP_CACHE_PADDING(4);                              /**< Padding to align to cache line */

    // Time tracking - fifth cache line
    MCP_ATOMIC_TYPE(time_t) start_time;                /**< Time when metrics collection started */
    MCP_ATOMIC_TYPE(time_t) last_reset_time;           /**< Time when metrics were last reset */
    MCP_CACHE_PADDING(5);                              /**< Padding to align to cache line */
} MCP_CACHE_ALIGNED mcp_performance_metrics_t;

/**
 * @brief Timer structure for measuring operation durations
 *
 * This structure is optimized for fast time measurements with minimal overhead.
 */
typedef struct mcp_performance_timer {
    struct timespec start_time;                /**< Start time of the operation */
    bool running;                              /**< Whether the timer is running */
    uint8_t padding[7];                        /**< Padding for alignment */
} mcp_performance_timer_t;

/**
 * @brief Initialize the performance metrics system
 *
 * @return 0 on success, -1 on failure
 */
int mcp_performance_metrics_init(void);

/**
 * @brief Shutdown the performance metrics system
 */
void mcp_performance_metrics_shutdown(void);

/**
 * @brief Get the global performance metrics instance
 *
 * @return Pointer to the global metrics instance
 */
mcp_performance_metrics_t* mcp_performance_metrics_get_instance(void);

/**
 * @brief Reset all performance metrics
 */
void mcp_performance_metrics_reset(void);

/**
 * @brief Record a request being processed
 *
 * @param success Whether the request was successful
 * @param latency_us Latency of the request in microseconds
 * @param bytes_sent Number of bytes sent
 * @param bytes_received Number of bytes received
 */
void mcp_performance_metrics_record_request(bool success, uint64_t latency_us,
                                           uint64_t bytes_sent, uint64_t bytes_received);

/**
 * @brief Record a request timeout
 */
void mcp_performance_metrics_record_timeout(void);

/**
 * @brief Update the active connections count
 *
 * @param delta Change in the number of connections (positive for new connections, negative for closed connections)
 */
void mcp_performance_metrics_update_connections(int delta);

/**
 * @brief Create a new performance timer
 *
 * @return Initialized timer structure
 */
mcp_performance_timer_t mcp_performance_timer_create(void);

/**
 * @brief Start a performance timer
 *
 * @param timer Pointer to the timer structure
 */
void mcp_performance_timer_start(mcp_performance_timer_t* timer);

/**
 * @brief Stop a performance timer and get the elapsed time
 *
 * @param timer Pointer to the timer structure
 * @return Elapsed time in microseconds
 */
uint64_t mcp_performance_timer_stop(mcp_performance_timer_t* timer);

/**
 * @brief Get the current performance metrics as a JSON string
 *
 * @param buffer Buffer to store the JSON string
 * @param size Size of the buffer
 * @return Number of bytes written to the buffer, or -1 on error
 */
int mcp_performance_metrics_to_json(char* buffer, size_t size);

/**
 * @brief Calculate the average request latency
 *
 * @return Average latency in microseconds, or 0 if no requests have been processed
 */
uint64_t mcp_performance_metrics_get_avg_latency(void);

/**
 * @brief Calculate the current request throughput
 *
 * @return Requests per second, or 0 if metrics collection just started
 */
double mcp_performance_metrics_get_throughput(void);

/**
 * @brief Calculate the error rate
 *
 * @return Error rate as a percentage (0-100), or 0 if no requests have been processed
 */
double mcp_performance_metrics_get_error_rate(void);

/**
 * @brief Export performance metrics to a file in JSON format
 *
 * @param filename Name of the file to export to
 * @return 0 on success, -1 on failure
 */
int mcp_performance_metrics_export(const char* filename);

#ifdef __cplusplus
}
#endif

#endif /* MCP_PERFORMANCE_METRICS_H */
