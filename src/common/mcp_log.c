#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_log.h"
#include "mcp_json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#   define PATH_SEPARATOR "\\"
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#   define PATH_SEPARATOR "/"
#include <pthread.h>
#endif

// Platform-specific mutex implementation
#ifdef _WIN32
typedef CRITICAL_SECTION mcp_log_mutex_t;
#define INIT_LOG_MUTEX(mutex) InitializeCriticalSection(mutex)
#define DESTROY_LOG_MUTEX(mutex) DeleteCriticalSection(mutex)
#define LOCK_LOG_MUTEX(mutex) EnterCriticalSection(mutex)
#define UNLOCK_LOG_MUTEX(mutex) LeaveCriticalSection(mutex)
#else
typedef pthread_mutex_t mcp_log_mutex_t;
#define INIT_LOG_MUTEX(mutex) pthread_mutex_init(mutex, NULL)
#define DESTROY_LOG_MUTEX(mutex) pthread_mutex_destroy(mutex)
#define LOCK_LOG_MUTEX(mutex) pthread_mutex_lock(mutex)
#define UNLOCK_LOG_MUTEX(mutex) pthread_mutex_unlock(mutex)
#endif


// --- Static Global Variables ---

/** @internal File pointer for the optional log file. NULL if file logging is disabled. */
static FILE* g_log_file = NULL;
/** @internal Current minimum log level. Messages below this level are ignored. */
static mcp_log_level_t g_log_level = MCP_LOG_LEVEL_INFO; // Default level
/** @internal String representations of log levels for output. */
static const char* g_log_level_names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
/** @internal Current log output format. */
static mcp_log_format_t g_log_format = MCP_LOG_FORMAT_TEXT; // Default format
/** @internal Flag to disable all output. */
static bool g_log_quiet = false;
/** @internal Flag to enable/disable colored output. */
static bool g_log_use_color = false; // Default to no color
/** @internal Mutex for thread safety. */
static mcp_log_mutex_t g_log_mutex;
/** @internal Flag indicating if the mutex has been initialized. */
static bool g_log_mutex_initialized = false;


// --- Internal Helper ---

// (Removed internal escape_json_string function)

// --- Public API Implementation ---

