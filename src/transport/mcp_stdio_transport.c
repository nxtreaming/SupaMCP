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
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define MAX_LINE_LENGTH 4096          // Max length for a single line read from stdin
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Maximum message size (1MB)

/**
 * @internal
 * @brief Internal data specific to the stdio transport implementation.
 */
typedef struct {
    bool running;                       /**< Flag indicating if the transport (read thread) is active. */
    mcp_transport_t* transport_handle;  /**< Pointer back to the generic transport handle containing callbacks. */
    mcp_thread_t read_thread;           /**< Handle for the background stdin reading thread (cross-platform). */
} mcp_stdio_transport_data_t;

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
    (void)transport;   // Unused in this function
    (void)timeout_ms;  // Timeout is ignored, fgets is blocking

    // Validate parameters
    if (data_out == NULL || size_out == NULL) {
        return -1;
    }

    // Initialize output parameters
    *data_out = NULL;
    *size_out = 0;

    // Stack buffer for reading the line
    char line_buffer[MAX_LINE_LENGTH];

    // Blocking read using fgets
    if (fgets(line_buffer, sizeof(line_buffer), stdin) == NULL) {
        // Handle EOF or error
        if (feof(stdin)) {
            mcp_log_info("EOF reached on stdin during receive.");
            return -1;
        } else {
            // Format error message
            char err_buf[128];
#ifdef _WIN32
            strerror_s(err_buf, sizeof(err_buf), errno);
#else
            if (strerror_r(errno, err_buf, sizeof(err_buf)) != 0) {
                snprintf(err_buf, sizeof(err_buf), "Unknown error");
            }
#endif
            mcp_log_error("Failed to read from stdin: %s (errno: %d)", err_buf, errno);
            return -1;
        }
    }

    // Remove trailing newline characters
    line_buffer[strcspn(line_buffer, "\r\n")] = 0;
    *size_out = strlen(line_buffer);

    // Allocate memory for the result (caller must free)
    // Note: We always use malloc here because the caller expects to free with free()
    *data_out = (char*)malloc(*size_out + 1);
    if (*data_out == NULL) {
        mcp_log_error("Failed to allocate memory for received message.");
        *size_out = 0;
        return -1;
    }

    // Copy data including null terminator
    memcpy(*data_out, line_buffer, *size_out + 1);

    return 0;
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

    mcp_log_debug("Read thread started (using length prefix framing).");

    // Loop reading messages as long as the transport is running
    while (data->running) {
        // 1. Read the 4-byte length prefix
        if (fread(length_buf, 1, sizeof(length_buf), stdin) != sizeof(length_buf)) {
            if (feof(stdin)) {
                mcp_log_info("EOF reached on stdin while reading length.");
            } else {
                mcp_log_error("Error reading length prefix from stdin: %s", strerror(errno));
            }
            data->running = false; // Stop reading on EOF or error
            break;
        }

        // 2. Decode length (Network to Host byte order)
        memcpy(&message_length_net, length_buf, 4);
        message_length_host = ntohl(message_length_net);

        // 3. Sanity check length
        if (message_length_host == 0 || message_length_host > MAX_MCP_MESSAGE_SIZE) {
            mcp_log_error("Invalid message length received: %u", message_length_host);
            data->running = false; // Treat as fatal error
            break;
        }

        // 4. Allocate buffer for message body (+1 for null terminator)
        // Use memory pool if available
        if (mcp_memory_pool_system_is_initialized()) {
            message_buf = (char*)mcp_pool_alloc(message_length_host + 1);
        } else {
            message_buf = (char*)malloc(message_length_host + 1);
        }

        if (message_buf == NULL) {
            mcp_log_error("Failed to allocate buffer for message size %u",
                         message_length_host);
            data->running = false; // Treat as fatal error
            break;
        }

        // 5. Read the message body
        size_t bytes_read = fread(message_buf, 1, message_length_host, stdin);
        if (bytes_read != message_length_host) {
            if (feof(stdin)) {
                mcp_log_info("EOF reached on stdin while reading body. "
                            "Expected %u bytes, got %zu", message_length_host, bytes_read);
            } else {
                mcp_log_error("Error reading message body from stdin: %s",
                             strerror(errno));
            }

            // Free using the same method as allocation
            if (mcp_memory_pool_system_is_initialized()) {
                mcp_pool_free(message_buf);
            } else {
                free(message_buf);
            }
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

            // Handle the response
            if (response_str != NULL) {
                // Send the response back via stdout (send function handles framing)
                if (stdio_transport_send(transport, response_str, strlen(response_str)) != 0) {
                    mcp_log_error("Failed to send response via stdout.");
                    // We continue processing despite send errors
                }
                // Free the response string (always use free since callback uses malloc)
                free(response_str);
            }
            else if (callback_error_code != 0) {
                // Callback indicated an error but returned no response
                mcp_log_warn("Message callback indicated error (%d) "
                            "but returned no response string.", callback_error_code);
            }
            // If response_str is NULL and no error, it was a notification or response not needed.
        }

        // 7. Free the message buffer for the next read
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(message_buf);
        } else {
            free(message_buf);
        }
        message_buf = NULL;
    } // End while(data->running)

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

    if (transport->transport_data != NULL) {
        mcp_stdio_transport_data_t* data = (mcp_stdio_transport_data_t*)transport->transport_data;

        // Ensure transport is stopped first
        if (data->running) {
            mcp_log_debug("Stopping transport during destroy.");
            stdio_transport_stop(transport);
        }

        // Free the specific stdio data using the appropriate method
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(transport->transport_data);
        } else {
            free(transport->transport_data);
        }
        transport->transport_data = NULL;
    }

    // Free the main transport struct using the appropriate method
    if (mcp_memory_pool_system_is_initialized()) {
        mcp_pool_free(transport);
    } else {
        free(transport);
    }

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
    // Allocate the generic transport struct using memory pool if available
    mcp_transport_t* transport = NULL;
    if (mcp_memory_pool_system_is_initialized()) {
        transport = (mcp_transport_t*)mcp_pool_alloc(sizeof(mcp_transport_t));
    } else {
        transport = (mcp_transport_t*)malloc(sizeof(mcp_transport_t));
    }

    if (transport == NULL) {
        mcp_log_error("Failed to allocate transport structure.");
        return NULL;
    }

    // Zero-initialize the transport structure
    memset(transport, 0, sizeof(mcp_transport_t));

    // Allocate the stdio-specific data struct
    mcp_stdio_transport_data_t* stdio_data = NULL;
    if (mcp_memory_pool_system_is_initialized()) {
        stdio_data = (mcp_stdio_transport_data_t*)mcp_pool_alloc(sizeof(mcp_stdio_transport_data_t));
    } else {
        stdio_data = (mcp_stdio_transport_data_t*)malloc(sizeof(mcp_stdio_transport_data_t));
    }

    if (stdio_data == NULL) {
        mcp_log_error("Failed to allocate transport data structure.");
        if (mcp_memory_pool_system_is_initialized()) {
            mcp_pool_free(transport);
        } else {
            free(transport);
        }
        return NULL;
    }

    // Initialize stdio data
    memset(stdio_data, 0, sizeof(mcp_stdio_transport_data_t));
    stdio_data->running = false;
    stdio_data->transport_handle = transport; // Link back

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

    mcp_log_debug("Transport created successfully.");
    return transport;
}
