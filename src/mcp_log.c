#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#   define PATH_SEPARATOR "\\"
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#   define PATH_SEPARATOR "/"
#endif

// Global variables for logging state
static FILE* g_log_file = NULL;
static log_level_t g_log_level = LOG_LEVEL_INFO; // Default level
static const char* g_log_level_names[] = {"ERROR", "WARN", "INFO", "DEBUG"};

/**
 * Log a message to the console and/or log file
 */
void log_message(log_level_t level, const char* format, ...) {
    if (level > g_log_level) {
        return;
    }

    time_t now;
    struct tm timeinfo_storage; // Storage for thread-safe variants
    struct tm* timeinfo;
    char timestamp[20];

    time(&now);

    // Use thread-safe localtime variants
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


    va_list args;
    va_start(args, format);

    char message[1024];
    // Use vsnprintf for safety
    int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Ensure null termination even if vsnprintf truncated
    if (written >= (int)sizeof(message)) {
         message[sizeof(message) - 1] = '\0';
    } else if (written < 0) {
        // Encoding error occurred, write a fallback message
        snprintf(message, sizeof(message), "Encoding error in log message format: %s", format);
    }


    // Log to console (stderr)
    fprintf(stderr, "[%s] [%s] %s\n", timestamp, g_log_level_names[level], message);

    // Log to file if available
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%s] [%s] %s\n", timestamp, g_log_level_names[level], message);
        fflush(g_log_file); // Ensure logs are written immediately to file
    }
}


/**
 * Create log directory if it doesn't exist
 */
static int create_log_directory(const char* log_file_path) {
    if (log_file_path == NULL) {
        return 0; // No path provided, nothing to do
    }

    // Duplicate the path string because dirname might modify it
    char* path_copy = strdup(log_file_path);
    if (path_copy == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to duplicate log file path for directory creation.");
        return -1;
    }

    // Find the last path separator
    char* last_separator = strrchr(path_copy, PATH_SEPARATOR[0]);
    if (last_separator == NULL) {
        // No directory part in the path (e.g., just "logfile.log")
        free(path_copy);
        return 0;
    }

    // Null-terminate at the separator to get just the directory path
    *last_separator = '\0';

    // Check if directory is empty (e.g. "/logfile.log") or root ("/")
     if (strlen(path_copy) == 0 || strcmp(path_copy, PATH_SEPARATOR) == 0) {
        free(path_copy);
        return 0; // No directory needs to be created or root directory
    }


    // Create the directory recursively (more robust)
    int result = 0;
    char* p = path_copy;
    // Skip leading separator if present (for absolute paths)
    if (*p == PATH_SEPARATOR[0]) {
        p++;
    }

    while ((p = strchr(p, PATH_SEPARATOR[0])) != NULL) {
        *p = '\0'; // Temporarily terminate at the separator

#ifdef _WIN32
        if (CreateDirectory(path_copy, NULL) == 0) {
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                log_message(LOG_LEVEL_ERROR, "Failed to create log directory component: %s (Error: %lu)", path_copy, GetLastError());
                result = -1;
                break;
            }
        }
#else
        if (mkdir(path_copy, 0755) != 0) {
            if (errno != EEXIST) {
                log_message(LOG_LEVEL_ERROR, "Failed to create log directory component: %s (errno: %d - %s)", path_copy, errno, strerror(errno));
                result = -1;
                break;
            }
        }
#endif
        *p = PATH_SEPARATOR[0]; // Restore separator
        p++; // Move past the separator
    }

    // Create the final directory component if loop didn't fail
    if (result == 0) {
#ifdef _WIN32
         if (CreateDirectory(path_copy, NULL) == 0) {
            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                log_message(LOG_LEVEL_ERROR, "Failed to create final log directory: %s (Error: %lu)", path_copy, GetLastError());
                result = -1;
            }
        }
#else
         if (mkdir(path_copy, 0755) != 0) {
            if (errno != EEXIST) {
                log_message(LOG_LEVEL_ERROR, "Failed to create final log directory: %s (errno: %d - %s)", path_copy, errno, strerror(errno));
                result = -1;
            }
        }
#endif
    }


    free(path_copy);
    return result;
}

/**
 * Initialize logging
 */
int init_logging(const char* log_file_path, log_level_t level) {
    // Ensure level is valid
    if (level < LOG_LEVEL_ERROR || level > LOG_LEVEL_DEBUG) {
        level = LOG_LEVEL_INFO; // Default to INFO if invalid
    }
    g_log_level = level;

    // Close existing log file if any
    close_logging();

    if (log_file_path != NULL && strlen(log_file_path) > 0) {
        // Create log directory if needed
        if (create_log_directory(log_file_path) != 0) {
            // Error already logged by create_log_directory
            return -1;
        }

        // Open log file in append mode using platform-specific safe function
#ifdef _WIN32
        errno_t err = fopen_s(&g_log_file, log_file_path, "a");
        if (err != 0 || g_log_file == NULL) {
            char err_buf[128];
            strerror_s(err_buf, sizeof(err_buf), err); // Get error message for errno_t
            log_message(LOG_LEVEL_ERROR, "Failed to open log file '%s': %s (errno: %d)", log_file_path, err_buf, err);
            g_log_file = NULL; // Ensure it's NULL on failure
#else
        g_log_file = fopen(log_file_path, "a");
        if (g_log_file == NULL) {
            // Use log_message which will print to stderr
            char err_buf[128];
             if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
                 log_message(LOG_LEVEL_ERROR, "Failed to open log file '%s': %s (errno: %d)", log_file_path, err_buf, errno);
             } else {
                 log_message(LOG_LEVEL_ERROR, "Failed to open log file '%s': (errno: %d, strerror_r failed)", log_file_path, errno);
             }
#endif
            return -1;
        }
        log_message(LOG_LEVEL_INFO, "Logging to file: %s", log_file_path);
    } else {
         log_message(LOG_LEVEL_INFO, "File logging disabled.");
    }

    return 0;
}

/**
 * Close logging
 */
void close_logging(void) {
    if (g_log_file != NULL) {
        log_message(LOG_LEVEL_INFO, "Closing log file.");
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
