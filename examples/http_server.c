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

// Extended HTTP server configuration
typedef struct http_server_config_t {
    // Basic HTTP configuration (passed to mcp_http_transport)
    mcp_http_config_t http_config;

    // Logging configuration
    int log_level;
    bool log_to_file;
    char* log_file_path;
    int log_max_size;
    int log_max_files;

    // Security settings
    bool enable_cors;
    char* cors_allow_origin;
    char* cors_allow_methods;
    char* cors_allow_headers;
    int cors_max_age;

    // Content Security Policy
    bool enable_csp;
    char* csp_policy;

    // Cache control
    bool enable_cache_control;
    int cache_max_age;
    bool cache_public;

    // Static file settings
    bool enable_directory_listing;
    char* default_mime_type;
    char* index_files;

    // Connection settings
    int max_connections;
    bool keep_alive;
    int keep_alive_timeout;

    // SSE settings
    int max_sse_clients;
    int max_sse_events;
    int sse_event_ttl;

    // Rate limiting
    bool enable_rate_limiting;
    int rate_limit_requests;
    int rate_limit_window;
    bool rate_limit_by_ip;

    // Advanced settings
    int thread_pool_size;
    int task_queue_size;
    int max_request_size;
} http_server_config_t;

// Global instances for signal handling and SSE events
static mcp_server_t* g_server = NULL;
static mcp_transport_t* g_transport = NULL;
static const char* g_doc_root = NULL; // To store the doc_root path

// Example tool handler
static mcp_error_code_t http_tool_handler(
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



// MIME type mapping structure
typedef struct {
    const char* extension;
    const char* mime_type;
} mime_type_mapping_t;

// MIME type mappings
static const mime_type_mapping_t mime_type_mappings[] = {
    // Text formats
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "text/javascript"},
    {".txt", "text/plain"},
    {".md", "text/markdown"},
    {".csv", "text/csv"},
    {".tsv", "text/tab-separated-values"},
    {".xml", "text/xml"},
    {".xsl", "text/xsl"},

    // Image formats
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".webp", "image/webp"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".bmp", "image/bmp"},
    {".tiff", "image/tiff"},
    {".tif", "image/tiff"},

    // Audio formats
    {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"},
    {".ogg", "audio/ogg"},
    {".m4a", "audio/mp4"},
    {".aac", "audio/aac"},
    {".flac", "audio/flac"},
    {".opus", "audio/opus"},

    // Video formats
    {".mp4", "video/mp4"},
    {".webm", "video/webm"},
    {".ogv", "video/ogg"},
    {".avi", "video/x-msvideo"},
    {".mov", "video/quicktime"},
    {".wmv", "video/x-ms-wmv"},
    {".flv", "video/x-flv"},
    {".mkv", "video/x-matroska"},

    // Application formats
    {".json", "application/json"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".gz", "application/gzip"},
    {".tar", "application/x-tar"},
    {".rar", "application/vnd.rar"},
    {".7z", "application/x-7z-compressed"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},

    // Font formats
    {".ttf", "font/ttf"},
    {".otf", "font/otf"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".eot", "application/vnd.ms-fontobject"},

    // Other formats
    {".swf", "application/x-shockwave-flash"},
    {".wasm", "application/wasm"},
    {".webmanifest", "application/manifest+json"},

    // End of list
    {NULL, NULL}
};

// Function to get MIME type from file extension
static const char* get_mime_type(const char* filename, const char* default_mime_type) {
    if (!filename) {
        return default_mime_type;
    }

    // Find the last dot in the filename
    const char* ext = strrchr(filename, '.');
    if (!ext) {
        return default_mime_type;
    }

    // Convert extension to lowercase for case-insensitive comparison
    char ext_lower[32] = {0};
    size_t ext_len = strlen(ext);
    if (ext_len >= sizeof(ext_lower)) {
        ext_len = sizeof(ext_lower) - 1;
    }

    for (size_t i = 0; i < ext_len; i++) {
        ext_lower[i] = (char)tolower(ext[i]);
    }

    // Search for the extension in the mappings
    for (int i = 0; mime_type_mappings[i].extension != NULL; i++) {
        if (strcmp(ext_lower, mime_type_mappings[i].extension) == 0) {
            return mime_type_mappings[i].mime_type;
        }
    }

    // If not found, return the default MIME type
    return default_mime_type;
}

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

