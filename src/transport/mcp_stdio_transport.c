#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "mcp_stdio_transport.h"
#include "internal/transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define MAX_LINE_LENGTH 4096          // Max length for a single line read from stdin
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Maximum message size (1MB)
#define STDIO_BUFFER_SIZE (64 * 1024)      // 64KB buffer for stdin/stdout

/**
 * @internal
 * @brief Internal data specific to the stdio transport implementation.
 */
typedef struct {
    bool running;                       /**< Flag indicating if the transport (read thread) is active. */
    mcp_transport_t* transport_handle;  /**< Pointer back to the generic transport handle containing callbacks. */
    mcp_thread_t read_thread;           /**< Handle for the background stdin reading thread (cross-platform). */
    void* (*alloc_func)(size_t);        /**< Function pointer for memory allocation. */
    void (*free_func)(void*);           /**< Function pointer for memory deallocation. */
} mcp_stdio_transport_data_t;

// Initialize stdio streams for binary mode and optimal buffering
static int stdio_init_streams(void);
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
static void* stdio_read_thread_func(void* arg);

/**
 * @internal
 * @brief Helper function to read a length-prefixed message from stdin.
 *
 * @param[out] data_out Pointer to receive the allocated buffer containing the message.
 * @param[out] size_out Pointer to receive the length of the message.
 * @param alloc_func Function to use for memory allocation (if NULL, malloc is used).
 * @param free_func Function to use for memory deallocation on error (if NULL, free is used).
 * @return 0 on success, -1 on error or EOF.
 */
static int read_length_prefixed_message(char** data_out, size_t* size_out,
                                       void* (*alloc_func)(size_t),
                                       void (*free_func)(void*)) {
    // Use malloc/free as default if none provided
    if (alloc_func == NULL) {
        alloc_func = malloc;
    }
    if (free_func == NULL) {
        free_func = free;
    }

    // Initialize output parameters
    *data_out = NULL;
    if (size_out != NULL) {
        *size_out = 0;
    }

    // 1. Read the 4-byte length prefix
    char length_buf[4];
    uint32_t message_length_net, message_length_host;

    if (fread(length_buf, 1, sizeof(length_buf), stdin) != sizeof(length_buf)) {
        // Handle EOF or error
        if (feof(stdin)) {
            mcp_log_info("EOF reached on stdin while reading length prefix.");
        } else {
            mcp_log_error("Error reading length prefix from stdin: %s", strerror(errno));
        }
        return -1;
    }

    // 2. Decode length (Network to Host byte order)
    memcpy(&message_length_net, length_buf, 4);
    message_length_host = ntohl(message_length_net);

    // 3. Sanity check length
    if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
        mcp_log_error("Invalid message length received: %u", message_length_host);
        return -1;
    }

    // 4. Allocate buffer for message body (+1 for null terminator)
    *data_out = (char*)alloc_func(message_length_host + 1);
    if (*data_out == NULL) {
        mcp_log_error("Failed to allocate buffer for message size %u", message_length_host);
        return -1;
    }

    // 5. Read the message body
    size_t bytes_read = fread(*data_out, 1, message_length_host, stdin);
    if (bytes_read != message_length_host) {
        if (feof(stdin)) {
            mcp_log_info("EOF reached on stdin while reading message body. "
                        "Expected %u bytes, got %zu", message_length_host, bytes_read);
        } else {
            mcp_log_error("Error reading message body from stdin: %s", strerror(errno));
        }

        // Free the allocated buffer on error
        free_func(*data_out);
        *data_out = NULL;
        return -1;
    }

    // 6. Null-terminate the message (for string operations)
    (*data_out)[message_length_host] = '\0';
    if (size_out != NULL) {
        *size_out = message_length_host;
    }

    return 0;
}

/**
 * @internal
 * @brief Initializes stdin and stdout for binary mode and optimal buffering.
 *
 * This function:
 * 1. Sets stdin and stdout to binary mode on Windows to prevent newline translation
 * 2. Optimizes buffering for stdin and stdout using setvbuf with static buffers
 *
 * @return 0 on success, -1 on error.
 */
