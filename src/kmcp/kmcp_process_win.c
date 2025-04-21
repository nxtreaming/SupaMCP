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
    HANDLE process_handle; // Windows process handle
    DWORD process_id;      // Windows process ID
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

    // Allocate memory and initialize to zero
    kmcp_process_t* process = (kmcp_process_t*)calloc(1, sizeof(kmcp_process_t));
    if (!process) {
        mcp_log_error("Failed to allocate memory for process (size: %zu bytes)", sizeof(kmcp_process_t));
        return NULL;
    }
    process->command = mcp_strdup(command);
    if (!process->command) {
        mcp_log_error("Failed to duplicate command string");
        free(process);
        return NULL;
    }

    // Copy arguments array
    if (args && args_count > 0) {
        process->args = (char**)calloc(args_count, sizeof(char*));
        if (!process->args) {
            mcp_log_error("Failed to allocate memory for arguments array (size: %zu bytes)", args_count * sizeof(char*));
            free(process->command);
            free(process);
            return NULL;
        }

        // calloc already initialized all pointers to NULL
        process->args_count = args_count;

        for (size_t i = 0; i < args_count; i++) {
            if (args[i]) {
                process->args[i] = mcp_strdup(args[i]);
                if (!process->args[i]) {
                    mcp_log_error("Failed to duplicate argument string");
                    // Clean up already allocated strings
                    for (size_t j = 0; j < i; j++) {
                        free(process->args[j]);
                    }
                    free(process->args);
                    free(process->command);
                    free(process);
                    return NULL;
                }
            }
        }
    }

    // Copy environment variables array
    if (env && env_count > 0) {
        process->env = (char**)calloc(env_count, sizeof(char*));
        if (!process->env) {
            mcp_log_error("Failed to allocate memory for environment variables array (size: %zu bytes)", env_count * sizeof(char*));
            // Clean up arguments
            if (process->args) {
                for (size_t i = 0; i < process->args_count; i++) {
                    free(process->args[i]);
                }
                free(process->args);
            }
            free(process->command);
            free(process);
            return NULL;
        }

        // calloc already initialized all pointers to NULL
        process->env_count = env_count;

        for (size_t i = 0; i < env_count; i++) {
            if (env[i]) {
                process->env[i] = mcp_strdup(env[i]);
                if (!process->env[i]) {
                    mcp_log_error("Failed to duplicate environment variable string");
                    // Clean up already allocated strings
                    for (size_t j = 0; j < i; j++) {
                        free(process->env[j]);
                    }
                    free(process->env);
                    // Clean up arguments
                    if (process->args) {
                        for (size_t j = 0; j < process->args_count; j++) {
                            free(process->args[j]);
                        }
                        free(process->args);
                    }
                    free(process->command);
                    free(process);
                    return NULL;
                }
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

    // Allocate memory and initialize to zero
    char* env_block = (char*)calloc(1, total_size);
    if (!env_block) {
        mcp_log_error("Failed to allocate memory for environment block (size: %zu bytes)", total_size);
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

    // Initialize environment block to NULL
    LPVOID env_block = NULL;

    // Log the command line for debugging
    mcp_log_info("Starting process with command line: %s", cmd_line);

    // Build environment block if needed
    if (process->env && process->env_count > 0) {
        env_block = build_environment_block(process->env, process->env_count);
        if (!env_block) {
            mcp_log_error("Failed to build environment block");
            free(cmd_line);
            return -1;
        }
    }

    // Get the directory part of the command
    char working_dir[MAX_PATH] = {0};
    if (strlen(process->command) >= MAX_PATH) {
        mcp_log_error("Command path too long");
        free(cmd_line);
        free(env_block);
        return -1;
    }
    strncpy(working_dir, process->command, MAX_PATH - 1);
    working_dir[MAX_PATH - 1] = '\0'; // Ensure null termination

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

    // Calculate required size for parameters string
    size_t params_size = 1; // Start with 1 for null terminator
    for (size_t i = 0; i < process->args_count; i++) {
        if (process->args[i]) {
            params_size += strlen(process->args[i]) + 3; // +3 for space and quotes
        }
    }

    // Allocate memory for parameters string and initialize to zero
    char* params = (char*)calloc(1, params_size);
    if (!params) {
        mcp_log_error("Failed to allocate memory for parameters string (size: %zu bytes)", params_size);
        free(cmd_line);
        free(env_block);
        return -1;
    }

    // Initialize parameters string
    params[0] = '\0';

    // Build parameters string (all arguments combined)
    for (size_t i = 0; i < process->args_count; i++) {
        if (process->args[i]) {
            if (i > 0 || params[0] != '\0') {
                if (strlen(params) + 1 < params_size) {
                    strcat(params, " ");
                } else {
                    mcp_log_error("Parameters string buffer overflow");
                    free(params);
                    free(cmd_line);
                    free(env_block);
                    return -1;
                }
            }

            // Add quotes if the argument contains spaces
            if (strchr(process->args[i], ' ') != NULL) {
                size_t required_len = strlen(params) + strlen(process->args[i]) + 3; // +3 for quotes and null terminator
                if (required_len <= params_size) {
                    strcat(params, "\"");
                    strcat(params, process->args[i]);
                    strcat(params, "\"");
                } else {
                    mcp_log_error("Parameters string buffer overflow");
                    free(params);
                    free(cmd_line);
                    free(env_block);
                    return -1;
                }
            } else {
                size_t required_len = strlen(params) + strlen(process->args[i]) + 1; // +1 for null terminator
                if (required_len <= params_size) {
                    strcat(params, process->args[i]);
                } else {
                    mcp_log_error("Parameters string buffer overflow");
                    free(params);
                    free(cmd_line);
                    free(env_block);
                    return -1;
                }
            }
        }
    }

    mcp_log_info("Using ShellExecuteEx to start process: %s with params: %s", process->command, params);

    // Use ShellExecuteEx to start the process and get a handle
    SHELLEXECUTEINFO sei = {0};
    sei.cbSize = sizeof(SHELLEXECUTEINFO);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS; // Get process handle
    sei.hwnd = NULL;
    sei.lpVerb = "open";
    sei.lpFile = process->command;
    sei.lpParameters = params[0] != '\0' ? params : NULL;
    sei.lpDirectory = working_dir[0] != '\0' ? working_dir : NULL;
    sei.nShow = SW_SHOW;

    // Execute the process
    if (!ShellExecuteEx(&sei)) {
        DWORD error = GetLastError();
        mcp_log_error("ShellExecuteEx failed with error code: %lu", error);
        free(cmd_line);
        free(env_block);
        return -1;
    }

    // Store process handle and ID
    process->process_handle = sei.hProcess;
    process->process_id = GetProcessId(sei.hProcess);
    process->handle_valid = true;

    mcp_log_info("Process created with handle: %p and PID: %lu", process->process_handle, process->process_id);

    // Free resources
    free(cmd_line);
    free(env_block);
    free(params);

    // Mark as running
    process->is_running = true;
    process->handle_valid = true;

    // Check if the process is still running
    if (!kmcp_process_is_running(process)) {
        mcp_log_error("Process exited immediately with code: %d", process->exit_code);
        return -1;
    }

    mcp_log_info("Process started successfully with PID: %lu", process->process_id);
    return 0;
}

/**
 * @brief Check if a process is running
 */
bool kmcp_process_is_running(kmcp_process_t* process) {
    if (!process || !process->handle_valid) {
        return false;
    }

    // Check if the process is still running
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process->process_handle, &exit_code)) {
        if (exit_code == STILL_ACTIVE) {
            return true;
        } else {
            // Process has exited
            process->is_running = false;
            process->exit_code = (int)exit_code;
            return false;
        }
    } else {
        // Failed to get exit code, assume process is not running
        DWORD error = GetLastError();
        mcp_log_error("Failed to get process exit code: %lu", error);
        process->is_running = false;
        return false;
    }
}

/**
 * @brief Terminate a process
 */
int kmcp_process_terminate(kmcp_process_t* process) {
    if (!process) {
        mcp_log_error("Invalid parameter: process is NULL");
        return -1;
    }

    // If the process is not running or handle is invalid, return success
    if (!process->is_running || !process->handle_valid) {
        process->is_running = false;
        return 0;
    }

    // Terminate the process
    if (TerminateProcess(process->process_handle, 1)) {
        mcp_log_info("Process terminated successfully");
        process->is_running = false;
        process->exit_code = 1; // Forced termination
        return 0;
    } else {
        DWORD error = GetLastError();
        mcp_log_error("Failed to terminate process: %lu", error);
        return -1;
    }
}

/**
 * @brief Wait for a process to end
 */
int kmcp_process_wait(kmcp_process_t* process, int timeout_ms) {
    if (!process) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // If the process is not running or handle is invalid, return success immediately
    if (!process->is_running || !process->handle_valid) {
        return 0;
    }

    // Wait for the process to terminate
    DWORD wait_result = WaitForSingleObject(process->process_handle, timeout_ms > 0 ? (DWORD)timeout_ms : INFINITE);

    if (wait_result == WAIT_OBJECT_0) {
        // Process has terminated
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process->process_handle, &exit_code)) {
            process->exit_code = (int)exit_code;
        }
        process->is_running = false;
        return 0;
    } else if (wait_result == WAIT_TIMEOUT) {
        // Timeout occurred
        return 1;
    } else {
        // Error occurred
        DWORD error = GetLastError();
        mcp_log_error("Failed to wait for process: %lu", error);
        return -1;
    }
}

