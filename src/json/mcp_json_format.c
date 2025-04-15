#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "mcp_json_rpc.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"

/**
 * Format a JSON-RPC request without using the thread-local arena.
 * This is a direct implementation that uses malloc/free instead of the arena.
 * Optimized to avoid unnecessary memory allocations and copies.
 */
char* mcp_json_format_request_direct(uint64_t id, const char* method, const char* params) {
    if (method == NULL) {
        return NULL;
    }

    // Calculate the required buffer size more accurately to avoid reallocations
    size_t method_len = strlen(method);
    size_t params_len = (params != NULL) ? strlen(params) : 0;

    // Base size: {"jsonrpc":"2.0","id":ID,"method":"METHOD"} + null terminator
    // 32 bytes for the fixed parts + method length + 2 for quotes around method
    size_t buffer_size = 32 + method_len + 2;

    // Add space for params if provided: ,"params":PARAMS
    // 11 bytes for ,"params": + params length
    if (params != NULL) {
        buffer_size += 11 + params_len;
    }

    // Add space for closing brace and null terminator
    buffer_size += 2;

    // Allocate the buffer with the calculated size
    char* buffer = (char*)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }

    // Format the basic request structure
    int written = snprintf(buffer, buffer_size,
                          "{\"jsonrpc\":\"2.0\",\"id\":%" PRIu64 ",\"method\":\"%s\"",
                          id, method);

    if (written < 0 || (size_t)written >= buffer_size) {
        // Buffer too small or error
        free(buffer);
        return NULL;
    }

    // Add params if provided
    if (params != NULL) {
        // Append the params
        written += snprintf(buffer + written, buffer_size - written,
                           ",\"params\":%s", params);

        if (written < 0 || (size_t)written >= buffer_size) {
            // Buffer calculation error or snprintf error
            free(buffer);
            return NULL;
        }
    }

    // Add closing brace
    if ((size_t)written + 2 <= buffer_size) {
        buffer[written] = '}';
        buffer[written + 1] = '\0';
    } else {
        // This should never happen with our buffer size calculation
        free(buffer);
        return NULL;
    }

    return buffer;
}
