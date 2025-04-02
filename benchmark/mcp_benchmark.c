#include "mcp_benchmark.h"
#include "../include/mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

// Platform-specific includes for threads and sockets
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <winsock2.h> // MUST be included before windows.h
#include <windows.h>
#include <process.h> // For _beginthreadex
#else
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#endif

// Define SOCKET type if not defined (e.g., on POSIX)
#ifndef _WIN32
#ifndef SOCKET
typedef int SOCKET;
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#endif


// --- Benchmark Implementation ---

// Structure to pass arguments to each client thread
typedef struct {
    const mcp_benchmark_config_t* config;
    size_t client_id;
    size_t num_requests;
    double* latencies; // Array to store latencies for this thread
    size_t* success_count;
    size_t* failure_count;
    size_t* timeout_count;
    // Add atomic counters if using C11 atomics or platform specifics
} client_thread_args_t;

// Structure to hold aggregated results from all threads
typedef struct {
    size_t total_success;
    size_t total_failure;
    size_t total_timeout;
    double min_latency;
    double max_latency;
    double total_latency;
    size_t total_requests_processed; // successful + failed + timeout
} aggregated_results_t;


// Forward declaration for the client thread function
#ifdef _WIN32
static unsigned __stdcall client_thread_func(void* arg);
#else
static void* client_thread_func(void* arg);
#endif

// Forward declaration for socket connection helper (assuming it exists elsewhere or define basic one)
// For simplicity, let's assume a basic connect/close helper exists or is defined here.
// If using connection pool, include mcp_connection_pool.h and use its functions.
// #include "../include/mcp_connection_pool.h" // Example if using pool

// Basic socket connection helper (replace with actual implementation or pool usage)
// NOTE: This is a VERY basic simulation and doesn't use the real connection pool logic yet.
static SOCKET connect_socket(const char* host, int port, int timeout_ms) {
     // Simplified: Use a function similar to create_new_connection from pool example
     // This is just a placeholder call, real implementation needed
     (void)host; (void)port; (void)timeout_ms;
     // Simulate connection attempt
     #ifdef _WIN32
        // usleep not standard on Windows, use Sleep
        Sleep(5 + rand() % 10); // Simulate 5-15ms connection time
     #else
        usleep((5000 + rand() % 10000)); // Simulate 5-15ms connection time
     #endif
     // Simulate connection failure/timeout based on random chance for this placeholder
     int outcome = rand() % 100;
     if (outcome < 5) return INVALID_SOCKET; // 5% failure
     // if (outcome < 8) return -2; // Simulate timeout (using a different code) - Not used here yet
     return 1; // Return dummy valid socket
}
static void close_socket(SOCKET sock) {
    (void)sock; // No-op for dummy socket
}

