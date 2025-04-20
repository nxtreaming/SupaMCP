/**
 * @file kmcp_cli_main.c
 * @brief Main entry point for KMCP command-line interface
 */

#include "kmcp_cli.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * @brief Print usage information
 */
static void print_usage(void) {
    printf("Usage: kmcp <command> [options]\n");
    printf("Run 'kmcp help' for a list of available commands.\n");
}

/**
 * @brief Main entry point
 */
int main(int argc, char** argv) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);
    
    // Check for configuration file
    const char* config_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                config_file = argv[i + 1];
                
                // Remove --config and its value from arguments
                for (int j = i; j < argc - 2; j++) {
                    argv[j] = argv[j + 2];
                }
                argc -= 2;
                break;
            }
        }
    }
    
    // Create CLI context
    kmcp_cli_context_t* context = kmcp_cli_create(config_file);
    if (!context) {
        fprintf(stderr, "Failed to initialize KMCP CLI\n");
        mcp_log_close();
        return 1;
    }
    
    // Execute command
    kmcp_error_t result = kmcp_cli_execute(context, argc, argv);
    
    // Clean up
    kmcp_cli_close(context);
    mcp_log_close();
    
    return (result == KMCP_SUCCESS) ? 0 : 1;
}