/**
 * @brief Get process exit code
 */
int kmcp_process_get_exit_code(kmcp_process_t* process, int* exit_code) {
    if (!process || !exit_code) {
        mcp_log_error("Invalid parameters");
        return -1;
    }

    // If the process is still running, check its status
    if (process->is_running) {
        // Update the running status
        if (kmcp_process_is_running(process)) {
            mcp_log_error("Process is still running");
            return -1;
        }
        // If we get here, the process has exited but our state wasn't updated
    }

    // If we have a valid handle, get the actual exit code
    if (process->handle_valid) {
        DWORD win_exit_code = 0;
        if (GetExitCodeProcess(process->process_handle, &win_exit_code)) {
            if (win_exit_code == STILL_ACTIVE) {
                mcp_log_error("Process is still running");
                return -1;
            }
            process->exit_code = (int)win_exit_code;
        } else {
            DWORD error = GetLastError();
            mcp_log_error("Failed to get process exit code: %lu", error);
            return -1;
        }
    }

    // Return the stored exit code
    *exit_code = process->exit_code;
    return 0;
}

/**
 * @brief Close process handle and free resources
 *
 * @param process Process to close (can be NULL)
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

    // Close process handle if valid
    if (process->handle_valid && process->process_handle != NULL) {
        CloseHandle(process->process_handle);
        process->process_handle = NULL;
    }

    // Ensure is_running is set to false and handle_valid is false
    process->is_running = false;
    process->handle_valid = false;

    // Free resources with null checks
    if (process->command) {
        free(process->command);
        process->command = NULL;
    }

    if (process->args) {
        for (size_t i = 0; i < process->args_count; i++) {
            if (process->args[i]) {
                free(process->args[i]);
                process->args[i] = NULL;
            }
        }
        free(process->args);
        process->args = NULL;
        process->args_count = 0;
    }

    if (process->env) {
        for (size_t i = 0; i < process->env_count; i++) {
            if (process->env[i]) {
                free(process->env[i]);
                process->env[i] = NULL;
            }
        }
        free(process->env);
        process->env = NULL;
        process->env_count = 0;
    }

    // Finally free the process structure
    free(process);
}

#endif // _WIN32
