/**
 * @file mcp_sys_utils.c
 * @brief Implementation of system utility functions for MCP library.
 *
 * This file implements cross-platform system utility functions including
 * time operations and sleep functionality. These functions are separated
 * from socket utilities to avoid header conflicts and provide cleaner
 * dependency management.
 */

#include "mcp_sys_utils.h"
#include <stdbool.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

void mcp_sleep_ms(uint32_t milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

long long mcp_get_time_ms(void) {
#ifdef _WIN32
    // GetTickCount64 is simpler and often sufficient for intervals
    // Returns milliseconds since system boot, wraps around every ~49.7 days
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

long long mcp_time_elapsed_ms(long long start_time, long long end_time) {
    // Handle potential overflow/wraparound
    if (end_time >= start_time) {
        return end_time - start_time;
    } else {
        // Handle wraparound case (mainly for Windows GetTickCount64)
        // This assumes the wraparound happened once, which is reasonable
        // for most use cases (49.7 days on Windows)
        return (LLONG_MAX - start_time) + end_time + 1;
    }
}

bool mcp_time_has_timeout(long long start_time, uint32_t timeout_ms) {
    long long current_time = mcp_get_time_ms();
    long long elapsed = mcp_time_elapsed_ms(start_time, current_time);
    return elapsed >= timeout_ms;
}