// Function declarations
static void init_http_server_config(http_server_config_t* config, const char* host, uint16_t port, const char* doc_root);

// Initialize configuration with default values
static void init_http_server_config(http_server_config_t* config, const char* host, uint16_t port, const char* doc_root) {
    // Initialize HTTP transport config
    config->http_config.host = mcp_strdup(host);
    config->http_config.port = port;
    config->http_config.use_ssl = false;
    config->http_config.cert_path = NULL;
    config->http_config.key_path = NULL;
    config->http_config.doc_root = mcp_strdup(doc_root);
    config->http_config.timeout_ms = 30000;

    // Initialize logging configuration
    config->log_level = MCP_LOG_LEVEL_INFO;
    config->log_to_file = false;
    config->log_file_path = mcp_strdup("logs/http_server.log");
    config->log_max_size = 10;
    config->log_max_files = 5;

    // Initialize security settings
    config->enable_cors = true;
    config->cors_allow_origin = mcp_strdup("*");
    config->cors_allow_methods = mcp_strdup("GET,POST,OPTIONS");
    config->cors_allow_headers = mcp_strdup("Content-Type,Authorization");
    config->cors_max_age = 86400;

    // Initialize Content Security Policy
    config->enable_csp = true;
    config->csp_policy = mcp_strdup("default-src 'self'; script-src 'self'; style-src 'self';");

    // Initialize cache control
    config->enable_cache_control = true;
    config->cache_max_age = 3600;
    config->cache_public = true;

    // Initialize static file settings
    config->enable_directory_listing = false;
    config->default_mime_type = mcp_strdup("application/octet-stream");
    config->index_files = mcp_strdup("index.html,index.htm");

    // Initialize connection settings
    config->max_connections = 1000;
    config->keep_alive = true;
    config->keep_alive_timeout = 5000;

    // Initialize SSE settings
    config->max_sse_clients = 10000;
    config->max_sse_events = 1000;
    config->sse_event_ttl = 3600;

    // Initialize rate limiting
    config->enable_rate_limiting = false;
    config->rate_limit_requests = 100;
    config->rate_limit_window = 60;
    config->rate_limit_by_ip = true;

    // Initialize advanced settings
    config->thread_pool_size = 4;
    config->task_queue_size = 32;
    config->max_request_size = 1048576; // 1MB
}

// Free resources allocated for configuration
static void free_http_server_config(http_server_config_t* config) {
    // Free HTTP transport config
    if (config->http_config.host) free(config->http_config.host);
    if (config->http_config.cert_path) free(config->http_config.cert_path);
    if (config->http_config.key_path) free(config->http_config.key_path);
    if (config->http_config.doc_root) free(config->http_config.doc_root);

    // Free logging configuration
    if (config->log_file_path) free(config->log_file_path);

    // Free security settings
    if (config->cors_allow_origin) free(config->cors_allow_origin);
    if (config->cors_allow_methods) free(config->cors_allow_methods);
    if (config->cors_allow_headers) free(config->cors_allow_headers);

    // Free Content Security Policy
    if (config->csp_policy) free(config->csp_policy);

    // Free static file settings
    if (config->default_mime_type) free(config->default_mime_type);
    if (config->index_files) free(config->index_files);
}

// Function to parse a boolean value from a string
static bool parse_bool(const char* value) {
    return (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
            strcmp(value, "yes") == 0 || strcmp(value, "y") == 0 ||
            strcmp(value, "on") == 0);
}

