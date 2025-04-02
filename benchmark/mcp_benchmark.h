#ifndef MCP_BENCHMARK_H
#define MCP_BENCHMARK_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for a benchmark run.
 */
typedef struct {
    const char* name;                 // Name of the benchmark scenario
    size_t client_count;              // Number of concurrent clients
    size_t requests_per_client;       // Number of requests each client sends
    size_t concurrent_requests;       // Max concurrent requests allowed across all clients (0 for unlimited)
    bool random_delays;               // Whether clients should introduce random delays between requests
    int min_delay_ms;                 // Minimum delay in milliseconds (if random_delays is true)
    int max_delay_ms;                 // Maximum delay in milliseconds (if random_delays is true)
    const char* test_resource_uri;    // URI for resource access tests (optional)
    const char* test_tool_name;       // Tool name for tool call tests (optional)
    const char* test_tool_args;       // JSON string arguments for tool call tests (optional)
    const char* server_host;          // Server host address
    int server_port;                  // Server port
    int request_timeout_ms;           // Timeout for individual requests in milliseconds
} mcp_benchmark_config_t;

/**
 * @brief Results of a benchmark run.
 */
typedef struct {
    double min_latency_ms;            // Minimum request latency
    double max_latency_ms;            // Maximum request latency
    double avg_latency_ms;            // Average request latency
    double p50_latency_ms;            // 50th percentile latency (median)
    double p90_latency_ms;            // 90th percentile latency
    double p99_latency_ms;            // 99th percentile latency
    double total_duration_s;          // Total duration of the benchmark run in seconds
    size_t requests_per_second;       // Average requests per second (throughput)
    size_t successful_requests;       // Total number of successful requests
    size_t failed_requests;           // Total number of failed requests (errors)
    size_t timeout_requests;          // Total number of requests that timed out
} mcp_benchmark_result_t;

/**
 * @brief Runs a benchmark based on the provided configuration.
 *
 * @param config The benchmark configuration.
 * @param result Pointer to store the benchmark results.
 * @return 0 on success, -1 on failure.
 */
int mcp_run_benchmark(const mcp_benchmark_config_t* config, mcp_benchmark_result_t* result);

/**
 * @brief Saves benchmark results to a CSV file.
 *
 * @param filename The name of the CSV file to save to.
 * @param results An array of benchmark results.
 * @param count The number of results in the array.
 * @return 0 on success, -1 on failure.
 */
int mcp_benchmark_save_results(const char* filename, const mcp_benchmark_result_t* results, size_t count);

/**
 * @brief Compares two benchmark results and prints a summary.
 *
 * @param baseline The baseline benchmark result.
 * @param current The current benchmark result to compare against the baseline.
 */
void mcp_benchmark_compare(const mcp_benchmark_result_t* baseline, const mcp_benchmark_result_t* current);

#ifdef __cplusplus
}
#endif

#endif // MCP_BENCHMARK_H
