/**
 * @file mcp_sys_utils.h
 * @brief System utility functions for MCP library.
 *
 * This header provides cross-platform system utility functions including
 * time operations and sleep functionality. These functions are separated
 * from socket utilities to avoid header conflicts and provide cleaner
 * dependency management.
 */

#ifndef MCP_SYS_UTILS_H
#define MCP_SYS_UTILS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pauses execution for the specified number of milliseconds.
 * 
 * This function provides cross-platform sleep functionality with millisecond
 * precision. On Windows, it uses Sleep(), and on POSIX systems, it uses usleep().
 * 
 * @param milliseconds The duration to sleep in milliseconds.
 */
void mcp_sleep_ms(uint32_t milliseconds);

/**
 * @brief Get the current time in milliseconds.
 * 
 * This function returns the current system time in milliseconds since an
 * arbitrary epoch. The exact epoch depends on the platform:
 * - Windows: Uses GetTickCount64() (milliseconds since system boot)
 * - POSIX: Uses gettimeofday() (milliseconds since Unix epoch)
 * 
 * This function is suitable for measuring time intervals and timeouts,
 * but should not be used for absolute time calculations across different
 * systems or reboots.
 * 
 * @return Current time in milliseconds.
 */
long long mcp_get_time_ms(void);

/**
 * @brief Calculate the elapsed time between two timestamps.
 * 
 * This function calculates the difference between two timestamps obtained
 * from mcp_get_time_ms(). It handles potential overflow issues and provides
 * a safe way to measure elapsed time.
 * 
 * @param start_time The starting timestamp in milliseconds.
 * @param end_time The ending timestamp in milliseconds.
 * @return The elapsed time in milliseconds.
 */
long long mcp_time_elapsed_ms(long long start_time, long long end_time);

/**
 * @brief Check if a timeout has occurred.
 * 
 * This function checks if the specified timeout period has elapsed since
 * the given start time. It's a convenience function for timeout checking
 * in loops and polling operations.
 * 
 * @param start_time The starting timestamp in milliseconds.
 * @param timeout_ms The timeout period in milliseconds.
 * @return true if the timeout has occurred, false otherwise.
 */
bool mcp_time_has_timeout(long long start_time, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* MCP_SYS_UTILS_H */
