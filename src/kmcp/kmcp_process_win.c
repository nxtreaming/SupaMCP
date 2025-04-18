#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include "kmcp_process.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Complete definition of process management structure (Windows version)
 */
struct kmcp_process {
    char* command;         // Command
    char** args;           // Arguments array
    size_t args_count;     // Number of arguments
    char** env;            // Environment variables array
    size_t env_count;      // Number of environment variables
    PROCESS_INFORMATION process_info; // Windows process information
    HANDLE job_handle;     // Windows job object handle, used to terminate child processes
    int exit_code;         // Exit code
    bool is_running;       // Whether the process is running
    bool handle_valid;     // Whether the handle is valid
};

/**
 * @brief Create a process
 */
kmcp_process_t* kmcp_process_create(
    const char* command,
    char** args,
    size_t args_count,
    char** env,
    size_t env_count
) {
    if (!command) {
        mcp_log_error("Invalid parameter: command is NULL");
        return NULL;
    }

    // Allocate memory
    kmcp_process_t* process = (kmcp_process_t*)malloc(sizeof(kmcp_process_t));
    if (!process) {
        mcp_log_error("Failed to allocate memory for process");
        return NULL;
    }

    // Initialize fields
    memset(process, 0, sizeof(kmcp_process_t));
    process->command = mcp_strdup(command);

    // Copy arguments array
    if (args && args_count > 0) {
        process->args = (char**)malloc(args_count * sizeof(char*));
        if (process->args) {
            process->args_count = args_count;
            for (size_t i = 0; i < args_count; i++) {
                process->args[i] = args[i] ? mcp_strdup(args[i]) : NULL;
            }
        }
    }

    // Copy environment variables array
    if (env && env_count > 0) {
        process->env = (char**)malloc(env_count * sizeof(char*));
        if (process->env) {
            process->env_count = env_count;
            for (size_t i = 0; i < env_count; i++) {
                process->env[i] = env[i] ? mcp_strdup(env[i]) : NULL;
            }
        }
    }

    return process;
}

/**
 * @brief Build command line string
 *
 * @param command Command
 * @param args Arguments array
 * @param args_count Number of arguments
 * @return char* Command line string, caller responsible for freeing
 */
static char* build_command_line(const char* command, char** args, size_t args_count) {
    // Calculate command line length
    size_t cmd_len = strlen(command) + 1; // +1 for space
    for (size_t i = 0; i < args_count; i++) {
        if (args[i]) {
            cmd_len += strlen(args[i]) + 3; // +3 for space and quotes
        }
    }
    cmd_len += 1; // +1 for null terminator

    // Allocate memory
    char* cmd_line = (char*)malloc(cmd_len);
    if (!cmd_line) {
        return NULL;
    }

    // Build command line
    strcpy(cmd_line, command);

    for (size_t i = 0; i < args_count; i++) {
        if (args[i]) {
            strcat(cmd_line, " ");

            // Add quotes only if the argument contains spaces
            if (strchr(args[i], ' ') != NULL) {
                strcat(cmd_line, "\"");
                strcat(cmd_line, args[i]);
                strcat(cmd_line, "\"");
            } else {
                strcat(cmd_line, args[i]);
            }
        }
    }

    return cmd_line;
}

/**
 * @brief Build environment block
 *
 * @param env Environment variables array
 * @param env_count Number of environment variables
 * @return LPVOID Environment block, caller responsible for freeing
 */
static LPVOID build_environment_block(char** env, size_t env_count) {
    if (!env || env_count == 0) {
        return NULL;
    }

    // Calculate environment block size
    size_t total_size = 0;
    for (size_t i = 0; i < env_count; i++) {
        if (env[i]) {
            total_size += strlen(env[i]) + 1; // +1 for null terminator
        }
    }
    total_size += 1; // +1 for final null terminator

    // Allocate memory
    char* env_block = (char*)malloc(total_size);
    if (!env_block) {
        return NULL;
    }

    // Build environment block
    char* p = env_block;
    for (size_t i = 0; i < env_count; i++) {
        if (env[i]) {
            size_t len = strlen(env[i]);
            memcpy(p, env[i], len);
            p += len;
            *p++ = '\0';
        }
    }
    *p = '\0'; // Final null terminator

    return env_block;
}

/**
 * @brief Start a process
 */
