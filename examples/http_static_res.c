/**
 * @file http_static_res.c
 * @brief Implementation of functions for creating static resource files for the HTTP server
 */

#include "http_static_res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mcp_log.h"

/**
 * @brief Check if a file exists
 *
 * @param path Path to the file
 * @return int 1 if the file exists, 0 otherwise
 */
int http_file_exists(const char* path) {
    FILE* file = fopen(path, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

/**
 * @brief Create a simple index.html file
 *
 * @param index_html Path to the index.html file
 * @param host Host name or IP address
 * @param port Port number
 */
void http_create_index_html(const char* index_html, const char *host, uint16_t port) {
    FILE* f = fopen(index_html, "w");
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
        mcp_log_info("Created a simple %s file in the current directory", index_html);
    }
    else {
        mcp_log_error("Failed to create %s file in the current directory", index_html);
    }
}

/**
 * @brief Create a styles.css file
 *
 * @param styles_css Path to the styles.css file
 */
void http_create_styles_css(const char* styles_css) {
    FILE* css_file = fopen(styles_css, "w");
    if (css_file) {
        fprintf(css_file, "body {\n");
        fprintf(css_file, "    font-family: Arial, sans-serif;\n");
        fprintf(css_file, "    margin: 0;\n");
        fprintf(css_file, "    padding: 0;\n");
        fprintf(css_file, "    line-height: 1.6;\n");
        fprintf(css_file, "    color: #333;\n");
        fprintf(css_file, "    background-color: #f5f5f5;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".container {\n");
        fprintf(css_file, "    max-width: 1000px;\n");
        fprintf(css_file, "    margin: 0 auto;\n");
        fprintf(css_file, "    padding: 20px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "h1, h2, h3 {\n");
        fprintf(css_file, "    color: #333;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "h1 {\n");
        fprintf(css_file, "    border-bottom: 2px solid #4CAF50;\n");
        fprintf(css_file, "    padding-bottom: 10px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "pre {\n");
        fprintf(css_file, "    background-color: #f0f0f0;\n");
        fprintf(css_file, "    padding: 15px;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "    overflow-x: auto;\n");
        fprintf(css_file, "    border-left: 4px solid #4CAF50;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".endpoint {\n");
        fprintf(css_file, "    background-color: white;\n");
        fprintf(css_file, "    padding: 20px;\n");
        fprintf(css_file, "    margin: 20px 0;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".endpoint h2 {\n");
        fprintf(css_file, "    margin-top: 0;\n");
        fprintf(css_file, "    color: #4CAF50;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "a {\n");
        fprintf(css_file, "    color: #0066cc;\n");
        fprintf(css_file, "    text-decoration: none;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "a:hover {\n");
        fprintf(css_file, "    text-decoration: underline;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "code {\n");
        fprintf(css_file, "    background-color: #f0f0f0;\n");
        fprintf(css_file, "    padding: 2px 4px;\n");
        fprintf(css_file, "    border-radius: 3px;\n");
        fprintf(css_file, "    font-family: monospace;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "footer {\n");
        fprintf(css_file, "    text-align: center;\n");
        fprintf(css_file, "    margin-top: 40px;\n");
        fprintf(css_file, "    padding: 20px;\n");
        fprintf(css_file, "    background-color: #333;\n");
        fprintf(css_file, "    color: white;\n");
        fprintf(css_file, "}\n");
        fclose(css_file);
        mcp_log_info("Created %s file in the current directory", styles_css);
    }
    else {
        mcp_log_error("Failed to create %s file in the current directory", styles_css);
    }
}

/**
 * @brief Create a CSS file for SSE test
 *
 * @param sse_test_css Path to the sse_test.css file
 */
void http_create_sse_test_css(const char* sse_test_css) {
    FILE* css_file = fopen(sse_test_css, "w");
    if (css_file) {
        fprintf(css_file, "body {\n");
        fprintf(css_file, "    font-family: Arial, sans-serif;\n");
        fprintf(css_file, "    max-width: 800px;\n");
        fprintf(css_file, "    margin: 0 auto;\n");
        fprintf(css_file, "    padding: 20px;\n");
        fprintf(css_file, "    background-color: #f5f5f5;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "h1, h2 {\n");
        fprintf(css_file, "    color: #333;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".status-bar {\n");
        fprintf(css_file, "    display: flex;\n");
        fprintf(css_file, "    justify-content: space-between;\n");
        fprintf(css_file, "    margin-bottom: 10px;\n");
        fprintf(css_file, "    padding: 10px;\n");
        fprintf(css_file, "    background-color: #eee;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".controls {\n");
        fprintf(css_file, "    margin: 10px 0;\n");
        fprintf(css_file, "    padding: 10px;\n");
        fprintf(css_file, "    background-color: #eee;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "#events {\n");
        fprintf(css_file, "    height: 300px;\n");
        fprintf(css_file, "    overflow-y: auto;\n");
        fprintf(css_file, "    border: 1px solid #ccc;\n");
        fprintf(css_file, "    padding: 10px;\n");
        fprintf(css_file, "    background-color: white;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "    margin-bottom: 10px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event {\n");
        fprintf(css_file, "    margin-bottom: 5px;\n");
        fprintf(css_file, "    padding: 5px;\n");
        fprintf(css_file, "    border-bottom: 1px solid #eee;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event.info {\n");
        fprintf(css_file, "    color: #0066cc;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event.error {\n");
        fprintf(css_file, "    color: #cc0000;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event.echo {\n");
        fprintf(css_file, "    color: #006600;\n");
        fprintf(css_file, "    background-color: #e6f7ff;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event.reverse {\n");
        fprintf(css_file, "    color: #660066;\n");
        fprintf(css_file, "    background-color: #fff7e6;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event.tool_call {\n");
        fprintf(css_file, "    color: #0066cc;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, ".event.tool_result {\n");
        fprintf(css_file, "    color: #006600;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "button {\n");
        fprintf(css_file, "    padding: 8px 16px;\n");
        fprintf(css_file, "    margin-right: 10px;\n");
        fprintf(css_file, "    background-color: #4CAF50;\n");
        fprintf(css_file, "    color: white;\n");
        fprintf(css_file, "    border: none;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "    cursor: pointer;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "button:hover {\n");
        fprintf(css_file, "    background-color: #45a049;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "input, select {\n");
        fprintf(css_file, "    padding: 8px;\n");
        fprintf(css_file, "    margin-right: 5px;\n");
        fprintf(css_file, "    border: 1px solid #ccc;\n");
        fprintf(css_file, "    border-radius: 4px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "input[type=\"text\"] {\n");
        fprintf(css_file, "    width: 300px;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "#connection-status.connected {\n");
        fprintf(css_file, "    color: green;\n");
        fprintf(css_file, "    font-weight: bold;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "#connection-status.disconnected {\n");
        fprintf(css_file, "    color: red;\n");
        fprintf(css_file, "    font-weight: bold;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "#connection-status.reconnecting {\n");
        fprintf(css_file, "    color: orange;\n");
        fprintf(css_file, "    font-weight: bold;\n");
        fprintf(css_file, "}\n");
        fprintf(css_file, "\n");
        fprintf(css_file, "#connection-status.connecting {\n");
        fprintf(css_file, "    color: blue;\n");
        fprintf(css_file, "    font-weight: bold;\n");
        fprintf(css_file, "}\n");
        fclose(css_file);
        mcp_log_info("Created %s file in the current directory", sse_test_css);
    }
    else {
        mcp_log_error("Failed to create %s file in the current directory", sse_test_css);
    }
}

/**
 * @brief Create a JavaScript file for SSE test
 *
 * @param sse_test_js Path to the sse_test.js file
 */
void http_create_sse_test_js(const char *sse_test_js) {
    FILE* js_file = fopen(sse_test_js, "w");
    if (js_file) {
        fprintf(js_file, "// Function to generate a random session ID\n");
        fprintf(js_file, "function generateRandomSessionId() {\n");
        fprintf(js_file, "    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';\n");
        fprintf(js_file, "    let result = '';\n");
        fprintf(js_file, "    for (let i = 0; i < 10; i++) {\n");
        fprintf(js_file, "        result += chars.charAt(Math.floor(Math.random() * chars.length));\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    return result;\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Function to add an event to the events div\n");
        fprintf(js_file, "function addEvent(type, data, eventId = null) {\n");
        fprintf(js_file, "    const eventsDiv = document.getElementById('events');\n");
        fprintf(js_file, "    const eventDiv = document.createElement('div');\n");
        fprintf(js_file, "    eventDiv.className = `event ${type}`;\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    const now = new Date();\n");
        fprintf(js_file, "    const timestamp = now.toLocaleTimeString();\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Update last event ID if provided\n");
        fprintf(js_file, "    if (eventId) {\n");
        fprintf(js_file, "        window.lastEventId = eventId;\n");
        fprintf(js_file, "        const lastEventIdSpan = document.getElementById('last-event-id');\n");
        fprintf(js_file, "        if (lastEventIdSpan) {\n");
        fprintf(js_file, "            lastEventIdSpan.textContent = eventId;\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    let jsonData;\n");
        fprintf(js_file, "    try {\n");
        fprintf(js_file, "        jsonData = JSON.parse(data);\n");
        fprintf(js_file, "        let content = `[${timestamp}]`;\n");
        fprintf(js_file, "        if (eventId) {\n");
        fprintf(js_file, "            content += ` [ID:${eventId}]`;\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "        content += ` ${type}: ${jsonData.text}`;\n");
        fprintf(js_file, "        eventDiv.textContent = content;\n");
        fprintf(js_file, "    } catch (e) {\n");
        fprintf(js_file, "        let content = `[${timestamp}]`;\n");
        fprintf(js_file, "        if (eventId) {\n");
        fprintf(js_file, "            content += ` [ID:${eventId}]`;\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "        content += ` ${type}: ${data}`;\n");
        fprintf(js_file, "        eventDiv.textContent = content;\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventsDiv.appendChild(eventDiv);\n");
        fprintf(js_file, "    eventsDiv.scrollTop = eventsDiv.scrollHeight;\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Set up SSE connection\n");
        fprintf(js_file, "let eventSource;\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "function connectSSE(filter = null) {\n");
        fprintf(js_file, "    // Close existing connection if any\n");
        fprintf(js_file, "    if (eventSource) {\n");
        fprintf(js_file, "        eventSource.close();\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Build the URL with parameters\n");
        fprintf(js_file, "    let url = '/events';\n");
        fprintf(js_file, "    let params = [];\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Add filter if specified\n");
        fprintf(js_file, "    if (filter) {\n");
        fprintf(js_file, "        params.push(`filter=${encodeURIComponent(filter)}`);\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Add last event ID if available for reconnection\n");
        fprintf(js_file, "    if (window.lastEventId) {\n");
        fprintf(js_file, "        params.push(`lastEventId=${encodeURIComponent(window.lastEventId)}`);\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Add session ID if specified\n");
        fprintf(js_file, "    const sessionId = document.getElementById('session-id').value.trim();\n");
        fprintf(js_file, "    if (sessionId) {\n");
        fprintf(js_file, "        params.push(`session_id=${encodeURIComponent(sessionId)}`);\n");
        fprintf(js_file, "        console.log(`Connecting to SSE with session_id: ${sessionId}`);\n");
        fprintf(js_file, "    } else {\n");
        fprintf(js_file, "        console.log('Connecting to SSE without session_id');\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Append parameters to URL\n");
        fprintf(js_file, "    if (params.length > 0) {\n");
        fprintf(js_file, "        url += '?' + params.join('&');\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Create new EventSource with the URL\n");
        fprintf(js_file, "    eventSource = new EventSource(url);\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Update connection status\n");
        fprintf(js_file, "    const statusSpan = document.getElementById('connection-status');\n");
        fprintf(js_file, "    if (statusSpan) {\n");
        fprintf(js_file, "        statusSpan.textContent = 'Connecting...';\n");
        fprintf(js_file, "        statusSpan.className = 'connecting';\n");
        fprintf(js_file, "    }\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.onopen = function() {\n");
        fprintf(js_file, "        addEvent('info', 'Connected to SSE stream');\n");
        fprintf(js_file, "        if (statusSpan) {\n");
        fprintf(js_file, "            statusSpan.textContent = 'Connected';\n");
        fprintf(js_file, "            statusSpan.className = 'connected';\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "    };\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.onerror = function(error) {\n");
        fprintf(js_file, "        addEvent('error', 'SSE connection error, reconnecting...');\n");
        fprintf(js_file, "        if (statusSpan) {\n");
        fprintf(js_file, "            statusSpan.textContent = 'Reconnecting...';\n");
        fprintf(js_file, "            statusSpan.className = 'reconnecting';\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "        // The browser will automatically try to reconnect\n");
        fprintf(js_file, "    };\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.onmessage = function(event) {\n");
        fprintf(js_file, "        addEvent('message', event.data, event.lastEventId);\n");
        fprintf(js_file, "    };\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Listen for specific event types\n");
        fprintf(js_file, "    eventSource.addEventListener('echo', function(event) {\n");
        fprintf(js_file, "        addEvent('echo', event.data, event.lastEventId);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.addEventListener('reverse', function(event) {\n");
        fprintf(js_file, "        addEvent('reverse', event.data, event.lastEventId);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.addEventListener('tool_call', function(event) {\n");
        fprintf(js_file, "        addEvent('tool_call', event.data, event.lastEventId);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    eventSource.addEventListener('tool_result', function(event) {\n");
        fprintf(js_file, "        addEvent('tool_result', event.data, event.lastEventId);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Set up button click handlers\n");
        fprintf(js_file, "function setupButtons() {\n");
        fprintf(js_file, "    // Add handler for session ID changes\n");
        fprintf(js_file, "    document.getElementById('session-id').addEventListener('change', function() {\n");
        fprintf(js_file, "        // Notify user that they need to reconnect for the session ID to take effect\n");
        fprintf(js_file, "        addEvent('info', 'Session ID changed. Click \"Reconnect\" to apply the new session ID.');\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Add handler for generate session ID button\n");
        fprintf(js_file, "    document.getElementById('generate-session-id-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        document.getElementById('session-id').value = generateRandomSessionId();\n");
        fprintf(js_file, "        // Notify user that they need to reconnect for the session ID to take effect\n");
        fprintf(js_file, "        addEvent('info', 'Session ID generated. Click \"Reconnect\" to apply the new session ID.');\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Add handler for reconnect button\n");
        fprintf(js_file, "    document.getElementById('reconnect-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const filter = document.getElementById('event-filter').value;\n");
        fprintf(js_file, "        connectSSE(filter);\n");
        fprintf(js_file, "        addEvent('info', 'Reconnected with current filter and session ID settings.');\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Connect button\n");
        fprintf(js_file, "    document.getElementById('connect-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const filter = document.getElementById('event-filter').value;\n");
        fprintf(js_file, "        connectSSE(filter);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Disconnect button\n");
        fprintf(js_file, "    document.getElementById('disconnect-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        if (eventSource) {\n");
        fprintf(js_file, "            eventSource.close();\n");
        fprintf(js_file, "            eventSource = null;\n");
        fprintf(js_file, "            addEvent('info', 'Disconnected from SSE stream');\n");
        fprintf(js_file, "            \n");
        fprintf(js_file, "            const statusSpan = document.getElementById('connection-status');\n");
        fprintf(js_file, "            if (statusSpan) {\n");
        fprintf(js_file, "                statusSpan.textContent = 'Disconnected';\n");
        fprintf(js_file, "                statusSpan.className = 'disconnected';\n");
        fprintf(js_file, "            }\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Apply filter button\n");
        fprintf(js_file, "    document.getElementById('apply-filter-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const filter = document.getElementById('event-filter').value;\n");
        fprintf(js_file, "        connectSSE(filter);\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "    \n");
        fprintf(js_file, "    // Echo button\n");
        fprintf(js_file, "    document.getElementById('echo-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const text = document.getElementById('text-input').value;\n");
        fprintf(js_file, "        const sessionId = document.getElementById('session-id').value.trim();\n");
        fprintf(js_file, "        \n");
        fprintf(js_file, "        // Prepare arguments object\n");
        fprintf(js_file, "        const args = { text: text };\n");
        fprintf(js_file, "        \n");
        fprintf(js_file, "        // Add session_id if available\n");
        fprintf(js_file, "        if (sessionId) {\n");
        fprintf(js_file, "            args.session_id = sessionId;\n");
        fprintf(js_file, "            console.log(`Calling echo tool with session_id: ${sessionId}`);\n");
        fprintf(js_file, "        } else {\n");
        fprintf(js_file, "            console.log('Calling echo tool without session_id');\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "        \n");
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
        fprintf(js_file, "                    arguments: args\n");
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
        fprintf(js_file, "    // Reverse button\n");
        fprintf(js_file, "    document.getElementById('reverse-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        const text = document.getElementById('text-input').value;\n");
        fprintf(js_file, "        const sessionId = document.getElementById('session-id').value.trim();\n");
        fprintf(js_file, "        \n");
        fprintf(js_file, "        // Prepare arguments object\n");
        fprintf(js_file, "        const args = { text: text };\n");
        fprintf(js_file, "        \n");
        fprintf(js_file, "        // Add session_id if available\n");
        fprintf(js_file, "        if (sessionId) {\n");
        fprintf(js_file, "            args.session_id = sessionId;\n");
        fprintf(js_file, "            console.log(`Calling reverse tool with session_id: ${sessionId}`);\n");
        fprintf(js_file, "        } else {\n");
        fprintf(js_file, "            console.log('Calling reverse tool without session_id');\n");
        fprintf(js_file, "        }\n");
        fprintf(js_file, "        \n");
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
        fprintf(js_file, "                    arguments: args\n");
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
        fprintf(js_file, "    // Clear events button\n");
        fprintf(js_file, "    document.getElementById('clear-btn').addEventListener('click', function() {\n");
        fprintf(js_file, "        document.getElementById('events').innerHTML = '';\n");
        fprintf(js_file, "    });\n");
        fprintf(js_file, "}\n");
        fprintf(js_file, "\n");
        fprintf(js_file, "// Initialize when the page loads\n");
        fprintf(js_file, "document.addEventListener('DOMContentLoaded', function() {\n");
        fprintf(js_file, "    setupButtons();\n");
        fprintf(js_file, "    connectSSE(); // Connect automatically on page load\n");
        fprintf(js_file, "});\n");
        fclose(js_file);
        mcp_log_info("Created %s file in the current directory", sse_test_js);
    }
    else {
        mcp_log_error("Failed to create %s file in the current directory", sse_test_js);
    }
}

/**
 * @brief Create an HTML file for SSE test
 *
 * @param sse_test_html Path to the sse_test.html file
 */
void http_create_sse_test_html(const char* sse_test_html) {
    FILE* html_file = fopen(sse_test_html, "w");
    if (html_file) {
        fprintf(html_file, "<!DOCTYPE html>\n");
        fprintf(html_file, "<html>\n");
        fprintf(html_file, "<head>\n");
        fprintf(html_file, "    <title>Server-Sent Events Test</title>\n");
        fprintf(html_file, "    <link rel=\"stylesheet\" href=\"sse_test.css\">\n");
        fprintf(html_file, "    <script src=\"sse_test.js\"></script>\n");
        fprintf(html_file, "</head>\n");
        fprintf(html_file, "<body>\n");
        fprintf(html_file, "    <h1>Server-Sent Events (SSE) Test</h1>\n");
        fprintf(html_file, "    <p>This page demonstrates the use of Server-Sent Events (SSE) to receive real-time updates from the server.</p>\n");
        fprintf(html_file, "    \n");
        fprintf(html_file, "    <div class=\"status-bar\">\n");
        fprintf(html_file, "        <div>\n");
        fprintf(html_file, "            <strong>Connection Status:</strong> <span id=\"connection-status\" class=\"disconnected\">Disconnected</span>\n");
        fprintf(html_file, "        </div>\n");
        fprintf(html_file, "        <div>\n");
        fprintf(html_file, "            <strong>Last Event ID:</strong> <span id=\"last-event-id\">None</span>\n");
        fprintf(html_file, "        </div>\n");
        fprintf(html_file, "    </div>\n");
        fprintf(html_file, "    \n");
        fprintf(html_file, "    <div class=\"controls\">\n");
        fprintf(html_file, "        <div>\n");
        fprintf(html_file, "            <button id=\"connect-btn\">Connect</button>\n");
        fprintf(html_file, "            <button id=\"disconnect-btn\">Disconnect</button>\n");
        fprintf(html_file, "            <button id=\"reconnect-btn\">Reconnect</button>\n");
        fprintf(html_file, "            <button id=\"clear-btn\">Clear Events</button>\n");
        fprintf(html_file, "        </div>\n");
        fprintf(html_file, "        <div style=\"margin-top: 10px;\">\n");
        fprintf(html_file, "            <label for=\"session-id\">Session ID:</label>\n");
        fprintf(html_file, "            <input type=\"text\" id=\"session-id\" placeholder=\"Enter session ID (optional)\">\n");
        fprintf(html_file, "            <button id=\"generate-session-id-btn\">Generate Random ID</button>\n");
        fprintf(html_file, "        </div>\n");
        fprintf(html_file, "        <div style=\"margin-top: 10px;\">\n");
        fprintf(html_file, "            <input type=\"text\" id=\"event-filter\" placeholder=\"Event filter (e.g., echo,reverse)\">\n");
        fprintf(html_file, "            <button id=\"apply-filter-btn\">Apply Filter</button>\n");
        fprintf(html_file, "        </div>\n");
        fprintf(html_file, "        <div style=\"margin-top: 10px;\">\n");
        fprintf(html_file, "            <input type=\"text\" id=\"text-input\" placeholder=\"Enter text to send\" value=\"Hello, MCP Server!\">\n");
        fprintf(html_file, "            <button id=\"echo-btn\">Echo</button>\n");
        fprintf(html_file, "            <button id=\"reverse-btn\">Reverse</button>\n");
        fprintf(html_file, "        </div>\n");
        fprintf(html_file, "    </div>\n");
        fprintf(html_file, "    \n");
        fprintf(html_file, "    <h2>Events</h2>\n");
        fprintf(html_file, "    <div id=\"events\"></div>\n");
        fprintf(html_file, "    \n");
        fprintf(html_file, "    <h2>API Documentation</h2>\n");
        fprintf(html_file, "    <div class=\"endpoint\">\n");
        fprintf(html_file, "        <h3>SSE Endpoint</h3>\n");
        fprintf(html_file, "        <p><code>GET /events</code></p>\n");
        fprintf(html_file, "        <p>Connect to this endpoint to receive Server-Sent Events.</p>\n");
        fprintf(html_file, "        <p>Optional query parameters:</p>\n");
        fprintf(html_file, "        <ul>\n");
        fprintf(html_file, "            <li><code>filter</code> - Comma-separated list of event types to receive (e.g., <code>echo,reverse</code>)</li>\n");
        fprintf(html_file, "            <li><code>lastEventId</code> - ID of the last event received, for reconnection</li>\n");
        fprintf(html_file, "            <li><code>session_id</code> - Optional session ID to identify this client for targeted events</li>\n");
        fprintf(html_file, "        </ul>\n");
        fprintf(html_file, "    </div>\n");
        fprintf(html_file, "    \n");
        fprintf(html_file, "    <div class=\"endpoint\">\n");
        fprintf(html_file, "        <h3>Tool Call Endpoint</h3>\n");
        fprintf(html_file, "        <p><code>POST /call_tool</code></p>\n");
        fprintf(html_file, "        <p>Call a tool on the server.</p>\n");
        fprintf(html_file, "        <p>Request body example:</p>\n");
        fprintf(html_file, "        <pre>{\n");
        fprintf(html_file, "  \"jsonrpc\": \"2.0\",\n");
        fprintf(html_file, "  \"id\": 1,\n");
        fprintf(html_file, "  \"method\": \"call_tool\",\n");
        fprintf(html_file, "  \"params\": {\n");
        fprintf(html_file, "    \"name\": \"echo\",\n");
        fprintf(html_file, "    \"arguments\": {\n");
        fprintf(html_file, "      \"text\": \"Hello, MCP Server!\",\n");
        fprintf(html_file, "      \"session_id\": \"optional_session_id\"\n");
        fprintf(html_file, "    }\n");
        fprintf(html_file, "  }\n");
        fprintf(html_file, "}</pre>\n");
        fprintf(html_file, "    </div>\n");
        fprintf(html_file, "</body>\n");
        fprintf(html_file, "</html>\n");
        fclose(html_file);
        mcp_log_info("Created %s file in the current directory", sse_test_html);
    }
    else {
        mcp_log_error("Failed to create %s file in the current directory", sse_test_html);
    }
}
