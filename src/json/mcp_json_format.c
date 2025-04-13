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
 */
char* mcp_json_format_request_direct(uint64_t id, const char* method, const char* params) {
    if (method == NULL) {
        return NULL;
    }

    // Allocate a buffer for the JSON string
    // Start with a reasonable size and we'll realloc if needed
    size_t buffer_size = 256;
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
        // Check if we need to resize the buffer
        size_t params_len = strlen(params);
        size_t needed_size = written + params_len + 20; // Extra for ",\"params\":" and closing brace

        if (needed_size > buffer_size) {
            char* new_buffer = (char*)realloc(buffer, needed_size);
            if (!new_buffer) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            buffer_size = needed_size;
        }

        // Append the params
        written += snprintf(buffer + written, buffer_size - written,
                           ",\"params\":%s", params);

        if (written < 0 || (size_t)written >= buffer_size) {
            // Buffer too small or error
            free(buffer);
            return NULL;
        }
    }

    // Add closing brace
    if ((size_t)written + 2 > buffer_size) {
        // Need to resize for closing brace
        char* new_buffer = (char*)realloc(buffer, written + 2);
        if (!new_buffer) {
            free(buffer);
            return NULL;
        }
        buffer = new_buffer;
        buffer_size = written + 2;
    }

    buffer[written] = '}';
    buffer[written + 1] = '\0';

    return buffer;
}
