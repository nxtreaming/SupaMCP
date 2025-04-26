#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

// Include Windows socket compatibility header first
#include <win_socket_compat.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
// Windows.h is already included by win_socket_compat.h
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#include <mcp_server.h>
#include <mcp_transport.h>
#include <mcp_http_transport.h>
#include <mcp_log.h>
#include <mcp_json.h>
#include <mcp_string_utils.h>
#include "http_static_res.h"

// Global instances for signal handling and SSE events
static mcp_server_t* g_server = NULL;
static mcp_transport_t* g_transport = NULL;
static const char* g_doc_root = NULL; // To store the doc_root path

// Example tool handler
static mcp_error_code_t example_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message)
{
    (void)server; (void)user_data;

    mcp_log_info("Tool called: %s", name);

    // Initialize output params
    *content = NULL;
    *content_count = 0;
    *is_error = false;
    *error_message = NULL;
    mcp_content_item_t* item = NULL;
    char* result_data = NULL;
    const char* input_text = NULL;
    mcp_error_code_t err_code = MCP_ERROR_NONE;

    // Extract "text" parameter using mcp_json
    if (params == NULL || mcp_json_get_type(params) != MCP_JSON_OBJECT) {
        mcp_log_warn("Tool '%s': Invalid or missing params object.", name);
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid parameters object.");
        err_code = MCP_ERROR_INVALID_PARAMS;
        goto cleanup;
    }

    // Debug: Log the params object
    char* params_str = mcp_json_stringify(params);
    mcp_log_info("Tool '%s': Params: %s", name, params_str ? params_str : "NULL");
    if (params_str) free(params_str);

    // Get text directly from params
    mcp_json_t* text_node = mcp_json_object_get_property(params, "text");

    // If text not found directly, check for arguments object
    if (text_node == NULL) {
        mcp_json_t* args = mcp_json_object_get_property(params, "arguments");
        if (args != NULL && mcp_json_get_type(args) == MCP_JSON_OBJECT) {
            text_node = mcp_json_object_get_property(args, "text");
        }
    }

    if (text_node == NULL || mcp_json_get_type(text_node) != MCP_JSON_STRING ||
        mcp_json_get_string(text_node, &input_text) != 0 || input_text == NULL) {
        mcp_log_warn("Tool '%s': Missing or invalid 'text' string parameter.", name);
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid 'text' string parameter.");
        err_code = MCP_ERROR_INVALID_PARAMS;
        goto cleanup;
    }

    // Execute tool logic
    if (strcmp(name, "echo") == 0) {
        if (input_text) {
            result_data = mcp_strdup(input_text);
            mcp_log_info("Echo tool called with text: %s", input_text);

            // Send an SSE event with the echoed text
            if (g_transport) {
                char event_data[256];
                snprintf(event_data, sizeof(event_data), "{\"text\":\"%s\"}", input_text);
                mcp_log_info("Sending SSE event: echo - %s", event_data);
                int ret = mcp_http_transport_send_sse(g_transport, "echo", event_data);
                if (ret != 0) {
                    mcp_log_error("Failed to send SSE event: %d", ret);
                } else {
                    mcp_log_info("SSE event sent successfully");
                }
            } else {
                mcp_log_warn("Transport not available for SSE");
            }
        } else {
            mcp_log_warn("Echo tool called with NULL text");
            *is_error = true;
            *error_message = mcp_strdup("Missing or invalid 'text' parameter.");
            err_code = MCP_ERROR_INVALID_PARAMS;
            goto cleanup;
        }
    } else if (strcmp(name, "reverse") == 0) {
        if (input_text) {
            size_t len = strlen(input_text);
            result_data = (char*)malloc(len + 1);
            if (result_data != NULL) {
                for (size_t i = 0; i < len; i++) result_data[i] = input_text[len - i - 1];
                result_data[len] = '\0';
                mcp_log_info("Reverse tool called with text: %s, result: %s", input_text, result_data);

                // Send an SSE event with the reversed text
                if (g_transport) {
                    char event_data[256];
                    snprintf(event_data, sizeof(event_data), "{\"text\":\"%s\"}", result_data);
                    mcp_log_info("Sending SSE event: reverse - %s", event_data);
                    int ret = mcp_http_transport_send_sse(g_transport, "reverse", event_data);
                    if (ret != 0) {
                        mcp_log_error("Failed to send SSE event: %d", ret);
                    } else {
                        mcp_log_info("SSE event sent successfully");
                    }
                } else {
                    mcp_log_warn("Transport not available for SSE");
                }
            } else {
                mcp_log_error("Failed to allocate memory for reversed text");
                *is_error = true;
                *error_message = mcp_strdup("Memory allocation failed.");
                err_code = MCP_ERROR_INTERNAL_ERROR;
                goto cleanup;
            }
        } else {
            mcp_log_warn("Reverse tool called with NULL text");
            *is_error = true;
            *error_message = mcp_strdup("Missing or invalid 'text' parameter.");
            err_code = MCP_ERROR_INVALID_PARAMS;
            goto cleanup;
        }
    } else {
        mcp_log_warn("Unknown tool name: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Tool not found.");
        err_code = MCP_ERROR_TOOL_NOT_FOUND;
        goto cleanup;
    }

    if (!result_data) {
        mcp_log_error("Failed to allocate result data for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    // Create the response content
    *content = (mcp_content_item_t**)malloc(sizeof(mcp_content_item_t*));
    if (!*content) {
        mcp_log_error("Failed to allocate content array for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }
    (*content)[0] = NULL; // Initialize

    item = (mcp_content_item_t*)malloc(sizeof(mcp_content_item_t));
    if (!item) {
        mcp_log_error("Failed to allocate content item struct for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");
    item->data = result_data; // Transfer ownership
    item->data_size = strlen(result_data) + 1; // Include null terminator
    result_data = NULL; // Avoid double free

    if (!item->mime_type) {
        mcp_log_error("Failed to allocate mime type for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    (*content)[0] = item;
    *content_count = 1;
    err_code = MCP_ERROR_NONE; // Success

cleanup:
    // Free intermediate allocations on error
    if (result_data) {
        free(result_data);
        result_data = NULL;
    }

    if (err_code != MCP_ERROR_NONE) {
        if (item) {
            mcp_content_item_free(item);
            item = NULL;
        }
        if (*content) {
            free(*content);
            *content = NULL;
        }
        *content_count = 0;
        if (*error_message == NULL) {
            *error_message = mcp_strdup("An unexpected error occurred processing the tool.");
        }
    }
    return err_code;
}

// Flag to indicate shutdown is in progress
static volatile bool shutdown_requested = false;



// Function to trim whitespace from a string
static char* trim(char* str) {
    if (!str) return NULL;

    // Trim leading whitespace
    char* start = str;
    while (isspace((unsigned char)*start)) start++;

    if (*start == 0) return start; // All spaces

    // Trim trailing whitespace
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end + 1) = '\0';

    return start;
}

// Function to parse a configuration file
static int parse_config_file(const char* filename, mcp_http_config_t* config, int* log_level) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Warning: Could not open config file %s\n", filename);
        return -1;
    }

    printf("Info: Successfully opened config file %s\n", filename);

    char line[512];
    char* key;
    char* value;

    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Find the equals sign
        char* equals = strchr(line, '=');
        if (!equals) continue;

        // Split the line into key and value
        *equals = '\0';
        key = trim(line);
        value = trim(equals + 1);

        // Remove trailing newline from value if present
        char* newline = strchr(value, '\n');
        if (newline) *newline = '\0';

        // Parse the key-value pair
        if (strcmp(key, "host") == 0) {
            config->host = mcp_strdup(value);
        } else if (strcmp(key, "port") == 0) {
            config->port = (uint16_t)atoi(value);
        } else if (strcmp(key, "doc_root") == 0) {
            // Handle doc_root path
            if (value[0] != '/' && !(value[0] && value[1] == ':' && (value[2] == '/' || value[2] == '\\'))) {
                // This is a relative path, make it absolute relative to the config file
                char abs_path[512];
                char config_dir[512] = ".";

                // Get the directory of the config file
                char* last_slash = strrchr(filename, '/');
                char* last_backslash = strrchr(filename, '\\');
                char* last_separator = last_slash > last_backslash ? last_slash : last_backslash;

                if (last_separator) {
                    size_t dir_len = last_separator - filename;
                    strncpy(config_dir, filename, dir_len);
                    config_dir[dir_len] = '\0';
                }

                snprintf(abs_path, sizeof(abs_path), "%s/%s", config_dir, value);
                config->doc_root = mcp_strdup(abs_path);
                printf("Converted config doc_root to absolute path: %s\n", abs_path);
            } else {
                // This is already an absolute path
                config->doc_root = mcp_strdup(value);
            }
        } else if (strcmp(key, "use_ssl") == 0) {
            config->use_ssl = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "cert_path") == 0) {
            config->cert_path = mcp_strdup(value);
        } else if (strcmp(key, "key_path") == 0) {
            config->key_path = mcp_strdup(value);
        } else if (strcmp(key, "timeout_ms") == 0) {
            config->timeout_ms = atoi(value);
        } else if (strcmp(key, "log_level") == 0) {
            *log_level = atoi(value);
        }
    }

    fclose(file);
    return 0;
}

// Signal handler
static void signal_handler(int sig) {
    printf("Received signal %d, shutting down...\n", sig);

    // Set the shutdown flag to break out of the main loop
    shutdown_requested = true;

    // On Windows, we need to use a more forceful approach for some signals
#ifdef _WIN32
    if (sig == SIGINT || sig == SIGTERM) {
        // Force exit after a short delay if normal shutdown fails
        static HANDLE timer = NULL;
        if (!timer) {
            timer = CreateWaitableTimer(NULL, TRUE, NULL);
            if (timer) {
                LARGE_INTEGER li;
                li.QuadPart = -10000000LL; // 1 second in 100-nanosecond intervals
                SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE);

                // Create a thread to force exit after the timer expires
                HANDLE thread = CreateThread(NULL, 0,
                    (LPTHREAD_START_ROUTINE)ExitProcess,
                    (LPVOID)1, 0, NULL);
                if (thread) CloseHandle(thread);
            }
        }
    }
#endif
}


int main(int argc, char** argv) {
    // Default configuration
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* config_file = "http_server.conf";
    const char* doc_root = ".";
    int log_level = MCP_LOG_LEVEL_INFO;
    bool config_file_specified = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
            config_file_specified = true;
        } else if (strcmp(argv[i], "--doc-root") == 0 && i + 1 < argc) {
            doc_root = argv[++i];
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            log_level = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST         Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT         Port to bind to (default: 8080)\n");
            printf("  --config FILE       Configuration file to use (default: http_server.conf)\n");
            printf("  --doc-root PATH     Document root for static files (default: .)\n");
            printf("  --log-level LEVEL   Log level (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)\n");
            printf("  --help              Show this help message\n");
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    // Get current working directory for path resolution
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Current working directory: %s\n", cwd);
    } else {
        printf("Failed to get current working directory\n");
        strcpy(cwd, "."); // Fallback to current directory
    }

    // Create HTTP transport configuration with default values
    mcp_http_config_t http_config = {
        .host = mcp_strdup(host),
        .port = port,
        .use_ssl = false,
        .cert_path = NULL,
        .key_path = NULL,
        .doc_root = NULL, // Will be set after path resolution
        .timeout_ms = 0 // No timeout
    };

    // Convert relative doc_root to absolute path if needed
    if (doc_root && doc_root[0] != '/' &&
        !(doc_root[0] && doc_root[1] == ':' && (doc_root[2] == '/' || doc_root[2] == '\\'))) {
        // This is a relative path, make it absolute
        char abs_path[512];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, doc_root);
        http_config.doc_root = mcp_strdup(abs_path);
        printf("Converted relative doc_root to absolute path: %s\n", abs_path);
    } else {
        // This is already an absolute path
        http_config.doc_root = mcp_strdup(doc_root);
    }

    // Try to load configuration from file
    if (config_file) {
        printf("Trying to load configuration from file: %s\n", config_file);

        // First try in the current directory
        printf("Trying config file in current directory: %s\n", config_file);
        if (parse_config_file(config_file, &http_config, &log_level) != 0) {
            // If not found and not explicitly specified, try in web/html directory
            if (!config_file_specified) {
                char web_config_path[512];
                snprintf(web_config_path, sizeof(web_config_path), "web/html/%s", config_file);
                printf("Trying config file in web/html directory: %s\n", web_config_path);
                if (parse_config_file(web_config_path, &http_config, &log_level) != 0) {
                    // If still not found, try in the executable directory
                    char exe_path[512];
                    if (getcwd(exe_path, sizeof(exe_path)) != NULL) {
                        // Try in current directory
                        char exe_config_path[512];
                        snprintf(exe_config_path, sizeof(exe_config_path), "%s/%s", exe_path, config_file);
                        printf("Trying config file in exe directory: %s\n", exe_config_path);
                        if (parse_config_file(exe_config_path, &http_config, &log_level) != 0) {
                            // Try in Debug subdirectory (for Visual Studio)
                            snprintf(exe_config_path, sizeof(exe_config_path), "%s/Debug/%s", exe_path, config_file);
                            printf("Trying config file in Debug subdirectory: %s\n", exe_config_path);
                            if (parse_config_file(exe_config_path, &http_config, &log_level) != 0) {
                                // Try in Release subdirectory
                                snprintf(exe_config_path, sizeof(exe_config_path), "%s/Release/%s", exe_path, config_file);
                                printf("Trying config file in Release subdirectory: %s\n", exe_config_path);
                                parse_config_file(exe_config_path, &http_config, &log_level);
                            }
                        }
                    }
                }
            }
        }
    }

    // Initialize logging
    mcp_log_init(NULL, log_level);

    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create server configuration
    mcp_server_config_t server_config = {
        .name = "http-example-server",
        .version = "1.0.0",
        .description = "HTTP MCP Server Example with SSE",
        .thread_pool_size = 4,
        .task_queue_size = 32,
        .max_message_size = 1024 * 1024, // 1MB
        .api_key = NULL // No API key required
    };

    // Set server capabilities
    mcp_server_capabilities_t capabilities = {
        .resources_supported = false, // No resources in this example
        .tools_supported = true
    };

    // Create server
    g_server = mcp_server_create(&server_config, &capabilities);
    if (!g_server) {
        mcp_log_error("Failed to create server");
        return 1;
    }

    // Register tool handler
    if (mcp_server_set_tool_handler(g_server, example_tool_handler, NULL) != 0) {
        mcp_log_error("Failed to set tool handler");
        mcp_server_destroy(g_server);
        return 1;
    }

    // Add example tools
    mcp_tool_t* echo_tool = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_t* reverse_tool = mcp_tool_create("reverse", "Reverse Tool");

    if (echo_tool) {
        mcp_tool_add_param(echo_tool, "text", "string", "Text to echo", true);
        mcp_server_add_tool(g_server, echo_tool);
        mcp_tool_free(echo_tool);
    }

    if (reverse_tool) {
        mcp_tool_add_param(reverse_tool, "text", "string", "Text to reverse", true);
        mcp_server_add_tool(g_server, reverse_tool);
        mcp_tool_free(reverse_tool);
    }

    // Log the configuration
    mcp_log_info("HTTP Server Configuration:");
    mcp_log_info("  Host: %s", http_config.host);
    mcp_log_info("  Port: %d", http_config.port);
    mcp_log_info("  Document Root: %s", http_config.doc_root ? http_config.doc_root : "(null)");
    mcp_log_info("  Use SSL: %s", http_config.use_ssl ? "true" : "false");
    mcp_log_info("  Log Level: %d", log_level);

    // Check if static files exist in the document root
    if (http_config.doc_root) {
        char path[512];

        // Check index.html
        snprintf(path, sizeof(path), "%s/index.html", http_config.doc_root);
        mcp_log_info("Checking if index.html exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check styles.css
        snprintf(path, sizeof(path), "%s/styles.css", http_config.doc_root);
        mcp_log_info("Checking if styles.css exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check sse_test.html
        snprintf(path, sizeof(path), "%s/sse_test.html", http_config.doc_root);
        mcp_log_info("Checking if sse_test.html exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check sse_test.css
        snprintf(path, sizeof(path), "%s/sse_test.css", http_config.doc_root);
        mcp_log_info("Checking if sse_test.css exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check sse_test.js
        snprintf(path, sizeof(path), "%s/sse_test.js", http_config.doc_root);
        mcp_log_info("Checking if sse_test.js exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");
    }

    // Check if we need to create static files (only if doc_root is ".")
    if (http_config.doc_root && strcmp(http_config.doc_root, ".") == 0) {
        mcp_log_info("Document root is current directory, creating static files...");
        http_create_index_html("index.html", http_config.host, http_config.port);
        http_create_styles_css("styles.css");
        http_create_sse_test_css("sse_test.css");
        http_create_sse_test_js("sse_test.js");
        http_create_sse_test_html("sse_test.html");
    }

    // Store the doc_root
    g_doc_root = mcp_strdup(http_config.doc_root);

    // Create HTTP transport
    g_transport = mcp_transport_http_create(&http_config);
    if (!g_transport) {
        mcp_log_error("Failed to create HTTP transport");
        mcp_server_destroy(g_server);
        return 1;
    }

    // Start server
    printf("Starting HTTP server on %s:%d\n", http_config.host, http_config.port);
    printf("- Tool calls: http://%s:%d/call_tool\n", http_config.host, http_config.port);
    printf("- SSE events: http://%s:%d/events\n", http_config.host, http_config.port);

    if (mcp_server_start(g_server, g_transport) != 0) {
        mcp_log_error("Failed to start server");
        mcp_server_destroy(g_server);
        mcp_transport_destroy(g_transport);
        g_transport = NULL;
        g_server = NULL;
        return 1;
    }

    // Main loop
    printf("Server running. Press Ctrl+C to stop.\n");
    while (!shutdown_requested && g_server) {
#ifdef _WIN32
        Sleep(100); // Sleep 100ms for more responsive shutdown
#else
        usleep(100000); // Sleep 100ms
#endif
    }

    // Perform clean shutdown
    printf("Performing clean shutdown...\n");

    // Stop the server first
    if (g_server) {
        mcp_server_stop(g_server);
    }

    // Then destroy the transport
    if (g_transport) {
        mcp_transport_destroy(g_transport);
        g_transport = NULL;
    }

    // Free the doc_root memory if it was allocated
    if (g_doc_root) {
        free((void*)g_doc_root);
        g_doc_root = NULL;
    }

    // Finally destroy the server
    if (g_server) {
        mcp_server_destroy(g_server);
        g_server = NULL;
    }

    printf("Server shutdown complete\n");
    mcp_log_close();
    return 0;
}
