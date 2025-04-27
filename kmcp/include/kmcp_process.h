/**
 * @file kmcp_process.h
 * @brief Process management for launching and managing local processes
 */

#ifndef KMCP_PROCESS_H
#define KMCP_PROCESS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process management structure
 */
typedef struct kmcp_process kmcp_process_t;

/**
 * @brief Create a process
 *
 * @param command Command
 * @param args Arguments array
 * @param args_count Number of arguments
 * @param env Environment variables array
 * @param env_count Number of environment variables
 * @return kmcp_process_t* Returns process pointer on success, NULL on failure
 */
kmcp_process_t* kmcp_process_create(
    const char* command,
    char** args,
    size_t args_count,
    char** env,
    size_t env_count
);

/**
 * @brief Start a process
 *
 * @param process Process
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_process_start(kmcp_process_t* process);

/**
 * @brief Check if a process is running
 *
 * @param process Process
 * @return bool Returns true if running, false otherwise
 */
bool kmcp_process_is_running(kmcp_process_t* process);

/**
 * @brief Terminate a process
 *
 * @param process Process
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_process_terminate(kmcp_process_t* process);

/**
 * @brief Wait for a process to end
 *
 * @param process Process
 * @param timeout_ms Timeout in milliseconds, 0 means wait indefinitely
 * @return int Returns 0 on success, 1 on timeout, other non-zero error code on failure
 */
int kmcp_process_wait(kmcp_process_t* process, int timeout_ms);

/**
 * @brief Get process exit code
 *
 * @param process Process
 * @param exit_code Pointer to exit code
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_process_get_exit_code(kmcp_process_t* process, int* exit_code);

/**
 * @brief Close process handle
 *
 * @param process Process
 */
void kmcp_process_close(kmcp_process_t* process);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_PROCESS_H */