// Core logging function
void mcp_log_log(mcp_log_level_t level, const char* file, int line, const char* format, ...) {
    // 1. Check if logging is quiet or level is too low
    if (g_log_quiet || level < g_log_level) {
        return;
    }

    // 2. Get current time and format it as a timestamp string
    time_t now;
    struct tm timeinfo_storage; // Use local storage for thread-safety
    struct tm* timeinfo;
    char timestamp[20]; // Buffer for "YYYY-MM-DD HH:MM:SS" + null terminator

    time(&now);

    // Use platform-specific thread-safe functions to get local time
#ifdef _WIN32
    errno_t err = localtime_s(&timeinfo_storage, &now);
    if (err != 0) {
        timeinfo = NULL; // Indicate error
    } else {
        timeinfo = &timeinfo_storage;
    }
#else
    timeinfo = localtime_r(&now, &timeinfo_storage);
    // localtime_r returns NULL on error
#endif

    // Check if time conversion failed
    if (timeinfo == NULL) {
#ifdef _MSC_VER
        strncpy_s(timestamp, sizeof(timestamp), "DATE_TIME_ERROR", _TRUNCATE);
#else
        strncpy(timestamp, "DATE_TIME_ERROR", sizeof(timestamp) - 1);
        timestamp[sizeof(timestamp) - 1] = '\0'; // Ensure null termination for strncpy
#endif
         // Optionally log an error about the time conversion failure itself
         // fprintf(stderr, "Error converting time.\n");
    } else {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    }


    // 3. Format the log message using varargs
    va_list args;
    va_start(args, format);

    char message[1024]; // Fixed-size buffer for the formatted message
    // Use vsnprintf for safe formatting, preventing buffer overflows
    int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Ensure null termination, especially if vsnprintf truncated the output
    if (written >= (int)sizeof(message)) {
         message[sizeof(message) - 1] = '\0'; // Manually null-terminate if truncated
    } else if (written < 0) {
        // vsnprintf failed (e.g., encoding error in format string)
        // Write a fallback error message to indicate the issue
        snprintf(message, sizeof(message), "Encoding error in log message format: %s", format);
    }
    // If written is non-negative and less than buffer size, vsnprintf added null terminator.


    // 4. Lock mutex before accessing shared resources
    LOCK_LOG_MUTEX(&g_log_mutex);

    // 5. Output the formatted message based on g_log_format
    // Extract filename from path
    const char *filename = strrchr(file, PATH_SEPARATOR[0]);
    filename = (filename != NULL) ? filename + 1 : file;

    // Color codes (ANSI escape codes) - only used if g_log_use_color is true
    const char* color_start = "";
    const char* color_end = "\x1b[0m"; // Reset color
    if (g_log_use_color) {
        switch (level) {
            case MCP_LOG_LEVEL_TRACE: color_start = "\x1b[90m"; break; // Gray
            case MCP_LOG_LEVEL_DEBUG: color_start = "\x1b[36m"; break; // Cyan
            case MCP_LOG_LEVEL_INFO:  color_start = "\x1b[32m"; break; // Green
            case MCP_LOG_LEVEL_WARN:  color_start = "\x1b[33m"; break; // Yellow
            case MCP_LOG_LEVEL_ERROR: color_start = "\x1b[31m"; break; // Red
            case MCP_LOG_LEVEL_FATAL: color_start = "\x1b[35m"; break; // Magenta
        }
    } else {
        color_end = ""; // No need to reset if no color was started
    }


    if (g_log_format == MCP_LOG_FORMAT_JSON) {
        // Use fixed-size buffers. mcp_json_escape_string handles truncation.
        char escaped_message[sizeof(message) * 2];
        char escaped_file[256];

        // Use the new utility function. Ignore return value for now, rely on truncation.
        mcp_json_escape_string(message, escaped_message, sizeof(escaped_message));
        mcp_json_escape_string(filename, escaped_file, sizeof(escaped_file));
        // Ensure null termination is handled by mcp_json_escape_string even on truncation.

        const char* json_fmt = "{\"timestamp\":\"%s\", \"level\":\"%s\", \"file\":\"%s\", \"line\":%d, \"message\":\"%s\"}\n";

        if (g_log_file != NULL) {
             fprintf(g_log_file, json_fmt,
                     timestamp, g_log_level_names[level], escaped_file, line, escaped_message);
             fflush(g_log_file);
        }
         // Outputting to stderr might still interleave output from different threads,
         // but the access to g_log_file and g_log_format is protected.
         fprintf(stderr, json_fmt,
                 timestamp, g_log_level_names[level], escaped_file, line, escaped_message);

    } else { // Default to TEXT format
        const char* text_fmt = "%s[%s] [%s:%d] [%s] %s%s\n";
        if (g_log_file != NULL) {
            fprintf(g_log_file, text_fmt,
                    "", // No color for file
                    timestamp, filename, line, g_log_level_names[level], message, "");
            fflush(g_log_file); // Ensure immediate write to file
        }
         fprintf(stderr, text_fmt,
                 color_start, // Start color
                 timestamp, filename, line, g_log_level_names[level], message,
                 color_end); // End color
    }

    UNLOCK_LOG_MUTEX(&g_log_mutex); // Unlock after accessing shared resources
}

/**
 * @internal
 * @brief Attempts to create the directory path for the log file if it doesn't exist.
 * Handles nested directories.
 * @param log_file_path The full path to the log file.
 * @return 0 on success or if no directory needed creating, -1 on failure.
 */
