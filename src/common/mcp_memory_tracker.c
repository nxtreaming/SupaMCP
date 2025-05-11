#include "mcp_memory_tracker.h"
#include "mcp_sync.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include "mcp_cache_aligned.h"
#include "mcp_memory_pool.h"
#include "mcp_atom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Platform-specific includes for backtrace functionality
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#endif

// Maximum number of frames to capture in backtraces
#define MAX_BACKTRACE_FRAMES 16

// Initial hash table capacity - power of 2 for better distribution
#define INITIAL_HASHTABLE_CAPACITY 4096

// String table entry for file paths
typedef struct mcp_string_entry {
    const char* str;                    // Original string
    char* copy;                         // Our copy of the string
    size_t ref_count;                   // Reference count
    struct mcp_string_entry* next;      // Next entry in the hash bucket
} mcp_string_entry_t;

// String table for deduplicating file paths
typedef struct {
    mcp_string_entry_t** buckets;       // Hash buckets
    size_t bucket_count;                // Number of buckets
    size_t total_strings;               // Total number of unique strings
    size_t total_bytes_saved;           // Bytes saved through deduplication
    mcp_memory_pool_t* entry_pool;      // Memory pool for string entries
    // Padding to ensure alignment to cache line boundary
    char padding[MCP_CACHE_LINE_SIZE -
                ((sizeof(mcp_string_entry_t**) + 3 * sizeof(size_t) +
                  sizeof(mcp_memory_pool_t*)) % MCP_CACHE_LINE_SIZE)];
} MCP_CACHE_ALIGNED mcp_string_table_t;

// Structure to track an allocation - aligned for better cache performance
typedef struct {
    void* ptr;                            // Pointer to the allocated memory
    size_t size;                          // Size of the allocation in bytes
    const char* file;                     // Source file where the allocation occurred (pooled)
    int line;                             // Line number where the allocation occurred
    int backtrace_size;                   // Number of frames in the backtrace
    void* backtrace[MAX_BACKTRACE_FRAMES]; // Backtrace of the allocation
    // Padding to ensure alignment to cache line boundary
    char padding[MCP_CACHE_LINE_SIZE -
                ((sizeof(void*) + sizeof(size_t) + sizeof(char*) +
                  sizeof(int) + sizeof(int) +
                  (MAX_BACKTRACE_FRAMES * sizeof(void*))) % MCP_CACHE_LINE_SIZE)];
} MCP_CACHE_ALIGNED mcp_allocation_record_t;

// Global tracking state - aligned for better cache performance
typedef struct {
    mcp_mutex_t* lock;                // Mutex for thread safety
    mcp_hashtable_t* allocations;     // Hash table of active allocations
    mcp_string_table_t* string_table; // String table for file paths
    bool track_allocations;           // Whether to track individual allocations
    bool track_backtraces;            // Whether to capture backtraces
    bool initialized;                 // Whether the tracker is initialized
    bool symbolize_backtraces;        // Whether to symbolize backtraces in reports

    // Statistics - these are updated atomically when possible
    size_t total_allocations;         // Total number of allocations
    size_t total_frees;               // Total number of frees
    size_t current_allocations;       // Current number of active allocations
    size_t peak_allocations;          // Peak number of active allocations
    size_t total_bytes_allocated;     // Total bytes allocated
    size_t current_bytes;             // Current bytes allocated
    size_t peak_bytes;                // Peak bytes allocated
    size_t memory_limit;              // Maximum allowed memory usage (0 = no limit)

    // Padding to ensure alignment to cache line boundary
    char padding[64]; // Fixed size padding for simplicity
} MCP_CACHE_ALIGNED mcp_memory_tracker_t;

// Global tracker instance
static mcp_memory_tracker_t g_tracker = {0};

// Forward declarations
static unsigned long ptr_hash(const void* key);
static bool ptr_compare(const void* key1, const void* key2);
static void record_free(void* value);

// String table functions
static mcp_string_table_t* string_table_create(size_t bucket_count);
static void string_table_destroy(mcp_string_table_t* table);
static const char* string_table_intern(mcp_string_table_t* table, const char* str);
static void string_table_release(mcp_string_table_t* table, const char* str);