static int stdio_init_streams(void) {
    // Use static buffers to avoid memory leaks
    // These buffers will exist for the lifetime of the program
    static char stdin_buffer[STDIO_BUFFER_SIZE];
    static char stdout_buffer[STDIO_BUFFER_SIZE];

    // Set binary mode for stdin and stdout on Windows
    // This is necessary for JSON-RPC transmission using length-prefixed framing
    // to ensure data integrity and prevent newline conversion issues on Windows
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
        mcp_log_error("Failed to set stdin to binary mode: %s", strerror(errno));
        return -1;
    }

    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        mcp_log_error("Failed to set stdout to binary mode: %s", strerror(errno));
        return -1;
    }

    mcp_log_debug("Set stdin and stdout to binary mode for reliable JSON-RPC transmission");
#endif

    // Set optimal buffering for stdin and stdout
    if (setvbuf(stdin, stdin_buffer, _IOFBF, STDIO_BUFFER_SIZE) != 0) {
        mcp_log_error("Failed to set stdin buffer: %s", strerror(errno));
        return -1;
    }

    if (setvbuf(stdout, stdout_buffer, _IOFBF, STDIO_BUFFER_SIZE) != 0) {
        mcp_log_error("Failed to set stdout buffer: %s", strerror(errno));
        return -1;
    }

    mcp_log_debug("Optimized stdin and stdout buffering (buffer size: %d KB)", STDIO_BUFFER_SIZE / 1024);
    return 0;
}

/**
 * @internal
 * @brief Synchronously reads a message from stdin using length-prefixed framing.
 * Used when the stdio transport is employed in a simple synchronous client role.
 * Blocks until a complete message is read or an error/EOF occurs.
 * @param transport The transport handle (unused).
 * @param[out] data_out Pointer to receive the malloc'd buffer containing the message. Caller must free.
 * @param[out] size_out Pointer to receive the length of the message.
 * @param timeout_ms Timeout parameter (ignored by this implementation).
 * @return 0 on success, -1 on error or EOF.
 */
static int stdio_transport_receive(mcp_transport_t* transport, char** data_out, size_t* size_out, uint32_t timeout_ms) {
    (void)transport;   // Unused in this function
    (void)timeout_ms;  // Timeout is ignored, fread is blocking

    // Validate parameters
    if (data_out == NULL || size_out == NULL) {
        return -1;
    }

    // Note: We always use malloc/free here because the caller expects to free with free()
    return read_length_prefixed_message(data_out, size_out, malloc, free);
}

/**
 * @internal
 * @brief Background thread function that continuously reads messages from stdin.
 * This is used when the stdio transport is started via mcp_transport_start.
 * Each message is read with a length prefix, processed via the registered callback,
 * and any response is sent back via stdout.
 *
 * @param arg Pointer to the mcp_stdio_transport_data_t structure.
 * @return NULL when thread exits.
 */