static int create_log_directory(const char* log_file_path) {
    if (log_file_path == NULL) {
        return 0; // Nothing to do
    }

    // Duplicate the path string because dirname-like operations will modify it
    char* path_copy = strdup(log_file_path);
    if (path_copy == NULL) {
        // Use fprintf directly as log_message might not be fully initialized or working
        fprintf(stderr, "[ERROR] Failed to duplicate log file path for directory creation.\n");
        return -1;
    }

    // Find the last path separator to isolate the directory part
    char* last_separator = strrchr(path_copy, PATH_SEPARATOR[0]);
    if (last_separator == NULL) {
        // No directory part found (e.g., filename only in current dir)
        free(path_copy);
        return 0; // Nothing to create
    }

    // Temporarily terminate the string at the separator to get the directory path
    *last_separator = '\0';

    // Handle edge cases: empty path or root directory ("/" or "C:\")
     if (strlen(path_copy) == 0 || (strlen(path_copy) == 1 && path_copy[0] == PATH_SEPARATOR[0])
#ifdef _WIN32 // Check for drive letter root like "C:"
        || (strlen(path_copy) == 2 && path_copy[1] == ':')
#endif
     ) {
        free(path_copy);
        return 0; // Root directory or similar, no creation needed/possible
    }

    // --- Recursive directory creation ---
    int result = 0;
    char* p = path_copy;

    // Skip potential leading separator (e.g., '/' in "/path/to/log")
    // or drive letter (e.g., 'C:' in "C:\path\to\log")
    if (*p == PATH_SEPARATOR[0]) {
        p++;
    }
#ifdef _WIN32
    else if (strlen(p) >= 2 && p[1] == ':') {
        p += 2; // Skip drive letter "C:"
        if (*p == PATH_SEPARATOR[0]) p++; // Skip separator after drive letter if present
    }
#endif

    // Iterate through path components separated by PATH_SEPARATOR
    while (result == 0 && (p = strchr(p, PATH_SEPARATOR[0])) != NULL) {
        // Temporarily terminate the path at the current separator
        *p = '\0';

        // Attempt to create the directory component
#ifdef _WIN32
        if (CreateDirectory(path_copy, NULL) == 0) {
            // Check if failure was because it already exists (which is OK)
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                fprintf(stderr, "[ERROR] Failed to create log directory component: %s (Error: %lu)\n", path_copy, GetLastError());
                result = -1; // Mark failure
            }
        }
#else
        if (mkdir(path_copy, 0755) != 0) { // Use standard POSIX permissions
            // Check if failure was because it already exists (which is OK)
            if (errno != EEXIST) {
                fprintf(stderr, "[ERROR] Failed to create log directory component: %s (errno: %d - %s)\n", path_copy, errno, strerror(errno));
                result = -1; // Mark failure
            }
        }
#endif
        // Restore the separator and move to the next component
        *p = PATH_SEPARATOR[0];
        p++;
    }

    // Attempt to create the final, full directory path if no error occurred yet
    if (result == 0) {
#ifdef _WIN32
         if (CreateDirectory(path_copy, NULL) == 0) {
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                fprintf(stderr, "[ERROR] Failed to create final log directory: %s (Error: %lu)\n", path_copy, GetLastError());
                result = -1;
            }
        }
#else
         if (mkdir(path_copy, 0755) != 0) {
            if (errno != EEXIST) {
                fprintf(stderr, "[ERROR] Failed to create final log directory: %s (errno: %d - %s)\n", path_copy, errno, strerror(errno));
                result = -1;
            }
        }
#endif
    }

    free(path_copy); // Free the duplicated path string
    return result;
}

