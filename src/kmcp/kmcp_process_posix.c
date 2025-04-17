#ifndef _WIN32

#include "kmcp_process.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

/**
 * @brief Complete definition of process management structure (POSIX version)
 */
struct kmcp_process {
    char* command;         // Command
    char** args;           // Arguments array
    size_t args_count;     // Number of arguments
    char** env;            // Environment variables array
    size_t env_count;      // Number of environment variables
    pid_t pid;             // Process ID
    int exit_code;         // Exit code
    bool is_running;       // Whether the process is running
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

    // Create arguments array, including the command itself as the first argument
    char** argv = (char**)malloc((process->args_count + 2) * sizeof(char*));
    if (!argv) {
        mcp_log_error("Failed to allocate memory for argv");
        return -1;
    }

    argv[0] = process->command;
    for (size_t i = 0; i < process->args_count; i++) {
        argv[i + 1] = process->args[i];
    }
    argv[process->args_count + 1] = NULL;

    // Create environment variables array
    char** envp = NULL;
    if (process->env && process->env_count > 0) {
        envp = (char**)malloc((process->env_count + 1) * sizeof(char*));
        if (!envp) {
            mcp_log_error("Failed to allocate memory for envp");
            free(argv);
            return -1;
        }

        for (size_t i = 0; i < process->env_count; i++) {
            envp[i] = process->env[i];
        }
        envp[process->env_count] = NULL;
    }

    // Create child process
    pid_t pid = fork();

    if (pid < 0) {
        // fork failed
        mcp_log_error("Failed to fork process: %s", strerror(errno));
        free(argv);
        free(envp);
        return -1;
    } else if (pid == 0) {
        // Child process

        // Redirect standard input/output/error to /dev/null
        int dev_null = open("/dev/null", O_RDWR);
        if (dev_null >= 0) {
            dup2(dev_null, STDIN_FILENO);
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            close(dev_null);
        }

        // Execute command
        if (envp) {
            execve(process->command, argv, envp);
        } else {
            execv(process->command, argv);
        }

        // If execv/execve returns, it means an error occurred
        _exit(127);
    } else {
        // Parent process
        process->pid = pid;
        process->is_running = true;
    }

    // Free resources
    free(argv);
    free(envp);

    return 0;
}

/**
 * @brief Check if a process is running
 */
bool kmcp_process_is_running(kmcp_process_t* process) {
    if (!process || process->pid <= 0) {
        return false;
    }

    // If the process is not running, return false directly
    if (!process->is_running) {
        return false;
    }

    // Check if the process is still running
    int status;
    pid_t result = waitpid(process->pid, &status, WNOHANG);

    if (result == 0) {
        // Process is still running
        return true;
    } else if (result == process->pid) {
        // Process has terminated
        process->is_running = false;

        if (WIFEXITED(status)) {
            process->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            process->exit_code = 128 + WTERMSIG(status);
        } else {
            process->exit_code = -1;
        }

        return false;
    } else {
        // waitpid failed
        mcp_log_error("Failed to check process status: %s", strerror(errno));
        return false;
    }
}

/**
 * @brief Terminate a process
 */
int kmcp_process_terminate(kmcp_process_t* process) {
    if (!process || process->pid <= 0) {
        mcp_log_error("Invalid parameters or process not started");
        return -1;
    }

    // If the process is not running, return success immediately
    if (!process->is_running) {
        return 0;
    }

    // Send SIGTERM signal
    if (kill(process->pid, SIGTERM) != 0) {
        mcp_log_error("Failed to send SIGTERM to process: %s", strerror(errno));
        return -1;
    }

    // Wait for the process to terminate (maximum 1 second)
    time_t start_time = time(NULL);
    while (time(NULL) - start_time < 1) {
        if (!kmcp_process_is_running(process)) {
            return 0;
        }
        usleep(10000); // Sleep for 10 milliseconds
    }

    // If the process is still running, send SIGKILL signal
    if (process->is_running) {
        if (kill(process->pid, SIGKILL) != 0) {
            mcp_log_error("Failed to send SIGKILL to process: %s", strerror(errno));
            return -1;
        }

        // Wait for the process to terminate again
        start_time = time(NULL);
        while (time(NULL) - start_time < 1) {
            if (!kmcp_process_is_running(process)) {
                return 0;
            }
            usleep(10000); // Sleep for 10 milliseconds
        }
    }

    return 0;
}

/**
 * @brief Wait for a process to end
 */
int kmcp_process_wait(kmcp_process_t* process, int timeout_ms) {
    if (!process || process->pid <= 0) {
        mcp_log_error("Invalid parameters or process not started");
        return -1;
    }

    // If the process is not running, return success immediately
    if (!process->is_running) {
        return 0;
    }

    // If timeout_ms is 0, wait indefinitely
    if (timeout_ms == 0) {
        int status;
        pid_t result = waitpid(process->pid, &status, 0);

        if (result == process->pid) {
            // Process has terminated
            process->is_running = false;

            if (WIFEXITED(status)) {
                process->exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                process->exit_code = 128 + WTERMSIG(status);
            } else {
                process->exit_code = -1;
            }

            return 0;
        } else {
            // waitpid failed
            mcp_log_error("Failed to wait for process: %s", strerror(errno));
            return -1;
        }
    } else {
        // With timeout limit, use polling
        time_t start_time = time(NULL);
        int elapsed_ms = 0;

        while (elapsed_ms < timeout_ms) {
            if (!kmcp_process_is_running(process)) {
                return 0;
            }

            // Sleep for 10 milliseconds
            usleep(10000);
            elapsed_ms += 10;
        }

        // Timeout
        return 1;
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
        kmcp_process_terminate(process);
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

#endif // !_WIN32
