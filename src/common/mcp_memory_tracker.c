#include "mcp_memory_tracker.h"
#include "mcp_sync.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>  // For uintptr_t

// Maximum number of frames to capture in backtraces
#define MAX_BACKTRACE_FRAMES 16

// Structure to track an allocation
typedef struct {
    void* ptr;              // Pointer to the allocated memory
    size_t size;            // Size of the allocation in bytes
    const char* file;       // Source file where the allocation occurred
    int line;               // Line number where the allocation occurred
    void* backtrace[MAX_BACKTRACE_FRAMES]; // Backtrace of the allocation
    int backtrace_size;     // Number of frames in the backtrace
} mcp_allocation_record_t;

// Global tracking state
static struct {
    mcp_mutex_t* lock;                // Mutex for thread safety
    mcp_hashtable_t* allocations;     // Hash table of active allocations
    bool track_allocations;           // Whether to track individual allocations
    bool track_backtraces;            // Whether to capture backtraces
    size_t total_allocations;         // Total number of allocations
    size_t total_frees;               // Total number of frees
    size_t current_allocations;       // Current number of active allocations
    size_t peak_allocations;          // Peak number of active allocations
    size_t total_bytes_allocated;     // Total bytes allocated
    size_t current_bytes;             // Current bytes allocated
    size_t peak_bytes;                // Peak bytes allocated
    size_t memory_limit;              // Maximum allowed memory usage (0 = no limit)
    bool initialized;                 // Whether the tracker is initialized
} g_tracker = {0};

// Forward declarations
static unsigned long ptr_hash(const void* key);
static bool ptr_compare(const void* key1, const void* key2);
static void record_free(void* value);

bool mcp_memory_tracker_init(bool track_allocations, bool track_backtraces) {
    if (g_tracker.initialized) {
        mcp_log_warn("Memory tracker already initialized");
        return true;
    }

    // Initialize the lock
    g_tracker.lock = mcp_mutex_create();
    if (!g_tracker.lock) {
        mcp_log_error("Failed to create mutex for memory tracker");
        return false;
    }

    // Initialize the hash table if we're tracking allocations
    if (track_allocations) {
        g_tracker.allocations = mcp_hashtable_create(
            1024,       // Initial capacity
            0.75f,      // Load factor
            ptr_hash,   // Hash function
            ptr_compare, // Key comparison function
            NULL,       // Key duplication function (not needed for pointers)
            NULL,       // Key free function (not needed for pointers)
            record_free // Value free function
        );

        if (!g_tracker.allocations) {
            mcp_log_error("Failed to create hash table for memory tracker");
            mcp_mutex_destroy(g_tracker.lock);
            g_tracker.lock = NULL;
            return false;
        }
    }

    // Initialize tracking state
    g_tracker.track_allocations = track_allocations;
    g_tracker.track_backtraces = track_backtraces;
    g_tracker.total_allocations = 0;
    g_tracker.total_frees = 0;
    g_tracker.current_allocations = 0;
    g_tracker.peak_allocations = 0;
    g_tracker.total_bytes_allocated = 0;
    g_tracker.current_bytes = 0;
    g_tracker.peak_bytes = 0;
    g_tracker.memory_limit = 0;
    g_tracker.initialized = true;

    mcp_log_info("Memory tracker initialized (track_allocations=%d, track_backtraces=%d)",
                track_allocations, track_backtraces);

    return true;
}

void mcp_memory_tracker_cleanup(void) {
    if (!g_tracker.initialized) {
        return;
    }

    mcp_mutex_lock(g_tracker.lock);

    // Check for leaks
    if (g_tracker.current_allocations > 0) {
        mcp_log_warn("Memory leaks detected: %zu allocations, %zu bytes",
                    g_tracker.current_allocations, g_tracker.current_bytes);
    }

    // Clean up the hash table
    if (g_tracker.allocations) {
        mcp_hashtable_destroy(g_tracker.allocations);
        g_tracker.allocations = NULL;
    }

    g_tracker.initialized = false;

    mcp_mutex_unlock(g_tracker.lock);
    mcp_mutex_destroy(g_tracker.lock);
    g_tracker.lock = NULL;

    mcp_log_info("Memory tracker cleaned up");
}

void mcp_memory_tracker_record_alloc(void* ptr, size_t size, const char* file, int line) {
    if (!g_tracker.initialized || !ptr) {
        return;
    }

    mcp_mutex_lock(g_tracker.lock);

    // Update global statistics
    g_tracker.total_allocations++;
    g_tracker.current_allocations++;
    g_tracker.total_bytes_allocated += size;
    g_tracker.current_bytes += size;

    // Update peak statistics
    if (g_tracker.current_allocations > g_tracker.peak_allocations) {
        g_tracker.peak_allocations = g_tracker.current_allocations;
    }
    if (g_tracker.current_bytes > g_tracker.peak_bytes) {
        g_tracker.peak_bytes = g_tracker.current_bytes;
    }

    // Record the allocation if tracking is enabled
    if (g_tracker.track_allocations && g_tracker.allocations) {
        mcp_allocation_record_t* record = (mcp_allocation_record_t*)malloc(sizeof(mcp_allocation_record_t));
        if (record) {
            record->ptr = ptr;
            record->size = size;
            record->file = file;
            record->line = line;
            record->backtrace_size = 0;

            // Capture backtrace if enabled
            if (g_tracker.track_backtraces) {
                // This is platform-specific and would need to be implemented
                // using backtrace() on Unix or CaptureStackBackTrace() on Windows
                // For simplicity, we'll just skip it in this implementation
            }

            // Add to the hash table
            if (mcp_hashtable_put(g_tracker.allocations, ptr, record) != 0) {
                free(record);
                mcp_log_error("Failed to record allocation in hash table");
            }
        } else {
            mcp_log_error("Failed to allocate memory for allocation record");
        }
    }

    mcp_mutex_unlock(g_tracker.lock);
}