int mcp_log_init(const char* log_file_path, mcp_log_level_t level) {
    // 1. Validate and set the global log level
    if (level < MCP_LOG_LEVEL_TRACE || level > MCP_LOG_LEVEL_FATAL) {
        fprintf(stderr, "[WARN] Invalid log level specified (%d), defaulting to INFO.\n", level);
        level = MCP_LOG_LEVEL_INFO;
    }
    g_log_level = level;

    // Initialize mutex if not already done
    if (!g_log_mutex_initialized) {
#ifdef _WIN32
        // InitializeCriticalSection is void and doesn't return an error code directly.
        // Assume success for simplicity, or use structured exception handling if needed.
        INIT_LOG_MUTEX(&g_log_mutex);
#else
        if (INIT_LOG_MUTEX(&g_log_mutex) != 0) { // Check return value for pthreads
             fprintf(stderr, "[ERROR] Failed to initialize log mutex.\n");
             return -1; // Indicate failure
        }
#endif
        g_log_mutex_initialized = true;
    }

    // 2. Close any previously opened log file (needs lock)
    mcp_log_close(); // Use the renamed function

    // 3. If a log file path is provided, attempt to open it (needs lock)
    LOCK_LOG_MUTEX(&g_log_mutex);
    if (log_file_path != NULL && strlen(log_file_path) > 0) {
        // 3a. Ensure the directory exists
        if (create_log_directory(log_file_path) != 0) {
            // Error message already printed by create_log_directory
            return -1; // Failed to create directory
        }

        // 3b. Open the log file in append mode ("a")
#ifdef _WIN32
        // Use fopen_s on Windows for security
        errno_t err = fopen_s(&g_log_file, log_file_path, "a");
        if (err != 0 || g_log_file == NULL) {
            char err_buf[128];
            strerror_s(err_buf, sizeof(err_buf), err);
            // Use fprintf directly in case log_message relies on the file pointer
            fprintf(stderr, "[ERROR] Failed to open log file '%s': %s (errno: %d)\n", log_file_path, err_buf, err);
            g_log_file = NULL; // Ensure it's NULL on failure
#else
        // Use standard fopen on POSIX
        g_log_file = fopen(log_file_path, "a");
        if (g_log_file == NULL) {
            char err_buf[128];
             if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
                 fprintf(stderr, "[ERROR] Failed to open log file '%s': %s (errno: %d)\n", log_file_path, err_buf, errno);
             } else {
                 fprintf(stderr, "[ERROR] Failed to open log file '%s': (errno: %d, strerror_r failed)\n", log_file_path, errno);
             }
#endif
            UNLOCK_LOG_MUTEX(&g_log_mutex); // Unlock before returning error
            return -1; // File open failed
        }
        UNLOCK_LOG_MUTEX(&g_log_mutex); // Unlock after successful open or if no file path
        // Log successful file opening (will go to stderr and the file itself)
        mcp_log_info("Logging initialized to file: %s (Level: %s)", log_file_path, g_log_level_names[g_log_level]);
    } else {
         UNLOCK_LOG_MUTEX(&g_log_mutex); // Unlock if no file path
         // Log that file logging is disabled (will go to stderr only)
         mcp_log_info("File logging disabled. Logging to stderr only. (Level: %s)", g_log_level_names[g_log_level]);
    }

    return 0; // Success
}

void mcp_log_close(void) {
    if (!g_log_mutex_initialized) return; // Nothing to close or destroy if not initialized

    LOCK_LOG_MUTEX(&g_log_mutex);
    // Check if a log file is currently open
    if (g_log_file != NULL) {
        // Log the closing event (will go to stderr and the file)
        // Temporarily unlock to allow log_message to lock
        UNLOCK_LOG_MUTEX(&g_log_mutex);
        mcp_log_info("Closing log file.");
        LOCK_LOG_MUTEX(&g_log_mutex); // Re-lock

        fclose(g_log_file);
        g_log_file = NULL;
    }
    UNLOCK_LOG_MUTEX(&g_log_mutex);

    // Destroy mutex - should ideally happen only once at application exit
    // For simplicity here, we destroy it, but a real app might need a separate shutdown function.
    // if (g_log_mutex_initialized) {
    //     DESTROY_LOG_MUTEX(&g_log_mutex);
    //     g_log_mutex_initialized = false;
    // }
}


// --- Logging Enhancements ---

/**
 * @brief Sets the desired output format for logs.
 */
void mcp_log_set_format(mcp_log_format_t format) {
    if (format == MCP_LOG_FORMAT_TEXT || format == MCP_LOG_FORMAT_JSON) {
        LOCK_LOG_MUTEX(&g_log_mutex);
        g_log_format = format;
        UNLOCK_LOG_MUTEX(&g_log_mutex);

        mcp_log_info("Log format set to %s.",
                    (format == MCP_LOG_FORMAT_JSON) ? "JSON" : "TEXT");
    } else {
        mcp_log_warn("Attempted to set invalid log format (%d).", format);
    }
}

