#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "mcp_server.h"
#include "mcp_log.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_transport.h"
#include "mcp_profiler.h"

// Global server instance for signal handling
static mcp_server_t* g_server = NULL;

// Configuration structure
typedef struct {
    const char* transport_type;
    const char* host;
    uint16_t port;
    const char* log_file;
    log_level_t log_level;
    bool daemon;
} server_config_t;

// Forward declarations
static void cleanup(void);

// --- Logging functions moved to mcp_log.c ---

// Example resource handler (remains the same)
static int example_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count
) {
    (void)server; // Unused parameter
    (void)user_data; // Unused parameter
    log_message(LOG_LEVEL_INFO, "Resource requested: %s", uri);

    if (strncmp(uri, "example://", 10) != 0) {
        log_message(LOG_LEVEL_WARN, "Invalid resource URI: %s", uri);
        return -1;
    }
    const char* resource_name = uri + 10;

    *content = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (*content == NULL) return -1;

    (*content)->type = MCP_CONTENT_TYPE_TEXT;
    (*content)->mime_type = mcp_strdup("text/plain");
    (*content)->data = NULL; // Initialize data pointer

    if (strcmp(resource_name, "hello") == 0) {
        (*content)->data = mcp_strdup("Hello, world!");
    } else if (strcmp(resource_name, "info") == 0) {
        (*content)->data = mcp_strdup("This is an example MCP server.");
    }

    if ((*content)->data == NULL || (*content)->mime_type == NULL) {
        // Cleanup on allocation failure
        free((*content)->mime_type);
        free((*content)->data);
        free(*content);
        *content = NULL;
        return -1;
    }

    (*content)->data_size = strlen((const char*)(*content)->data);
    *content_count = 1;
    return 0;
}

// Example tool handler (remains the same)
static int example_tool_handler(
    mcp_server_t* server,
    const char* name,
    const char* arguments,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count,
    bool* is_error
) {
    (void)server; (void)user_data;
    log_message(LOG_LEVEL_INFO, "Tool called: %s", name);
    log_message(LOG_LEVEL_DEBUG, "Arguments: %s", arguments);

    *is_error = false;
    *content = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (*content == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for content item");
        return -1;
    }

    (*content)->type = MCP_CONTENT_TYPE_TEXT;
    (*content)->mime_type = mcp_strdup("text/plain");
    (*content)->data = NULL; // Initialize

    if (strcmp(name, "echo") == 0) {
        (*content)->data = mcp_strdup(arguments ? arguments : "");
    } else if (strcmp(name, "reverse") == 0 && arguments != NULL) {
        size_t len = strlen(arguments);
        char* reversed = (char*)malloc(len + 1);
        if (reversed != NULL) {
            for (size_t i = 0; i < len; i++) reversed[i] = arguments[len - i - 1];
            reversed[len] = '\0';
            (*content)->data = reversed;
        }
    } else {
        log_message(LOG_LEVEL_WARN, "Unknown tool or missing arguments: %s", name);
        *is_error = true;
        // Fall through to cleanup if data allocation failed or tool unknown
    }

    if ((*content)->data == NULL || (*content)->mime_type == NULL) {
        free((*content)->mime_type);
        free((*content)->data);
        free(*content);
        *content = NULL;
        *content_count = 0;
        // If it wasn't already an error, make it one due to allocation failure
        if (!*is_error) return -1; // Indicate internal error
        else return 0; // Indicate tool error (is_error is true)
    }

     (*content)->data_size = strlen((const char*)(*content)->data);
    *content_count = 1;
    return 0; // Success (even if is_error is true, the handler succeeded)
}

/**
 * Clean up resources
 */
static void cleanup(void) {
    log_message(LOG_LEVEL_INFO, "Cleaning up resources");
#ifdef MCP_ENABLE_PROFILING
    mcp_profile_report(stdout); // Print profile report on exit if enabled
#endif
    if (g_server != NULL) {
        // mcp_server_destroy calls stop internally
        mcp_server_destroy(g_server);
        g_server = NULL;
    }
    close_logging(); // Close log file
}

/**
 * Signal handler
 */
static void signal_handler(int sig) {
    log_message(LOG_LEVEL_INFO, "Received signal %d, initiating shutdown...", sig);
    // Setting g_server to NULL might be used by the main loop to exit,
    // but atexit(cleanup) is the primary cleanup mechanism.
    // A more robust approach might use a dedicated shutdown flag.
    if (g_server) {
         mcp_server_stop(g_server); // Attempt graceful stop
    }
    // Let atexit handle the rest, or call cleanup() directly if atexit is unreliable.
    // cleanup(); // Optional direct call
    // exit(0); // Exit might be too abrupt, let main loop exit if possible
    g_server = NULL; // Signal main loop to exit
}

#ifndef _WIN32
/**
 * Daemonize the process (Unix-like systems only)
 */
static int daemonize(void) {
    pid_t pid, sid;
    pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) exit(0); // Exit parent
    umask(0);
    sid = setsid();
    if (sid < 0) return -1;
    if (chdir("/") < 0) return -1;
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    return 0;
}
#endif

/**
 * Parse command line arguments
 */
