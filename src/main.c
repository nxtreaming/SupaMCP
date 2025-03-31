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
#   define PATH_SEPARATOR "\\"
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#   define PATH_SEPARATOR "/"
#endif

#include "mcp_server.h"
// Include specific transport creation headers instead of the generic one
#include "mcp_stdio_transport.h"
// #include "mcp_tcp_transport.h" // Example if TCP was implemented

// Logging levels
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3
} log_level_t;

// Global variables
static mcp_server_t* g_server = NULL;
static FILE* g_log_file = NULL;
static log_level_t g_log_level = LOG_LEVEL_INFO;
static const char* g_log_level_names[] = {"ERROR", "WARN", "INFO", "DEBUG"};

// Configuration
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

/**
 * Log a message to the console and/or log file
 */
static void log_message(log_level_t level, const char* format, ...) {
    if (level > g_log_level) {
        return;
    }

    time_t now;
    struct tm* timeinfo;
    char timestamp[20];

    time(&now);
    timeinfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    va_list args;
    va_start(args, format);

    char message[1024];
    vsnprintf(message, sizeof(message), format, args);

    // Log to console
    fprintf(stderr, "[%s] [%s] %s\n", timestamp, g_log_level_names[level], message);

    // Log to file if available
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%s] [%s] %s\n", timestamp, g_log_level_names[level], message);
        fflush(g_log_file);
    }

    va_end(args);
}

/**
 * Create log directory if it doesn't exist
 */
static int create_log_directory(const char* log_file_path) {
    if (log_file_path == NULL) {
        return 0;
    }

    char* path_copy = strdup(log_file_path);
    if (path_copy == NULL) {
        return -1;
    }

    // Find the last path separator
    char* last_separator = strrchr(path_copy, PATH_SEPARATOR[0]);
    if (last_separator == NULL) {
        free(path_copy);
        return 0; // No directory part
    }

    // Null-terminate at the separator to get just the directory path
    *last_separator = '\0';

    // Create the directory
#ifdef _WIN32
    int result = CreateDirectory(path_copy, NULL);
    if (result == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        log_message(LOG_LEVEL_ERROR, "Failed to create log directory: %s", path_copy);
        free(path_copy);
        return -1;
    }
#else
    int result = mkdir(path_copy, 0755);
    if (result != 0 && errno != EEXIST) {
        log_message(LOG_LEVEL_ERROR, "Failed to create log directory: %s (errno: %d)", path_copy, errno);
        free(path_copy);
        return -1;
    }
#endif

    free(path_copy);
    return 0;
}

/**
 * Initialize logging
 */
static int init_logging(const char* log_file_path, log_level_t level) {
    g_log_level = level;

    if (log_file_path != NULL) {
        // Create log directory if needed
        if (create_log_directory(log_file_path) != 0) {
            return -1;
        }

        g_log_file = fopen(log_file_path, "a");
        if (g_log_file == NULL) {
            fprintf(stderr, "Failed to open log file: %s\n", log_file_path);
            return -1;
        }
    }

    return 0;
}

/**
 * Close logging
 */
static void close_logging(void) {
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

// Example resource handler
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

    // Check if the URI is valid
    if (strncmp(uri, "example://", 10) != 0) {
        log_message(LOG_LEVEL_WARN, "Invalid resource URI: %s", uri);
        return -1;
    }

    // Extract the resource name
    const char* resource_name = uri + 10;

    // Create a content item
    *content = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (*content == NULL) {
        return -1;
    }

    // Initialize the content item
    (*content)->type = MCP_CONTENT_TYPE_TEXT;
    (*content)->mime_type = strdup("text/plain");

    // Set the content based on the resource name
    if (strcmp(resource_name, "hello") == 0) {
        (*content)->data = strdup("Hello, world!");
        (*content)->data_size = strlen((const char*)(*content)->data);
    } else if (strcmp(resource_name, "info") == 0) {
        (*content)->data = strdup("This is an example MCP server.");
        (*content)->data_size = strlen((const char*)(*content)->data);
    } else {
        // Unknown resource
        free((*content)->mime_type);
        free(*content);
        *content = NULL;
        return -1;
    }

    // Set the content count
    *content_count = 1;

    return 0;
}