// Backtrace functions
static int capture_backtrace(void** buffer, int max_frames);
static char** symbolize_backtrace(void** buffer, int size);
static void free_symbols(char** symbols, int size);

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
        // Use optimized initial capacity (power of 2)
        g_tracker.allocations = mcp_hashtable_create(
            INITIAL_HASHTABLE_CAPACITY, // Initial capacity (power of 2)
            0.75f,                      // Load factor
            ptr_hash,                   // Hash function
            ptr_compare,                // Key comparison function
            NULL,                       // Key duplication function (not needed for pointers)
            NULL,                       // Key free function (not needed for pointers)
            record_free                 // Value free function
        );

        if (!g_tracker.allocations) {
            mcp_log_error("Failed to create hash table for memory tracker");
            mcp_mutex_destroy(g_tracker.lock);
            g_tracker.lock = NULL;
            return false;
        }

        // Create string table for file paths
        g_tracker.string_table = string_table_create(256);
        if (!g_tracker.string_table) {
            mcp_log_error("Failed to create string table for memory tracker");
            mcp_hashtable_destroy(g_tracker.allocations);
            g_tracker.allocations = NULL;
            mcp_mutex_destroy(g_tracker.lock);
            g_tracker.lock = NULL;
            return false;
        }
    }

    // Initialize tracking state
    g_tracker.track_allocations = track_allocations;
    g_tracker.track_backtraces = track_backtraces;
    g_tracker.symbolize_backtraces = true; // Enable symbolization by default
    g_tracker.total_allocations = 0;
    g_tracker.total_frees = 0;
    g_tracker.current_allocations = 0;
    g_tracker.peak_allocations = 0;
    g_tracker.total_bytes_allocated = 0;
    g_tracker.current_bytes = 0;
    g_tracker.peak_bytes = 0;
    g_tracker.memory_limit = 0;
    g_tracker.initialized = true;

    // Initialize symbol handler for backtraces on Windows
#ifdef _WIN32
    if (track_backtraces) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(GetCurrentProcess(), NULL, TRUE)) {
            mcp_log_warn("Failed to initialize symbol handler for backtraces: %lu", GetLastError());
        }
    }
#endif

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

    // Clean up the string table
    if (g_tracker.string_table) {
        string_table_destroy(g_tracker.string_table);
        g_tracker.string_table = NULL;
    }

    // Clean up symbol handler on Windows
#ifdef _WIN32
    if (g_tracker.track_backtraces) {
        SymCleanup(GetCurrentProcess());
    }
#endif

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

    // Update global statistics using atomic operations
    ATOMIC_INCREMENT(g_tracker.total_allocations);
    ATOMIC_INCREMENT(g_tracker.current_allocations);
    ATOMIC_ADD(g_tracker.total_bytes_allocated, size);
    ATOMIC_ADD(g_tracker.current_bytes, size);

    // Update peak statistics atomically
    ATOMIC_EXCHANGE_MAX(g_tracker.peak_allocations, g_tracker.current_allocations);
    ATOMIC_EXCHANGE_MAX(g_tracker.peak_bytes, g_tracker.current_bytes);

    // Record the allocation if tracking is enabled
    if (g_tracker.track_allocations && g_tracker.allocations) {
        mcp_mutex_lock(g_tracker.lock);

        mcp_allocation_record_t* record = (mcp_allocation_record_t*)malloc(sizeof(mcp_allocation_record_t));
        if (record) {
            record->ptr = ptr;
            record->size = size;

            // Use string table for file paths to reduce memory usage
            if (g_tracker.string_table && file) {
                record->file = string_table_intern(g_tracker.string_table, file);
            } else {
                record->file = file;
            }

            record->line = line;
            record->backtrace_size = 0;

            // Capture backtrace if enabled
            if (g_tracker.track_backtraces) {
                record->backtrace_size = capture_backtrace(record->backtrace, MAX_BACKTRACE_FRAMES);
            }

            // Add to the hash table
            if (mcp_hashtable_put(g_tracker.allocations, ptr, record) != 0) {
                // Release the string from the table if we interned it
                if (g_tracker.string_table && file && record->file != file) {
                    string_table_release(g_tracker.string_table, record->file);
                }
                free(record);
                mcp_log_error("Failed to record allocation in hash table");
            }
        } else {
            mcp_log_error("Failed to allocate memory for allocation record");
        }

        mcp_mutex_unlock(g_tracker.lock);
    }
}

