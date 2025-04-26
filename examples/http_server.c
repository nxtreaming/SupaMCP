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

#ifdef _WIN32
// Windows.h is already included by win_socket_compat.h
#else
#include <unistd.h>
#endif

#include <mcp_server.h>
#include <mcp_transport.h>
#include <mcp_http_transport.h>
#include <mcp_log.h>
#include <mcp_json.h>
#include <mcp_string_utils.h>

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
    uint16_t port = 8280;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST         Host to bind to (default: 127.0.0.1)\n");
            printf("  --port PORT         Port to bind to (default: 8280)\n");
            printf("  --help              Show this help message\n");
            return 0;
        } else {
            printf("Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

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

    // Create HTTP transport configuration
    mcp_http_config_t http_config = {
        .host = host,
        .port = port,
        .use_ssl = false,
        .cert_path = NULL,
        .key_path = NULL,
        .doc_root = ".", // Use current directory
        .timeout_ms = 0 // No timeout
    };

    // Create a simple index.html file in the current directory
    FILE* f = fopen("index.html", "w");
    if (f) {
        fprintf(f, "<!DOCTYPE html>\n");
        fprintf(f, "<html>\n");
        fprintf(f, "<head>\n");
        fprintf(f, "    <title>MCP HTTP Server</title>\n");
        fprintf(f, "</head>\n");
        fprintf(f, "<body>\n");
        fprintf(f, "    <h1>MCP HTTP Server</h1>\n");
        fprintf(f, "    <p>This is a test page created by the MCP HTTP server.</p>\n");
        fprintf(f, "    <h2>Available Tools:</h2>\n");
        fprintf(f, "    <ul>\n");
        fprintf(f, "        <li><strong>echo</strong> - Echoes back the input text</li>\n");
        fprintf(f, "        <li><strong>reverse</strong> - Reverses the input text</li>\n");
        fprintf(f, "    </ul>\n");
        fprintf(f, "    <h2>Tool Call Example:</h2>\n");
        fprintf(f, "    <pre>curl -X POST http://%s:%d/call_tool -H \"Content-Type: application/json\" -d \"{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":1,\\\"method\\\":\\\"call_tool\\\",\\\"params\\\":{\\\"name\\\":\\\"echo\\\",\\\"arguments\\\":{\\\"text\\\":\\\"Hello, MCP Server!\\\"}}}\"</pre>\n", host, port);
        fprintf(f, "    <h2>SSE Test:</h2>\n");
        fprintf(f, "    <p><a href=\"sse_test.html\">Click here</a> to test Server-Sent Events (SSE)</p>\n");
        fprintf(f, "</body>\n");
        fprintf(f, "</html>\n");
        fclose(f);
        mcp_log_info("Created a simple index.html file in the current directory");
    } else {
        mcp_log_error("Failed to create index.html file in the current directory");
    }

    // Create CSS file for SSE test
    FILE* css_file = fopen("sse_test.css", "w");
    if (css_file) {
        fprintf(css_file, "body {\n");
        fprintf(css_file, "    font-family: Arial, sans-serif;\n");
        fprintf(css_file, "    max-width: 800px;\n");
        fprintf(css_file, "    margin: 0 auto;\n");
        fprintf(css_file, "    padding: 20px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "#events {\n");
        fprintf(css_file, "    border: 1px solid #ccc;\n");
        fprintf(css_file, "    padding: 10px;\n");
        fprintf(css_file, "    height: 300px;\n");
        fprintf(css_file, "    overflow-y: auto;\n");
        fprintf(css_file, "    margin-bottom: 20px;\n");
        fprintf(css_file, "    background-color: #f9f9f9;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, ".event {\n");
        fprintf(css_file, "    margin-bottom: 5px;\n");
        fprintf(css_file, "    padding: 5px;\n");
        fprintf(css_file, "    border-bottom: 1px solid #eee;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, ".echo {\n");
        fprintf(css_file, "    background-color: #e6f7ff;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, ".reverse {\n");
        fprintf(css_file, "    background-color: #fff7e6;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "button {\n");
        fprintf(css_file, "    padding: 8px 16px;\n");
        fprintf(css_file, "    margin-right: 10px;\n");
        fprintf(css_file, "    background-color: #4CAF50;\n");
        fprintf(css_file, "    color: white;\n");
        fprintf(css_file, "    border: none;\n");
        fprintf(css_file, "    cursor: pointer;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "button:hover {\n");
        fprintf(css_file, "    background-color: #45a049;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "input[type=\"text\"] {\n");
        fprintf(css_file, "    padding: 8px;\n");
        fprintf(css_file, "    width: 300px;\n");
        fprintf(css_file, "}\n");
        fclose(css_file);
        mcp_log_info("Created sse_test.css file in the current directory");
    } else {
        mcp_log_error("Failed to create sse_test.css file in the current directory");
    }

    // Create JavaScript file for SSE test
    FILE* js_file = fopen("sse_test.js", "w");
    if (js_file) {
        fprintf(js_file, "// Function to add an event to the events div\n");
        fprintf(js_file, "function addEvent(type, data) {\n");
        fprintf(js_file, "    const eventsDiv = document.getElementById('events');\n");
        fprintf(js_file, "    const eventDiv = document.createElement('div');\n");
        fprintf(js_file, "    eventDiv.className = `event ${type}`;\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    const now = new Date();\n");
        fprintf(js_file, "    const timestamp = now.toLocaleTimeString();\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    let jsonData;\n");
        fprintf(js_file, "    try {\n");
        fprintf(js_file, "        jsonData = JSON.parse(data);\n");
        fprintf(js_file, "        eventDiv.textContent = `[${timestamp}] ${type}: ${jsonData.text}`;\n");
        fprintf(js_file, "    } catch (e) {\n");
        fprintf(js_file, "        eventDiv.textContent = `[${timestamp}] ${type}: ${data}`;\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventsDiv.appendChild(eventDiv);\n");
        fprintf(js_file, "    eventsDiv.scrollTop = eventsDiv.scrollHeight;\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Set up SSE connection\n");
        fprintf(js_file, "let eventSource;\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "function connectSSE() {\n");
        fprintf(js_file, "    eventSource = new EventSource('/events');\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.onopen = function() {\n");
        fprintf(js_file, "        addEvent('info', 'Connected to SSE stream');\n");
        fprintf(js_file, "    };\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.onerror = function(error) {\n");
        fprintf(js_file, "        addEvent('error', 'SSE connection error, reconnecting...');\n");
        fprintf(js_file, "        // The browser will automatically try to reconnect\n");
        fprintf(js_file, "    };\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.onmessage = function(event) {\n");
        fprintf(js_file, "        addEvent('message', event.data);\n");
        fprintf(js_file, "    };\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Listen for specific event types\n");
        fprintf(js_file, "    eventSource.addEventListener('echo', function(event) {\n");
        fprintf(js_file, "        addEvent('echo', event.data);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.addEventListener('reverse', function(event) {\n");
        fprintf(js_file, "        addEvent('reverse', event.data);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Set up button click handlers\n");
        fprintf(js_file, "function setupButtons() {\n");
        fprintf(js_file, "    document.getElementById('echo-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const text = document.getElementById('text-input').value;\n");
        fprintf(js_file, "        fetch('/call_tool', {\n");
        fprintf(js_file, "            method: 'POST',\n");
        fprintf(js_file, "            headers: {\n");
        fprintf(js_file, "                'Content-Type': 'application/json'\n");
        fprintf(js_file, "            },\n");
        fprintf(js_file, "            body: JSON.stringify({\n");
        fprintf(js_file, "                jsonrpc: '2.0',\n");
        fprintf(js_file, "                id: 1,\n");
        fprintf(js_file, "                method: 'call_tool',\n");
        fprintf(js_file, "                params: {\n");
        fprintf(js_file, "                    name: 'echo',\n");
        fprintf(js_file, "                    arguments: {\n");
        fprintf(js_file, "                        text: text\n");
        fprintf(js_file, "                    }\n");
        fprintf(js_file, "                }\n");
        fprintf(js_file, "            })\n");
        fprintf(js_file, "        })\n");
        fprintf(js_file, "        .then(response => response.json())\n");
        fprintf(js_file, "        .then(data => {\n");
        fprintf(js_file, "            console.log('Echo response:', data);\n");
        fprintf(js_file, "        })\n");
        fprintf(js_file, "        .catch(error => {\n");
        fprintf(js_file, "            console.error('Error calling echo tool:', error);\n");
        fprintf(js_file, "            addEvent('error', 'Error calling echo tool: ' + error.message);\n");
        fprintf(js_file, "        });\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    document.getElementById('reverse-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const text = document.getElementById('text-input').value;\n");
        fprintf(js_file, "        fetch('/call_tool', {\n");
        fprintf(js_file, "            method: 'POST',\n");
        fprintf(js_file, "            headers: {\n");
        fprintf(js_file, "                'Content-Type': 'application/json'\n");
        fprintf(js_file, "            },\n");
        fprintf(js_file, "            body: JSON.stringify({\n");
        fprintf(js_file, "                jsonrpc: '2.0',\n");
        fprintf(js_file, "                id: 2,\n");
        fprintf(js_file, "                method: 'call_tool',\n");
        fprintf(js_file, "                params: {\n");
        fprintf(js_file, "                    name: 'reverse',\n");
        fprintf(js_file, "                    arguments: {\n");
        fprintf(js_file, "                        text: text\n");
        fprintf(js_file, "                    }\n");
        fprintf(js_file, "                }\n");
        fprintf(js_file, "            })\n");
        fprintf(js_file, "        })\n");
        fprintf(js_file, "        .then(response => response.json())\n");
        fprintf(js_file, "        .then(data => {\n");
        fprintf(js_file, "            console.log('Reverse response:', data);\n");
        fprintf(js_file, "        })\n");
        fprintf(js_file, "        .catch(error => {\n");
        fprintf(js_file, "            console.error('Error calling reverse tool:', error);\n");
        fprintf(js_file, "            addEvent('error', 'Error calling reverse tool: ' + error.message);\n");
        fprintf(js_file, "        });\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    document.getElementById('clear-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        document.getElementById('events').innerHTML = '';\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Add reload button handler\n");
        fprintf(js_file, "    document.getElementById('reload-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        // Clear cache and reload page\n");
        fprintf(js_file, "        window.location.reload(true);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Initialize when the page loads\n");
        fprintf(js_file, "document.addEventListener('DOMContentLoaded', function() {\n");
        fprintf(js_file, "    connectSSE();\n");
        fprintf(js_file, "    setupButtons();\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Clean up when the page is unloaded\n");
        fprintf(js_file, "    window.addEventListener('beforeunload', function() {\n");
        fprintf(js_file, "        if (eventSource) {\n");
        fprintf(js_file, "            eventSource.close();\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "});\n");
        fclose(js_file);
        mcp_log_info("Created sse_test.js file in the current directory");
    } else {
        mcp_log_error("Failed to create sse_test.js file in the current directory");
    }

    // Create sse_test.html file directly in the current directory
    FILE* sse_file = fopen("sse_test.html", "w");
    if (sse_file) {
        fprintf(sse_file, "<!DOCTYPE html>\n");
        fprintf(sse_file, "<html>\n");
        fprintf(sse_file, "<head>\n");
        fprintf(sse_file, "    <title>MCP Server SSE Test</title>\n");
        fprintf(sse_file, "    <meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'self'\">\n");
        fprintf(sse_file, "    <meta http-equiv=\"Cache-Control\" content=\"no-cache, no-store, must-revalidate\">\n");
        fprintf(sse_file, "    <meta http-equiv=\"Pragma\" content=\"no-cache\">\n");
        fprintf(sse_file, "    <meta http-equiv=\"Expires\" content=\"0\">\n");
        fprintf(sse_file, "    <link rel=\"stylesheet\" href=\"sse_test.css?v=%lu\">\n", (unsigned long)time(NULL));
        fprintf(sse_file, "</head>\n");
        fprintf(sse_file, "<body>\n");
        fprintf(sse_file, "    <h1>MCP Server SSE Test</h1>\n");
        fprintf(sse_file, "    \n");
        fprintf(sse_file, "    <div>\n");
        fprintf(sse_file, "        <h2>Server-Sent Events</h2>\n");
        fprintf(sse_file, "        <div id=\"events\"></div>\n");
        fprintf(sse_file, "        \n");
        fprintf(sse_file, "        <div>\n");
        fprintf(sse_file, "            <input type=\"text\" id=\"text-input\" placeholder=\"Enter text to echo or reverse\" value=\"Hello, MCP Server!\">\n");
        fprintf(sse_file, "            <button id=\"echo-btn\">Echo</button>\n");
        fprintf(sse_file, "            <button id=\"reverse-btn\">Reverse</button>\n");
        fprintf(sse_file, "            <button id=\"clear-btn\">Clear Events</button>\n");
        fprintf(sse_file, "            <button id=\"reload-btn\">Reload Page (Clear Cache)</button>\n");
        fprintf(sse_file, "        </div>\n");
        fprintf(sse_file, "    </div>\n");
        fprintf(sse_file, "    \n");
        fprintf(sse_file, "    <script src=\"sse_test.js?v=%lu\"></script>\n", (unsigned long)time(NULL));
        fprintf(sse_file, "</body>\n");
        fprintf(sse_file, "</html>\n");
        fclose(sse_file);
        mcp_log_info("Created sse_test.html file in the current directory");
    } else {
        mcp_log_error("Failed to create sse_test.html file in the current directory");
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
    printf("Starting HTTP server on %s:%d\n", host, port);
    printf("- Tool calls: http://%s:%d/call_tool\n", host, port);
    printf("- SSE events: http://%s:%d/events\n", host, port);

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
