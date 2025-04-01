#include "mcp_stdio_transport.h"
#include "mcp_transport_internal.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Platform-specific includes for threading
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h> // For sleep
#endif

// Max line length for reading from stdin
#define MAX_LINE_LENGTH 4096

// Internal structure for stdio transport data
typedef struct {
    bool running;
    mcp_transport_t* transport_handle; // Pointer back to the main handle
#ifdef _WIN32
    HANDLE read_thread;
#else
    pthread_t read_thread;
#endif
} mcp_stdio_transport_data_t;

// --- Forward Declarations for Static Functions ---
static int stdio_transport_send(mcp_transport_t* transport, const void* data_to_send, size_t size);

// --- Static Implementation Functions ---

// Max line length for reading from stdin
#define MAX_STDIO_LINE_LENGTH 4096

// Synchronous receive function for stdio (primarily for client)
static int stdio_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    (void)transport; // Not needed for stdio receive
    (void)timeout_ms; // Timeout ignored for simple blocking fgets

    if (data_out == NULL || size_out == NULL) {
        return -1;
    }
    *data_out = NULL;
    *size_out = 0;

    char line_buffer[MAX_STDIO_LINE_LENGTH];

    // Blocking read using fgets
    if (fgets(line_buffer, sizeof(line_buffer), stdin) == NULL) {
        if (feof(stdin)) {
            log_message(LOG_LEVEL_INFO, "EOF reached on stdin during receive.");
            return -1; // Or a specific EOF code? -1 for general error for now.
        } else {
            char err_buf[128];
#ifdef _WIN32
            strerror_s(err_buf, sizeof(err_buf), errno);
            log_message(LOG_LEVEL_ERROR, "Failed to read from stdin: %s (errno: %d)", err_buf, errno);
#else
            if (strerror_r(errno, err_buf, sizeof(err_buf)) == 0) {
                 log_message(LOG_LEVEL_ERROR, "Failed to read from stdin: %s (errno: %d)", err_buf, errno);
            } else {
                 log_message(LOG_LEVEL_ERROR, "Failed to read from stdin: (errno: %d, strerror_r failed)", errno);
            }
#endif
            return -1; // Read error
        }
    }

    // Remove trailing newline characters
    line_buffer[strcspn(line_buffer, "\r\n")] = 0;
    *size_out = strlen(line_buffer);

    // Allocate memory for the result (caller must free)
    *data_out = (char*)malloc(*size_out + 1);
    if (*data_out == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for received message.");
        *size_out = 0;
        return -1;
    }
    memcpy(*data_out, line_buffer, *size_out + 1); // Copy including null terminator

    return 0; // Success
}


// Thread function to read from stdin (for server callback)
#ifdef _WIN32
static DWORD WINAPI stdio_read_thread_func(LPVOID arg) {
#else
static void* stdio_read_thread_func(void* arg) {
#endif
    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)arg;
    mcp_transport_t* transport = data->transport_handle;
    char line_buffer[MAX_LINE_LENGTH];

    while (data->running) {
        if (fgets(line_buffer, sizeof(line_buffer), stdin) != NULL) {
            // Remove trailing newline characters
            line_buffer[strcspn(line_buffer, "\r\n")] = 0;
            size_t len = strlen(line_buffer);

            if (len > 0 && transport->message_callback != NULL) {
                int callback_error_code = 0;
                char* response_str = transport->message_callback(
                    transport->callback_user_data,
                    line_buffer,
                    len,
                    &callback_error_code
                );

                if (response_str != NULL) {
                    // Send the response back via stdout
                    // stdio_transport_send already adds newline and flushes
                    if (stdio_transport_send(transport, response_str, strlen(response_str)) != 0) {
                         fprintf(stderr, "[MCP Stdio Transport] Failed to send response via stdout.\n");
                         // Error sending response, maybe stop?
                         // data->running = false;
                    }
                    free(response_str); // Free the malloc'd response string
                } else if (callback_error_code != 0) {
                    // Callback indicated an error but returned no response
                     fprintf(stderr, "[MCP Stdio Transport] Message callback indicated error (%d) but returned no response string.\n", callback_error_code);
                     // data->running = false; // Optional: stop on callback error
                }
                // If response_str is NULL and no error, it was a notification or response not needed.
            }
        } else {
            // fgets returned NULL, either EOF or error
            if (feof(stdin)) {
                fprintf(stderr, "[MCP Stdio Transport] EOF reached on stdin.\n");
            } else if (ferror(stdin)) {
                perror("[MCP Stdio Transport] Error reading stdin");
            }
            data->running = false; // Stop reading on EOF or error
            break;
        }
    }
    fprintf(stderr, "[MCP Stdio Transport] Read thread exiting.\n");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// Update signature to match the function pointer type in mcp_transport struct
static int stdio_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data, mcp_transport_error_callback_t error_callback) {
    (void)message_callback; // Callback is stored in transport struct by generic start
    (void)user_data;        // User data is stored in transport struct by generic start
    (void)error_callback;   // Stdio transport doesn't use the error callback currently

    if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }
    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)transport->transport_data;

    if (data->running) {
        return 0; // Already running
    }

    data->running = true;