static int parse_arguments(int argc, char** argv, server_config_t* config) {
    // Set default values
    config->transport_type = "stdio";
    config->host = "127.0.0.1";
    config->port = 8080;
    config->log_file = NULL;
    config->log_level = LOG_LEVEL_INFO;
    config->daemon = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            config->transport_type = "tcp";
        } else if (strcmp(argv[i], "--stdio") == 0) {
            config->transport_type = "stdio";
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            config->host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config->port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            config->log_file = argv[++i];
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "error") == 0) config->log_level = LOG_LEVEL_ERROR;
            else if (strcmp(argv[i], "warn") == 0) config->log_level = LOG_LEVEL_WARN;
            else if (strcmp(argv[i], "info") == 0) config->log_level = LOG_LEVEL_INFO;
            else if (strcmp(argv[i], "debug") == 0) config->log_level = LOG_LEVEL_DEBUG;
            else { fprintf(stderr, "Invalid log level: %s\n", argv[i]); return -1; }
        } else if (strcmp(argv[i], "--daemon") == 0) {
#ifndef _WIN32
            config->daemon = true;
#else
            fprintf(stderr, "Daemon mode is not supported on Windows\n"); return -1;
#endif
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --tcp               Use TCP transport (default for daemon mode)\n");
            printf("  --stdio             Use stdio transport (default for interactive mode)\n");
            printf("  --host HOST         Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT         Port to bind to (default: 8080)\n");
            printf("  --log-file FILE     Log to file\n");
            printf("  --log-level LEVEL   Set log level (error, warn, info, debug)\n");
            printf("  --daemon            Run as daemon (Unix-like systems only)\n");
            printf("  --help              Show this help message\n");
            exit(0);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (config->daemon) {
        config->transport_type = "tcp"; // Force TCP for daemon
        if (config->log_file == NULL) {
            fprintf(stderr, "Log file is required in daemon mode\n");
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    server_config_t config;
    if (parse_arguments(argc, argv, &config) != 0) return 1;

    // Use new shared logging functions
    if (init_logging(config.log_file, config.log_level) != 0) {
        // Error already printed to stderr by init_logging
        return 1;
    }
    log_message(LOG_LEVEL_INFO, "Logging system initialized.");

#ifndef _WIN32
    if (config.daemon) {
        log_message(LOG_LEVEL_INFO, "Daemonizing process...");
        if (daemonize() != 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to daemonize");
            close_logging();
            return 1;
        }
         log_message(LOG_LEVEL_INFO, "Daemonization complete."); // This won't be seen on console
    }
#endif

    atexit(cleanup);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
#endif

    log_message(LOG_LEVEL_INFO, "Starting MCP server...");

    mcp_server_config_t server_config = {
        .name = "example-mcp-server",
        .version = "1.0.0",
        .description = "Example MCP server implementation"
    };
    mcp_server_capabilities_t capabilities = {
        .resources_supported = true,
        .tools_supported = true
    };

    g_server = mcp_server_create(&server_config, &capabilities);
    if (g_server == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create server");
        return 1;
    }

    if (mcp_server_set_resource_handler(g_server, example_resource_handler, NULL) != 0 ||
        mcp_server_set_tool_handler(g_server, example_tool_handler, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to set handlers");
        mcp_server_destroy(g_server); // cleanup will call destroy again, but it's safe
        g_server = NULL;
        return 1;
    }

    // Add example resources/tools (simplified error handling)
    mcp_resource_t* r1 = mcp_resource_create("example://hello", "Hello", "text/plain", NULL);
    mcp_resource_t* r2 = mcp_resource_create("example://info", "Info", "text/plain", NULL);
    mcp_resource_template_t* t1 = mcp_resource_template_create("example://{name}", "Example Template", NULL, NULL);
    mcp_tool_t* tool1 = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_t* tool2 = mcp_tool_create("reverse", "Reverse Tool");
    if (r1) mcp_server_add_resource(g_server, r1); mcp_resource_free(r1);
    if (r2) mcp_server_add_resource(g_server, r2); mcp_resource_free(r2);
    if (t1) mcp_server_add_resource_template(g_server, t1); mcp_resource_template_free(t1);
    if (tool1) { mcp_tool_add_param(tool1, "text", "string", "Text to echo", true); mcp_server_add_tool(g_server, tool1); mcp_tool_free(tool1); }
    if (tool2) { mcp_tool_add_param(tool2, "text", "string", "Text to reverse", true); mcp_server_add_tool(g_server, tool2); mcp_tool_free(tool2); }
    log_message(LOG_LEVEL_INFO, "Added example resources and tools.");


    // Create transport based on config
    mcp_transport_t* transport = NULL;
    if (strcmp(config.transport_type, "stdio") == 0) {
        log_message(LOG_LEVEL_INFO, "Using stdio transport");
        transport = mcp_transport_stdio_create();
    } else if (strcmp(config.transport_type, "tcp") == 0) {
        log_message(LOG_LEVEL_INFO, "Using TCP transport on %s:%d", config.host, config.port);
        // Pass idle timeout (e.g., 60000ms = 1 minute, 0 to disable)
        uint32_t idle_timeout = 60000; // TODO: Make this configurable via command line args
        transport = mcp_transport_tcp_create(config.host, config.port, idle_timeout);
    } else {
        log_message(LOG_LEVEL_ERROR, "Unknown transport type: %s", config.transport_type);
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    if (transport == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create transport");
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    // Start the server (transport handle is now owned by server)
    if (mcp_server_start(g_server, transport) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to start server");
        // Don't destroy transport here, server destroy should handle it
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "Server started successfully. Waiting for connections or input...");

    // Main loop (simple wait for signal)
    while (g_server != NULL) {
#ifdef _WIN32
        Sleep(1000); // Sleep 1 second
#else
        sleep(1);    // Sleep 1 second
#endif
    }

    log_message(LOG_LEVEL_INFO, "Main loop exiting.");
    // Cleanup is handled by atexit

    return 0;
}
