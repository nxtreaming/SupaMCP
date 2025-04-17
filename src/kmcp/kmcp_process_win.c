#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include "kmcp_process.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <windows.h>
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
            strcat(cmd_line, " \"");
            strcat(cmd_line, args[i]);
            strcat(cmd_line, "\"");
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

    // Build environment block
    LPVOID env_block = build_environment_block(process->env, process->env_count);

    // Create job object to terminate child processes
    process->job_handle = CreateJobObject(NULL, NULL);
    if (process->job_handle == NULL) {
        mcp_log_error("Failed to create job object");
        free(cmd_line);
        free(env_block);
        return -1;
    }

    // Set job object limit information so that when the main process terminates, all child processes will also terminate
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = {0};
    job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(process->job_handle, JobObjectExtendedLimitInformation, &job_info, sizeof(job_info))) {
        mcp_log_error("Failed to set job object information");
        CloseHandle(process->job_handle);
        process->job_handle = NULL;
        free(cmd_line);
        free(env_block);
        return -1;
    }

    // Create process
    STARTUPINFO startup_info = {0};
    startup_info.cb = sizeof(STARTUPINFO);

    if (!CreateProcess(
        NULL,               // Application name (use command line)
        cmd_line,           // Command line
        NULL,               // Process security attributes
        NULL,               // Thread security attributes
        FALSE,              // Don't inherit handles
        CREATE_NO_WINDOW,   // Creation flags
        env_block,          // Environment block
        NULL,               // Current directory
        &startup_info,      // Startup info
        &process->process_info // Process information
    )) {
        DWORD error = GetLastError();
        mcp_log_error("Failed to create process: %s (error code: %lu)", cmd_line, error);
        CloseHandle(process->job_handle);
        process->job_handle = NULL;
        free(cmd_line);
        free(env_block);
        return -1;
    }

    // Add process to job object
    if (!AssignProcessToJobObject(process->job_handle, process->process_info.hProcess)) {
        mcp_log_error("Failed to assign process to job object");
        // Continue execution because the process has already been created
    }

    // Free resources
    free(cmd_line);
    free(env_block);

    // Mark as running
    process->is_running = true;
    process->handle_valid = true;

    return 0;
}

/**
 * @brief Check if a process is running
 */
bool kmcp_process_is_running(kmcp_process_t* process) {
    if (!process || !process->handle_valid) {
        return false;
    }

    // If process handle is invalid, return false
    if (process->process_info.hProcess == NULL) {
        process->is_running = false;
        return false;
    }

    // Check if the process is still running
    DWORD exit_code;
    if (GetExitCodeProcess(process->process_info.hProcess, &exit_code)) {
        if (exit_code != STILL_ACTIVE) {
            process->is_running = false;
            process->exit_code = (int)exit_code;
        }
    } else {
        // Failed to get exit code, assume the process has terminated
        process->is_running = false;
    }

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

    // If handle is not valid, just mark as not running and return success
    if (!process->handle_valid) {
        mcp_log_warn("Process handle is not valid, marking as not running");
        process->is_running = false;
        return 0;
    }

    // If the process is not running, return success immediately
    if (!process->is_running) {
        return 0;
    }

    // Terminate the process
    if (process->job_handle != NULL) {
        // Use job object to terminate all related processes
        if (!TerminateJobObject(process->job_handle, 1)) {
            mcp_log_error("Failed to terminate job object");
            // Try to terminate the process directly
            if (!TerminateProcess(process->process_info.hProcess, 1)) {
                mcp_log_error("Failed to terminate process");
                return -1;
            }
        }
    } else {
        // Terminate the process directly
        if (!TerminateProcess(process->process_info.hProcess, 1)) {
            mcp_log_error("Failed to terminate process");
            return -1;
        }
    }

    // Wait for the process to terminate
    WaitForSingleObject(process->process_info.hProcess, 1000); // Wait for 1 second

    // Get exit code
    DWORD exit_code;
    if (GetExitCodeProcess(process->process_info.hProcess, &exit_code)) {
        process->exit_code = (int)exit_code;
    }

    // Mark as not running
    process->is_running = false;

    return 0;
}

/**
 * @brief Wait for a process to end
 */
int kmcp_process_wait(kmcp_process_t* process, int timeout_ms) {
    if (!process || !process->handle_valid) {
        mcp_log_error("Invalid parameters or process handle is not valid");
        return -1;
    }

    // If the process is not running, return success immediately
    if (!process->is_running) {
        return 0;
    }

    // Wait for the process to terminate
    DWORD wait_result = WaitForSingleObject(process->process_info.hProcess, timeout_ms == 0 ? INFINITE : (DWORD)timeout_ms);

    if (wait_result == WAIT_OBJECT_0) {
        // Process has terminated
        DWORD exit_code;
        if (GetExitCodeProcess(process->process_info.hProcess, &exit_code)) {
            process->exit_code = (int)exit_code;
        }
        process->is_running = false;
        return 0;
    } else if (wait_result == WAIT_TIMEOUT) {
        // Timeout
        return 1;
    } else {
        // Wait failed
        mcp_log_error("Failed to wait for process");
        return -1;
    }
}

/**
 * @brief Get process exit code
 */
int kmcp_process_get_exit_code(kmcp_process_t* process, int* exit_code) {
    if (!process || !exit_code || !process->handle_valid) {
        mcp_log_error("Invalid parameters or process handle is not valid");
        return -1;
    }

    // If the process is still running, return an error
    if (process->is_running) {
        mcp_log_error("Process is still running");
        return -1;
    }

    // Return exit code
    *exit_code = process->exit_code;
    return 0;
}

/**
 * @brief Close process handle
 */
void kmcp_process_close(kmcp_process_t* process) {
    if (!process) {
        return;
    }

    // If the process is still running, terminate it
    if (process->is_running) {
        mcp_log_debug("Process is still running, terminating it");
        kmcp_process_terminate(process);
    }

    // Ensure is_running is set to false
    process->is_running = false;

    // Close handles
    if (process->handle_valid) {
        if (process->process_info.hProcess != NULL) {
            CloseHandle(process->process_info.hProcess);
        }
        if (process->process_info.hThread != NULL) {
            CloseHandle(process->process_info.hThread);
        }
        if (process->job_handle != NULL) {
            CloseHandle(process->job_handle);
        }
        process->handle_valid = false;
    }

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