// Example tool handler
static int example_tool_handler(
    mcp_server_t* server,
    const char* name,
    const char* arguments,
    void* user_data,
    mcp_content_item_t** content,
    size_t* content_count,
    bool* is_error
) {
    (void)server; // Unused parameter
    (void)user_data; // Unused parameter
    log_message(LOG_LEVEL_INFO, "Tool called: %s", name);
    log_message(LOG_LEVEL_DEBUG, "Arguments: %s", arguments);

    // Initialize error flag
    *is_error = false;

    // Create a content item
    *content = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (*content == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for content item");
        return -1;
    }

    // Initialize the content item
    (*content)->type = MCP_CONTENT_TYPE_TEXT;
    (*content)->mime_type = strdup("text/plain");

    // Handle different tools
    if (strcmp(name, "echo") == 0) {
        // Echo tool just returns the arguments
        (*content)->data = strdup(arguments);
        (*content)->data_size = strlen((const char*)(*content)->data);
    } else if (strcmp(name, "reverse") == 0) {
        // Reverse tool reverses the input string
        size_t len = strlen(arguments);
        char* reversed = (char*)malloc(len + 1);
        if (reversed == NULL) {
            free((*content)->mime_type);
            free(*content);
            *content = NULL;
            return -1;
        }

        for (size_t i = 0; i < len; i++) {
            reversed[i] = arguments[len - i - 1];
        }
        reversed[len] = '\0';

        (*content)->data = reversed;
        (*content)->data_size = len;
    } else {
        // Unknown tool
        free((*content)->mime_type);
        free(*content);
        *content = NULL;
        *is_error = true;
        return -1;
    }

    // Set the content count
    *content_count = 1;

    return 0;
}

/**
 * Clean up resources
 */
static void cleanup(void) {
    log_message(LOG_LEVEL_INFO, "Cleaning up resources");

    if (g_server != NULL) {
        mcp_server_stop(g_server);
        mcp_server_destroy(g_server);
        g_server = NULL;
    }

    close_logging();
}

/**
 * Signal handler
 */
static void signal_handler(int sig) {
    log_message(LOG_LEVEL_INFO, "Received signal %d, shutting down...", sig);
    cleanup();
    exit(0);
}

#ifndef _WIN32
/**
 * Daemonize the process (Unix-like systems only)
 */
