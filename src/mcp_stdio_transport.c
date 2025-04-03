#include "mcp_stdio_transport.h"
#include "internal/mcp_transport_internal.h"
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
#define MAX_LINE_LENGTH 4096 // Max length for a single line read from stdin

/**
 * @internal
 * @brief Internal data specific to the stdio transport implementation.
 */
typedef struct {
    bool running;                       /**< Flag indicating if the transport (read thread) is active. */
    mcp_transport_t* transport_handle;  /**< Pointer back to the generic transport handle containing callbacks. */
#ifdef _WIN32
    HANDLE read_thread;
#else
    pthread_t read_thread;              /**< Handle for the background stdin reading thread. */
#endif
} mcp_stdio_transport_data_t;

// --- Static Function Declarations ---

// Implementation of the send function for stdio transport.
static int stdio_transport_send(mcp_transport_t* transport, const void* data_to_send, size_t size);
// Implementation of the synchronous receive function for stdio transport.
static int stdio_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms);
// Implementation of the start function for stdio transport.
static int stdio_transport_start(mcp_transport_t* transport, mcp_transport_message_callback_t message_callback, void* user_data, mcp_transport_error_callback_t error_callback);
// Implementation of the stop function for stdio transport.
static int stdio_transport_stop(mcp_transport_t* transport);
// Implementation of the destroy function for stdio transport.
static void stdio_transport_destroy(mcp_transport_t* transport);
// Background thread function for reading from stdin.
#ifdef _WIN32
static DWORD WINAPI stdio_read_thread_func(LPVOID arg);
#else
static void* stdio_read_thread_func(void* arg);
#endif


// --- Static Implementation Functions ---

// Note: MAX_LINE_LENGTH is defined globally near the top of the file.

/**
 * @internal
 * @brief Synchronously reads a single line (message) from stdin.
 * Used when the stdio transport is employed in a simple synchronous client role.
 * Blocks until a newline is encountered or an error/EOF occurs.
 * @param transport The transport handle (unused).
 * @param[out] data_out Pointer to receive the malloc'd buffer containing the line read (excluding newline). Caller must free.
 * @param[out] size_out Pointer to receive the length of the line read.
 * @param timeout_ms Timeout parameter (ignored by this implementation).
 * @return 0 on success, -1 on error or EOF.
 */
static int stdio_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    (void)transport; // Unused in this function
    (void)timeout_ms; // Timeout is ignored, fgets is blocking

    if (data_out == NULL || size_out == NULL) {
         return -1;
     }
     *data_out = NULL;
     *size_out = 0;

     char line_buffer[MAX_LINE_LENGTH]; // Use the globally defined MAX_LINE_LENGTH

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

    return 0;
}

/**
 * @internal
 * @brief Background thread function that continuously reads lines from stdin.
 * This is used when the stdio transport is started via mcp_transport_start
 * (typically in a server role). Each line read is treated as a message and
 * passed to the registered message callback. Responses from the callback are
 * sent to stdout.
 * @param arg Pointer to the mcp_stdio_transport_data_t structure.
 * @return 0 on Windows, NULL on POSIX.
 */
