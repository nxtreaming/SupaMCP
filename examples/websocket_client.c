#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#   define WIN32_LEAN_AND_MEAN  // Avoid Windows.h including Winsock.h
#   include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <locale.h>

#include "mcp_json_utils.h"
#include "mcp_client.h"
#include "mcp_transport_factory.h"
#include "mcp_websocket_transport.h"
#include "mcp_log.h"
#include "mcp_json_rpc.h"
#include "mcp_socket_utils.h"
#include "mcp_thread_local.h"

// Helper function to convert string from local encoding to UTF-8 on Windows
#ifdef _WIN32
static char* convert_to_utf8(const char* input) {
    if (!input) {
        return NULL;
    }

    // Get the required size for the wide character buffer
    int wide_size = MultiByteToWideChar(CP_ACP, 0, input, -1, NULL, 0);
    if (wide_size <= 0) {
        printf("Error: Failed to calculate wide character size\n");
        return _strdup(input); // Return a copy of the original string as fallback
    }

    // Allocate buffer for wide characters
    wchar_t* wide_buf = (wchar_t*)malloc(wide_size * sizeof(wchar_t));
    if (!wide_buf) {
        printf("Error: Failed to allocate memory for wide characters\n");
        return _strdup(input);
    }

    // Convert from local encoding to wide characters
    if (MultiByteToWideChar(CP_ACP, 0, input, -1, wide_buf, wide_size) <= 0) {
        printf("Error: Failed to convert to wide characters\n");
        free(wide_buf);
        return _strdup(input);
    }

    // Get the required size for the UTF-8 buffer
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_buf, -1, NULL, 0, NULL, NULL);
    if (utf8_size <= 0) {
        printf("Error: Failed to calculate UTF-8 size\n");
        free(wide_buf);
        return _strdup(input);
    }

    // Allocate buffer for UTF-8 characters
    char* utf8_buf = (char*)malloc(utf8_size);
    if (!utf8_buf) {
        printf("Error: Failed to allocate memory for UTF-8 characters\n");
        free(wide_buf);
        return _strdup(input);
    }

    // Convert from wide characters to UTF-8
    if (WideCharToMultiByte(CP_UTF8, 0, wide_buf, -1, utf8_buf, utf8_size, NULL, NULL) <= 0) {
        printf("Error: Failed to convert to UTF-8\n");
        free(wide_buf);
        free(utf8_buf);
        return _strdup(input);
    }

    // Clean up
    free(wide_buf);

    return utf8_buf;
}
#else
// On non-Windows platforms, just return a copy of the input
static char* convert_to_utf8(const char* input) {
    return strdup(input);
}
#endif

#ifdef _WIN32
// Helper function to convert string from UTF-8 back to local encoding on Windows
static char* convert_from_utf8(const char* utf8_input) {
    if (!utf8_input) {
        return NULL;
    }

    // Get the required size for the wide character buffer
    int wide_size = MultiByteToWideChar(CP_UTF8, 0, utf8_input, -1, NULL, 0);
    if (wide_size <= 0) {
        printf("Error: Failed to calculate wide character size for UTF-8 string\n");
        return _strdup(utf8_input); // Return a copy of the original string as fallback
    }

    // Allocate buffer for wide characters
    wchar_t* wide_buf = (wchar_t*)malloc(wide_size * sizeof(wchar_t));
    if (!wide_buf) {
        printf("Error: Failed to allocate memory for wide characters\n");
        return _strdup(utf8_input);
    }

    // Convert from UTF-8 to wide characters
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_input, -1, wide_buf, wide_size) <= 0) {
        printf("Error: Failed to convert from UTF-8 to wide characters\n");
        free(wide_buf);
        return _strdup(utf8_input);
    }

    // Get the required size for the local encoding buffer
    int local_size = WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, NULL, 0, NULL, NULL);
    if (local_size <= 0) {
        printf("Error: Failed to calculate local encoding size\n");
        free(wide_buf);
        return _strdup(utf8_input);
    }

    // Allocate buffer for local encoding characters
    char* local_buf = (char*)malloc(local_size);
    if (!local_buf) {
        printf("Error: Failed to allocate memory for local encoding characters\n");
        free(wide_buf);
        return _strdup(utf8_input);
    }

    // Convert from wide characters to local encoding
    if (WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, local_buf, local_size, NULL, NULL) <= 0) {
        printf("Error: Failed to convert to local encoding\n");
        free(wide_buf);
        free(local_buf);
        return _strdup(utf8_input);
    }

    // Clean up
    free(wide_buf);

    return local_buf;
}
#else
// On non-Windows platforms, just return a copy of the input
static char* convert_from_utf8(const char* utf8_input) {
    return strdup(utf8_input);
}
#endif

