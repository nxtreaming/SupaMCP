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
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "mcp_types.h"
#include "mcp_server.h"
#include "mcp_log.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_transport.h"
#include "mcp_http_transport.h"
#include "mcp_profiler.h"
#include "mcp_json.h"
#include "mcp_socket_utils.h"
#include "mcp_sys_utils.h"
#include "mcp_gateway.h"
#include "mcp_thread_local.h"
#include "mcp_connection_pool.h"
#include "server/internal/server_internal.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"
#include "mcp_arena.h"
#include "mcp_cache_aligned.h"

// HTTP client tool
extern int register_http_client_tool(mcp_server_t* server);
extern mcp_error_code_t http_client_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message);

// Global server instance for signal handling
static mcp_server_t* g_server = NULL;
static mcp_backend_info_t* g_backends = NULL;
static size_t g_backend_count = 0;

// Configuration structure
typedef struct {
    const char* transport_type;
    const char* host;
    uint16_t port;
    const char* log_file;
    mcp_log_level_t log_level;
    bool daemon;
    const char* api_key;
    bool gateway_mode;
    const char* doc_root;  // Document root for static file serving
} server_config_t;

/**
 * @internal
 * @brief Parses a "tcp://host:port" string.
 * @param address The input address string.
 * @param host_buf Buffer to store the extracted host.
 * @param host_buf_size Size of the host buffer.
 * @param port Pointer to store the extracted port number.
 * @return true on success, false on parsing failure.
 */
static bool parse_tcp_address(const char* address, char* host_buf, size_t host_buf_size, int* port) {
    if (!address || !host_buf || host_buf_size == 0 || !port) {
        return false;
    }

    if (strncmp(address, "tcp://", 6) != 0) {
        return false;
    }

    const char* host_start = address + 6;
    const char* port_sep = strrchr(host_start, ':');

    if (!port_sep || port_sep == host_start) {
        return false;
    }

    size_t host_len = port_sep - host_start;
    if (host_len >= host_buf_size) {
        return false;
    }

    memcpy(host_buf, host_start, host_len);
    host_buf[host_len] = '\0';

    const char* port_start = port_sep + 1;
    char* end_ptr = NULL;
    long parsed_port = strtol(port_start, &end_ptr, 10);

    // Check if parsing consumed the whole port string and if port is valid
    if (*end_ptr != '\0' || parsed_port <= 0 || parsed_port > 65535) {
        return false;
    }

    *port = (int)parsed_port;
    return true;
}