void mcp_log_set_level(mcp_log_level_t level) {
     if (level >= MCP_LOG_LEVEL_TRACE && level <= MCP_LOG_LEVEL_FATAL) {
        LOCK_LOG_MUTEX(&g_log_mutex);
        g_log_level = level;
        UNLOCK_LOG_MUTEX(&g_log_mutex);
    } else {
         mcp_log_warn("Attempted to set invalid log level (%d).", level);
    }
}

void mcp_log_set_quiet(bool quiet) {
    LOCK_LOG_MUTEX(&g_log_mutex);
    g_log_quiet = quiet;
    UNLOCK_LOG_MUTEX(&g_log_mutex);
}

void mcp_log_set_color(bool use_color) {
    LOCK_LOG_MUTEX(&g_log_mutex);
    g_log_use_color = use_color;
    UNLOCK_LOG_MUTEX(&g_log_mutex);
}

/**
 * @brief Records a structured log message with additional context.
 */
void mcp_log_structured(
    mcp_log_level_t level,
    const char* component,
    const char* event,
    const char* format, ...)
{
     // 1. Check if logging is quiet or level is too low
    if (g_log_quiet || level < g_log_level) {
        return;
    }

    // 2. Get timestamp (similar to log_message)
    time_t now;
    struct tm timeinfo_storage;
    struct tm* timeinfo;
    char timestamp[20];
    time(&now);
#ifdef _WIN32
    errno_t err = localtime_s(&timeinfo_storage, &now);
    timeinfo = (err == 0) ? &timeinfo_storage : NULL;
#else
    timeinfo = localtime_r(&now, &timeinfo_storage);
#endif
    if (timeinfo == NULL) {
        strncpy(timestamp, "DATE_TIME_ERROR", sizeof(timestamp) - 1);
        timestamp[sizeof(timestamp) - 1] = '\0';
    } else {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    }

    // 3. Format the main message
    va_list args;
    va_start(args, format);
    char base_message[1024];
    vsnprintf(base_message, sizeof(base_message), format, args);
    va_end(args);
    base_message[sizeof(base_message) - 1] = '\0'; // Ensure null termination

    // 4. Output based on format
    LOCK_LOG_MUTEX(&g_log_mutex); // Lock before accessing shared resources

     if (g_log_format == MCP_LOG_FORMAT_JSON) {
        char escaped_message[sizeof(base_message) * 2];
        char escaped_component[256]; // Assume max component/event length
        char escaped_event[256];

        // Use the new utility function. Ignore return value for now, rely on truncation.
        mcp_json_escape_string(base_message, escaped_message, sizeof(escaped_message));
        mcp_json_escape_string(component ? component : "", escaped_component, sizeof(escaped_component));
        mcp_json_escape_string(event ? event : "", escaped_event, sizeof(escaped_event));
        // Ensure null termination is handled by mcp_json_escape_string even on truncation.

        const char* json_fmt = "{\"timestamp\":\"%s\", \"level\":\"%s\", \"component\":\"%s\", \"event\":\"%s\", \"message\":\"%s\"}\n";

        if (g_log_file != NULL) {
             fprintf(g_log_file, json_fmt,
                     timestamp, g_log_level_names[level], escaped_component, escaped_event, escaped_message);
             fflush(g_log_file);
        }
         // Outputting to stderr might still interleave, but file access is protected
         fprintf(stderr, json_fmt,
                 timestamp, g_log_level_names[level], escaped_component, escaped_event, escaped_message);

    } else { // Default to TEXT format
        const char* text_fmt = "[%s] [%s] [%s|%s] %s\n";
         if (g_log_file != NULL) {
             fprintf(g_log_file, text_fmt,
                     timestamp, g_log_level_names[level],
                     component ? component : "-",
                     event ? event : "-",
                     base_message);
             fflush(g_log_file);
        }
         fprintf(stderr, text_fmt,
                 timestamp, g_log_level_names[level],
                 component ? component : "-",
                 event ? event : "-",
                 base_message);
    }

    UNLOCK_LOG_MUTEX(&g_log_mutex); // Unlock after accessing shared resources
}