// Helper function to validate UTF-8 string
// Returns true if the string is valid UTF-8, false otherwise
static bool is_valid_utf8(const char* input) {
    if (!input) {
        return false;
    }

    size_t input_len = strlen(input);

    for (size_t i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];

        if (c < 0x80) {
            // ASCII character, valid
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= input_len) {
                return false; // Incomplete sequence
            }

            unsigned char c2 = (unsigned char)input[i+1];
            if ((c2 & 0xC0) != 0x80) {
                return false; // Invalid continuation byte
            }

            // Skip the continuation byte
            i++;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= input_len) {
                return false; // Incomplete sequence
            }

            unsigned char c2 = (unsigned char)input[i+1];
            unsigned char c3 = (unsigned char)input[i+2];
            if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                return false; // Invalid continuation byte
            }

            // Skip the continuation bytes
            i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 >= input_len) {
                return false; // Incomplete sequence
            }

            unsigned char c2 = (unsigned char)input[i+1];
            unsigned char c3 = (unsigned char)input[i+2];
            unsigned char c4 = (unsigned char)input[i+3];
            if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80) {
                return false; // Invalid continuation byte
            }

            // Skip the continuation bytes
            i += 3;
        } else {
            // Invalid UTF-8 lead byte
            return false;
        }
    }

    return true;
}

// Helper function to properly escape JSON string with UTF-8 support
static char* escape_json_string_utf8(const char* input) {
    if (!input) {
        return NULL;
    }

    // First, check if the input is valid UTF-8
    if (!is_valid_utf8(input)) {
        printf("Warning: Input string is not valid UTF-8\n");
    }

    // Use the built-in JSON string formatting function
    return mcp_json_format_string(input);
}

// Global variables
static mcp_client_t* g_client = NULL;
static mcp_transport_t* g_transport = NULL;
static mcp_transport_config_t g_transport_config;
static mcp_client_config_t g_client_config;
static volatile bool g_running = true;

// Signal handler to gracefully shut down the client
static void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);

    // Set running flag to false to exit the main loop
    g_running = false;

    // Don't destroy resources here - let the main function handle cleanup
    // This avoids potential race conditions and ensures proper cleanup order

    // Don't call exit() here, as it bypasses normal cleanup
}

// Helper function to read a line of input from the user
static char* read_user_input(const char* prompt) {
    static char buffer[1024];

    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NULL;
    }

    // Remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    return buffer;
}

// Function to create a new client connection
static bool create_client_connection(const char* host, uint16_t port, const char* path) {
    // Clean up existing client and transport if any
    if (g_client) {
        mcp_client_destroy(g_client);
        g_client = NULL;
    }

    if (g_transport) {
        // Transport is destroyed by client, no need to destroy it here
        g_transport = NULL;
    }

    // Create WebSocket transport configuration
    g_transport_config = (mcp_transport_config_t){0};
    g_transport_config.ws.host = host;
    g_transport_config.ws.port = port;
    g_transport_config.ws.path = path;
    g_transport_config.ws.use_ssl = 0; // No SSL for this example

    // Create transport
    g_transport = mcp_transport_factory_create(
        MCP_TRANSPORT_WS_CLIENT,
        &g_transport_config
    );

    if (!g_transport) {
        mcp_log_error("Failed to create WebSocket transport");
        return false;
    }

    // Create client configuration
    g_client_config = (mcp_client_config_t){
        .request_timeout_ms = 5000 // 5 second timeout
    };

    // Create client
    g_client = mcp_client_create(&g_client_config, g_transport);
    if (!g_client) {
        mcp_log_error("Failed to create client");
        mcp_transport_destroy(g_transport);
        g_transport = NULL;
        return false;
    }

    printf("Connecting to WebSocket server at %s:%d%s\n", host, port, path);

    // Wait for connection to be established with timeout
    int max_wait_attempts = 100; // 100 * 100ms = 10 seconds (longer timeout)
    int wait_attempts = 0;
    int connection_state = 0;

    while (wait_attempts < max_wait_attempts) {
        connection_state = mcp_client_is_connected(g_client);
        if (connection_state == 1) {
            printf("Connected to server (verified).\n");
            return true;
        }

        // Wait a bit before checking again
        mcp_sleep_ms(100);
        wait_attempts++;

        // Print progress every second
        if (wait_attempts % 10 == 0) {
            printf("Waiting for connection to be established... (%d seconds)\n", wait_attempts / 10);
        }
    }

    if (connection_state != 1) {
        printf("Error: Connection not established after %d seconds (state: %d).\n",
               max_wait_attempts / 10, connection_state);

        // Destroy the client since connection failed
        if (g_client) {
            mcp_client_destroy(g_client);
            g_client = NULL;
        }

        return false;
    }

    return true;
}