// Function to parse a configuration file
static int parse_config_file(const char* filename, http_server_config_t* config) {
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
            if (config->http_config.host) free(config->http_config.host);
            config->http_config.host = mcp_strdup(value);
        } else if (strcmp(key, "port") == 0) {
            config->http_config.port = (uint16_t)atoi(value);
        } else if (strcmp(key, "doc_root") == 0) {
            // Handle doc_root path
            if (config->http_config.doc_root) free(config->http_config.doc_root);

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
                config->http_config.doc_root = mcp_strdup(abs_path);
                printf("Converted config doc_root to absolute path: %s\n", abs_path);
            } else {
                // This is already an absolute path
                config->http_config.doc_root = mcp_strdup(value);
            }
        } else if (strcmp(key, "use_ssl") == 0) {
            config->http_config.use_ssl = parse_bool(value);
        } else if (strcmp(key, "cert_path") == 0) {
            if (config->http_config.cert_path) free(config->http_config.cert_path);
            config->http_config.cert_path = mcp_strdup(value);
        } else if (strcmp(key, "key_path") == 0) {
            if (config->http_config.key_path) free(config->http_config.key_path);
            config->http_config.key_path = mcp_strdup(value);
        } else if (strcmp(key, "timeout_ms") == 0) {
            config->http_config.timeout_ms = atoi(value);
        }
        // Logging configuration
        else if (strcmp(key, "log_level") == 0) {
            config->log_level = atoi(value);
        } else if (strcmp(key, "log_to_file") == 0) {
            config->log_to_file = parse_bool(value);
        } else if (strcmp(key, "log_file_path") == 0) {
            if (config->log_file_path) free(config->log_file_path);
            config->log_file_path = mcp_strdup(value);
        } else if (strcmp(key, "log_max_size") == 0) {
            config->log_max_size = atoi(value);
        } else if (strcmp(key, "log_max_files") == 0) {
            config->log_max_files = atoi(value);
        }
        // Security settings
        else if (strcmp(key, "enable_cors") == 0) {
            config->enable_cors = parse_bool(value);
        } else if (strcmp(key, "cors_allow_origin") == 0) {
            if (config->cors_allow_origin) free(config->cors_allow_origin);
            config->cors_allow_origin = mcp_strdup(value);
        } else if (strcmp(key, "cors_allow_methods") == 0) {
            if (config->cors_allow_methods) free(config->cors_allow_methods);
            config->cors_allow_methods = mcp_strdup(value);
        } else if (strcmp(key, "cors_allow_headers") == 0) {
            if (config->cors_allow_headers) free(config->cors_allow_headers);
            config->cors_allow_headers = mcp_strdup(value);
        } else if (strcmp(key, "cors_max_age") == 0) {
            config->cors_max_age = atoi(value);
        }
        // Content Security Policy
        else if (strcmp(key, "enable_csp") == 0) {
            config->enable_csp = parse_bool(value);
        } else if (strcmp(key, "csp_policy") == 0) {
            if (config->csp_policy) free(config->csp_policy);
            config->csp_policy = mcp_strdup(value);
        }
        // Cache control
        else if (strcmp(key, "enable_cache_control") == 0) {
            config->enable_cache_control = parse_bool(value);
        } else if (strcmp(key, "cache_max_age") == 0) {
            config->cache_max_age = atoi(value);
        } else if (strcmp(key, "cache_public") == 0) {
            config->cache_public = parse_bool(value);
        }
        // Static file settings
        else if (strcmp(key, "enable_directory_listing") == 0) {
            config->enable_directory_listing = parse_bool(value);
        } else if (strcmp(key, "default_mime_type") == 0) {
            if (config->default_mime_type) free(config->default_mime_type);
            config->default_mime_type = mcp_strdup(value);
        } else if (strcmp(key, "index_files") == 0) {
            if (config->index_files) free(config->index_files);
            config->index_files = mcp_strdup(value);
        }
        // Connection settings
        else if (strcmp(key, "max_connections") == 0) {
            config->max_connections = atoi(value);
        } else if (strcmp(key, "keep_alive") == 0) {
            config->keep_alive = parse_bool(value);
        } else if (strcmp(key, "keep_alive_timeout") == 0) {
            config->keep_alive_timeout = atoi(value);
        }
        // SSE settings
        else if (strcmp(key, "max_sse_clients") == 0) {
            config->max_sse_clients = atoi(value);
        } else if (strcmp(key, "max_sse_events") == 0) {
            config->max_sse_events = atoi(value);
        } else if (strcmp(key, "sse_event_ttl") == 0) {
            config->sse_event_ttl = atoi(value);
        }
        // Rate limiting
        else if (strcmp(key, "enable_rate_limiting") == 0) {
            config->enable_rate_limiting = parse_bool(value);
        } else if (strcmp(key, "rate_limit_requests") == 0) {
            config->rate_limit_requests = atoi(value);
        } else if (strcmp(key, "rate_limit_window") == 0) {
            config->rate_limit_window = atoi(value);
        } else if (strcmp(key, "rate_limit_by_ip") == 0) {
            config->rate_limit_by_ip = parse_bool(value);
        }
        // Advanced settings
        else if (strcmp(key, "thread_pool_size") == 0) {
            config->thread_pool_size = atoi(value);
        } else if (strcmp(key, "task_queue_size") == 0) {
            config->task_queue_size = atoi(value);
        } else if (strcmp(key, "max_request_size") == 0) {
            config->max_request_size = atoi(value);
        }
    }

    fclose(file);
    return 0;
}

