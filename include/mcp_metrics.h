#ifndef MCP_METRICS_H
#define MCP_METRICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Metric types supported by the metrics system
 */
typedef enum mcp_metric_type {
    MCP_METRIC_COUNTER,    /**< A counter that only increases (e.g., total requests) */
    MCP_METRIC_GAUGE,      /**< A value that can go up and down (e.g., active connections) */
    MCP_METRIC_HISTOGRAM,  /**< A distribution of values (e.g., request latencies) */
    MCP_METRIC_METER       /**< A rate of events over time (e.g., requests per second) */
} mcp_metric_type_t;

/**
 * @brief Histogram bucket structure for storing distribution data
 */
typedef struct mcp_histogram_bucket {
    double upper_bound;                /**< Upper bound of this bucket */
    _Atomic(uint64_t) count;           /**< Count of values in this bucket */
    struct mcp_histogram_bucket* next; /**< Next bucket in the list */
} mcp_histogram_bucket_t;

/**
 * @brief Histogram structure for storing distribution data
 */
typedef struct mcp_histogram {
    mcp_histogram_bucket_t* buckets;   /**< Array of buckets */
    size_t bucket_count;               /**< Number of buckets */
    _Atomic(uint64_t) count;           /**< Total count of values */
    _Atomic(double) sum;               /**< Sum of all values */
    _Atomic(double) min;               /**< Minimum value observed */
    _Atomic(double) max;               /**< Maximum value observed */
} mcp_histogram_t;

/**
 * @brief Meter structure for measuring rates
 */
typedef struct mcp_meter {
    _Atomic(uint64_t) count;           /**< Total count of events */
    _Atomic(uint64_t) m1_rate;         /**< 1-minute rate (events per second * 1000) */
    _Atomic(uint64_t) m5_rate;         /**< 5-minute rate (events per second * 1000) */
    _Atomic(uint64_t) m15_rate;        /**< 15-minute rate (events per second * 1000) */
    _Atomic(time_t) last_update;       /**< Last time the rates were updated */
} mcp_meter_t;

/**
 * @brief Metric structure
 */
typedef struct mcp_metric {
    char* name;                        /**< Metric name */
    char* description;                 /**< Metric description */
    mcp_metric_type_t type;            /**< Metric type */
    
    union {
        _Atomic(uint64_t) counter;     /**< Counter value */
        _Atomic(double) gauge;         /**< Gauge value */
        mcp_histogram_t histogram;     /**< Histogram data */
        mcp_meter_t meter;             /**< Meter data */
    } value;
    
    struct mcp_metric* next;           /**< Next metric in the list */
} mcp_metric_t;

/**
 * @brief Metrics registry structure
 */
typedef struct mcp_metrics_registry {
    mcp_metric_t* metrics;             /**< List of metrics */
    void* lock;                        /**< Lock for thread safety */
} mcp_metrics_registry_t;

/**
 * @brief Timer structure for measuring durations
 */
typedef struct mcp_timer {
    mcp_metric_t* histogram;           /**< Histogram for storing durations */
    struct timespec start_time;        /**< Start time of the timer */
    bool running;                      /**< Whether the timer is running */
} mcp_timer_t;

/**
 * @brief Initialize the metrics system
 * 
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_init(void);

/**
 * @brief Shutdown the metrics system and free resources
 */
void mcp_metrics_shutdown(void);

/**
 * @brief Create a new counter metric
 * 
 * @param name Metric name
 * @param description Metric description
 * @return Pointer to the created metric, or NULL on failure
 */
mcp_metric_t* mcp_metrics_create_counter(const char* name, const char* description);

/**
 * @brief Create a new gauge metric
 * 
 * @param name Metric name
 * @param description Metric description
 * @return Pointer to the created metric, or NULL on failure
 */
mcp_metric_t* mcp_metrics_create_gauge(const char* name, const char* description);

/**
 * @brief Create a new histogram metric
 * 
 * @param name Metric name
 * @param description Metric description
 * @param buckets Array of bucket upper bounds
 * @param bucket_count Number of buckets
 * @return Pointer to the created metric, or NULL on failure
 */
mcp_metric_t* mcp_metrics_create_histogram(const char* name, const char* description, 
                                          const double* buckets, size_t bucket_count);

/**
 * @brief Create a new meter metric
 * 
 * @param name Metric name
 * @param description Metric description
 * @return Pointer to the created metric, or NULL on failure
 */
mcp_metric_t* mcp_metrics_create_meter(const char* name, const char* description);

/**
 * @brief Get a metric by name
 * 
 * @param name Metric name
 * @return Pointer to the metric, or NULL if not found
 */
mcp_metric_t* mcp_metrics_get(const char* name);

/**
 * @brief Increment a counter metric
 * 
 * @param metric Pointer to the counter metric
 * @param value Value to increment by (default: 1)
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_counter_inc(mcp_metric_t* metric, uint64_t value);

/**
 * @brief Set a gauge metric value
 * 
 * @param metric Pointer to the gauge metric
 * @param value Value to set
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_gauge_set(mcp_metric_t* metric, double value);

/**
 * @brief Increment a gauge metric
 * 
 * @param metric Pointer to the gauge metric
 * @param value Value to increment by
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_gauge_inc(mcp_metric_t* metric, double value);

/**
 * @brief Decrement a gauge metric
 * 
 * @param metric Pointer to the gauge metric
 * @param value Value to decrement by
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_gauge_dec(mcp_metric_t* metric, double value);

/**
 * @brief Record a value in a histogram metric
 * 
 * @param metric Pointer to the histogram metric
 * @param value Value to record
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_histogram_update(mcp_metric_t* metric, double value);

/**
 * @brief Mark an event in a meter metric
 * 
 * @param metric Pointer to the meter metric
 * @param count Number of events to mark (default: 1)
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_meter_mark(mcp_metric_t* metric, uint64_t count);

/**
 * @brief Create a new timer
 * 
 * @param name Timer name
 * @param description Timer description
 * @return Pointer to the created timer, or NULL on failure
 */
mcp_timer_t* mcp_metrics_timer_create(const char* name, const char* description);

/**
 * @brief Start a timer
 * 
 * @param timer Pointer to the timer
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_timer_start(mcp_timer_t* timer);

/**
 * @brief Stop a timer and record the duration
 * 
 * @param timer Pointer to the timer
 * @return Duration in milliseconds, or -1 on failure
 */
double mcp_metrics_timer_stop(mcp_timer_t* timer);

/**
 * @brief Destroy a timer
 * 
 * @param timer Pointer to the timer
 */
void mcp_metrics_timer_destroy(mcp_timer_t* timer);

/**
 * @brief Generate a metrics report in JSON format
 * 
 * @param buffer Buffer to store the report
 * @param size Size of the buffer
 * @return Number of bytes written, or -1 on failure
 */
int mcp_metrics_report_json(char* buffer, size_t size);

/**
 * @brief Export metrics to a file in JSON format
 * 
 * @param filename Name of the file to export to
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_export_json(const char* filename);

/**
 * @brief Reset all metrics to their initial values
 * 
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_reset_all(void);

/**
 * @brief Reset a specific metric to its initial value
 * 
 * @param metric Pointer to the metric
 * @return 0 on success, -1 on failure
 */
int mcp_metrics_reset(mcp_metric_t* metric);

#ifdef __cplusplus
}
#endif

#endif /* MCP_METRICS_H */