static void* stdio_read_thread_func(void* arg) {
    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)arg;
    mcp_transport_t* transport = data->transport_handle;
    char length_buf[4];
    uint32_t message_length_net, message_length_host;
    char* message_buf = NULL;

    // Get the stored memory allocation and deallocation functions
    void* (*alloc_func)(size_t) = data->alloc_func ? data->alloc_func : malloc;
    void (*free_func)(void*) = data->free_func ? data->free_func : free;

    mcp_log_debug("Read thread started (using length prefix framing).");

    // Loop reading messages as long as the transport is running
    while (data->running) {
        // Read a length-prefixed message using the helper function
        size_t message_length = 0;
        if (read_length_prefixed_message(&message_buf, &message_length, alloc_func, free_func) != 0) {
            // Error or EOF occurred
            data->running = false; // Stop reading on EOF or error
            break;
        }

        // Process the message via callback

        if (transport->message_callback != NULL) {
            int callback_error_code = 0;
            char* response_str = NULL;

            // Use a try-catch style with goto for error handling
            do {
                // Invoke the message callback with the message data
                response_str = transport->message_callback(
                    transport->callback_user_data, // Pass user data
                    message_buf,                   // Pass message content
                    message_length_host,           // Pass message length
                    &callback_error_code
                );

                // Handle the response
                if (response_str != NULL) {
                    // Send the response back via stdout (send function handles framing)
                    if (stdio_transport_send(transport, response_str, strlen(response_str)) != 0) {
                        mcp_log_error("Failed to send response via stdout.");
                        // We continue processing despite send errors
                    }
                    // Free the response string (always use free since callback uses malloc)
                    free(response_str);
                    response_str = NULL;
                }
                else if (callback_error_code != 0) {
                    // Callback indicated an error but returned no response
                    mcp_log_warn("Message callback indicated error (%d) "
                                "but returned no response string.", callback_error_code);
                }
                // If response_str is NULL and no error, it was a notification or response not needed.
            } while (0); // This is just a scope for the error handling

            // Ensure response_str is freed in case of any error path
            if (response_str != NULL) {
                free(response_str);
                response_str = NULL;
            }
        }

        // 7. Free the message buffer for the next read
        free_func(message_buf);
        message_buf = NULL;
    } // End while(data->running)

    // Final safety check - ensure message_buf is freed if we somehow exit the loop with it allocated
    if (message_buf != NULL) {
        free_func(message_buf);
        message_buf = NULL;
    }

    mcp_log_info("Read thread exiting.");
    return NULL;
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
    (void)error_callback;

    // Validate parameters
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid transport handle in start function.");
        return -1;
    }

    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)transport->transport_data;

    // Check if already running
    if (data->running) {
        mcp_log_debug("Transport already running, ignoring start request.");
        return 0;
    }

    // Set running flag before creating thread
    data->running = true;

    // Create the read thread using cross-platform thread API
    int thread_result = mcp_thread_create(&data->read_thread, stdio_read_thread_func, data);
    if (thread_result != 0) {
        mcp_log_error("Failed to create read thread: error code %d", thread_result);
        data->running = false;
        return -1;
    }

    mcp_log_info("Read thread started successfully.");
    return 0;
}

/**
 * @internal
 * @brief Stops the stdio transport's background read thread.
 * @param transport The transport handle.
 * @return 0 on success, -1 on error.
 */
static int stdio_transport_stop(mcp_transport_t* transport) {
    // Validate parameters
    if (transport == NULL || transport->transport_data == NULL) {
        mcp_log_error("Invalid transport handle in stop function.");
        return -1;
    }

    mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)transport->transport_data;

    // Check if already stopped
    if (!data->running) {
        mcp_log_debug("Transport already stopped, ignoring stop request.");
        return 0;
    }

    // Set running flag to false to signal thread to exit
    data->running = false;

    // Wait for thread to exit using cross-platform thread API
    if (mcp_thread_join(data->read_thread, NULL) != 0) {
        mcp_log_error("Error joining read thread");
    }

    mcp_log_info("Read thread stopped.");
    return 0;
}

/**
 * @internal
 * @brief Sends data via the stdio transport (writes to stdout).
 * Uses length-prefixed framing and flushes stdout after writing.
 *
 * @param transport The transport handle (unused).
 * @param payload_data Pointer to the data buffer to send.
 * @param payload_size Number of bytes in the payload to send.
 * @return 0 on success, -1 on error (e.g., write error, flush error).
 */