void mcp_memory_tracker_record_free(void* ptr) {
    if (!g_tracker.initialized || !ptr) {
        return;
    }

    // Look up the allocation record
    if (g_tracker.track_allocations && g_tracker.allocations) {
        mcp_mutex_lock(g_tracker.lock);

        mcp_allocation_record_t* record = NULL;
        if (mcp_hashtable_get(g_tracker.allocations, ptr, (void**)&record) == 0) {
            // Update global statistics using atomic operations
            ATOMIC_INCREMENT(g_tracker.total_frees);
            ATOMIC_DECREMENT(g_tracker.current_allocations);
            ATOMIC_SUBTRACT(g_tracker.current_bytes, record->size);

            // Release the string from the table if we interned it
            if (g_tracker.string_table && record->file) {
                string_table_release(g_tracker.string_table, record->file);
            }

            // Remove from the hash table
            mcp_hashtable_remove(g_tracker.allocations, ptr);
        } else {
            // This could be a double-free or a free of memory not tracked by us
            mcp_log_warn("Attempt to free untracked memory at %p", ptr);

            // Still increment the free counter
            ATOMIC_INCREMENT(g_tracker.total_frees);
        }

        mcp_mutex_unlock(g_tracker.lock);
    } else {
        // If we're not tracking individual allocations, just update the counters atomically
        ATOMIC_INCREMENT(g_tracker.total_frees);

        // Only decrement if we have allocations to avoid going negative
        if (g_tracker.current_allocations > 0) {
            ATOMIC_DECREMENT(g_tracker.current_allocations);
        }
        // We don't know the size, so we can't update current_bytes
    }
}

bool mcp_memory_tracker_get_stats(mcp_memory_stats_t* stats) {
    if (!g_tracker.initialized || !stats) {
        return false;
    }

    // No need for a lock since we're using atomic operations
    // and we're just reading values (not modifying them)
    stats->total_allocations = g_tracker.total_allocations;
    stats->total_frees = g_tracker.total_frees;
    stats->current_allocations = g_tracker.current_allocations;
    stats->peak_allocations = g_tracker.peak_allocations;
    stats->total_bytes_allocated = g_tracker.total_bytes_allocated;
    stats->current_bytes = g_tracker.current_bytes;
    stats->peak_bytes = g_tracker.peak_bytes;

    return true;
}

// Helper function to iterate through hash table entries
typedef struct {
    FILE* file;
    bool symbolize;
} leak_report_context_t;

static int report_leak(const void* key, void* value, void* user_data) {
    (void)key;
    leak_report_context_t* context = (leak_report_context_t*)user_data;
    mcp_allocation_record_t* record = (mcp_allocation_record_t*)value;
    FILE* file = context->file;

    fprintf(file, "Leak: %p, %zu bytes, allocated at %s:%d\n",
            record->ptr, record->size, record->file ? record->file : "unknown", record->line);

    // Print backtrace if available
    if (record->backtrace_size > 0 && context->symbolize) {
        fprintf(file, "  Backtrace:\n");

        // Symbolize the backtrace if requested
        char** symbols = symbolize_backtrace(record->backtrace, record->backtrace_size);
        if (symbols) {
            for (int i = 0; i < record->backtrace_size; i++) {
                fprintf(file, "    %s\n", symbols[i] ? symbols[i] : "???");
            }
            free_symbols(symbols, record->backtrace_size);
        } else {
            // Just print the raw addresses if symbolization failed
            for (int i = 0; i < record->backtrace_size; i++) {
                fprintf(file, "    %p\n", record->backtrace[i]);
            }
        }
    }

    fprintf(file, "\n");

    // Continue iteration
    return 0;
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

        // Set up context for the iterator
        leak_report_context_t context;
        context.file = file;
        context.symbolize = g_tracker.symbolize_backtraces;

        // Iterate through the hash table and print each allocation
        mcp_hashtable_foreach(g_tracker.allocations, report_leak, &context);
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

    // No need for a lock since we're just setting a single value atomically
    g_tracker.memory_limit = max_bytes;

    mcp_log_info("Memory limit set to %zu bytes", max_bytes);

    return true;
}