static int daemonize(void) {
    pid_t pid, sid;

    // Fork off the parent process
    pid = fork();
    if (pid < 0) {
        return -1;
    }

    // If we got a good PID, then we can exit the parent process
    if (pid > 0) {
        exit(0);
    }

    // Change the file mode mask
    umask(0);

    // Create a new SID for the child process
    sid = setsid();
    if (sid < 0) {
        return -1;
    }

    // Change the current working directory
    if (chdir("/") < 0) {
        return -1;
    }

    // Close standard file descriptors
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
            if (strcmp(argv[i], "error") == 0) {
                config->log_level = LOG_LEVEL_ERROR;
            } else if (strcmp(argv[i], "warn") == 0) {
                config->log_level = LOG_LEVEL_WARN;
            } else if (strcmp(argv[i], "info") == 0) {
                config->log_level = LOG_LEVEL_INFO;
            } else if (strcmp(argv[i], "debug") == 0) {
                config->log_level = LOG_LEVEL_DEBUG;
            } else {
                fprintf(stderr, "Invalid log level: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--daemon") == 0) {
#ifndef _WIN32
            config->daemon = true;
#else
            fprintf(stderr, "Daemon mode is not supported on Windows\n");
            return -1;
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

    // If running as daemon, force TCP transport and require log file
    if (config->daemon) {
        config->transport_type = "tcp";
        if (config->log_file == NULL) {
            fprintf(stderr, "Log file is required in daemon mode\n");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    // Parse command line arguments
    server_config_t config;
    if (parse_arguments(argc, argv, &config) != 0) {
        return 1;
    }

    // Initialize logging
    if (init_logging(config.log_file, config.log_level) != 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "Logging initialized");

    // Run as daemon if requested
#ifndef _WIN32
    if (config.daemon) {
        log_message(LOG_LEVEL_INFO, "Starting in daemon mode");
        if (daemonize() != 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to daemonize");
            close_logging();
            return 1;
        }
    }
#endif

    // Register cleanup function to be called at exit
    atexit(cleanup);

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
#endif

    log_message(LOG_LEVEL_INFO, "Starting MCP server");

    // Create server configuration
    mcp_server_config_t server_config;
    server_config.name = "example-mcp-server";
    server_config.version = "1.0.0";
    server_config.description = "Example MCP server implementation";

    // Create server capabilities
    mcp_server_capabilities_t capabilities;
    capabilities.resources_supported = true;
    capabilities.tools_supported = true;

    // Create the server
    mcp_server_t* server = mcp_server_create(&server_config, &capabilities);
    if (server == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create server");
        return 1;
    }

    // Set the global server instance for signal handling
    g_server = server;

    // Set resource handler
    if (mcp_server_set_resource_handler(server, example_resource_handler, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to set resource handler");
        mcp_server_destroy(server);
        return 1;
    }

    // Set tool handler
    if (mcp_server_set_tool_handler(server, example_tool_handler, NULL) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to set tool handler");
        mcp_server_destroy(server);
        return 1;
    }

    // Add example resources
    log_message(LOG_LEVEL_INFO, "Adding example resources");
    mcp_resource_t* hello_resource = mcp_resource_create(
        "example://hello",
        "Hello Resource",
        "text/plain",
        "A simple hello world resource"
    );
    if (hello_resource == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create hello resource");
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_server_add_resource(server, hello_resource) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add hello resource");
        mcp_resource_free(hello_resource);
        mcp_server_destroy(server);
        return 1;
    }

    mcp_resource_free(hello_resource);

    mcp_resource_t* info_resource = mcp_resource_create(
        "example://info",
        "Info Resource",
        "text/plain",
        "Information about the server"
    );
    if (info_resource == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create info resource");
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_server_add_resource(server, info_resource) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add info resource");
        mcp_resource_free(info_resource);
        mcp_server_destroy(server);
        return 1;
    }

    mcp_resource_free(info_resource);

    // Add example resource template
    log_message(LOG_LEVEL_INFO, "Adding example resource template");
    mcp_resource_template_t* example_template = mcp_resource_template_create(
        "example://{name}",
        "Example Resource Template",
        "text/plain",
        "A template for example resources"
    );
    if (example_template == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create example template");
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_server_add_resource_template(server, example_template) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add example template");
        mcp_resource_template_free(example_template);
        mcp_server_destroy(server);
        return 1;
    }

    mcp_resource_template_free(example_template);

    // Add example tools
    log_message(LOG_LEVEL_INFO, "Adding example tools");
    mcp_tool_t* echo_tool = mcp_tool_create(
        "echo",
        "Echo Tool"
    );
    if (echo_tool == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create echo tool");
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_tool_add_param(
        echo_tool,
        "text",
        "string",
        "Text to echo",
        true
    ) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add parameter to echo tool");
        mcp_tool_free(echo_tool);
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_server_add_tool(server, echo_tool) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add echo tool");
        mcp_tool_free(echo_tool);
        mcp_server_destroy(server);
        return 1;
    }

    mcp_tool_free(echo_tool);

    mcp_tool_t* reverse_tool = mcp_tool_create(
        "reverse",
        "Reverse Tool"
    );
    if (reverse_tool == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create reverse tool");
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_tool_add_param(
        reverse_tool,
        "text",
        "string",
        "Text to reverse",
        true
    ) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add parameter to reverse tool");
        mcp_tool_free(reverse_tool);
        mcp_server_destroy(server);
        return 1;
    }

    if (mcp_server_add_tool(server, reverse_tool) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to add reverse tool");
        mcp_tool_free(reverse_tool);
        mcp_server_destroy(server);
        return 1;
    }

    mcp_tool_free(reverse_tool);

    // Create transport
    mcp_transport_t* transport = NULL;

    if (strcmp(config.transport_type, "stdio") == 0) {
        log_message(LOG_LEVEL_INFO, "Using stdio transport");
        transport = mcp_transport_stdio_create(); // Use specific create function
    } else if (strcmp(config.transport_type, "tcp") == 0) {
        log_message(LOG_LEVEL_INFO, "Using TCP transport on %s:%d", config.host, config.port);
        // transport = mcp_transport_tcp_create(config.host, config.port); // Example if TCP was implemented
        log_message(LOG_LEVEL_WARN, "TCP transport not implemented yet, using stdio instead.");
        transport = mcp_transport_stdio_create(); // Fallback to stdio for now
    } else {
        log_message(LOG_LEVEL_ERROR, "Unknown transport type: %s", config.transport_type);
        mcp_server_destroy(server);
        return 1;
    }

    if (transport == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to create transport");
        mcp_server_destroy(server);
        return 1;
    }

    // Start the server using the generic transport handle
    if (mcp_server_start(server, transport) != 0) {
        log_message(LOG_LEVEL_ERROR, "Failed to start server");
        mcp_transport_destroy(transport); // Use generic destroy
        mcp_server_destroy(server);
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "Server started successfully");

    // Wait for the server to stop (e.g., via signal handler)
    while (g_server != NULL) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    // Cleanup is handled by atexit

    return 0;
}
