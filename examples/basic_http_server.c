#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
typedef SOCKET socket_t;
#define SOCKET_ERROR_VAL INVALID_SOCKET
#define close_socket closesocket
#else
typedef int socket_t;
#define SOCKET_ERROR_VAL -1
#define close_socket close
#endif

static volatile int running = 1;

// Signal handler
static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    running = 0;

#ifdef _WIN32
    // On Windows, we need to use a more forceful approach for some signals
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

// Initialize socket library (Windows only)
static int init_socket_lib() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    return 0;
#endif
}

// Cleanup socket library (Windows only)
static void cleanup_socket_lib() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Create a simple HTTP response
static const char* create_http_response() {
    static char response[4096];
    const char* html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Basic HTTP Server</title>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>Basic HTTP Server</h1>\n"
        "    <p>This is a test page created by the basic HTTP server.</p>\n"
        "    <h2>Available Tools:</h2>\n"
        "    <ul>\n"
        "        <li><strong>echo</strong> - Echoes back the input text</li>\n"
        "        <li><strong>reverse</strong> - Reverses the input text</li>\n"
        "    </ul>\n"
        "    <h2>Tool Call Example:</h2>\n"
        "    <pre>curl -X POST http://127.0.0.1:8080/call_tool -H \"Content-Type: application/json\" -d \"{\\\"name\\\":\\\"echo\\\",\\\"params\\\":{\\\"text\\\":\\\"Hello, Server!\\\"}}\"</pre>\n"
        "</body>\n"
        "</html>\n";

    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        (int)strlen(html), html);

    return response;
}

// Simple JSON parser to extract values
static char* extract_json_value(const char* json, const char* key) {
    static char value[1024];
    char search_key[256];
    const char* start, *end;

    // Format the key to search for
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    // Find the key
    start = strstr(json, search_key);
    if (!start) return NULL;

    // Move past the key and colon
    start += strlen(search_key);

    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;

    // Check if value is a string
    if (*start == '"') {
        start++; // Skip opening quote
        end = strchr(start, '"');
        if (!end) return NULL;

        // Copy the value
        size_t len = end - start;
        if (len >= sizeof(value)) len = sizeof(value) - 1;
        strncpy(value, start, len);
        value[len] = '\0';
    } else {
        // Value is not a string (number, boolean, null, object, array)
        // For simplicity, we'll just find the end of the value
        end = strpbrk(start, ",}]");
        if (!end) return NULL;

        // Copy the value
        size_t len = end - start;
        if (len >= sizeof(value)) len = sizeof(value) - 1;
        strncpy(value, start, len);
        value[len] = '\0';
    }

    return value;
}

// Extract nested JSON value
static char* extract_nested_json_value(const char* json, const char* parent_key, const char* child_key) {
    static char nested_json[4096];
    char search_key[256];
    const char* start, *end;
    int brace_count = 0;

    // Format the key to search for
    snprintf(search_key, sizeof(search_key), "\"%s\":", parent_key);

    // Find the key
    start = strstr(json, search_key);
    if (!start) return NULL;

    // Move past the key and colon
    start += strlen(search_key);

    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;

    // Check if value is an object
    if (*start != '{') return NULL;

    // Extract the object
    start++; // Skip opening brace
    brace_count = 1;
    end = start;

    while (brace_count > 0 && *end) {
        if (*end == '{') brace_count++;
        else if (*end == '}') brace_count--;
        end++;
    }

    if (brace_count != 0) return NULL;

    // Copy the object
    size_t len = end - start - 1; // -1 to exclude the closing brace
    if (len >= sizeof(nested_json)) len = sizeof(nested_json) - 1;
    strncpy(nested_json, start, len);
    nested_json[len] = '\0';

    // Now extract the child value from the nested JSON
    return extract_json_value(nested_json, child_key);
}