static mcp_error_code_t server_resource_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message)
{
    (void)server; (void)user_data;

    mcp_log_info("Resource requested: %s", uri);

    // Initialize output params
    *content = NULL;
    *content_count = 0;
    *error_message = NULL;
    mcp_content_item_t* item = NULL;
    char* data_copy = NULL;
    const char* resource_name = NULL;
    mcp_error_code_t err_code = MCP_ERROR_NONE;

    if (strncmp(uri, "example://", 10) != 0) {
        mcp_log_warn("Invalid resource URI prefix: %s", uri);
        *error_message = mcp_strdup("Resource not found (invalid prefix).");
        err_code = MCP_ERROR_RESOURCE_NOT_FOUND;
        goto cleanup;
    }
    resource_name = uri + 10;

    // Determine content based on resource name
    if (strcmp(resource_name, "hello") == 0) {
        data_copy = mcp_strdup("Hello, world!");
    } else if (strcmp(resource_name, "info") == 0) {
        data_copy = mcp_strdup("This is an example MCP server.");
    } else {
        // Check if this is a templated resource
        // For example, if the URI is example://john, it would match the template example://{name}
        // and we'd generate a personalized greeting
        char greeting[256];
        snprintf(greeting, sizeof(greeting), "Hello, %s!", resource_name);
        data_copy = mcp_strdup(greeting);
        if (!data_copy) {
            mcp_log_warn("Unknown resource name: %s", resource_name);
            *error_message = mcp_strdup("Resource not found.");
            err_code = MCP_ERROR_RESOURCE_NOT_FOUND;
            goto cleanup;
        }
    }

    if (!data_copy) {
        mcp_log_error("Failed to allocate data for resource: %s", resource_name);
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    // Allocate the array of pointers (size 1) using thread cache
    *content = (mcp_content_item_t**)mcp_thread_cache_alloc(sizeof(mcp_content_item_t*));
    if (!*content) {
        mcp_log_error("Failed to allocate content array for resource: %s", resource_name);
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }
    (*content)[0] = NULL;

    // Allocate the content item struct using thread cache
    item = (mcp_content_item_t*)mcp_thread_cache_alloc(sizeof(mcp_content_item_t));
    if (!item) {
        mcp_log_error("Failed to allocate content item struct for resource: %s", resource_name);
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }
    // Populate the item
    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");
    item->data = data_copy;
    item->data_size = strlen(data_copy) + 1;
    data_copy = NULL;

    if (!item->mime_type) {
        mcp_log_error("Failed to allocate mime type for resource: %s", resource_name);
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    // Assign item to array
    (*content)[0] = item;
    *content_count = 1;
    err_code = MCP_ERROR_NONE;

cleanup:
    // Free intermediate allocations on error
    if (data_copy) {
        free(data_copy);
    }

    if (err_code != MCP_ERROR_NONE) {
        if (item) {
            if (item->mime_type) {
                free((void*)item->mime_type);
            }
            mcp_thread_cache_free(item, sizeof(mcp_content_item_t));
        }
        if (*content) {
            mcp_thread_cache_free(*content, sizeof(mcp_content_item_t*));
            *content = NULL;
        }
        *content_count = 0;
        if (*error_message == NULL) {
            *error_message = mcp_strdup("An unexpected error occurred processing the resource.");
        }
    }
    return err_code;
}

static mcp_error_code_t server_tool_handler(
    mcp_server_t* server,
    const char* name,
    const mcp_json_t* params,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    bool* is_error,
    char** error_message)
{
    (void)user_data;

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

    // Extract parameters using mcp_json
    if (params == NULL || mcp_json_get_type(params) != MCP_JSON_OBJECT) {
        mcp_log_warn("Tool '%s': Invalid or missing params object.", name);
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid parameters object.");
        err_code = MCP_ERROR_INVALID_PARAMS;
        goto cleanup;
    }

    // Variables to store parameters
    const char* session_id = NULL;

    // Dump the entire params object for debugging
    char* params_json = mcp_json_stringify(params);
    mcp_log_debug("Tool '%s': Raw params: %s", name, params_json ? params_json : "NULL");
    if (params_json)
        free(params_json);

    // First try to get text directly from params
    mcp_json_t* text_node = mcp_json_object_get_property(params, "text");
    // If text not found directly, check for arguments object (for compatibility with different call formats)
    mcp_json_t* args = NULL;
    if (text_node == NULL) {
        mcp_log_debug("Tool '%s': No 'text' property found directly in params, checking 'arguments'", name);
        args = mcp_json_object_get_property(params, "arguments");
        if (args != NULL && mcp_json_get_type(args) == MCP_JSON_OBJECT) {
            mcp_log_debug("Tool '%s': Found 'arguments' object", name);
            text_node = mcp_json_object_get_property(args, "text");

            // Try to get session_id from arguments
            mcp_json_t* session_id_node = mcp_json_object_get_property(args, "session_id");
            if (session_id_node != NULL) {
                mcp_log_debug("Tool '%s': Found 'session_id' property in arguments, type: %d",
                             name, mcp_json_get_type(session_id_node));
                if (mcp_json_get_type(session_id_node) == MCP_JSON_STRING) {
                    mcp_json_get_string(session_id_node, &session_id);
                    mcp_log_info("Tool '%s': Found session_id in arguments: %s",
                                name, session_id ? session_id : "NULL");
                } else {
                    mcp_log_warn("Tool '%s': 'session_id' property is not a string", name);
                }
            } else {
                mcp_log_debug("Tool '%s': No 'session_id' property found in arguments", name);
            }
        } else {
            mcp_log_debug("Tool '%s': No valid 'arguments' object found", name);
        }
    } else {
        mcp_log_debug("Tool '%s': Found 'text' property directly in params", name);

        // Try to get session_id directly from params
        mcp_json_t* session_id_node = mcp_json_object_get_property(params, "session_id");
        if (session_id_node != NULL) {
            mcp_log_debug("Tool '%s': Found 'session_id' property directly in params, type: %d",
                         name, mcp_json_get_type(session_id_node));
            if (mcp_json_get_type(session_id_node) == MCP_JSON_STRING) {
                mcp_json_get_string(session_id_node, &session_id);
                mcp_log_info("Tool '%s': Found session_id directly in params: %s",
                            name, session_id ? session_id : "NULL");
            } else {
                mcp_log_warn("Tool '%s': 'session_id' property is not a string", name);
            }
        } else {
            mcp_log_debug("Tool '%s': No 'session_id' property found directly in params", name);
        }
    }

    // Execute tool logic
    if (strcmp(name, "http_client") == 0) {
        // Special case for HTTP client tool - delegate to its handler
        mcp_log_info("Delegating to HTTP client tool handler");
        return http_client_tool_handler(server, name, params, user_data, content, content_count, is_error, error_message);
    }

    // For other tools, require the 'text' parameter
    if (text_node == NULL || mcp_json_get_type(text_node) != MCP_JSON_STRING ||
        mcp_json_get_string(text_node, &input_text) != 0 || input_text == NULL) {
        mcp_log_warn("Tool '%s': Missing or invalid 'text' string parameter.", name);
        *is_error = true;
        *error_message = mcp_strdup("Missing or invalid 'text' string parameter.");
        err_code = MCP_ERROR_INVALID_PARAMS;
        goto cleanup;
    }

    // Handle standard tools
    if (strcmp(name, "echo") == 0) {
        result_data = mcp_strdup(input_text);
        mcp_log_info("Echo tool called with text: %s", input_text);
        if (server && server->transport) {
            mcp_transport_protocol_t protocol = mcp_transport_get_protocol(server->transport);
            mcp_log_info("Transport protocol: %d (HTTP=%d)", protocol, MCP_TRANSPORT_PROTOCOL_HTTP);
            if (protocol == MCP_TRANSPORT_PROTOCOL_HTTP) {
                char event_data[256];
                snprintf(event_data, sizeof(event_data), "{\"text\":\"%s\"}", input_text);
                if (session_id) {
                    mcp_log_info("Sending SSE event: echo - %s to session: %s", event_data, session_id);
                } else {
                    mcp_log_info("Sending SSE event: echo - %s (broadcast to clients without session_id)", event_data);
                }

                // Add more detailed logging
                mcp_log_debug("SSE parameters - event: %s, data: %s, session_id: %s",
                             "echo", event_data, session_id ? session_id : "NULL");

                int ret = mcp_http_transport_send_sse(server->transport, "echo", event_data, session_id);
                if (ret != 0) {
                    mcp_log_error("Failed to send SSE event: %d", ret);
                } else {
                    mcp_log_info("SSE event sent successfully");
                }
            }
        }
    } else if (strcmp(name, "reverse") == 0) {
        // UTF-8 aware string reversal
        size_t len = strlen(input_text);
        // Array to store the byte positions of each character - use thread cache for better performance
        size_t* char_positions = (size_t*)mcp_thread_cache_alloc((len + 1) * sizeof(size_t));
        if (!char_positions) {
            mcp_log_error("Failed to allocate memory for character positions");
            *is_error = true;
            *error_message = mcp_strdup("Memory allocation failed.");
            err_code = MCP_ERROR_INTERNAL_ERROR;
            goto cleanup;
        }

        // First, count the number of UTF-8 characters
        size_t char_count = 0;
        size_t byte_pos = 0;

        // Record the starting byte position of each character
        while (byte_pos < len) {
            char_positions[char_count++] = byte_pos;

            // Skip to the next UTF-8 character - optimized UTF-8 decoding
            unsigned char c = (unsigned char)input_text[byte_pos];
            if (c < 0x80) {
                // ASCII character (1 byte) - most common case first for better branch prediction
                byte_pos += 1;
            } else if ((c & 0xE0) == 0xC0) {
                // 2-byte UTF-8 character
                byte_pos += 2;
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte UTF-8 character (like Chinese)
                byte_pos += 3;
            } else if ((c & 0xF8) == 0xF0) {
                // 4-byte UTF-8 character
                byte_pos += 4;
            } else {
                // Invalid UTF-8 sequence, treat as 1 byte
                byte_pos += 1;
            }

            // Safety check for malformed UTF-8
            if (byte_pos > len) {
                byte_pos = len;
            }
        }

        // Add the position after the last character
        char_positions[char_count] = len;

        // Allocate memory for the reversed string using thread cache
        result_data = (char*)mcp_thread_cache_alloc(len + 1);
        if (result_data != NULL) {
            // Copy characters in reverse order - optimized with single pass
            size_t out_pos = 0;
            for (size_t i = char_count; i > 0; i--) {
                size_t char_start = char_positions[i-1];
                size_t char_len = char_positions[i] - char_positions[i-1];

                // Copy this character to the output
                memcpy(result_data + out_pos, input_text + char_start, char_len);
                out_pos += char_len;
            }
            result_data[len] = '\0';
            mcp_log_info("Reverse tool called with text: %s, result: %s", input_text, result_data);
            if (server && server->transport) {
                mcp_transport_protocol_t protocol = mcp_transport_get_protocol(server->transport);
                mcp_log_info("Transport protocol: %d (HTTP=%d)", protocol, MCP_TRANSPORT_PROTOCOL_HTTP);
                if (protocol == MCP_TRANSPORT_PROTOCOL_HTTP) {
                    char event_data[256];
                    snprintf(event_data, sizeof(event_data), "{\"text\":\"%s\"}", result_data);
                    if (session_id) {
                        mcp_log_info("Sending SSE event: reverse - %s to session: %s", event_data, session_id);
                    } else {
                        mcp_log_info("Sending SSE event: reverse - %s (broadcast to clients without session_id)", event_data);
                    }

                    // Add more detailed logging
                    mcp_log_debug("SSE parameters - event: %s, data: %s, session_id: %s",
                                 "reverse", event_data, session_id ? session_id : "NULL");

                    int ret = mcp_http_transport_send_sse(server->transport, "reverse", event_data, session_id);
                    if (ret != 0) {
                        mcp_log_error("Failed to send SSE event: %d", ret);
                    } else {
                        mcp_log_info("SSE event sent successfully");
                    }
                }
            }

            // Free the character positions array
            mcp_thread_cache_free(char_positions, (len + 1) * sizeof(size_t));
        } else {
            mcp_thread_cache_free(char_positions, (len + 1) * sizeof(size_t));
            // Error handling will be done below
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
        *is_error = true; // Indicate tool execution failed internally
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    // --- Create the response content ---
    *content = (mcp_content_item_t**)mcp_thread_cache_alloc(sizeof(mcp_content_item_t*));
    if (!*content) {
        mcp_log_error("Failed to allocate content array for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }
    (*content)[0] = NULL;

    item = (mcp_content_item_t*)mcp_thread_cache_alloc(sizeof(mcp_content_item_t));
    if (!item) {
        mcp_log_error("Failed to allocate content item struct for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    item->type = MCP_CONTENT_TYPE_TEXT;
    item->mime_type = mcp_strdup("text/plain");
    item->data = result_data;
    item->data_size = strlen(result_data) + 1;
    result_data = NULL;
    if (!item->mime_type) {
        mcp_log_error("Failed to allocate mime type for tool: %s", name);
        *is_error = true;
        *error_message = mcp_strdup("Internal server error: memory allocation failed.");
        err_code = MCP_ERROR_INTERNAL_ERROR;
        goto cleanup;
    }

    (*content)[0] = item;
    *content_count = 1;
    err_code = MCP_ERROR_NONE;

cleanup:
    // Free intermediate allocations on error
    if (result_data) {
        free(result_data);
    }

    if (err_code != MCP_ERROR_NONE) {
        if (item) {
            if (item->mime_type) {
                free((void*)item->mime_type);
            }
            mcp_thread_cache_free(item, sizeof(mcp_content_item_t));
        }
        if (*content) {
            mcp_thread_cache_free(*content, sizeof(mcp_content_item_t*));
            *content = NULL;
        }
        *content_count = 0;
        if (*error_message == NULL) {
            *error_message = mcp_strdup("An unexpected error occurred processing the tool.");
        }
    }
    // Note: *is_error might be true even if err_code is MCP_ERROR_NONE,
    // if the tool logic itself represents an error state but the handler executed correctly.
    return err_code;
}

/**
 * Clean up resources
 */
static void server_cleanup(void) {
    mcp_log_info("Cleaning up resources");
#ifdef MCP_ENABLE_PROFILING
    mcp_profile_report(stdout);
#endif

    // Cleanup socket library
    mcp_socket_cleanup();
    mcp_log_info("Socket library cleaned up");

    // Destroy connection pools before freeing the backend list
    if (g_backends != NULL) {
        mcp_log_info("Destroying backend connection pools...");
        for (size_t i = 0; i < g_backend_count; ++i) {
            if (g_backends[i].pool != NULL) {
                mcp_connection_pool_destroy(g_backends[i].pool);
                g_backends[i].pool = NULL;
            }
        }
    }

    // Free gateway backend list (frees internal strings, then the list itself)
    mcp_log_info("Freeing gateway backend list...");
    mcp_free_backend_list(g_backends, g_backend_count);
    g_backends = NULL;
    g_backend_count = 0;

    if (g_server != NULL) {
        // mcp_server_destroy calls stop internally
        mcp_server_destroy(g_server);
        g_server = NULL;
    }

    // Clean up thread-local memory
    mcp_log_info("Cleaning up thread-local memory...");
    mcp_thread_cache_cleanup();

    // Clean up memory pool system
    mcp_log_info("Cleaning up memory pool system...");
    mcp_memory_pool_system_cleanup();

    mcp_log_close();
}

/**
 * Signal handler
 *
 * This optimized signal handler ensures clean shutdown with proper resource cleanup.
 */
static void signal_handler(int sig) {
    // Use atomic operation to prevent multiple signal handlers from running simultaneously
    static volatile int shutdown_in_progress = 0;

    // Fast path for already in-progress shutdown
    if (shutdown_in_progress) {
        mcp_log_info("Shutdown already in progress, forcing exit...");
        exit(1); // Force exit if we get a second signal
    }

    // Mark shutdown as in progress
    shutdown_in_progress = 1;

    mcp_log_info("Received signal %d, initiating shutdown...", sig);

    if (g_server) {
        mcp_log_info("Stopping server...");
        // Attempt graceful stop
        mcp_server_stop(g_server);

        // Wait briefly for server to stop
        mcp_log_info("Waiting for server to stop (max 1 second)...");

        // Wait up to 1 second for server to stop gracefully
        mcp_sleep_ms(1000);

        // Call cleanup directly to ensure resources are freed
        server_cleanup();
    }

    // Force exit to ensure we don't hang
    mcp_log_info("Exiting process...");
    exit(0);
}

#ifndef _WIN32
/**
 * Daemonize the process (Unix-like systems only)
 */
static int daemonize(void) {
    pid_t pid, sid;
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        exit(0); // Exit parent
    umask(0);
    sid = setsid();
    if (sid < 0)
        return -1;
    if (chdir("/") < 0)
        return -1;
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
    config->log_level = MCP_LOG_LEVEL_INFO;
    config->daemon = false;
    config->api_key = NULL;
    config->gateway_mode = false;
    config->doc_root = NULL;  // Default: no static file serving

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            config->transport_type = "tcp";
        } else if (strcmp(argv[i], "--http") == 0) {
            config->transport_type = "http";
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
            if (strcmp(argv[i], "error") == 0) config->log_level = MCP_LOG_LEVEL_ERROR;
            else if (strcmp(argv[i], "warn") == 0) config->log_level = MCP_LOG_LEVEL_WARN;
            else if (strcmp(argv[i], "info") == 0) config->log_level = MCP_LOG_LEVEL_INFO;
            else if (strcmp(argv[i], "debug") == 0) config->log_level = MCP_LOG_LEVEL_DEBUG;
            else if (strcmp(argv[i], "trace") == 0) config->log_level = MCP_LOG_LEVEL_TRACE; // Add trace
            else { fprintf(stderr, "Invalid log level: %s\n", argv[i]); return -1; }
        } else if (strcmp(argv[i], "--daemon") == 0) {
#ifndef _WIN32
            config->daemon = true;
#else
            fprintf(stderr, "Daemon mode is not supported on Windows\n"); return -1;
#endif
        } else if (strcmp(argv[i], "--api-key") == 0 && i + 1 < argc) {
            config->api_key = argv[++i];
        } else if (strcmp(argv[i], "--gateway") == 0) {
            config->gateway_mode = true;
        } else if (strcmp(argv[i], "--doc-root") == 0 && i + 1 < argc) {
            config->doc_root = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --tcp               Use TCP transport (default for daemon mode)\n");
            printf("  --http              Use HTTP transport with SSE support\n");
            printf("  --stdio             Use stdio transport (default for interactive mode)\n");
            printf("  --host HOST         Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT         Port to bind to (default: 8080)\n");
            printf("  --log-file FILE     Log to file\n");
            printf("  --log-level LEVEL   Set log level (error, warn, info, debug)\n");
            printf("  --api-key KEY       Require API key for authentication\n");
            printf("  --gateway           Enable MCP Gateway mode (requires gateway_config.json)\n");
            printf("  --doc-root PATH     Document root for serving static files (HTTP mode only)\n");
            printf("  --daemon            Run as daemon (Unix-like systems only)\n");
            printf("  --help              Show this help message\n");
            exit(0);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return -1;
        }
    }

    if (config->daemon) {
        // Force TCP for daemon
        config->transport_type = "tcp";
        if (config->log_file == NULL) {
            fprintf(stderr, "Log file is required in daemon mode\n");
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    server_config_t config;
    if (parse_arguments(argc, argv, &config) != 0)
        return 1;

    if (mcp_log_init(config.log_file, config.log_level) != 0)
        return 1;
    mcp_log_info("Logging system initialized.");

#ifndef _WIN32
    if (config.daemon) {
        mcp_log_info("Daemonizing process...");
        if (daemonize() != 0) {
            mcp_log_error("Failed to daemonize");
            mcp_log_close();
            return 1;
        }
        mcp_log_info("Daemonization complete.");
    }
#endif

    atexit(server_cleanup);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
#endif

    mcp_log_info("Starting MCP server...");

    // Initialize memory pool system
    mcp_log_info("Initializing memory pool system...");
    if (!mcp_memory_pool_system_init(64, 32, 16)) {
        mcp_log_error("Failed to initialize memory pool system");
        mcp_log_close();
        return 1;
    }
    mcp_log_info("Memory pool system initialized");

    // Initialize thread-local cache
    mcp_log_info("Initializing thread-local cache...");
    if (!mcp_thread_cache_init()) {
        mcp_log_error("Failed to initialize thread-local cache");
        mcp_memory_pool_system_cleanup();
        mcp_log_close();
        return 1;
    }
    mcp_log_info("Thread-local cache initialized");

    // Initialize socket library
    if (mcp_socket_init() != 0) {
        mcp_log_error("Failed to initialize socket library");
        mcp_thread_cache_cleanup();
        mcp_memory_pool_system_cleanup();
        mcp_log_close();
        return 1;
    }
    mcp_log_info("Socket library initialized");

    mcp_server_config_t server_config = {
        .name = "supa-mcp-server",
        .version = "1.0.0",
        .description = "Supa MCP server implementation",
        .api_key = config.api_key // Pass the parsed API key
    };
    mcp_server_capabilities_t capabilities = {
        .resources_supported = true,
        .tools_supported = true
    };

    g_server = mcp_server_create(&server_config, &capabilities);
    if (g_server == NULL) {
        mcp_log_error("Failed to create server");
        return 1;
    }

    // Initialize thread-local storage (arena) for the main thread BEFORE using it (e.g., in JSON parsing)
    // Using 1MB as the initial size. Adjust if needed.
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) {
        mcp_log_error("Failed to initialize thread-local arena for main thread.");
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    // --- Gateway Mode Setup ---
    g_server->is_gateway_mode = config.gateway_mode; // Set flag on server instance
    if (config.gateway_mode) {
        mcp_log_info("Gateway mode enabled. Loading backend configuration...");
        // Load gateway configuration
        const char* gateway_config_path = "d:/workspace/SupaMCPServer/gateway_config.json"; // TODO: Make configurable?
        mcp_error_code_t load_err = load_gateway_config(gateway_config_path, &g_backends, &g_backend_count);

        if (load_err != MCP_ERROR_NONE && load_err != MCP_ERROR_INVALID_REQUEST /* Allow file not found */) {
            mcp_log_error("Failed to load gateway config '%s' (Error %d). Exiting.", gateway_config_path, load_err);
            mcp_server_destroy(g_server); g_server = NULL;
            return 1;
        } else if (load_err == MCP_ERROR_NONE) {
            mcp_log_info("Loaded %zu backend(s) from gateway config '%s'.", g_backend_count, gateway_config_path);

            // Assign loaded backends to the server instance
            g_server->backends = g_backends;
            g_server->backend_count = g_backend_count;

            // Initialize connection pools for loaded TCP backends
            mcp_log_info("Initializing backend connection pools...");
            for (size_t i = 0; i < g_backend_count; ++i) {
                mcp_backend_info_t* backend = &g_backends[i];
                backend->pool = NULL; // Initialize pool pointer

                if (strncmp(backend->address, "tcp://", 6) == 0) {
                    char host_buf[256];
                    int port = 0;
                    if (parse_tcp_address(backend->address, host_buf, sizeof(host_buf), &port)) {
                        // TODO: Make pool parameters configurable? Using defaults for now.
                        size_t min_conn = 1;
                        size_t max_conn = 4;
                        int idle_timeout = 60000; // 60 seconds
                        int connect_timeout = 5000; // 5 seconds
                        int health_check_interval = 30000; // 30 seconds
                        int health_check_timeout = 2000; // 2 seconds
                        mcp_log_info("Creating connection pool for backend '%s' (%s:%d) with health checks every %d ms...",
                                    backend->name, host_buf, port, health_check_interval);
                        backend->pool = mcp_connection_pool_create(host_buf, port, min_conn, max_conn,
                                                                 idle_timeout, connect_timeout,
                                                                 health_check_interval, health_check_timeout);
                        if (backend->pool == NULL) {
                            mcp_log_error("Failed to create connection pool for backend '%s'. Gateway routing for this backend will fail.", backend->name);
                        }
                    } else {
                        mcp_log_error("Failed to parse TCP address '%s' for backend '%s'.", backend->address, backend->name);
                    }
                } else {
                    mcp_log_warn("Backend '%s' address '%s' is not TCP. Connection pool not created.", backend->name, backend->address);
                }
            }
        } else {
            mcp_log_info("Gateway config file '%s' not found or empty. Running gateway without backends.", gateway_config_path);
            g_server->backends = NULL;
            g_server->backend_count = 0;
        }
    } else {
        mcp_log_info("Gateway mode disabled.");
        g_server->backends = NULL;
        g_server->backend_count = 0;
    }
    // --- End Gateway Mode Setup ---

    // Set local handlers (these might be used if no backend matches a request)
    if (mcp_server_set_resource_handler(g_server, server_resource_handler, NULL) != 0 ||
        mcp_server_set_tool_handler(g_server, server_tool_handler, NULL) != 0) {
        mcp_log_error("Failed to set local handlers");
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
    if (r1) {
        mcp_server_add_resource(g_server, r1);
        mcp_resource_free(r1);
    }

    if (r2) {
        mcp_server_add_resource(g_server, r2);
        mcp_resource_free(r2);
    }

    if (t1) {
        mcp_server_add_resource_template(g_server, t1);
        mcp_resource_template_free(t1);
    }

    if (tool1) {
        mcp_tool_add_param(tool1, "text", "string", "Text to echo", true);
        mcp_server_add_tool(g_server, tool1);
        mcp_tool_free(tool1);
    }

    if (tool2) {
        mcp_tool_add_param(tool2, "text", "string", "Text to reverse", true);
        mcp_server_add_tool(g_server, tool2);
        mcp_tool_free(tool2);
    }

    mcp_log_info("Added example resources and tools.");

    // Register HTTP client tool
    if (register_http_client_tool(g_server) != 0) {
        mcp_log_error("Failed to register HTTP client tool");
    } else {
        mcp_log_info("HTTP client tool registered successfully");
    }

    // Create transport based on config
    mcp_transport_t* transport = NULL;
    if (strcmp(config.transport_type, "stdio") == 0) {
        mcp_log_info("Using stdio transport");
        transport = mcp_transport_stdio_create();
    } else if (strcmp(config.transport_type, "tcp") == 0) {
        mcp_log_info("Using TCP transport on %s:%d", config.host, config.port);
        // Pass idle timeout (e.g., 60000ms = 1 minute, 0 to disable)
        // TODO: confgure this from command line or config file
        uint32_t idle_timeout = 0; // Disabled server-side idle timeout
        mcp_log_info("Server-side idle timeout disabled.");
        transport = mcp_transport_tcp_create(config.host, config.port, idle_timeout);
    } else if (strcmp(config.transport_type, "http") == 0) {
        mcp_log_info("Using HTTP transport on %s:%d", config.host, config.port);
        // Create HTTP transport configuration
        mcp_http_config_t http_config = {
            .host = config.host,
            .port = config.port,
            .use_ssl = false,  // No SSL for now
            .cert_path = NULL,
            .key_path = NULL,
            .doc_root = config.doc_root,
            .timeout_ms = 0    // No timeout
        };

        if (config.doc_root) {
            mcp_log_info("Static file serving enabled, document root: %s", config.doc_root);
        }
        transport = mcp_transport_http_create(&http_config);

        // Explicitly set the protocol type to HTTP
        if (transport) {
            mcp_transport_set_protocol(transport, MCP_TRANSPORT_PROTOCOL_HTTP);
            mcp_log_info("Transport protocol explicitly set to HTTP");
        }
    } else {
        mcp_log_error("Unknown transport type: %s", config.transport_type);
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    if (transport == NULL) {
        mcp_log_error("Failed to create transport");
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    // Start the server (transport handle is now owned by server)
    if (mcp_server_start(g_server, transport) != 0) {
        mcp_log_error("Failed to start server");
        // Don't destroy transport here, server destroy should handle it
        mcp_server_destroy(g_server); g_server = NULL;
        return 1;
    }

    mcp_log_info("Server started successfully. Waiting for connections or input...");

    // Main loop with optimized wait strategy
    // Use a condition variable or semaphore if available in the future
    while (g_server != NULL) {
        // Sleep for 1 second - this is efficient as it doesn't consume CPU
        // In a future optimization, this could be replaced with a condition variable wait
        mcp_sleep_ms(1000);
    }

    mcp_log_info("Main loop exiting.");

    return 0;
}