bool mcp_memory_tracker_would_exceed_limit(size_t size) {
    if (!g_tracker.initialized || g_tracker.memory_limit == 0) {
        return false;
    }

    // No need for a lock since we're just reading values atomically
    return (g_tracker.current_bytes + size > g_tracker.memory_limit);
}

bool mcp_memory_tracker_set_symbolize_backtraces(bool enable) {
    if (!g_tracker.initialized) {
        return false;
    }

    g_tracker.symbolize_backtraces = enable;

    mcp_log_info("Backtrace symbolization %s", enable ? "enabled" : "disabled");

    return true;
}

bool mcp_memory_tracker_get_string_pool_stats(size_t* unique_strings, size_t* bytes_saved) {
    if (!g_tracker.initialized || !g_tracker.string_table) {
        if (unique_strings) *unique_strings = 0;
        if (bytes_saved) *bytes_saved = 0;
        return false;
    }

    mcp_mutex_lock(g_tracker.lock);

    if (unique_strings) *unique_strings = g_tracker.string_table->total_strings;
    if (bytes_saved) *bytes_saved = g_tracker.string_table->total_bytes_saved;

    mcp_mutex_unlock(g_tracker.lock);

    return true;
}

// Hash function for pointer keys - optimized for better distribution
static unsigned long ptr_hash(const void* key) {
    // Use uintptr_t to avoid pointer truncation warnings
    uintptr_t ptr_val = (uintptr_t)key;

    // FNV-1a hash for better distribution
    const unsigned long FNV_PRIME = 16777619UL;
    const unsigned long FNV_OFFSET_BASIS = 2166136261UL;

    unsigned long hash = FNV_OFFSET_BASIS;
    unsigned char* bytes = (unsigned char*)&ptr_val;

    for (size_t i = 0; i < sizeof(uintptr_t); i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }

    return hash;
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

// String hash function
static unsigned long string_hash(const char* str) {
    if (!str) return 0;

    // FNV-1a hash
    const unsigned long FNV_PRIME = 16777619UL;
    const unsigned long FNV_OFFSET_BASIS = 2166136261UL;

    unsigned long hash = FNV_OFFSET_BASIS;

    for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
        hash ^= *p;
        hash *= FNV_PRIME;
    }

    return hash;
}