#ifdef _WIN32
    data->read_thread = CreateThread(NULL, 0, stdio_read_thread_func, data, 0, NULL);
    if (data->read_thread == NULL) {
        fprintf(stderr, "[MCP Stdio Transport] Failed to create read thread.\n");
        data->running = false;
        return -1;
    }
#else
    if (pthread_create(&data->read_thread, NULL, stdio_read_thread_func, data) != 0) {
        perror("[MCP Stdio Transport] Failed to create read thread");
        data->running = false;
        return -1;
    }
#endif
    fprintf(stderr, "[MCP Stdio Transport] Read thread started.\n");
    return 0;
}

static int stdio_transport_stop(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) {
        return -1;
    }
    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)transport->transport_data;

    if (!data->running) {
        return 0; // Already stopped
    }

    data->running = false;

    // How to reliably stop the fgets call?
    // On POSIX, we could potentially close(STDIN_FILENO) or use pthread_cancel.
    // On Windows, closing stdin might be tricky.
    // For now, we rely on the thread checking the 'running' flag or hitting EOF/error.
    // A more robust solution might involve select/poll or platform-specific IPC.

#ifdef _WIN32
    // Wait for the thread to finish (with a timeout?)
    // WaitForSingleObject(data->read_thread, INFINITE); // Could block indefinitely if fgets doesn't return
    // CloseHandle(data->read_thread); // Close handle after thread exits
#else
    // pthread_cancel(data->read_thread); // Force cancellation (might leave resources locked)
    pthread_join(data->read_thread, NULL); // Wait for thread to exit cleanly
#endif
    fprintf(stderr, "[MCP Stdio Transport] Read thread stopped.\n");

    return 0;
}

static int stdio_transport_send(mcp_transport_t* transport, const void* data_to_send, size_t size) {
    (void)transport; // Not needed for stdio send
    if (data_to_send == NULL || size == 0) {
        return -1;
    }

    // Write the data, followed by a newline (as expected by fgets on the other side)
    if (fwrite(data_to_send, 1, size, stdout) != size) {
        perror("[MCP Stdio Transport] Failed to write data to stdout");
        return -1;
    }
    if (fputc('\n', stdout) == EOF) {
         perror("[MCP Stdio Transport] Failed to write newline to stdout");
        return -1;
    }
    // Ensure the output is flushed immediately
    if (fflush(stdout) != 0) {
        perror("[MCP Stdio Transport] Failed to flush stdout");
        return -1;
    }
    return 0;
}

static void stdio_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return;
    }
    // Ensure transport is stopped first
    stdio_transport_stop(transport);

    // Free the specific stdio data
    free(transport->transport_data);
    transport->transport_data = NULL;
    // The generic mcp_transport_destroy will free the main transport struct
}


// --- Public Creation Function ---

mcp_transport_t* mcp_transport_stdio_create(void) {
    // Allocate the generic transport struct
    mcp_transport_t* transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    if (transport == NULL) {
        return NULL;
    }

    // Allocate the stdio-specific data struct
    mcp_stdio_transport_data_t* stdio_data = (mcp_stdio_transport_data_t*)malloc(sizeof(mcp_stdio_transport_data_t));
    if (stdio_data == NULL) {
        free(transport);
        return NULL;
    }

    // Initialize stdio data
    stdio_data->running = false;
    stdio_data->transport_handle = transport; // Link back
#ifdef _WIN32
    stdio_data->read_thread = NULL;
#else
    // pthread_t doesn't need explicit NULL initialization
#endif

    // Initialize the generic transport struct
    transport->start = stdio_transport_start;
    transport->stop = stdio_transport_stop;
    transport->send = stdio_transport_send;
    transport->receive = stdio_transport_receive; // Assign receive function
    transport->destroy = stdio_transport_destroy;
    transport->transport_data = stdio_data; // Store specific data
    transport->message_callback = NULL;     // Will be set by mcp_transport_start
    transport->callback_user_data = NULL; // Will be set by mcp_transport_start

    return transport;
}
