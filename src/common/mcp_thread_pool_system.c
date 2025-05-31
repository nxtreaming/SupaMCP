/**
 * @file mcp_thread_pool_system.c
 * @brief System load monitoring and optimal thread count calculation.
 *
 * This file implements system load monitoring functionality and provides
 * utilities for determining optimal thread counts based on system resources.
 */
#include "internal/mcp_thread_pool_internal.h"

size_t mcp_get_optimal_thread_count(void) {
    size_t num_cores = 4; // Default fallback

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    num_cores = sysinfo.dwNumberOfProcessors;
#else
    num_cores = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    // 2 * num_cores + 1 is a good balance for I/O bound workloads
    return (2 * num_cores) + 1;
}

// Get current system load metrics
int get_system_load_metrics(system_load_metrics_t* metrics) {
    if (!metrics) return -1;

    uint64_t current_time = mcp_get_time_ms();

    // Update metrics every 5 seconds to avoid overhead
    if (metrics->metrics_valid && (current_time - metrics->last_update_time) < 5000) {
        return 0; // Use cached metrics
    }

#ifdef _WIN32
    // Windows implementation
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        metrics->available_memory_mb = (size_t)(mem_status.ullAvailPhys / (1024 * 1024));
    } else {
        metrics->available_memory_mb = 1024; // 1GB fallback
    }

    // Simple CPU usage estimation (not perfect but sufficient)
    static ULARGE_INTEGER last_idle, last_kernel, last_user;
    FILETIME idle_time, kernel_time, user_time;

    if (GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idle_time.dwLowDateTime;
        idle.HighPart = idle_time.dwHighDateTime;
        kernel.LowPart = kernel_time.dwLowDateTime;
        kernel.HighPart = kernel_time.dwHighDateTime;
        user.LowPart = user_time.dwLowDateTime;
        user.HighPart = user_time.dwHighDateTime;

        if (last_idle.QuadPart != 0) {
            ULONGLONG idle_diff = idle.QuadPart - last_idle.QuadPart;
            ULONGLONG kernel_diff = kernel.QuadPart - last_kernel.QuadPart;
            ULONGLONG user_diff = user.QuadPart - last_user.QuadPart;
            ULONGLONG total_diff = kernel_diff + user_diff;

            if (total_diff > 0) {
                metrics->cpu_usage_percent = 100.0 - (100.0 * idle_diff / total_diff);
            } else {
                metrics->cpu_usage_percent = 0.0;
            }
        } else {
            metrics->cpu_usage_percent = 50.0; // Initial estimate
        }

        last_idle = idle;
        last_kernel = kernel;
        last_user = user;
    } else {
        metrics->cpu_usage_percent = 50.0; // Fallback
    }

#else
    // POSIX implementation
    // Get available memory
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        metrics->available_memory_mb = (size_t)((pages * page_size) / (1024 * 1024));
    } else {
        metrics->available_memory_mb = 1024; // 1GB fallback
    }

    // Simple CPU load estimation using load average
    double load_avg[3];
    if (getloadavg(load_avg, 1) != -1) {
        size_t num_cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (num_cores > 0) {
            // Convert load average to percentage (rough estimate)
            metrics->cpu_usage_percent = (load_avg[0] / num_cores) * 100.0;
            if (metrics->cpu_usage_percent > 100.0) {
                metrics->cpu_usage_percent = 100.0;
            }
        } else {
            metrics->cpu_usage_percent = 50.0;
        }
    } else {
        metrics->cpu_usage_percent = 50.0; // Fallback
    }
#endif

    metrics->last_update_time = current_time;
    metrics->metrics_valid = true;

    return 0;
}
