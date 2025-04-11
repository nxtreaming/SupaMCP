#include "internal/connection_pool_internal.h"
#include <stdlib.h>
#include <time.h>

// Platform-specific includes for time functions
#ifdef _WIN32
// Included via internal header
#else
#include <sys/time.h>
#endif

// Helper function to get current time in milliseconds
long long get_current_time_ms() {
#ifdef _WIN32
    // GetTickCount64 is simpler and often sufficient for intervals
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

// Helper function to calculate deadline for timed wait (POSIX only)
#ifndef _WIN32
void calculate_deadline(int timeout_ms, struct timespec* deadline) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long nsec = tv.tv_usec * 1000 + (long long)(timeout_ms % 1000) * 1000000;
    deadline->tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
    deadline->tv_nsec = nsec % 1000000000;
}
#endif