// Create a string table
static mcp_string_table_t* string_table_create(size_t bucket_count) {
    mcp_string_table_t* table = (mcp_string_table_t*)malloc(sizeof(mcp_string_table_t));
    if (!table) {
        return NULL;
    }

    table->buckets = (mcp_string_entry_t**)calloc(bucket_count, sizeof(mcp_string_entry_t*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    // Create a memory pool for string entries
    table->entry_pool = mcp_memory_pool_create(sizeof(mcp_string_entry_t), 64, 0);
    if (!table->entry_pool) {
        free(table->buckets);
        free(table);
        return NULL;
    }

    table->bucket_count = bucket_count;
    table->total_strings = 0;
    table->total_bytes_saved = 0;

    return table;
}

// Destroy a string table
static void string_table_destroy(mcp_string_table_t* table) {
    if (!table) {
        return;
    }

    // Free all entries
    for (size_t i = 0; i < table->bucket_count; i++) {
        mcp_string_entry_t* entry = table->buckets[i];
        while (entry) {
            mcp_string_entry_t* next = entry->next;
            free(entry->copy);
            // Don't free the entry itself, it will be freed when the pool is destroyed
            entry = next;
        }
    }

    // Destroy the memory pool
    mcp_memory_pool_destroy(table->entry_pool);

    free(table->buckets);
    free(table);
}

// Intern a string in the table
static const char* string_table_intern(mcp_string_table_t* table, const char* str) {
    if (!table || !str) {
        return str;
    }

    // Calculate hash and bucket
    unsigned long hash = string_hash(str);
    size_t bucket = hash % table->bucket_count;

    // Look for existing entry
    mcp_string_entry_t* entry = table->buckets[bucket];
    while (entry) {
        if (strcmp(entry->str, str) == 0) {
            // Found it, increment reference count
            entry->ref_count++;
            return entry->str;
        }
        entry = entry->next;
    }

    // Not found, create new entry from the memory pool
    entry = (mcp_string_entry_t*)mcp_memory_pool_alloc(table->entry_pool);
    if (!entry) {
        return str; // Fall back to original string on allocation failure
    }

    // Make a copy of the string
    size_t len = strlen(str) + 1;
    entry->copy = (char*)malloc(len);
    if (!entry->copy) {
        mcp_memory_pool_free(table->entry_pool, entry);
        return str; // Fall back to original string on allocation failure
    }

    memcpy(entry->copy, str, len);
    entry->str = entry->copy;
    entry->ref_count = 1;

    // Add to bucket
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;

    // Update statistics
    table->total_strings++;
    table->total_bytes_saved += len - sizeof(mcp_string_entry_t);

    return entry->str;
}

// Release a string from the table
static void string_table_release(mcp_string_table_t* table, const char* str) {
    if (!table || !str) {
        return;
    }

    // Calculate hash and bucket
    unsigned long hash = string_hash(str);
    size_t bucket = hash % table->bucket_count;

    // Look for the entry
    mcp_string_entry_t** pp = &table->buckets[bucket];
    mcp_string_entry_t* entry = *pp;

    while (entry) {
        if (entry->str == str) {
            // Found it, decrement reference count
            entry->ref_count--;

            // If reference count is zero, remove it
            if (entry->ref_count == 0) {
                *pp = entry->next;
                free(entry->copy);
                mcp_memory_pool_free(table->entry_pool, entry);
                table->total_strings--;
            }

            return;
        }

        pp = &entry->next;
        entry = *pp;
    }
}

// Capture a backtrace
static int capture_backtrace(void** buffer, int max_frames) {
#ifdef _WIN32
    // Windows implementation using CaptureStackBackTrace
    return CaptureStackBackTrace(1, max_frames, buffer, NULL);
#else
    // Unix implementation using backtrace
    return backtrace(buffer, max_frames);
#endif
}

// Symbolize a backtrace
static char** symbolize_backtrace(void** buffer, int size) {
#ifdef _WIN32
    // Windows implementation using SymFromAddr
    char** symbols = (char**)malloc(size * sizeof(char*));
    if (!symbols) {
        return NULL;
    }

    HANDLE process = GetCurrentProcess();

    for (int i = 0; i < size; i++) {
        symbols[i] = NULL;

        // Skip frames with NULL addresses
        if (!buffer[i]) {
            continue;
        }

        // Allocate buffer for symbol info
        SYMBOL_INFO* symbol_info = (SYMBOL_INFO*)calloc(1, sizeof(SYMBOL_INFO) + 256);
        if (!symbol_info) {
            continue;
        }

        symbol_info->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol_info->MaxNameLen = 255;

        DWORD64 displacement = 0;

        if (SymFromAddr(process, (DWORD64)buffer[i], &displacement, symbol_info)) {
            // Get file and line info
            IMAGEHLP_LINE64 line_info = {0};
            line_info.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD line_displacement = 0;

            char* symbol_str = (char*)malloc(512);
            if (symbol_str) {
                if (SymGetLineFromAddr64(process, (DWORD64)buffer[i], &line_displacement, &line_info)) {
                    snprintf(symbol_str, 512, "%s in %s:%lu", symbol_info->Name, line_info.FileName, line_info.LineNumber);
                } else {
                    snprintf(symbol_str, 512, "%s", symbol_info->Name);
                }
                symbols[i] = symbol_str;
            }
        }

        free(symbol_info);
    }

    return symbols;
#else
    // Unix implementation using backtrace_symbols
    return backtrace_symbols(buffer, size);
#endif
}

// Free symbols from a backtrace
static void free_symbols(char** symbols, int size) {
    if (!symbols) {
        return;
    }

#ifdef _WIN32
    // Windows implementation - we allocated each symbol individually
    for (int i = 0; i < size; i++) {
        free(symbols[i]);
    }
    free(symbols);
#else
    // Unix implementation - backtrace_symbols returns a single allocation
    free(symbols);
#endif
}