// Error codes
typedef enum {
    HTTP_SERVER_ERROR_NONE = 0,
    HTTP_SERVER_ERROR_INVALID_ARGS = -1,
    HTTP_SERVER_ERROR_FILE_NOT_FOUND = -2,
    HTTP_SERVER_ERROR_MEMORY_ALLOCATION = -3,
    HTTP_SERVER_ERROR_SERVER_CREATION = -4,
    HTTP_SERVER_ERROR_TRANSPORT_CREATION = -5,
    HTTP_SERVER_ERROR_SERVER_START = -6,
    HTTP_SERVER_ERROR_CONFIG_PARSE = -7,
    HTTP_SERVER_ERROR_TOOL_HANDLER = -8,
    HTTP_SERVER_ERROR_UNKNOWN = -99
} http_server_error_t;

// Error messages
static const char* http_server_error_messages[] = {
    "Success",
    "Invalid arguments",
    "File not found",
    "Memory allocation failure",
    "Server creation failure",
    "Transport creation failure",
    "Server start failure",
    "Configuration parse failure",
    "Tool handler registration failure",
    "Unknown error"
};

// Function to get error message from error code
static const char* http_server_error_message(http_server_error_t error) {
    if (error == HTTP_SERVER_ERROR_NONE) {
        return http_server_error_messages[0];
    } else if (error == HTTP_SERVER_ERROR_INVALID_ARGS) {
        return http_server_error_messages[1];
    } else if (error == HTTP_SERVER_ERROR_FILE_NOT_FOUND) {
        return http_server_error_messages[2];
    } else if (error == HTTP_SERVER_ERROR_MEMORY_ALLOCATION) {
        return http_server_error_messages[3];
    } else if (error == HTTP_SERVER_ERROR_SERVER_CREATION) {
        return http_server_error_messages[4];
    } else if (error == HTTP_SERVER_ERROR_TRANSPORT_CREATION) {
        return http_server_error_messages[5];
    } else if (error == HTTP_SERVER_ERROR_SERVER_START) {
        return http_server_error_messages[6];
    } else if (error == HTTP_SERVER_ERROR_CONFIG_PARSE) {
        return http_server_error_messages[7];
    } else if (error == HTTP_SERVER_ERROR_TOOL_HANDLER) {
        return http_server_error_messages[8];
    } else {
        return http_server_error_messages[9];
    }
}

// HTTP header handler callback - simplified version
static int http_header_handler(const char* uri, const char* method, void* headers, void* user_data) {
    (void)method; (void)user_data;
    // This is a simplified version that doesn't actually modify headers
    // since we don't have access to the header manipulation functions

    // Skip if no headers or URI
    if (!headers || !uri) {
        return 0;
    }

    // In a real implementation, we would add security headers, CORS headers, etc.
    // based on the configuration

    // Log that we're handling headers for this URI
    mcp_log_debug("HTTP header handler called for URI: %s", uri);

    // In a real implementation, we would add various headers here:
    // - Security headers (X-Content-Type-Options, X-Frame-Options, etc.)
    // - CORS headers for cross-origin requests
    // - Cache control headers for static files and API endpoints
    // - Content Security Policy headers

    // For now, we'll just return success

    return 0;
}