// Comparison function for qsort (sorting latencies for percentile calculation)
static int compare_doubles(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

// Helper function to get current time in milliseconds (if not already available)
#ifndef get_current_time_ms
static long long get_current_time_ms() {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}
#endif

// --- Client Thread Function ---
#ifdef _WIN32
static unsigned __stdcall client_thread_func(void* arg) {
#else
static void* client_thread_func(void* arg) {
#endif
    client_thread_args_t* args = (client_thread_args_t*)arg;
    const mcp_benchmark_config_t* config = args->config;
    size_t success = 0;
    size_t failure = 0;
    size_t timeout = 0; // Placeholder for timeout tracking

    // Seed random generator per thread if using random delays
    srand((unsigned int)time(NULL) ^ (unsigned int)args->client_id);

    for (size_t i = 0; i < args->num_requests; ++i) {
        long long req_start_time = get_current_time_ms();

        // Simulate performing a request (e.g., connect, send/recv, close)
        // Using simplified connect/close for now
        SOCKET sock = connect_socket(config->server_host, config->server_port, config->request_timeout_ms);

        long long req_end_time = get_current_time_ms();
        double latency_ms = (double)(req_end_time - req_start_time);

        if (sock != INVALID_SOCKET) {
            // Simulate work & close
            close_socket(sock);
            args->latencies[success] = latency_ms; // Store latency only on success
            success++;
        } else {
            // Simple failure simulation for now
            failure++;
            // TODO: Differentiate between connection failure and timeout
        }

        // Optional random delay between requests
        if (config->random_delays && config->max_delay_ms > 0) {
            int delay = config->min_delay_ms;
            if (config->max_delay_ms > config->min_delay_ms) {
                delay += rand() % (config->max_delay_ms - config->min_delay_ms + 1);
            }
            #ifdef _WIN32
                Sleep(delay);
            #else
                usleep(delay * 1000);
            #endif
        }
    }

    // Store results back (using simple pointers, assumes no race condition after join)
    *(args->success_count) = success;
    *(args->failure_count) = failure;
    *(args->timeout_count) = timeout; // Timeout tracking not implemented in this simple version

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}


// --- Main Benchmark Function ---
int mcp_run_benchmark(const mcp_benchmark_config_t* config, mcp_benchmark_result_t* result) {
    if (!config || !result) {
        log_message(LOG_LEVEL_ERROR, "mcp_run_benchmark received NULL arguments.");
        return -1;
    }
    if (config->client_count == 0 || config->requests_per_client == 0) {
         log_message(LOG_LEVEL_ERROR, "mcp_run_benchmark: client_count and requests_per_client must be > 0.");
        return -1;
    }

    log_message(LOG_LEVEL_INFO, "Starting benchmark: %s", config->name);
    log_message(LOG_LEVEL_INFO, "  Clients: %zu, Requests/Client: %zu", config->client_count, config->requests_per_client);
    log_message(LOG_LEVEL_INFO, "  Server: %s:%d", config->server_host, config->server_port);

    memset(result, 0, sizeof(mcp_benchmark_result_t));
    result->min_latency_ms = (double)INT_MAX; // Initialize min to max possible value

    size_t total_requests_to_run = config->client_count * config->requests_per_client;
    // Allocate space for all potential latencies (only successful ones will be stored contiguously later)
    double* all_latencies = (double*)malloc(total_requests_to_run * sizeof(double));
    if (!all_latencies) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for latency results.");
        return -1;
    }

    aggregated_results_t aggregated = {0};
    aggregated.min_latency = (double)INT_MAX; // Initialize min latency

#ifdef _WIN32
    HANDLE* threads = (HANDLE*)malloc(config->client_count * sizeof(HANDLE));
#else
    pthread_t* threads = (pthread_t*)malloc(config->client_count * sizeof(pthread_t));
#endif
    client_thread_args_t* thread_args = (client_thread_args_t*)malloc(config->client_count * sizeof(client_thread_args_t));

    if (!threads || !thread_args) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for threads or arguments.");
        free(all_latencies);
        free(threads); // threads might be NULL here, free(NULL) is safe
        free(thread_args); // thread_args might be NULL here
        return -1;
    }

    // Seed random number generator once for the main thread (used by connect_socket placeholder)
    srand((unsigned int)time(NULL));

    long long benchmark_start_time = get_current_time_ms();

    // --- Create and start client threads ---
    for (size_t i = 0; i < config->client_count; ++i) {
        thread_args[i].config = config;
        thread_args[i].client_id = i;
        thread_args[i].num_requests = config->requests_per_client;
        // Assign a portion of the pre-allocated array to each thread
        thread_args[i].latencies = all_latencies + (i * config->requests_per_client);
        thread_args[i].success_count = (size_t*)calloc(1, sizeof(size_t));
        thread_args[i].failure_count = (size_t*)calloc(1, sizeof(size_t));
        thread_args[i].timeout_count = (size_t*)calloc(1, sizeof(size_t));

        if (!thread_args[i].success_count || !thread_args[i].failure_count || !thread_args[i].timeout_count) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for thread counters.");
             // TODO: Proper cleanup of already created threads/args
             free(all_latencies); free(threads); free(thread_args); // Basic cleanup
             return -1;
        }

#ifdef _WIN32
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, client_thread_func, &thread_args[i], 0, NULL);
        if (threads[i] == 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to create client thread %zu: %d", i, errno); // errno might not be set correctly by _beginthreadex
            // TODO: Proper cleanup
             free(all_latencies); free(threads); free(thread_args); // Basic cleanup
            return -1;
        }
#else
        int rc = pthread_create(&threads[i], NULL, client_thread_func, &thread_args[i]);
        if (rc) {
            log_message(LOG_LEVEL_ERROR, "Failed to create client thread %zu: %s", i, strerror(rc));
             // TODO: Proper cleanup
             free(all_latencies); free(threads); free(thread_args); // Basic cleanup
            return -1;
        }