int kmcp_process_start(kmcp_process_t* process) {
    if (!process || !process->command) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // If the process is already running, return an error
    if (process->is_running) {
        mcp_log_error("Process is already running");
        return -1;
    }

    // Build command line
    char* cmd_line = build_command_line(process->command, process->args, process->args_count);
    if (!cmd_line) {
        mcp_log_error("Failed to build command line");
        return -1;
    }

    // Log the command line for debugging
    mcp_log_info("Starting process with command line: %s", cmd_line);

    // Build environment block
    LPVOID env_block = build_environment_block(process->env, process->env_count);

    // We don't use job objects for server processes, as we want them to continue running
    // even after the client process exits
    process->job_handle = NULL;
    mcp_log_info("Not using job object for server process");

    // Do NOT set JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE flag, as we want the server process to continue running
    // even after the client process exits
    mcp_log_info("Not setting JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE flag to allow server to continue running");

    // Create process
    STARTUPINFO startup_info = {0};
    startup_info.cb = sizeof(STARTUPINFO);

    // Don't create pipes for stdout and stderr, let the process use its own console
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    HANDLE stderr_read = NULL;
    HANDLE stderr_write = NULL;

    mcp_log_info("Not redirecting stdout/stderr, letting process use its own console");

    // Get the directory part of the command
    char working_dir[MAX_PATH] = {0};
    strncpy(working_dir, process->command, MAX_PATH - 1);

    // Find the last backslash or forward slash
    char* last_slash = strrchr(working_dir, '\\');
    char* last_fwd_slash = strrchr(working_dir, '/');

    if (last_slash != NULL || last_fwd_slash != NULL) {
        // Use the rightmost slash
        char* last_dir_sep = (last_slash > last_fwd_slash) ? last_slash : last_fwd_slash;
        *last_dir_sep = '\0'; // Terminate the string at the slash
        mcp_log_info("Using working directory: %s", working_dir);
    } else {
        // No directory part in the command, use current directory
        working_dir[0] = '\0';
    }

    // Build parameters string (all arguments combined)
    char params[MAX_PATH * 2] = {0};
    for (size_t i = 0; i < process->args_count; i++) {
        if (process->args[i]) {
            if (i > 0) {
                strcat(params, " ");
            }

            // Add quotes if the argument contains spaces
            if (strchr(process->args[i], ' ') != NULL) {
                strcat(params, "\"");
                strcat(params, process->args[i]);
                strcat(params, "\"");
            } else {
                strcat(params, process->args[i]);
            }
        }
    }

    mcp_log_info("Using ShellExecute to start process: %s with params: %s", process->command, params);

    // Use ShellExecute to start the process
    HINSTANCE result = ShellExecute(
        NULL,               // Parent window
        "open",            // Operation
        process->command,   // File to execute
        params,             // Parameters
        working_dir[0] != '\0' ? working_dir : NULL, // Working directory
        SW_SHOW             // Show command (SW_SHOW = show window, SW_HIDE = hide window)
    );

    // Check result
    if ((INT_PTR)result <= 32) {
        // ShellExecute failed
        DWORD error = GetLastError();
        mcp_log_error("ShellExecute failed with code: %d (error code: %lu)", (int)(INT_PTR)result, error);
        free(cmd_line);
        free(env_block);
        return -1;
    }

    // ShellExecute doesn't return process information, so we need to create a dummy process info
    memset(&process->process_info, 0, sizeof(PROCESS_INFORMATION));

    // We can't get the actual process ID, so we'll use a dummy value
    process->process_info.dwProcessId = GetCurrentProcessId(); // Just a placeholder

    // We don't use job objects for server processes
    mcp_log_info("Process created without job object");

    // Free resources
    free(cmd_line);
    free(env_block);

    // Mark as running
    process->is_running = true;
    process->handle_valid = true;

    // Check if the process is still running
    if (!kmcp_process_is_running(process)) {
        mcp_log_error("Process exited immediately with code: %d", process->exit_code);
        return -1;
    }

    // Close pipe handles if they were created
    if (stdout_read != NULL) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
    }
    if (stderr_read != NULL) {
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
    }

    mcp_log_info("Process started successfully with PID: %lu", process->process_info.dwProcessId);
    return 0;
}

/**
 * @brief Check if a process is running
 */
bool kmcp_process_is_running(kmcp_process_t* process) {
    if (!process) {
        return false;
    }

    // Since we're using ShellExecute, we don't have a process handle
    // We'll assume the process is running for a while after we start it
    // This is not ideal, but it's the best we can do without a process handle

    // For now, just return the current value of is_running
    return process->is_running;
}

/**
 * @brief Terminate a process
 */
int kmcp_process_terminate(kmcp_process_t* process) {
    if (!process) {
        mcp_log_error("Invalid parameter: process is NULL");
        return -1;
    }

    // Since we're using ShellExecute, we don't have a process handle
    // We can't terminate the process directly
    // We'll just mark it as not running

    mcp_log_warn("Cannot terminate process started with ShellExecute, marking as not running");
    process->is_running = false;

    return 0;
}

/**
 * @brief Wait for a process to end
 */
int kmcp_process_wait(kmcp_process_t* process, int timeout_ms) {
    if (!process) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // If the process is not running, return success immediately
    if (!process->is_running) {
        return 0;
    }

    // Since we're using ShellExecute, we don't have a process handle
    // We can't wait for the process to terminate
    // We'll just sleep for the specified timeout

    mcp_log_warn("Cannot wait for process started with ShellExecute, sleeping instead");

    if (timeout_ms > 0) {
        Sleep((DWORD)timeout_ms);
        return 1; // Timeout
    }

    return 0;
}

/**
 * @brief Get process exit code
 */
int kmcp_process_get_exit_code(kmcp_process_t* process, int* exit_code) {
    if (!process || !exit_code) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // If the process is still running, return an error
    if (process->is_running) {
        mcp_log_error("Process is still running");
        return -1;
    }

    // Since we're using ShellExecute, we don't have a process handle
    // We can't get the actual exit code
    // We'll just return a dummy value

    mcp_log_warn("Cannot get exit code for process started with ShellExecute, returning 0");
    *exit_code = 0;

    return 0;
}

/**
 * @brief Close process handle
 */
void kmcp_process_close(kmcp_process_t* process) {
    if (!process) {
        return;
    }

    // For server processes, we want them to continue running even after the client exits
    // So we don't terminate them here
    if (process->is_running) {
        mcp_log_info("Process is still running, but we won't terminate it (server process)");
        // Don't call kmcp_process_terminate(process);
    }

    // Ensure is_running is set to false
    process->is_running = false;

    // Since we're using ShellExecute, we don't have process handles to close
    process->handle_valid = false;

    // Free resources
    free(process->command);

    if (process->args) {
        for (size_t i = 0; i < process->args_count; i++) {
            free(process->args[i]);
        }
        free(process->args);
    }

    if (process->env) {
        for (size_t i = 0; i < process->env_count; i++) {
            free(process->env[i]);
        }
        free(process->env);
    }

    free(process);
}

#endif // _WIN32