// Function to check if client is connected and reconnect if necessary
static bool ensure_client_connected(const char* host, uint16_t port, const char* path) {
    // Simple check - if client is NULL, we definitely need to reconnect
    if (g_client == NULL) {
        printf("Client not connected. Reconnecting...\n");
        return create_client_connection(host, port, path);
    }

    // Check if the transport is still valid
    if (g_transport == NULL) {
        printf("Transport is invalid. Reconnecting...\n");

        // Destroy the old client before creating a new one
        if (g_client) {
            mcp_client_destroy(g_client);
            g_client = NULL;
        }

        return create_client_connection(host, port, path);
    }

    // Check the actual connection state using the client API
    int connection_state = mcp_client_is_connected(g_client);
    if (connection_state != 1) {
        printf("Client connection is not established (state: %d). Reconnecting...\n", connection_state);

        // Destroy the old client before creating a new one
        if (g_client) {
            mcp_client_destroy(g_client);
            g_client = NULL;
        }

        return create_client_connection(host, port, path);
    }
    return true;
}

int main(int argc, char* argv[]) {
    // Set locale to support UTF-8
    setlocale(LC_ALL, "");

#ifdef _WIN32
    // Set console code page to UTF-8 on Windows
    SetConsoleOutputCP(CP_UTF8);
#endif

    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Default configuration
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* path = "/ws";
    const char* message = "Hello, WebSocket!";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--message") == 0 && i + 1 < argc) {
            message = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST       Host to connect to (default: 127.0.0.1)\n");
            printf("  --port PORT       Port to connect to (default: 8080)\n");
            printf("  --path PATH       WebSocket endpoint path (default: /ws)\n");
            printf("  --message MESSAGE Message to send (default: \"Hello, WebSocket!\")\n");
            printf("  --help            Show this help message\n");
            return 0;
        }
    }

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Initialize thread-local arena for JSON parsing
    if (mcp_arena_init_current_thread(4096) != 0) {
        printf("Failed to initialize thread-local arena\n");
        return 1;
    }

    // Create initial client connection
    if (!create_client_connection(host, port, path)) {
        mcp_log_error("Failed to create initial client connection");
        return 1;
    }

    // Interactive message loop
    printf("Enter messages to send. Type 'exit' to quit.\n");

    while (g_running) {
        // Get message from user input
        const char* input = read_user_input("Enter message: ");

        // Check for exit command
        if (input == NULL || strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            printf("Exiting...\n");
            break;
        }

        // Skip empty messages
        if (strlen(input) == 0) {
            continue;
        }

        // Convert input to UTF-8 and copy to user_message
        char* utf8_input = convert_to_utf8(input);
        if (!utf8_input) {
            printf("Error: Failed to convert input to UTF-8\n");
            continue;
        }

        char user_message[1024];
        strncpy(user_message, utf8_input, sizeof(user_message) - 1);
        user_message[sizeof(user_message) - 1] = '\0'; // Ensure null termination

        // Free the converted string
        free(utf8_input);

        // Log the message for debugging, especially for non-ASCII characters
        printf("Input message length: %zu bytes\n", strlen(user_message));

        // Print hex representation for debugging
        printf("Message hex representation: ");
        for (size_t i = 0; i < strlen(user_message) && i < 32; i++) {
            printf("%02X ", (unsigned char)user_message[i]);
        }
        printf("\n");

        // Check if the message contains non-ASCII characters
        bool contains_non_ascii = false;
        for (size_t i = 0; i < strlen(user_message); i++) {
            if ((unsigned char)user_message[i] > 127) {
                contains_non_ascii = true;
                break;
            }
        }

        // For non-ASCII characters, validate UTF-8 encoding
        if (contains_non_ascii) {
            printf("Message contains non-ASCII characters (UTF-8).\n");

            // Check if the string is valid UTF-8
            bool is_utf8_valid = is_valid_utf8(user_message);
            if (!is_utf8_valid) {
                printf("Warning: Input may contain invalid UTF-8 sequences after conversion.\n");
                printf("Will attempt to send anyway.\n");
            }

            printf("UTF-8 message: \"%s\"\n", user_message);
        }

        // Ensure we're still connected before sending
        if (!ensure_client_connected(host, port, path)) {
            printf("Error: Failed to establish connection. Please check server status.\n");
            continue;
        }

        printf("Sending echo request with message: \"%s\"\n", user_message);

        // Try different formats for the echo request
        char params_buffer[1024];
        char* response = NULL;
        mcp_error_code_t error_code = MCP_ERROR_NONE;
        char* error_message = NULL;
        int result = -1;

        // Format 1: Using call_tool with message parameter - properly handle all characters
        // Use the JSON API to build the request, which will handle all escaping properly

        // Use our UTF-8 aware JSON string escaping for all messages
        printf("Using UTF-8 aware JSON formatting.\n");

        // Create a properly escaped JSON string with UTF-8 support
        char* escaped_message = escape_json_string_utf8(user_message);
        if (!escaped_message) {
            printf("Error: Failed to format message as JSON string\n");
            continue;
        }

        // The escaped_message includes quotes, so we need to use it directly in the JSON
        printf("Escaped message: %s\n", escaped_message);

        // Format the JSON request with the properly escaped message
        snprintf(params_buffer, sizeof(params_buffer),
            "{\"name\":\"echo\",\"arguments\":{\"message\":%s}}",
            escaped_message);

        free(escaped_message);

        // Send the request
        result = mcp_client_send_request(g_client, "call_tool", params_buffer, &response, &error_code, &error_message);

        // Handle error if any
        if (error_code != MCP_ERROR_NONE || result != 0) {
            printf("Request failed with error code %d: %s (result: %d)\n",
                   error_code, error_message ? error_message : "Unknown error", result);

            if (error_message) {
                free(error_message);
            }

            // Check if the error is related to UTF-8 characters
            bool is_utf8_error = (result == -1 || result == -2);

            if (is_utf8_error) {
                printf("The error may be related to UTF-8 characters in your message.\n");
                printf("Trying to reconnect and will retry with ASCII-only characters...\n");

                // Try to reconnect on error
                if (!ensure_client_connected(host, port, path)) {
                    printf("Error: Failed to re-establish connection. Please check server status.\n");
                    // Skip the retry attempt
                    goto cleanup;
                }

                // Try again with a simplified message (ASCII only)
                printf("Retrying with ASCII-only version of the message...\n");

                // Create a sanitized version of the message (ASCII only)
                char sanitized_message[1024] = "Fallback message - ASCII only";
                // We don't have access to the original user_message here, so we use a fallback

                printf("Sanitized message: \"%s\"\n", sanitized_message);

                // Format the JSON request with the sanitized message
                snprintf(params_buffer, sizeof(params_buffer),
                    "{\"name\":\"echo\",\"arguments\":{\"message\":\"%s\"}}",
                    sanitized_message);

                // Try sending the sanitized message
                result = mcp_client_send_request(g_client, "call_tool", params_buffer, &response, &error_code, &error_message);

                if (error_code != MCP_ERROR_NONE || result != 0) {
                    printf("Retry failed with error code %d: %s (result: %d)\n",
                           error_code, error_message ? error_message : "Unknown error", result);

                    if (error_message) {
                        free(error_message);
                    }

                    goto cleanup;
                }

                // If we get here, the retry succeeded
                printf("Retry succeeded with sanitized message.\n");
            } else {
                // For non-UTF8 errors, just try to reconnect
                if (!ensure_client_connected(host, port, path)) {
                    printf("Error: Failed to re-establish connection. Please check server status.\n");
                }

                goto cleanup;
            }
        }

cleanup:
        // Free error message if it was set (shouldn't be if error_code is NONE)
        if (error_message != NULL) {
            free(error_message);
        }

        // Print response if we have one
        if (response != NULL) {
            // Extract text content from JSON response
            printf("Received raw response: %s\n", response);

            // Try to extract text content and convert to local encoding
            // Using simple string search - in a real project, use a JSON parsing library
            const char* text_marker = "\"text\":\"";
            char* text_start = strstr(response, text_marker);
            if (text_start) {
                text_start += strlen(text_marker); // Move to the start of text content
                char* text_end = strchr(text_start, '\"');
                if (text_end) {
                    // Temporarily replace ending quote with null terminator
                    *text_end = '\0';

                    // Convert UTF-8 text to local encoding
                    char* local_text = convert_from_utf8(text_start);
                    if (local_text) {
                        printf("Echo response (Local Encoding): %s\n", local_text);
                        free(local_text);
                    }

                    // Restore ending quote
                    *text_end = '\"';
                }
            }

            free(response);
        }
    }

    // Connection is closed automatically when the client is destroyed

    // Clean up
    if (g_client) {
        mcp_client_destroy(g_client);
        g_client = NULL;
    }

    // Transport is destroyed by client, no need to destroy it here
    g_transport = NULL;

    // Close log file
    mcp_log_close();

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();

    printf("Client shutdown complete\n");
    return 0;
}
