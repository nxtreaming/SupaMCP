#include "mcp_json_utils.h"
#include "mcp_json.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief Internal helper to write a character or string safely to the output buffer.
 *
 * @param output The output buffer.
 * @param output_size The total size of the output buffer.
 * @param out_idx Pointer to the current write index in the output buffer.
 * @param str The string or character sequence to write.
 * @param len The length of the string/sequence to write.
 * @return true if the write was successful (or would fit), false otherwise.
 */
static inline bool write_safe(char* output, size_t output_size, size_t* out_idx, const char* str, size_t len) {
    if (output != NULL && output_size > 0) {
        // Check if there's enough space *including* potential null terminator later
        if (*out_idx + len < output_size) {
            memcpy(output + *out_idx, str, len);
        } else if (*out_idx < output_size) {
            // Write partial if possible, but indicate truncation by returning false later
            size_t remaining_space = output_size - *out_idx -1; // Leave space for null term
            if (remaining_space > 0) {
                memcpy(output + *out_idx, str, remaining_space);
            }
            // Ensure null termination even if truncated
            output[output_size - 1] = '\0';
        }
        // If output_size is 0 or 1, we can't write anything meaningful + null term
    }
    *out_idx += len; // Always advance index to calculate total required size
    return true; // Return true for calculation purposes, actual success depends on size check later
}

int mcp_json_escape_string(const char* input, char* output, size_t output_size) {
    if (input == NULL) {
        return -1; // Indicate error for NULL input
    }

    size_t out_idx = 0;       // Current index in the output buffer
    size_t required_size = 0; // Total required size including null terminator

    for (size_t i = 0; input[i] != '\0'; ++i) {
        char c = input[i];
        const char* escaped = NULL;
        char unicode_buf[7]; // For "\\uXXXX"
        size_t escape_len = 1; // Default length is 1 (for non-escaped chars)

        switch (c) {
            case '\\': escaped = "\\\\"; escape_len = 2; break;
            case '"':  escaped = "\\\""; escape_len = 2; break;
            case '\b': escaped = "\\b"; escape_len = 2; break;
            case '\f': escaped = "\\f"; escape_len = 2; break;
            case '\n': escaped = "\\n"; escape_len = 2; break;
            case '\r': escaped = "\\r"; escape_len = 2; break;
            case '\t': escaped = "\\t"; escape_len = 2; break;
            default:
                // Check for control characters (U+0000 to U+001F)
                if ((unsigned char)c < 32) {
                    // Format as \uXXXX
                    snprintf(unicode_buf, sizeof(unicode_buf), "\\u%04x", (unsigned char)c);
                    escaped = unicode_buf;
                    escape_len = 6;
                }
                // Otherwise, no escape needed for this character
                break;
        }

        if (escaped != NULL) {
            write_safe(output, output_size, &out_idx, escaped, escape_len);
        } else {
            // Write the original character directly
            write_safe(output, output_size, &out_idx, &c, 1);
        }
    }

    // Add null terminator if space allows
    if (output != NULL && output_size > 0) {
        if (out_idx < output_size) {
            output[out_idx] = '\0';
        } else {
            // Ensure null termination even if truncated exactly at the end
            output[output_size - 1] = '\0';
        }
    }

    required_size = out_idx + 1; // +1 for the null terminator

    return (int)required_size;
}

// Utility functions for working with JSON values

bool mcp_json_is_string(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_STRING;
}

const char* mcp_json_string_value(const mcp_json_t* json) {
    const char* value = NULL;
    if (mcp_json_is_string(json) && mcp_json_get_string(json, &value) == 0) {
        return value;
    }
    return NULL;
}

bool mcp_json_is_number(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_NUMBER;
}

double mcp_json_number_value(const mcp_json_t* json) {
    double value = 0.0;
    if (mcp_json_is_number(json) && mcp_json_get_number(json, &value) == 0) {
        return value;
    }
    return 0.0;
}

bool mcp_json_is_boolean(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_BOOLEAN;
}

bool mcp_json_boolean_value(const mcp_json_t* json) {
    bool value = false;
    if (mcp_json_is_boolean(json) && mcp_json_get_boolean(json, &value) == 0) {
        return value;
    }
    return false;
}

bool mcp_json_is_null(const mcp_json_t* json) {
    return json == NULL || mcp_json_get_type(json) == MCP_JSON_NULL;
}

bool mcp_json_is_array(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_ARRAY;
}

bool mcp_json_is_object(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_OBJECT;
}

size_t mcp_json_object_size(const mcp_json_t* json) {
    if (!mcp_json_is_object(json)) {
        return 0;
    }

    char** names = NULL;
    size_t count = 0;
    if (mcp_json_object_get_property_names(json, &names, &count) != 0) {
        return 0;
    }

    // Free the property names
    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);

    return count;
}

int mcp_json_object_get_at(const mcp_json_t* json, size_t index, const char** name, mcp_json_t** value) {
    if (!mcp_json_is_object(json) || name == NULL || value == NULL) {
        return -1;
    }

    char** names = NULL;
    size_t count = 0;
    if (mcp_json_object_get_property_names(json, &names, &count) != 0) {
        return -1;
    }

    if (index >= count) {
        // Free the property names
        for (size_t i = 0; i < count; i++) {
            free(names[i]);
        }
        free(names);
        return -1;
    }

    *name = names[index];
    *value = mcp_json_object_get_property(json, names[index]);

    // Free the property names (except the one we're returning)
    for (size_t i = 0; i < count; i++) {
        if (i != index) {
            free(names[i]);
        }
    }
    free(names);

    return 0;
}