// Handle tool calls
static const char* handle_tool_call(const char* request_body) {
    static char response[4096];
    char* tool_name, *text;

    // Extract tool name
    tool_name = extract_json_value(request_body, "name");
    if (!tool_name) {
        sprintf(response,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"Missing tool name\"}",
            (int)strlen("{\"error\":\"Missing tool name\"}"));
        return response;
    }

    // Extract parameters
    if (strcmp(tool_name, "echo") == 0) {
        // Echo tool
        text = extract_nested_json_value(request_body, "params", "text");
        if (!text) {
            sprintf(response,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"error\":\"Missing text parameter\"}",
                (int)strlen("{\"error\":\"Missing text parameter\"}"));
            return response;
        }

        // Create JSON response
        char json_response[4096];
        sprintf(json_response, "{\"result\":\"%s\"}", text);

        sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(json_response), json_response);
    } else if (strcmp(tool_name, "reverse") == 0) {
        // Reverse tool
        text = extract_nested_json_value(request_body, "params", "text");
        if (!text) {
            sprintf(response,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"error\":\"Missing text parameter\"}",
                (int)strlen("{\"error\":\"Missing text parameter\"}"));
            return response;
        }

        // UTF-8 aware string reversal
        char reversed[1024];
        int len = (int)strlen(text);

        // First, count the number of UTF-8 characters
        int char_count = 0;
        int byte_pos = 0;

        // Array to store the byte positions of each character
        int char_positions[1024]; // Assuming max 1024 characters

        // Record the starting byte position of each character
        while (byte_pos < len) {
            char_positions[char_count++] = byte_pos;

            // Skip to the next UTF-8 character
            unsigned char c = (unsigned char)text[byte_pos];
            if (c < 0x80) {
                // ASCII character (1 byte)
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

        // Copy characters in reverse order
        int out_pos = 0;
        for (int i = char_count; i > 0; i--) {
            int char_start = char_positions[i-1];
            int char_len = char_positions[i] - char_positions[i-1];

            // Copy this character to the output
            memcpy(reversed + out_pos, text + char_start, char_len);
            out_pos += char_len;
        }

        // Null-terminate the result
        reversed[out_pos] = '\0';

        // Create JSON response
        char json_response[4096];
        sprintf(json_response, "{\"result\":\"%s\"}", reversed);

        sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            (int)strlen(json_response), json_response);
    } else {
        // Unknown tool
        sprintf(response,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"Unknown tool: %s\"}",
            (int)(strlen("{\"error\":\"Unknown tool: ") + strlen(tool_name)) + 2,
            tool_name);
    }

    return response;
}

// Handle a client connection
static void handle_client(socket_t client_sock) {
    char buffer[4096];
    int bytes_received;

    // Set socket to blocking mode for client communication
#ifdef _WIN32
    u_long mode = 0;  // 0 to disable non-blocking socket (i.e., make it blocking)
    ioctlsocket(client_sock, FIONBIO, &mode);
#else
    // For Unix systems, we would use fcntl to set blocking
    // int flags = fcntl(client_sock, F_GETFL, 0);
    // fcntl(client_sock, F_SETFL, flags & ~O_NONBLOCK);
#endif

    // Set receive timeout to avoid hanging
#ifdef _WIN32
    DWORD timeout = 5000; // 5 seconds
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 5;  // 5 seconds
    tv.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

    // Receive request
    bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Received request:\n%s\n", buffer);

        // Parse request to get method and path
        char method[16], path[256], version[16];
        sscanf(buffer, "%15s %255s %15s", method, path, version);
        printf("Method: %s, Path: %s, Version: %s\n", method, path, version);

        const char* response;

        // Check if this is a tool call
        if (strcmp(method, "POST") == 0 && strcmp(path, "/call_tool") == 0) {
            // Find the request body (after the double CRLF)
            const char* body = strstr(buffer, "\r\n\r\n");
            if (body) {
                body += 4; // Skip the double CRLF
                printf("Request body: %s\n", body);

                // Handle the tool call
                response = handle_tool_call(body);
            } else {
                // No request body
                response = "HTTP/1.1 400 Bad Request\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: 30\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "{\"error\":\"Missing request body\"}";
            }
        } else {
            // For all other requests, serve the HTML page
            response = create_http_response();
        }

        // Send response with error checking
        int bytes_sent = send(client_sock, response, (int)strlen(response), 0);
        if (bytes_sent < 0) {
            printf("Failed to send response: %d\n",
#ifdef _WIN32
                WSAGetLastError()
#else
                errno
#endif
            );
        } else {
            printf("Sent %d bytes\n", bytes_sent);
        }

        // Add a small delay to ensure the data is sent before closing
#ifdef _WIN32
        Sleep(100);  // 100 milliseconds
#else
        usleep(100000);  // 100 milliseconds
#endif
    } else {
        printf("Failed to receive request: %d\n",
#ifdef _WIN32
            WSAGetLastError()
#else
            errno
#endif
        );
    }

    // Shutdown the socket before closing
    shutdown(client_sock,
#ifdef _WIN32
        SD_BOTH
#else
        SHUT_RDWR
#endif
    );

    // Close connection
    close_socket(client_sock);
}