#endif
    }

    // --- Wait for threads to complete and aggregate results ---
    size_t current_latency_write_idx = 0; // Track where to write next successful latency
    for (size_t i = 0; i < config->client_count; ++i) {
#ifdef _WIN32
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
#else
        pthread_join(threads[i], NULL);
#endif
        // Aggregate results (simple sum, not atomic - safe because threads are joined)
        aggregated.total_success += *thread_args[i].success_count;
        aggregated.total_failure += *thread_args[i].failure_count;
        aggregated.total_timeout += *thread_args[i].timeout_count;

        // Aggregate latencies (find min/max, sum for average) and compact the array
        size_t thread_success_count = *thread_args[i].success_count;
        double* thread_latencies = thread_args[i].latencies; // Pointer to this thread's section
        for(size_t j=0; j < thread_success_count; ++j) {
            double latency = thread_latencies[j];
            if (latency >= 0) { // Check for valid latency (should be >= 0)
                 aggregated.total_latency += latency;
                 if (latency < aggregated.min_latency) aggregated.min_latency = latency;
                 if (latency > aggregated.max_latency) aggregated.max_latency = latency;
                 // Compact successful latencies into the beginning of all_latencies
                 if (current_latency_write_idx < total_requests_to_run) {
                    all_latencies[current_latency_write_idx++] = latency;
                 } else {
                     // Should not happen if allocation was correct
                     log_message(LOG_LEVEL_WARN, "Latency array overflow during aggregation.");
                 }
            }
        }

        // Free thread-specific counters
        free(thread_args[i].success_count);
        free(thread_args[i].failure_count);
        free(thread_args[i].timeout_count);
    }

    long long benchmark_end_time = get_current_time_ms();
    result->total_duration_s = (double)(benchmark_end_time - benchmark_start_time) / 1000.0;

    // --- Calculate final statistics ---
    result->successful_requests = aggregated.total_success;
    result->failed_requests = aggregated.total_failure;
    result->timeout_requests = aggregated.total_timeout;
    aggregated.total_requests_processed = result->successful_requests + result->failed_requests + result->timeout_requests;

    if (result->successful_requests > 0) {
        result->min_latency_ms = aggregated.min_latency;
        result->max_latency_ms = aggregated.max_latency;
        result->avg_latency_ms = aggregated.total_latency / result->successful_requests;

        // Calculate Percentiles (requires sorting successful latencies)
        // We have already compacted successful latencies into the start of all_latencies
        if (current_latency_write_idx != result->successful_requests) {
             log_message(LOG_LEVEL_WARN, "Mismatch between successful requests (%zu) and compacted latencies (%zu). Percentiles might be inaccurate.",
                     result->successful_requests, current_latency_write_idx);
             // Use the smaller count to avoid reading uninitialized memory
             size_t count_for_sort = (current_latency_write_idx < result->successful_requests) ? current_latency_write_idx : result->successful_requests;
             qsort(all_latencies, count_for_sort, sizeof(double), compare_doubles);
             // Indicate potential inaccuracy
             result->p50_latency_ms = (count_for_sort > 0) ? all_latencies[(size_t)(count_for_sort * 0.50)] : 0.0;
             result->p90_latency_ms = (count_for_sort > 0) ? all_latencies[(size_t)(count_for_sort * 0.90)] : 0.0;
             size_t p99_index = (size_t)(count_for_sort * 0.99);
             if (p99_index >= count_for_sort) p99_index = count_for_sort > 0 ? count_for_sort - 1 : 0;
             result->p99_latency_ms = (count_for_sort > 0) ? all_latencies[p99_index] : 0.0;

        } else {
            // Sort the compacted array of successful latencies
            qsort(all_latencies, result->successful_requests, sizeof(double), compare_doubles);

            result->p50_latency_ms = all_latencies[(size_t)(result->successful_requests * 0.50)];
            result->p90_latency_ms = all_latencies[(size_t)(result->successful_requests * 0.90)];
            // Ensure index doesn't exceed bounds for P99
            size_t p99_index = (size_t)(result->successful_requests * 0.99);
            if (p99_index >= result->successful_requests) {
                 p99_index = result->successful_requests > 0 ? result->successful_requests - 1 : 0;
            }
             // Handle case where successful_requests is 0 after check
            result->p99_latency_ms = (result->successful_requests > 0) ? all_latencies[p99_index] : 0.0;
        }

    } else {
        // Handle case with zero successful requests
        result->min_latency_ms = 0.0;
        result->max_latency_ms = 0.0;
        result->avg_latency_ms = 0.0;
        result->p50_latency_ms = 0.0;
        result->p90_latency_ms = 0.0;
        result->p99_latency_ms = 0.0;
    }

    if (result->total_duration_s > 0) {
        result->requests_per_second = (size_t)(aggregated.total_requests_processed / result->total_duration_s);
    } else {
        result->requests_per_second = 0;
    }

    log_message(LOG_LEVEL_INFO, "Benchmark '%s' finished.", config->name);

    // --- Cleanup ---
    free(all_latencies);
    free(threads);
    free(thread_args);

    return 0;
}