#ifdef _WIN32
static DWORD WINAPI stdio_read_thread_func(LPVOID arg) {
#else
static void* stdio_read_thread_func(void* arg) {
#endif
    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)arg;
    mcp_transport_t* transport = data->transport_handle;
    char length_buf[4];
    uint32_t message_length_net, message_length_host;
    char* message_buf = NULL;

    log_message(LOG_LEVEL_DEBUG, "Stdio read thread started (using length prefix framing).");

    // Loop reading messages as long as the transport is running
    while (data->running) {
        // 1. Read the 4-byte length prefix
        if (fread(length_buf, 1, sizeof(length_buf), stdin) != sizeof(length_buf)) {
            if (feof(stdin)) {
                log_message(LOG_LEVEL_INFO, "[MCP Stdio Transport] EOF reached on stdin while reading length.");
            } else {
                perror("[MCP Stdio Transport] Error reading length prefix from stdin");
            }
            data->running = false; // Stop reading on EOF or error
            break;
        }

        // 2. Decode length (Network to Host byte order)
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);

        // 3. Sanity check length (using MAX_MCP_MESSAGE_SIZE from TCP transport for consistency)
        #define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Re-define or include from common header
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
             log_message(LOG_LEVEL_ERROR, "[MCP Stdio Transport] Invalid message length received: %u", message_length_host);
             data->running = false; // Treat as fatal error
             break;
        }

        // 4. Allocate buffer for message body (+1 for null terminator)
        message_buf = (char*)malloc(message_length_host + 1);
        if (message_buf == NULL) {
             log_message(LOG_LEVEL_ERROR, "[MCP Stdio Transport] Failed to allocate buffer for message size %u", message_length_host);
             data->running = false; // Treat as fatal error
             break;
        }

        // 5. Read the message body
        if (fread(message_buf, 1, message_length_host, stdin) != message_length_host) {
             if (feof(stdin)) {
                 log_message(LOG_LEVEL_INFO, "[MCP Stdio Transport] EOF reached on stdin while reading body.");
             } else {
                 perror("[MCP Stdio Transport] Error reading message body from stdin");
             }
             free(message_buf);
             message_buf = NULL;
             data->running = false; // Stop reading on EOF or error
             break;
        }

        // 6. Null-terminate and process the message via callback
        message_buf[message_length_host] = '\0';
        if (transport->message_callback != NULL) {
            int callback_error_code = 0;
            // Invoke the message callback with the message data
            char* response_str = transport->message_callback(
                transport->callback_user_data, // Pass user data
                message_buf,                   // Pass message content
                message_length_host,           // Pass message length
                &callback_error_code
            );

            if (response_str != NULL) {
                // Send the response back via stdout (send function handles framing)
                if (stdio_transport_send(transport, response_str, strlen(response_str)) != 0) {
                     log_message(LOG_LEVEL_ERROR, "[MCP Stdio Transport] Failed to send response via stdout.");
                     // Error sending response, maybe stop?
                     // data->running = false;
                }
                free(response_str); // Free the malloc'd response string
            } else if (callback_error_code != 0) {
                // Callback indicated an error but returned no response
                 log_message(LOG_LEVEL_WARN, "[MCP Stdio Transport] Message callback indicated error (%d) but returned no response string.", callback_error_code);
                 // data->running = false; // Optional: stop on callback error
            }
            // If response_str is NULL and no error, it was a notification or response not needed.
        }

        // 7. Free the message buffer for the next read
        free(message_buf);
        message_buf = NULL;

    } // End while(data->running)
    fprintf(stderr, "[MCP Stdio Transport] Read thread exiting.\n");
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * @internal
 * @brief Starts the stdio transport, primarily by launching the background read thread.
 * @param transport The transport handle.
 * @param message_callback Callback for received messages (stored in transport by generic start).
 * @param user_data User data for callbacks (stored in transport by generic start).
 * @param error_callback Error callback (stored in transport by generic start, but unused here).
 * @return 0 on success, -1 on error (e.g., thread creation failed).
 */
static int stdio_transport_start(
    mcp_transport_t* transport,
    mcp_transport_message_callback_t message_callback,
    void* user_data,
    mcp_transport_error_callback_t error_callback
) {
    // Callbacks and user_data are already stored in the transport handle
    // by the generic mcp_transport_start function before this is called.
    (void)message_callback;
    (void)user_data;
    (void)error_callback; // Stdio transport doesn't currently signal transport errors via callback

    if (transport == NULL || transport->transport_data == NULL) {
        return -1; // Invalid arguments
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

/**
 * @internal
 * @brief Stops the stdio transport's background read thread.
 * @param transport The transport handle.
 * @return 0 on success, -1 on error.
 */
static int stdio_transport_stop(mcp_transport_t* transport) {
     if (transport == NULL || transport->transport_data == NULL) {
        return -1; // Invalid arguments
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

/**
 * @internal
 * @brief Sends data via the stdio transport (writes to stdout).
 * Appends a newline character and flushes stdout after writing the data.
 * @param transport The transport handle (unused).
 * @param data_to_send Pointer to the data buffer to send.
 * @param size Number of bytes in the payload to send.
 * @return 0 on success, -1 on error (e.g., write error, flush error).
 */
static int stdio_transport_send(mcp_transport_t* transport, const void* payload_data, size_t payload_size) {
    (void)transport; // Unused in this function
    if (payload_data == NULL || payload_size == 0) {
        return -1; // Invalid arguments
    }
    // Ensure payload size isn't too large (optional sanity check)
    // if (payload_size > SOME_MAX_LIMIT) return -1;

    // 1. Send 4-byte length prefix (network byte order)
    uint32_t net_len = htonl((uint32_t)payload_size);
    if (fwrite(&net_len, 1, sizeof(net_len), stdout) != sizeof(net_len)) {
        perror("[MCP Stdio Transport] Failed to write length prefix to stdout");
        return -1;
    }

    // 2. Send the actual payload data
    if (fwrite(payload_data, 1, payload_size, stdout) != payload_size) {
        perror("[MCP Stdio Transport] Failed to write payload data to stdout");
        return -1;
    }

    // 3. Ensure the output is flushed immediately
    if (fflush(stdout) != 0) {
        perror("[MCP Stdio Transport] Failed to flush stdout");
        return -1;
    }
    return 0;
}

/**
 * @internal
 * @brief Destroys the stdio transport specific data.
 * Ensures the transport is stopped and frees the internal data structure.
 * @param transport The transport handle.
 */
static void stdio_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL || transport->transport_data == NULL) {
        return; // Nothing to do
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
    transport->error_callback = NULL;     // Will be set by mcp_transport_start

    return transport;
}