int main(int argc, char** argv) {
    socket_t server_sock;
    struct sockaddr_in server_addr;
    int port = 8080;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        }
    }

    // Set up signal handler
    signal(SIGINT, signal_handler);

    // Initialize socket library
    if (init_socket_lib() != 0) {
        printf("Failed to initialize socket library\n");
        return 1;
    }

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == SOCKET_ERROR_VAL) {
        printf("Failed to create socket\n");
        cleanup_socket_lib();
        return 1;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        printf("Failed to set SO_REUSEADDR option\n");
        close_socket(server_sock);
        cleanup_socket_lib();
        return 1;
    }

    // Set TCP_NODELAY option to disable Nagle's algorithm
    if (setsockopt(server_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt)) < 0) {
        printf("Failed to set TCP_NODELAY option\n");
        // Not critical, continue anyway
    }

    // Set linger option to ensure all data is sent before closing
    struct linger linger_opt;
    linger_opt.l_onoff = 1;    // Enable linger
    linger_opt.l_linger = 5;   // Linger for 5 seconds
    if (setsockopt(server_sock, SOL_SOCKET, SO_LINGER, (const char*)&linger_opt, sizeof(linger_opt)) < 0) {
        printf("Failed to set SO_LINGER option\n");
        // Not critical, continue anyway
    }

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((unsigned short)port);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Failed to bind socket\n");
        close_socket(server_sock);
        cleanup_socket_lib();
        return 1;
    }

    // Listen for connections
    if (listen(server_sock, 5) < 0) {
        printf("Failed to listen on socket\n");
        close_socket(server_sock);
        cleanup_socket_lib();
        return 1;
    }

    printf("Basic HTTP server started on port %d\n", port);
    printf("Press Ctrl+C to exit\n");

    // Main loop
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        socket_t client_sock;

        // Set socket to non-blocking mode for Windows
#ifdef _WIN32
        u_long mode = 1;  // 1 to enable non-blocking socket
        ioctlsocket(server_sock, FIONBIO, &mode);
#else
        // For Unix systems, we would use fcntl to set non-blocking
        // fcntl(server_sock, F_SETFL, fcntl(server_sock, F_GETFL, 0) | O_NONBLOCK);
#endif

        // Reset any error state
#ifdef _WIN32
        WSASetLastError(0);
#else
        errno = 0;
#endif

        // Accept connection with timeout
        fd_set read_fds;
        struct timeval tv;

        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        // Set timeout to 1 second
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        // Wait for activity on the socket
        int activity = select((int)server_sock + 1, &read_fds, NULL, NULL, &tv);

        if (activity < 0) {
            if (running) {
                printf("Select error\n");
            }
            continue;
        }

        // If timeout occurred, just continue the loop to check running flag
        if (activity == 0) {
            continue;
        }

        // Accept connection if there is one
        if (FD_ISSET(server_sock, &read_fds)) {
            client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock == SOCKET_ERROR_VAL) {
                if (running) {
                    printf("Failed to accept connection\n");
                }
                continue;
            }

            // Handle client
            handle_client(client_sock);
        }
    }

    // Clean up
    close_socket(server_sock);
    cleanup_socket_lib();

    printf("Server shutdown complete\n");
    return 0;
}