// Placeholder for saving results
int mcp_benchmark_save_results(const char* filename, const mcp_benchmark_result_t* results, size_t count) {
    if (!filename || !results || count == 0) {
        fprintf(stderr, "Error: mcp_benchmark_save_results received invalid arguments.\n");
        return -1;
    }

    FILE* fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Error: mcp_benchmark_save_results failed to open file '%s'.\n", filename);
        return -1;
    }

    // Write header
    fprintf(fp, "MinLatencyMs,MaxLatencyMs,AvgLatencyMs,P50LatencyMs,P90LatencyMs,P99LatencyMs,TotalDurationS,RequestsPerSecond,SuccessfulRequests,FailedRequests,TimeoutRequests\n");

    // Write data
    for (size_t i = 0; i < count; ++i) {
        fprintf(fp, "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu\n",
                results[i].min_latency_ms,
                results[i].max_latency_ms,
                results[i].avg_latency_ms,
                results[i].p50_latency_ms,
                results[i].p90_latency_ms,
                results[i].p99_latency_ms,
                results[i].total_duration_s,
                results[i].requests_per_second,
                results[i].successful_requests,
                results[i].failed_requests,
                results[i].timeout_requests);
    }

    fclose(fp);
    fprintf(stdout, "Info: Benchmark results saved to '%s'.\n", filename);
    return 0;
}

// Helper to print comparison line
static void print_comparison_line(const char* metric, double baseline, double current) {
    double change = 0.0;
    const char* indicator = "";
    // Determine indicator based on metric (lower is better for latency/failures, higher for throughput/success)
    bool lower_is_better = (strstr(metric, "Latency") != NULL || strstr(metric, "Failed") != NULL || strstr(metric, "Timeout") != NULL);

    if (baseline > 1e-9 || baseline < -1e-9) { // Avoid division by zero or near-zero
        change = ((current - baseline) / baseline) * 100.0;
         if (fabs(change) < 0.01) {
             indicator = " ~"; // Indicate negligible change
         } else if ((change > 0 && !lower_is_better) || (change < 0 && lower_is_better)) {
            indicator = " (+)"; // Improvement
        } else {
            indicator = " (-)"; // Regression
        }
    } else if (current > 1e-9 || current < -1e-9) {
         change = 100.0; // Baseline was zero, current is not
         indicator = " (+)"; // Indicate increase from zero
    } else {
         change = 0.0; // Both are zero
         indicator = " ~";
    }

    printf("  %-20s: %10.3f -> %10.3f %s%.2f%%\n", metric, baseline, current, indicator, fabs(change));
}

// Helper to print comparison line for size_t
static void print_comparison_line_sz(const char* metric, size_t baseline, size_t current) {
    double change = 0.0;
    const char* indicator = "";
     bool lower_is_better = (strstr(metric, "Failed") != NULL || strstr(metric, "Timeout") != NULL);

    if (baseline > 0) {
        change = (((double)current - (double)baseline) / (double)baseline) * 100.0;
         if (fabs(change) < 0.01) {
             indicator = " ~";
         } else if ((change > 0 && !lower_is_better) || (change < 0 && lower_is_better)) {
            indicator = " (+)"; // Improvement
        } else {
            indicator = " (-)"; // Regression
        }
    } else if (current > 0) {
         change = 100.0;
         indicator = " (+)";
    } else {
         change = 0.0;
         indicator = " ~";
    }
     printf("  %-20s: %10zu -> %10zu %s%.2f%%\n", metric, baseline, current, indicator, fabs(change));
}


// Placeholder for comparing results
void mcp_benchmark_compare(const mcp_benchmark_result_t* baseline, const mcp_benchmark_result_t* current) {
    if (!baseline || !current) {
        fprintf(stderr, "Warning: mcp_benchmark_compare received NULL results.\n");
        return;
    }

    printf("\nBenchmark Comparison:\n");
    printf("  Metric              : Baseline   -> Current      (Change)\n");
    printf("------------------------------------------------------------\n");

    print_comparison_line("Min Latency (ms)", baseline->min_latency_ms, current->min_latency_ms);
    print_comparison_line("Avg Latency (ms)", baseline->avg_latency_ms, current->avg_latency_ms);
    print_comparison_line("P50 Latency (ms)", baseline->p50_latency_ms, current->p50_latency_ms);
    print_comparison_line("P90 Latency (ms)", baseline->p90_latency_ms, current->p90_latency_ms);
    print_comparison_line("P99 Latency (ms)", baseline->p99_latency_ms, current->p99_latency_ms);
    print_comparison_line("Max Latency (ms)", baseline->max_latency_ms, current->max_latency_ms);
    print_comparison_line_sz("Throughput (RPS)", baseline->requests_per_second, current->requests_per_second);
    print_comparison_line_sz("Successful Requests", baseline->successful_requests, current->successful_requests);
    print_comparison_line_sz("Failed Requests", baseline->failed_requests, current->failed_requests);
    print_comparison_line_sz("Timeout Requests", baseline->timeout_requests, current->timeout_requests);
    print_comparison_line("Total Duration (s)", baseline->total_duration_s, current->total_duration_s);


    printf("------------------------------------------------------------\n");
    printf("  (+) indicates improvement, (-) indicates regression, (~) indicates negligible change.\n\n");
}