// Function to handle errors and perform cleanup
static void http_server_handle_error(http_server_error_t error, const char* context, http_server_config_t* config) {
    if (error == HTTP_SERVER_ERROR_NONE) {
        return;
    }

    // Log the error
    mcp_log_error("%s: %s", context, http_server_error_message(error));

    // Perform cleanup
    if (g_server) {
        mcp_server_stop(g_server);
        mcp_server_destroy(g_server);
        g_server = NULL;
    }

    if (g_transport) {
        mcp_transport_destroy(g_transport);
        g_transport = NULL;
    }

    if (g_doc_root) {
        free((void*)g_doc_root);
        g_doc_root = NULL;
    }

    if (config) {
        free_http_server_config(config);
    }

    mcp_log_close();
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
    // Default configuration values
    const char* host = "127.0.0.1";
    uint16_t port = 8280;
    const char* config_file = "http_server.conf";
    const char* doc_root = ".";
    bool config_file_specified = false;

    // Initialize server configuration with default values
    http_server_config_t server_config;
    init_http_server_config(&server_config, host, port, doc_root);

    // We'll use the server_config directly instead of through a global variable
    // g_server_config = &server_config;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            if (server_config.http_config.host) free(server_config.http_config.host);
            server_config.http_config.host = mcp_strdup(argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server_config.http_config.port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
            config_file_specified = true;
        } else if (strcmp(argv[i], "--doc-root") == 0 && i + 1 < argc) {
            if (server_config.http_config.doc_root) free(server_config.http_config.doc_root);
            server_config.http_config.doc_root = mcp_strdup(argv[++i]);
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            server_config.log_level = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            server_config.log_to_file = true;
            if (server_config.log_file_path) free(server_config.log_file_path);
            server_config.log_file_path = mcp_strdup(argv[++i]);
        } else if (strcmp(argv[i], "--ssl") == 0) {
            server_config.http_config.use_ssl = true;
        } else if (strcmp(argv[i], "--cert") == 0 && i + 1 < argc) {
            if (server_config.http_config.cert_path) free(server_config.http_config.cert_path);
            server_config.http_config.cert_path = mcp_strdup(argv[++i]);
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            if (server_config.http_config.key_path) free(server_config.http_config.key_path);
            server_config.http_config.key_path = mcp_strdup(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST         Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT         Port to bind to (default: 8080)\n");
            printf("  --config FILE       Configuration file to use (default: http_server.conf)\n");
            printf("  --doc-root PATH     Document root for static files (default: .)\n");
            printf("  --log-level LEVEL   Log level (0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL)\n");
            printf("  --log-file PATH     Log to file at specified path\n");
            printf("  --ssl               Enable SSL/TLS\n");
            printf("  --cert PATH         Path to SSL certificate file\n");
            printf("  --key PATH          Path to SSL private key file\n");
            printf("  --help              Show this help message\n");
            free_http_server_config(&server_config);
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            free_http_server_config(&server_config);
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

    // Convert relative doc_root to absolute path if needed
    if (server_config.http_config.doc_root &&
        server_config.http_config.doc_root[0] != '/' &&
        !(server_config.http_config.doc_root[0] &&
          server_config.http_config.doc_root[1] == ':' &&
          (server_config.http_config.doc_root[2] == '/' || server_config.http_config.doc_root[2] == '\\'))) {
        // This is a relative path, make it absolute
        char abs_path[512];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, server_config.http_config.doc_root);
        char* old_path = server_config.http_config.doc_root;
        server_config.http_config.doc_root = mcp_strdup(abs_path);
        free(old_path);
        printf("Converted relative doc_root to absolute path: %s\n", abs_path);
    }

    // Try to load configuration from file
    if (config_file) {
        printf("Trying to load configuration from file: %s\n", config_file);

        // First try in the current directory
        printf("Trying config file in current directory: %s\n", config_file);
        if (parse_config_file(config_file, &server_config) != 0) {
            // If not found and not explicitly specified, try in web/html directory
            if (!config_file_specified) {
                char web_config_path[512];
                snprintf(web_config_path, sizeof(web_config_path), "web/html/%s", config_file);
                printf("Trying config file in web/html directory: %s\n", web_config_path);
                if (parse_config_file(web_config_path, &server_config) != 0) {
                    // If still not found, try in the executable directory
                    char exe_path[512];
                    if (getcwd(exe_path, sizeof(exe_path)) != NULL) {
                        // Try in current directory
                        char exe_config_path[512];
                        snprintf(exe_config_path, sizeof(exe_config_path), "%s/%s", exe_path, config_file);
                        printf("Trying config file in exe directory: %s\n", exe_config_path);
                        if (parse_config_file(exe_config_path, &server_config) != 0) {
                            // Try in Debug subdirectory (for Visual Studio)
                            snprintf(exe_config_path, sizeof(exe_config_path), "%s/Debug/%s", exe_path, config_file);
                            printf("Trying config file in Debug subdirectory: %s\n", exe_config_path);
                            if (parse_config_file(exe_config_path, &server_config) != 0) {
                                // Try in Release subdirectory
                                snprintf(exe_config_path, sizeof(exe_config_path), "%s/Release/%s", exe_path, config_file);
                                printf("Trying config file in Release subdirectory: %s\n", exe_config_path);
                                parse_config_file(exe_config_path, &server_config);
                            }
                        }
                    }
                }
            }
        }
    }

    // Initialize logging
    if (server_config.log_to_file) {
        // Create logs directory if it doesn't exist
        #ifdef _WIN32
        _mkdir("logs");
        #else
        mkdir("logs", 0755);
        #endif

        mcp_log_init(server_config.log_file_path, server_config.log_level);
        mcp_log_info("Logging to file: %s", server_config.log_file_path);
    } else {
        mcp_log_init(NULL, server_config.log_level);
    }

    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create MCP server configuration
    mcp_server_config_t mcp_server_config = {
        .name = "http-example-server",
        .version = "1.0.0",
        .description = "HTTP MCP Server Example with SSE",
        .thread_pool_size = server_config.thread_pool_size,
        .task_queue_size = server_config.task_queue_size,
        .max_message_size = server_config.max_request_size,
        .api_key = NULL // No API key required
    };

    // Set server capabilities
    mcp_server_capabilities_t capabilities = {
        .resources_supported = false, // No resources in this example
        .tools_supported = true
    };

    // Create server
    g_server = mcp_server_create(&mcp_server_config, &capabilities);
    if (!g_server) {
        http_server_handle_error(HTTP_SERVER_ERROR_SERVER_CREATION, "Failed to create server", &server_config);
        return 1;
    }

    // Register tool handler
    if (mcp_server_set_tool_handler(g_server, http_tool_handler, NULL) != 0) {
        http_server_handle_error(HTTP_SERVER_ERROR_TOOL_HANDLER, "Failed to set tool handler", &server_config);
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
    mcp_log_info("  Host: %s", server_config.http_config.host);
    mcp_log_info("  Port: %d", server_config.http_config.port);
    mcp_log_info("  Document Root: %s", server_config.http_config.doc_root ? server_config.http_config.doc_root : "(null)");
    mcp_log_info("  Use SSL: %s", server_config.http_config.use_ssl ? "true" : "false");
    if (server_config.http_config.use_ssl) {
        mcp_log_info("  Certificate: %s", server_config.http_config.cert_path ? server_config.http_config.cert_path : "(null)");
        mcp_log_info("  Private Key: %s", server_config.http_config.key_path ? server_config.http_config.key_path : "(null)");
    }
    mcp_log_info("  Log Level: %d", server_config.log_level);
    mcp_log_info("  Log to File: %s", server_config.log_to_file ? "true" : "false");
    if (server_config.log_to_file) {
        mcp_log_info("  Log File Path: %s", server_config.log_file_path);
    }

    // Log security settings
    mcp_log_info("Security Settings:");
    mcp_log_info("  CORS Enabled: %s", server_config.enable_cors ? "true" : "false");
    if (server_config.enable_cors) {
        mcp_log_info("  CORS Allow Origin: %s", server_config.cors_allow_origin);
        mcp_log_info("  CORS Allow Methods: %s", server_config.cors_allow_methods);
        mcp_log_info("  CORS Allow Headers: %s", server_config.cors_allow_headers);
        mcp_log_info("  CORS Max Age: %d", server_config.cors_max_age);
    }
    mcp_log_info("  CSP Enabled: %s", server_config.enable_csp ? "true" : "false");
    if (server_config.enable_csp) {
        mcp_log_info("  CSP Policy: %s", server_config.csp_policy);
    }

    // Log cache settings
    mcp_log_info("Cache Settings:");
    mcp_log_info("  Cache Control Enabled: %s", server_config.enable_cache_control ? "true" : "false");
    if (server_config.enable_cache_control) {
        mcp_log_info("  Cache Max Age: %d", server_config.cache_max_age);
        mcp_log_info("  Cache Public: %s", server_config.cache_public ? "true" : "false");
    }

    // Log static file settings
    mcp_log_info("Static File Settings:");
    mcp_log_info("  Directory Listing: %s", server_config.enable_directory_listing ? "true" : "false");
    mcp_log_info("  Default MIME Type: %s", server_config.default_mime_type);
    mcp_log_info("  Index Files: %s", server_config.index_files);

    // Log connection settings
    mcp_log_info("Connection Settings:");
    mcp_log_info("  Max Connections: %d", server_config.max_connections);
    mcp_log_info("  Keep Alive: %s", server_config.keep_alive ? "true" : "false");
    if (server_config.keep_alive) {
        mcp_log_info("  Keep Alive Timeout: %d ms", server_config.keep_alive_timeout);
    }
    mcp_log_info("  Connection Timeout: %d ms", server_config.http_config.timeout_ms);

    // Log SSE settings
    mcp_log_info("SSE Settings:");
    mcp_log_info("  Max SSE Clients: %d", server_config.max_sse_clients);
    mcp_log_info("  Max SSE Events: %d", server_config.max_sse_events);
    mcp_log_info("  SSE Event TTL: %d seconds", server_config.sse_event_ttl);

    // Log rate limiting settings
    mcp_log_info("Rate Limiting:");
    mcp_log_info("  Rate Limiting Enabled: %s", server_config.enable_rate_limiting ? "true" : "false");
    if (server_config.enable_rate_limiting) {
        mcp_log_info("  Rate Limit Requests: %d", server_config.rate_limit_requests);
        mcp_log_info("  Rate Limit Window: %d seconds", server_config.rate_limit_window);
        mcp_log_info("  Rate Limit By IP: %s", server_config.rate_limit_by_ip ? "true" : "false");
    }

    // Check if static files exist in the document root
    if (server_config.http_config.doc_root) {
        char path[512];

        // Check index.html
        snprintf(path, sizeof(path), "%s/index.html", server_config.http_config.doc_root);
        mcp_log_info("Checking if index.html exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check styles.css
        snprintf(path, sizeof(path), "%s/styles.css", server_config.http_config.doc_root);
        mcp_log_info("Checking if styles.css exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check sse_test.html
        snprintf(path, sizeof(path), "%s/sse_test.html", server_config.http_config.doc_root);
        mcp_log_info("Checking if sse_test.html exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check sse_test.css
        snprintf(path, sizeof(path), "%s/sse_test.css", server_config.http_config.doc_root);
        mcp_log_info("Checking if sse_test.css exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");

        // Check sse_test.js
        snprintf(path, sizeof(path), "%s/sse_test.js", server_config.http_config.doc_root);
        mcp_log_info("Checking if sse_test.js exists: %s - %s", path, http_file_exists(path) ? "YES" : "NO");
    }

    // Check if we need to create static files (only if doc_root is ".")
    if (server_config.http_config.doc_root && strcmp(server_config.http_config.doc_root, ".") == 0) {
        mcp_log_info("Document root is current directory, creating static files...");
        http_create_index_html("index.html", server_config.http_config.host, server_config.http_config.port);
        http_create_styles_css("styles.css");
        http_create_sse_test_css("sse_test.css");
        http_create_sse_test_js("sse_test.js");
        http_create_sse_test_html("sse_test.html");
    }

    // Store the doc_root
    g_doc_root = mcp_strdup(server_config.http_config.doc_root);

    // Create HTTP transport
    g_transport = mcp_transport_http_create(&server_config.http_config);
    if (!g_transport) {
        http_server_handle_error(HTTP_SERVER_ERROR_TRANSPORT_CREATION, "Failed to create HTTP transport", &server_config);
        return 1;
    }

    // These functions are not yet implemented in the transport layer
    // We'll comment them out for now and implement them when they're available

    /*
    // Configure SSE limits if supported
    if (mcp_http_transport_set_sse_limits(g_transport, server_config.max_sse_clients, server_config.max_sse_events) != 0) {
        mcp_log_warn("Failed to set SSE limits, using defaults");
    } else {
        mcp_log_info("SSE limits set: max_clients=%d, max_events=%d",
                    server_config.max_sse_clients, server_config.max_sse_events);
    }

    // Configure CORS if enabled
    if (server_config.enable_cors) {
        if (mcp_http_transport_set_cors(g_transport,
                                       server_config.cors_allow_origin,
                                       server_config.cors_allow_methods,
                                       server_config.cors_allow_headers,
                                       server_config.cors_max_age) != 0) {
            mcp_log_warn("Failed to set CORS headers, CORS will not be enabled");
        } else {
            mcp_log_info("CORS headers configured successfully");
        }
    }

    // Configure CSP if enabled
    if (server_config.enable_csp) {
        if (mcp_http_transport_set_csp(g_transport, server_config.csp_policy) != 0) {
            mcp_log_warn("Failed to set Content Security Policy, CSP will not be enabled");
        } else {
            mcp_log_info("Content Security Policy configured successfully");
        }
    }

    // Configure cache control if enabled
    if (server_config.enable_cache_control) {
        if (mcp_http_transport_set_cache_control(g_transport,
                                               server_config.cache_max_age,
                                               server_config.cache_public) != 0) {
            mcp_log_warn("Failed to set cache control headers, caching will use defaults");
        } else {
            mcp_log_info("Cache control configured: max_age=%d, public=%s",
                        server_config.cache_max_age,
                        server_config.cache_public ? "true" : "false");
        }
    }

    // Configure directory listing if enabled
    if (server_config.enable_directory_listing) {
        if (mcp_http_transport_enable_directory_listing(g_transport) != 0) {
            mcp_log_warn("Failed to enable directory listing");
        } else {
            mcp_log_info("Directory listing enabled");
        }
    }
    */

    // Log configuration settings
    mcp_log_info("Server configuration:");
    mcp_log_info("- SSE: max_clients=%d, max_events=%d",
                server_config.max_sse_clients, server_config.max_sse_events);

    if (server_config.enable_cors) {
        mcp_log_info("- CORS: enabled with origin '%s'", server_config.cors_allow_origin);
    } else {
        mcp_log_info("- CORS: disabled");
    }

    if (server_config.enable_csp) {
        mcp_log_info("- CSP: enabled with policy '%s'", server_config.csp_policy);
    } else {
        mcp_log_info("- CSP: disabled");
    }

    if (server_config.enable_cache_control) {
        mcp_log_info("- Cache control: enabled (max_age=%d, public=%s)",
                    server_config.cache_max_age,
                    server_config.cache_public ? "true" : "false");
    } else {
        mcp_log_info("- Cache control: disabled");
    }

    if (server_config.enable_directory_listing) {
        mcp_log_info("- Directory listing: enabled");
    } else {
        mcp_log_info("- Directory listing: disabled");
    }

    // Note: These features will be implemented in future versions
    mcp_log_info("Note: Advanced HTTP features (CORS, CSP, etc.) will be implemented in future versions");

    // These functions are not yet implemented in the transport layer
    // We'll comment them out for now and implement them when they're available

    /*
    // Configure MIME types
    if (mcp_http_transport_set_default_mime_type(g_transport, server_config.default_mime_type) != 0) {
        mcp_log_warn("Failed to set default MIME type, using transport default");
    } else {
        mcp_log_info("Default MIME type set to: %s", server_config.default_mime_type);
    }

    // Register MIME type handler
    if (mcp_http_transport_set_mime_type_handler(g_transport, get_mime_type, server_config.default_mime_type) != 0) {
        mcp_log_warn("Failed to set MIME type handler, using transport default");
    } else {
        mcp_log_info("Custom MIME type handler registered");
    }

    // Register HTTP header handler
    if (mcp_http_transport_set_header_handler(g_transport, http_header_handler, &server_config) != 0) {
        mcp_log_warn("Failed to set HTTP header handler, using transport default");
    } else {
        mcp_log_info("HTTP header handler registered");
    }
    */

    // Log that we're using default MIME type handling
    mcp_log_info("Using default MIME type handling");

    // We'll use the get_mime_type function directly in our code when needed

    // Start server
    printf("Starting HTTP server on %s:%d\n", server_config.http_config.host, server_config.http_config.port);
    printf("- Tool calls: http://%s:%d/call_tool\n", server_config.http_config.host, server_config.http_config.port);
    printf("- SSE events: http://%s:%d/events\n", server_config.http_config.host, server_config.http_config.port);

    if (mcp_server_start(g_server, g_transport) != 0) {
        http_server_handle_error(HTTP_SERVER_ERROR_SERVER_START, "Failed to start server", &server_config);
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

    // Free configuration resources
    free_http_server_config(&server_config);

    printf("Server shutdown complete\n");
    mcp_log_close();
    return 0;
}