void mcp_memory_tracker_record_free(void* ptr) {
    if (!g_tracker.initialized || !ptr) {
        return;
    }

    mcp_mutex_lock(g_tracker.lock);

    // Look up the allocation record
    mcp_allocation_record_t* record = NULL;
    if (g_tracker.track_allocations && g_tracker.allocations) {
        if (mcp_hashtable_get(g_tracker.allocations, ptr, (void**)&record) == 0) {
            // Update global statistics
            g_tracker.total_frees++;
            g_tracker.current_allocations--;
            g_tracker.current_bytes -= record->size;

            // Remove from the hash table
            mcp_hashtable_remove(g_tracker.allocations, ptr);
        } else {
            // This could be a double-free or a free of memory not tracked by us
            mcp_log_warn("Attempt to free untracked memory at %p", ptr);
        }
    } else {
        // If we're not tracking individual allocations, just update the counters
        g_tracker.total_frees++;
        if (g_tracker.current_allocations > 0) {
            g_tracker.current_allocations--;
        }
        // We don't know the size, so we can't update current_bytes
    }

    mcp_mutex_unlock(g_tracker.lock);
}

bool mcp_memory_tracker_get_stats(mcp_memory_stats_t* stats) {
    if (!g_tracker.initialized || !stats) {
        return false;
    }

    mcp_mutex_lock(g_tracker.lock);

    stats->total_allocations = g_tracker.total_allocations;
    stats->total_frees = g_tracker.total_frees;
    stats->current_allocations = g_tracker.current_allocations;
    stats->peak_allocations = g_tracker.peak_allocations;
    stats->total_bytes_allocated = g_tracker.total_bytes_allocated;
    stats->current_bytes = g_tracker.current_bytes;
    stats->peak_bytes = g_tracker.peak_bytes;

    mcp_mutex_unlock(g_tracker.lock);

    return true;
}

bool mcp_memory_tracker_dump_leaks(const char* filename) {
    if (!g_tracker.initialized || !g_tracker.track_allocations || !g_tracker.allocations) {
        return false;
    }

    FILE* file = fopen(filename, "w");
    if (!file) {
        mcp_log_error("Failed to open leak report file: %s", filename);
        return false;
    }

    mcp_mutex_lock(g_tracker.lock);

    fprintf(file, "Memory Leak Report\n");
    fprintf(file, "=================\n\n");
    fprintf(file, "Total allocations: %zu\n", g_tracker.total_allocations);
    fprintf(file, "Total frees: %zu\n", g_tracker.total_frees);
    fprintf(file, "Current allocations: %zu\n", g_tracker.current_allocations);
    fprintf(file, "Current bytes: %zu\n\n", g_tracker.current_bytes);

    if (g_tracker.current_allocations > 0) {
        fprintf(file, "Leaked allocations:\n");
        fprintf(file, "-------------------\n\n");

        // Iterate through the hash table and print each allocation
        // This is a simplified version - a real implementation would use a proper iterator
        // For now, we'll just report the summary
        fprintf(file, "Detailed leak information not available in this implementation.\n");
    } else {
        fprintf(file, "No memory leaks detected.\n");
    }

    mcp_mutex_unlock(g_tracker.lock);

    fclose(file);

    mcp_log_info("Memory leak report written to %s", filename);

    return true;
}

bool mcp_memory_tracker_set_limit(size_t max_bytes) {
    if (!g_tracker.initialized) {
        return false;
    }

    mcp_mutex_lock(g_tracker.lock);
    g_tracker.memory_limit = max_bytes;
    mcp_mutex_unlock(g_tracker.lock);

    mcp_log_info("Memory limit set to %zu bytes", max_bytes);

    return true;
}

bool mcp_memory_tracker_would_exceed_limit(size_t size) {
    if (!g_tracker.initialized || g_tracker.memory_limit == 0) {
        return false;
    }

    bool would_exceed;

    mcp_mutex_lock(g_tracker.lock);
    would_exceed = (g_tracker.current_bytes + size > g_tracker.memory_limit);
    mcp_mutex_unlock(g_tracker.lock);

    return would_exceed;
}

// Hash function for pointer keys
static unsigned long ptr_hash(const void* key) {
    // Simple hash function for pointers
    // Use uintptr_t to avoid pointer truncation warnings
    uintptr_t ptr_val = (uintptr_t)key;
    // Cast to unsigned long for the hash function return type
    return (unsigned long)(ptr_val ^ (ptr_val >> 32));
}

// Comparison function for pointer keys
static bool ptr_compare(const void* key1, const void* key2) {
    return key1 == key2;
}

// Free function for allocation records
static void record_free(void* value) {
    if (value) {
        free(value);
    }
}