static int stdio_transport_send(mcp_transport_t* transport, const void* payload_data, size_t payload_size) {
    (void)transport; // Unused in this function

    // Validate parameters
    if (payload_data == NULL || payload_size == 0) {
        mcp_log_error("Invalid payload in send function.");
        return -1;
    }

    // Sanity check payload size
    if (payload_size > MAX_MCP_MESSAGE_SIZE) {
        mcp_log_error("Payload size %zu exceeds maximum allowed size (%d).",
                     payload_size, MAX_MCP_MESSAGE_SIZE);
        return -1;
    }

    // 1. Send 4-byte length prefix (network byte order)
    uint32_t net_len = htonl((uint32_t)payload_size);
    if (fwrite(&net_len, 1, sizeof(net_len), stdout) != sizeof(net_len)) {
        mcp_log_error("Failed to write length prefix to stdout: %s",
                     strerror(errno));
        return -1;
    }

    // 2. Send the actual payload data
    if (fwrite(payload_data, 1, payload_size, stdout) != payload_size) {
        mcp_log_error("Failed to write payload data to stdout: %s",
                     strerror(errno));
        return -1;
    }

    // 3. Ensure the output is flushed immediately
    if (fflush(stdout) != 0) {
        mcp_log_error("Failed to flush stdout: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * @internal
 * @brief Destroys the stdio transport specific data.
 * Ensures the transport is stopped and frees the internal data structure.
 *
 * @param transport The transport handle.
 */
static void stdio_transport_destroy(mcp_transport_t* transport) {
    if (transport == NULL) {
        mcp_log_debug("Attempted to destroy NULL transport.");
        return;
    }

    // Default deallocation function in case transport_data is NULL
    void (*free_func)(void*) = free;

    if (transport->transport_data != NULL) {
        mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)transport->transport_data;

        // Get the stored deallocation function
        free_func = data->free_func ? data->free_func : free;

        // Ensure transport is stopped first
        if (data->running) {
            mcp_log_debug("Stopping transport during destroy.");
            stdio_transport_stop(transport);
        }

        // Free the specific stdio data using the stored deallocation function
        free_func(transport->transport_data);
        transport->transport_data = NULL;
    }

    // Free the main transport struct using the same deallocation function
    free_func(transport);

    mcp_log_debug("Transport destroyed.");
}

/**
 * @brief Creates a transport layer instance that uses standard input/output.
 *
 * This transport reads messages with length prefixes from stdin and sends messages
 * with length prefixes to stdout. It's suitable for inter-process communication
 * where the other process also uses length-prefixed framing.
 *
 * @return A pointer to the created transport instance, or NULL on failure.
 *         The caller is responsible for destroying the transport using
 *         mcp_transport_destroy().
 */
mcp_transport_t* mcp_transport_stdio_create(void) {
    // Determine which memory allocation functions to use
    void* (*alloc_func)(size_t) = malloc;
    void (*free_func)(void*) = free;

    if (mcp_memory_pool_system_is_initialized()) {
        alloc_func = mcp_pool_alloc;
        free_func = mcp_pool_free;
    }

    // Allocate the generic transport struct using selected allocation function
    mcp_transport_t* transport = (mcp_transport_t*)alloc_func(sizeof(mcp_transport_t));
    if (transport == NULL) {
        mcp_log_error("Failed to allocate transport structure.");
        return NULL;
    }

    // Zero-initialize the transport structure
    memset(transport, 0, sizeof(mcp_transport_t));

    // Allocate the stdio-specific data struct using selected allocation function
    mcp_stdio_transport_data_t* stdio_data = (mcp_stdio_transport_data_t*)alloc_func(sizeof(mcp_stdio_transport_data_t));
    if (stdio_data == NULL) {
        mcp_log_error("Failed to allocate transport data structure.");
        free_func(transport);
        return NULL;
    }

    // Initialize stdio data
    memset(stdio_data, 0, sizeof(mcp_stdio_transport_data_t));
    stdio_data->running = false;
    stdio_data->transport_handle = transport; // Link back
    stdio_data->alloc_func = alloc_func;      // Store allocation function
    stdio_data->free_func = free_func;        // Store deallocation function

    // Set transport type and protocol
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_STDIO;

    // Initialize client operations
    transport->client.start = stdio_transport_start;
    transport->client.stop = stdio_transport_stop;
    transport->client.destroy = stdio_transport_destroy;
    transport->client.send = stdio_transport_send;
    transport->client.sendv = NULL; // No vectored send implementation for stdio
    transport->client.receive = stdio_transport_receive;

    // Set transport data
    transport->transport_data = stdio_data;

    // Initialize stdin and stdout for binary mode and optimal buffering
    if (stdio_init_streams() != 0) {
        mcp_log_warn("Failed to initialize stdio streams optimally, continuing with default settings");
        // We continue despite initialization failure, as the transport can still work with default settings
    }

    mcp_log_debug("Transport created successfully.");
    return transport;
}
