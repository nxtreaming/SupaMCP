#ifndef MCP_PERFORMANCE_COLLECTOR_H
#define MCP_PERFORMANCE_COLLECTOR_H

#include "mcp_arena.h"
#include "mcp_server.h"
#include "mcp_auth.h"
#include "mcp_performance_metrics.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize a timer and record the start of a request
 *
 * @param timer Pointer to a performance timer structure
 */
void mcp_performance_collect_request_start(mcp_performance_timer_t* timer);

/**
 * @brief Record the end of a request and update metrics
 *
 * @param timer Pointer to the timer started with mcp_performance_collect_request_start
 * @param success Whether the request was successful
 * @param bytes_sent Number of bytes sent in the response
 * @param bytes_received Number of bytes received in the request
 */
void mcp_performance_collect_request_end(mcp_performance_timer_t* timer, bool success,
                                        size_t bytes_sent, size_t bytes_received);

/**
 * @brief Record a request timeout
 */
void mcp_performance_collect_request_timeout(void);

/**
 * @brief Export current performance metrics to a file
 *
 * @param filename Name of the file to export to
 * @return 0 on success, -1 on failure
 */
int mcp_performance_export_metrics(const char* filename);

/**
 * @brief Get current performance metrics as a JSON string
 *
 * @param buffer Buffer to store the JSON string
 * @param size Size of the buffer
 * @return Number of bytes written to the buffer, or -1 on error
 */
int mcp_performance_get_metrics_json(char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MCP_PERFORMANCE_COLLECTOR_H */
